// PS-WRAP: ภาพแนวตั้ง 9:16 — preview สด (VerticalPreviewWindow.qml) · เลย์เอาต์อยู่ pswrapvertical.cpp
// GUI thread: เปิด/ปิด preview, เลย์เอาต์, ตำแหน่งตัด, กรอบ facecam (StreamView ส่งมาทุก 200ms)
// render thread: วาดภาพเล็ก 540x960 ไม่เกิน ~30 ภาพ/วิ → download async → image provider → QML Image
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
#include "qmlmainwindow.h"
#include "pswrapvertical.h"
#include "pswrapverticallayers.h"
#include "qmlbackend.h"

#include <chiaki/time.h>

#include <pswraprecorder.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QQuickImageProvider>
#include <QThread>

namespace {

constexpr int kPreviewW = 540, kPreviewH = 960;
constexpr qint64 kPreviewIntervalUs = 33000;      // ~30 ภาพ/วิ
constexpr qint64 kIdleReleaseUs = 5000000;        // ปิด preview แล้ว 5 วิ → คืน GPU resource

// ค่าที่ render thread อ่าน (GUI thread เขียน) + ภาพล่าสุดที่ provider ส่งให้ QML
struct VerticalShared
{
	QMutex mutex;
	bool enabled = false;
	PsWrapVerticalLayout layout;
	QRectF cam_logical;   // logical px ของหน้าต่าง
	QImage latest;
	QRectF game_n, cam_n; // กรอบล่าสุดใน preview (สัดส่วน 0..1) ให้ QML hit-test ตอนลาก
	bool chat_on = false; // การ์ดแชทในภาพแนวตั้ง (settings pswrap/verticalChat)
	QRectF chat_logical;  // logical px ของการ์ดแชทบนจอ
	QRectF chat_n;
};
VerticalShared &shared()
{
	static VerticalShared s;
	return s;
}

class VerticalProvider : public QQuickImageProvider
{
public:
	VerticalProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
	QImage requestImage(const QString &, QSize *size, const QSize &requested) override
	{
		QImage img;
		{
			QMutexLocker locker(&shared().mutex);
			img = shared().latest;
		}
		if (img.isNull()) {
			img = QImage(kPreviewW, kPreviewH, QImage::Format_RGBX8888);
			img.fill(Qt::black);
		}
		if (size)
			*size = img.size();
		if (requested.isValid() && !requested.isEmpty() && requested != img.size())
			return img.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		return img;
	}
};

// "cx,cy,w" → layout (ว่าง/เสีย = ค่าเริ่มต้นของเลย์เอาต์) · เรียกใต้ shared().mutex
void applyCam(PsWrapVerticalLayout &l, const QString &v)
{
	const QStringList parts = v.split(QLatin1Char(','));
	bool ok1 = false, ok2 = false, ok3 = false;
	const float cx = parts.size() == 3 ? parts[0].toFloat(&ok1) : 0.0f;
	const float cy = parts.size() == 3 ? parts[1].toFloat(&ok2) : 0.0f;
	const float w = parts.size() == 3 ? parts[2].toFloat(&ok3) : 0.0f;
	if (ok1 && ok2 && ok3 && w > 0.0f) {
		l.cam_cx = cx;
		l.cam_cy = cy;
		l.cam_w = w;
	} else {
		l.cam_w = 0.0f;
	}
}

// "cx,cy,w" → ตำแหน่งการ์ดแชท (ว่าง/เสีย = ค่าเริ่มต้น) · เรียกใต้ shared().mutex
void applyChat(PsWrapVerticalLayout &l, const QString &v)
{
	const QStringList parts = v.split(QLatin1Char(','));
	bool ok1 = false, ok2 = false, ok3 = false;
	const float cx = parts.size() == 3 ? parts[0].toFloat(&ok1) : 0.0f;
	const float cy = parts.size() == 3 ? parts[1].toFloat(&ok2) : 0.0f;
	const float w = parts.size() == 3 ? parts[2].toFloat(&ok3) : 0.0f;
	if (ok1 && ok2 && ok3 && w > 0.0f) {
		l.chat_cx = cx;
		l.chat_cy = cy;
		l.chat_w = w;
	} else {
		l.chat_w = 0.0f;
	}
}

QVariantMap rectMap(const QRectF &r)
{
	return {{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()}, {QStringLiteral("w"), r.width()}, {QStringLiteral("h"), r.height()}};
}

} // namespace

QQuickImageProvider *pswrapCreateVerticalProvider() { return new VerticalProvider; }

bool QmlMainWindow::verticalPreview() const
{
	QMutexLocker locker(&shared().mutex);
	return shared().enabled;
}

void QmlMainWindow::setVerticalPreview(bool on)
{
	if (on)
		verticalLayers();
	{
		QMutexLocker locker(&shared().mutex);
		if (shared().enabled == on)
			return;
		shared().enabled = on;
		shared().layout.mode = settings->GetVerticalLayout();
		shared().layout.crop_x = float(settings->GetVerticalCropX());
		applyCam(shared().layout, settings->GetVerticalCam(shared().layout.mode));
		applyChat(shared().layout, settings->GetVerticalChatPos(shared().layout.mode));
		shared().chat_on = settings->GetVerticalChat();
		if (!on)
			shared().latest = QImage();
	}
	emit verticalPreviewChanged();
}

int QmlMainWindow::verticalLayout() const { return settings->GetVerticalLayout(); }

void QmlMainWindow::setVerticalLayout(int mode)
{
	mode = qBound(0, mode, 2);
	if (mode == settings->GetVerticalLayout())
		return;
	settings->SetVerticalLayout(mode);
	{
		QMutexLocker locker(&shared().mutex);
		shared().layout.mode = mode;
		applyCam(shared().layout, settings->GetVerticalCam(mode));
		applyChat(shared().layout, settings->GetVerticalChatPos(mode));
	}
	verticalLayers()->setMode(mode);   // รูป/GIF เก็บแยกต่อเลย์เอาต์
	emit verticalLayoutChanged();
}

qreal QmlMainWindow::verticalCropX() const { return settings->GetVerticalCropX(); }

void QmlMainWindow::setVerticalCropX(qreal x)
{
	x = qBound(0.0, x, 1.0);
	if (qFuzzyCompare(1.0 + x, 1.0 + settings->GetVerticalCropX()))
		return;
	settings->SetVerticalCropX(x);
	{
		QMutexLocker locker(&shared().mutex);
		shared().layout.crop_x = float(x);
	}
	emit verticalCropXChanged();
}

QVariantMap QmlMainWindow::verticalHitRects() const
{
	QMutexLocker locker(&shared().mutex);
	QVariantMap m;
	m[QStringLiteral("game")] = rectMap(shared().game_n);
	m[QStringLiteral("cam")] = shared().cam_n.isEmpty() ? QVariant() : QVariant(rectMap(shared().cam_n));
	m[QStringLiteral("chat")] = shared().chat_n.isEmpty() ? QVariant() : QVariant(rectMap(shared().chat_n));
	return m;
}

void QmlMainWindow::setVerticalCam(qreal cx, qreal cy, qreal w)
{
	const int mode = settings->GetVerticalLayout();
	const QString v = w > 0 ? QStringLiteral("%1,%2,%3").arg(qBound(0.0, cx, 1.0), 0, 'f', 4).arg(qBound(0.0, cy, 1.0), 0, 'f', 4).arg(qBound(0.05, w, 1.0), 0, 'f', 4)
	                        : QString();
	settings->SetVerticalCam(mode, v);
	QMutexLocker locker(&shared().mutex);
	applyCam(shared().layout, v);
}

void QmlMainWindow::setVerticalCamRect(qreal x, qreal y, qreal w, qreal h)
{
	QMutexLocker locker(&shared().mutex);
	shared().cam_logical = (w > 0 && h > 0) ? QRectF(x, y, w, h) : QRectF();
}

void QmlMainWindow::pswrapVerticalCapture(const pl_frame_mix *mix, const pl_frame *single, const pl_render_params &params,
                                          const pl_frame &screen_target, const pl_overlay *overlay)
{
	Q_ASSERT(QThread::currentThread() == render_thread);
	const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
	bool enabled;
	{
		QMutexLocker locker(&shared().mutex);
		enabled = shared().enabled;
	}
	if (!enabled || !session) {
		if (pswrap_vertical && pswrap_vertical->idle() && now_us - pswrap_vertical_last_us > kIdleReleaseUs) {
			delete pswrap_vertical;
			pswrap_vertical = nullptr;
		}
		return;
	}
	if (now_us - pswrap_vertical_last_us < kPreviewIntervalUs)
		return;
	if (!single && (!mix || mix->num_frames <= 0))
		return;
	if (!pswrap_vertical) {
		pl_gpu gpu = placeboGpu();
		if (!gpu)
			return;
		pswrap_vertical = new PsWrapVerticalPreview(gpu, placebo_log);
	}
	if (!pswrap_vertical->idle())
		return;   // ภาพก่อนยัง download ไม่เสร็จ — ข้ามรอบนี้

	const int sw = swapchain_size.width(), sh = swapchain_size.height();
	const PsWrapVerticalLayout layout = pswrapVerticalLayoutNow(sw, sh);

	QPointer<QmlMainWindow> self(this);
	const bool started = pswrap_vertical->capture(mix, single, params, screen_target, overlay, sw, sh, layout,
	                                               kPreviewW, kPreviewH, [self](QImage img) {
		{
			QMutexLocker locker(&shared().mutex);
			if (!shared().enabled)
				return;
			shared().latest = std::move(img);
		}
		QMetaObject::invokeMethod(qApp, [self]() {
			if (!self)
				return;
			self->pswrap_vertical_frame++;
			emit self->verticalFrameChanged();
		}, Qt::QueuedConnection);
	});
	if (started) {
		pswrap_vertical_last_us = now_us;
		// กรอบที่เพิ่งวาด → สัดส่วนของ canvas ให้ QML ลาก
		const PsWrapVerticalGeometry &g = pswrap_vertical->lastGeometry();
		auto norm = [](const pl_rect2df &r) {
			return QRectF(r.x0 / kPreviewW, r.y0 / kPreviewH, (r.x1 - r.x0) / kPreviewW, (r.y1 - r.y0) / kPreviewH);
		};
		QMutexLocker locker(&shared().mutex);
		shared().game_n = norm(g.game_dst);
		shared().cam_n = g.cam_dst.x1 > g.cam_dst.x0 ? norm(g.cam_dst) : QRectF();
		shared().chat_n = g.chat_dst.x1 > g.chat_dst.x0 ? norm(g.chat_dst) : QRectF();
	}
}

// render thread: เลย์เอาต์ล่าสุด + กรอบ facecam แปลง logical → pixel ของ swapchain (คิดจากขนาดหน้าต่างจริง)
PsWrapVerticalLayout QmlMainWindow::pswrapVerticalLayoutNow(int sw, int sh) const
{
	PsWrapVerticalLayout layout;
	QRectF cam_logical, chat_logical;
	{
		QMutexLocker locker(&shared().mutex);
		layout = shared().layout;
		cam_logical = shared().cam_logical;
		if (shared().chat_on)
			chat_logical = shared().chat_logical;
	}
	if (width() > 0 && height() > 0) {
		const qreal kx = qreal(sw) / width(), ky = qreal(sh) / height();
		auto toPx = [kx, ky](const QRectF &r) { return QRectF(r.x() * kx, r.y() * ky, r.width() * kx, r.height() * ky); };
		if (!cam_logical.isEmpty())
			layout.cam = toPx(cam_logical);
		if (!chat_logical.isEmpty())
			layout.chat = toPx(chat_logical);
	}
	return layout;
}

void QmlMainWindow::pswrapLoadVerticalLayout()
{
	verticalLayers();   // GUI thread: โหลดรูป/GIF ของเลย์เอาต์ปัจจุบัน (อัด/ไลฟ์แนวตั้งโดยไม่เปิด preview ก็ได้รูป)
	QMutexLocker locker(&shared().mutex);
	shared().layout.mode = settings->GetVerticalLayout();
	shared().layout.crop_x = float(settings->GetVerticalCropX());
	applyCam(shared().layout, settings->GetVerticalCam(shared().layout.mode));
	applyChat(shared().layout, settings->GetVerticalChatPos(shared().layout.mode));
	shared().chat_on = settings->GetVerticalChat();
}

bool QmlMainWindow::verticalChat() const { return settings->GetVerticalChat(); }

void QmlMainWindow::setVerticalChat(bool on)
{
	if (on == settings->GetVerticalChat())
		return;
	settings->SetVerticalChat(on);
	{
		QMutexLocker locker(&shared().mutex);
		shared().chat_on = on;
	}
	if (on)
		setChatOverlay(true);   // ภาพในแนวตั้งตัดมาจากการ์ดบนจอ — ต้องเปิดอยู่
	emit verticalChatChanged();
}

void QmlMainWindow::setVerticalChatRect(qreal x, qreal y, qreal w, qreal h)
{
	QMutexLocker locker(&shared().mutex);
	shared().chat_logical = (w > 0 && h > 0) ? QRectF(x, y, w, h) : QRectF();
}

void QmlMainWindow::setVerticalChatPos(qreal cx, qreal cy, qreal w)
{
	const int mode = settings->GetVerticalLayout();
	const QString v = w > 0 ? QStringLiteral("%1,%2,%3").arg(qBound(0.0, cx, 1.0), 0, 'f', 4).arg(qBound(0.0, cy, 1.0), 0, 'f', 4).arg(qBound(0.08, w, 1.0), 0, 'f', 4)
	                        : QString();
	settings->SetVerticalChatPos(mode, v);
	QMutexLocker locker(&shared().mutex);
	applyChat(shared().layout, v);
}

PsWrapVerticalLayers *QmlMainWindow::verticalLayers()
{
	auto *l = findChild<PsWrapVerticalLayers *>(QString(), Qt::FindDirectChildrenOnly);
	if (l)
		return l;
	l = new PsWrapVerticalLayers(this, [this]() { return settings; });
	// GIF เล่นเฉพาะตอนมีสตรีม (ไม่กิน CPU ตอนอยู่หน้าแรก)
	if (backend)
		connect(backend, &QmlBackend::sessionChanged, l, [this, l](StreamSession *) {
			QMetaObject::invokeMethod(l, [this, l]() { l->setAnimating(session != nullptr); }, Qt::QueuedConnection);
		});
	l->setAnimating(session != nullptr);
	return l;
}

QObject *QmlMainWindow::verticalLayersObject() { return verticalLayers(); }

QObject *QmlMainWindow::verticalRecorderObject() const { return pswrap_vrec; }

// อัดคลิปแนวตั้ง 1080x1920 SDR (Shorts/TikTok/Reels ไม่ต้องการ HDR) — pipeline แยกจากไฟล์อัดปกติ อัดพร้อมกันได้
void QmlMainWindow::toggleVerticalRecording()
{
	if (!pswrap_vrec || pswrap_vrec->isBusy())
		return;
	if (pswrap_vrec->isRecording()) {
		pswrap_vrec->stop();
		return;
	}
	PsWrapRecConfig cfg;
	QString err;
	if (!pswrapBuildRecConfig(&cfg, &err)) {   // เช็คสตรีม + fps ของสตรีม
		emit pswrap_vrec->failed(err);
		return;
	}
	cfg.width = 1080;
	cfg.height = 1920;
	cfg.hdr = false;
	cfg.hdr_info = PsWrapRecHdrInfo();
	pswrapLoadVerticalLayout();
	const QString folder = QDir::fromNativeSeparators(recordingFolder());
	const QString name = QStringLiteral("PS-WRAP Vertical %1.mp4").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss")));
	cfg.path = QDir(folder).filePath(name);
	if (pswrap_vrec->start(cfg, &err))
		return;
	// เขียนโฟลเดอร์ที่ตั้งไว้ไม่ได้ (Controlled folder access) → โฟลเดอร์สำรองเดียวกับคลิปปกติ
	const QString fallback = QDir(QDir::homePath()).filePath(QStringLiteral("PS-WRAP Recordings"));
	if (pswrap_vrec->lastStartFileError() && QDir::cleanPath(folder) != QDir::cleanPath(fallback)) {
		cfg.path = QDir(fallback).filePath(name);
		QString err2;
		if (pswrap_vrec->start(cfg, &err2)) {
			emit pswrap_vrec->notice(tr("Windows blocked saving to %1, so this recording is saved to %2.")
				.arg(QDir::toNativeSeparators(folder), QDir::toNativeSeparators(fallback)), cfg.path);
			return;
		}
	}
	emit pswrap_vrec->failed(err);
}

void QmlMainWindow::pswrapDestroyVertical()
{
	delete pswrap_vertical;   // destructor รอ download ที่ค้าง
	pswrap_vertical = nullptr;
	pswrapVerticalLayersReleaseGpu(placeboGpu());   // texture ของรูป/GIF (render thread จบแล้ว)
}
