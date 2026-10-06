// PS-WRAP: ภาพหน้าจอ 1 ปุ่ม — ส่วนของ QmlMainWindow (แยกไฟล์เพื่อลด conflict ตอน merge upstream, ดู docs/04-upstream-sync.md)
//
// GUI thread: takeScreenshot() ตั้ง request id → render thread หยิบไปในเฟรมถัดไป (pswrapShotCapture)
// → PsWrapShotCapture วาดเฟรมเดียวกับจอ + overlay QML ลง texture RGBA แล้ว download แบบ async
// → callback ของ GPU ส่ง QImage ต่อให้ QThreadPool เข้ารหัส PNG + เขียนไฟล์ → signal กลับ GUI thread
#include "qmlmainwindow.h"

#include <pswrapreccapture.h>

#include <chiaki/time.h>

#include <QBuffer>
#include <cstring>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

namespace {

constexpr int kShotTimeoutMs = 3000;          // ไม่มีเฟรมถูก render ภายในนี้ → แจ้งพัง
constexpr qint64 kShotIdleReleaseUs = 20000000; // ว่าง 20 วิ → คืน renderer/texture (ถ่ายรัวๆ ไม่ต้องสร้างใหม่)

// โฟลเดอร์ปลายทาง — GUI thread เขียน (ตอนกดถ่าย), worker อ่าน (QSettings ห้ามแตะนอก GUI thread)
struct ShotFolder
{
	QMutex mutex;
	QString folder;
};
ShotFolder &shotFolder()
{
	static ShotFolder s;
	return s;
}

quint32 pngCrc32(const uchar *data, int len)
{
	quint32 c = 0xFFFFFFFFu;
	for (int i = 0; i < len; i++) {
		c ^= data[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return c ^ 0xFFFFFFFFu;
}

// แทรก chunk cICP (PNG 3rd edition) หลัง IHDR: BT.2020 (9) / PQ (16) / RGB (0) / full range (1)
// viewer ที่รองรับ (Chrome/Edge, Windows Photos รุ่นใหม่) จะแสดงเป็น HDR · ตัวที่ไม่รู้จัก chunk จะข้ามไป
bool insertCicp(QByteArray *png)
{
	static const char sig[8] = {char(0x89), 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
	if (png->size() < 33 || memcmp(png->constData(), sig, 8) != 0 || memcmp(png->constData() + 12, "IHDR", 4) != 0)
		return false;
	uchar chunk[16] = {0, 0, 0, 4, 'c', 'I', 'C', 'P', 9, 16, 0, 1, 0, 0, 0, 0};
	const quint32 crc = pngCrc32(chunk + 4, 8);
	chunk[12] = uchar(crc >> 24);
	chunk[13] = uchar(crc >> 16);
	chunk[14] = uchar(crc >> 8);
	chunk[15] = uchar(crc);
	png->insert(33, reinterpret_cast<const char *>(chunk), sizeof(chunk));
	return true;
}

bool writeFileAtomic(const QString &path, const QByteArray &bytes, QString *err)
{
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
		*err = f.errorString();
		return false;
	}
	return true;
}

QByteArray encodePng(const QImage &img, QString *err)
{
	QByteArray out;
	QBuffer buf(&out);
	buf.open(QIODevice::WriteOnly);
	QImageWriter w(&buf, "png");
	w.setCompression(1); // zlib ระดับต่ำ: 1440p ~0.1-0.3 วิ แทน ~1 วิ (ไฟล์ใหญ่ขึ้นนิดหน่อย)
	if (!w.write(img)) {
		*err = w.errorString();
		return {};
	}
	return out;
}

// worker thread: เข้ารหัส + เขียนลง folder (ถ้าโดนบล็อก → โฟลเดอร์สำรองแบบเดียวกับ toggleRecording)
// คืน path ของภาพ SDR · ว่าง = พัง (err บอกเหตุ)
QString saveShot(const QImage &sdr, const QImage &pq, const QString &folder, const QDateTime &when, QString *err)
{
	const QString base = QStringLiteral("PS-WRAP Shot %1").arg(when.toString(QStringLiteral("yyyy-MM-dd HH-mm-ss-zzz")));
	const QByteArray sdr_png = encodePng(sdr, err);
	if (sdr_png.isEmpty())
		return {};
	QByteArray pq_png;
	if (!pq.isNull()) {
		QString pq_err;
		pq_png = encodePng(pq, &pq_err);
		if (pq_png.isEmpty() || !insertCicp(&pq_png)) {
			qCWarning(chiakiGui) << "PSWRAP screenshot: HDR PNG encode failed" << pq_err << "— SDR only";
			pq_png.clear();
		}
	}

	auto tryFolder = [&](const QString &dir, QString *e) -> QString {
		QDir().mkpath(dir);
		const QString path = QDir(dir).filePath(base + QStringLiteral(".png"));
		if (!writeFileAtomic(path, sdr_png, e))
			return {};
		if (!pq_png.isEmpty()) {
			QString pq_err;
			if (!writeFileAtomic(QDir(dir).filePath(base + QStringLiteral(" HDR.png")), pq_png, &pq_err))
				qCWarning(chiakiGui) << "PSWRAP screenshot: HDR PNG write failed" << pq_err;
		}
		return path;
	};

	QString path = tryFolder(folder, err);
	if (!path.isEmpty())
		return path;
	// เขียนไม่ได้ (Windows "Controlled folder access" บล็อก Pictures/Videos) → home\PS-WRAP Recordings
	const QString fallback = QDir(QDir::homePath()).filePath(QStringLiteral("PS-WRAP Recordings"));
	if (QDir::cleanPath(folder) != QDir::cleanPath(fallback)) {
		QString err2;
		path = tryFolder(fallback, &err2);
		if (!path.isEmpty()) {
			qCInfo(chiakiGui) << "PSWRAP screenshot: fallback folder" << fallback << "because" << *err;
			return path;
		}
	}
	return {};
}

} // namespace

void QmlMainWindow::takeScreenshot()
{
	if (!session || stream_session_active.loadAcquire() == 0 || !has_video) {
		emit screenshotFailed(tr("Start a stream first."));
		return;
	}
	{
		ShotFolder &sf = shotFolder();
		QMutexLocker locker(&sf.mutex);
		sf.folder = QDir::fromNativeSeparators(recordingFolder());
	}
	static int next_id = 0;
	next_id = next_id >= 0x3FFFFFFF ? 1 : next_id + 1;
	const int id = next_id;
	pswrap_shot_request.storeRelease(id); // กดซ้ำก่อนเฟรมถัดไป = รวมเป็นภาพเดียว (ตัวจับเวลาของคำขอเก่าจะไม่ยิง)
	QTimer::singleShot(kShotTimeoutMs, this, [this, id]() {
		if (pswrap_shot_request.testAndSetOrdered(id, 0))
			emit screenshotFailed(tr("Couldn't capture the screen — no video frame was drawn. Try again."));
	});
	requestOverlayUpdate(); // ให้มีเฟรมถูก render แน่ๆ แม้ภาพนิ่ง
}

void QmlMainWindow::pswrapShotCapture(const pl_frame_mix *mix, const pl_frame *single, const pl_render_params &params,
                                      const pl_frame &screen_target, const pl_overlay *overlay)
{
	Q_ASSERT(QThread::currentThread() == render_thread);
	const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
	if (pswrap_shot_request.loadAcquire() == 0) {
		// ว่างนาน → คืน GPU resource (เหมือนที่ recorder ลบ capture ตอนไม่ได้อัด)
		if (pswrap_shot_capture && pswrap_shot_capture->idle() && now_us - pswrap_shot_last_us > kShotIdleReleaseUs) {
			delete pswrap_shot_capture;
			pswrap_shot_capture = nullptr;
		}
		return;
	}
	if (!single && (!mix || mix->num_frames <= 0))
		return; // ยังไม่มีภาพในเฟรมนี้ — รอเฟรมถัดไป (มี timeout ฝั่ง GUI)
	const int id = pswrap_shot_request.fetchAndStoreOrdered(0);
	if (id == 0)
		return; // timeout ชิงไปแล้ว
	pswrap_shot_last_us = now_us;

	QPointer<QmlMainWindow> self(this);
	auto fail = [self](const QString &msg) {
		QMetaObject::invokeMethod(qApp, [self, msg]() {
			if (self)
				emit self->screenshotFailed(msg);
		}, Qt::QueuedConnection);
	};

	if (!pswrap_shot_capture) {
		pl_gpu gpu = placeboGpu();
		if (!gpu) {
			fail(tr("Couldn't capture the screen."));
			return;
		}
		pswrap_shot_capture = new PsWrapShotCapture(gpu, placebo_log);
	}

	const QDateTime when = QDateTime::currentDateTime();
	// done: รันบน thread ของ GPU (ส่วนใหญ่ render thread) — ส่งต่อให้ worker ทันที ไม่ทำงานหนักตรงนี้
	auto done = [self, fail, when](QImage sdr, QImage pq) {
		if (sdr.isNull()) {
			fail(QCoreApplication::translate("QmlMainWindow", "Couldn't read the screen image from the GPU."));
			return;
		}
		QThreadPool::globalInstance()->start([self, fail, when, sdr = std::move(sdr), pq = std::move(pq)]() {
			QString folder;
			{
				ShotFolder &sf = shotFolder();
				QMutexLocker locker(&sf.mutex);
				folder = sf.folder;
			}
			QString err;
			const QString path = saveShot(sdr, pq, folder, when, &err);
			if (path.isEmpty()) {
				qCWarning(chiakiGui) << "PSWRAP screenshot: save failed" << folder << err;
				fail(QCoreApplication::translate("QmlMainWindow", "Couldn't save the screenshot: %1").arg(err));
				return;
			}
			qCInfo(chiakiGui) << "PSWRAP screenshot:" << path << sdr.size() << (pq.isNull() ? "SDR" : "SDR + HDR PQ");
			const QString native = QDir::toNativeSeparators(path);
			QMetaObject::invokeMethod(qApp, [self, native]() {
				if (self)
					emit self->screenshotSaved(native);
			}, Qt::QueuedConnection);
		});
	};
	if (!pswrap_shot_capture->capture(mix, single, params, screen_target, overlay,
	                                  swapchain_size.width(), swapchain_size.height(), std::move(done)))
		fail(tr("Couldn't capture the screen."));
}

void QmlMainWindow::pswrapDestroyShot()
{
	delete pswrap_shot_capture; // destructor รอ download ที่ค้าง (pl_gpu_finish) ก่อนคืน texture
	pswrap_shot_capture = nullptr;
}
