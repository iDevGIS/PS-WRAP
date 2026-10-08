// PS-WRAP: ภาพแนวตั้ง 9:16 — preview สด (VerticalPreviewWindow.qml) · เลย์เอาต์อยู่ pswrapvertical.cpp
// GUI thread: เปิด/ปิด preview, เลย์เอาต์, ตำแหน่งตัด, กรอบ facecam (StreamView ส่งมาทุก 200ms)
// render thread: วาดภาพเล็ก 540x960 ไม่เกิน ~30 ภาพ/วิ → download async → image provider → QML Image
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
#include "qmlmainwindow.h"
#include "pswrapvertical.h"
#include "pswrapverticallayers.h"
#include "qmlbackend.h"
#include "pswrapchat.h"

#include <chiaki/time.h>

#include <pswraprecorder.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QPainter>
#include <QQuickImageProvider>
#include <QTextDocument>
#include <QTimer>
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
	QRectF chat_area;     // พื้นที่การ์ดแชท (สัดส่วน canvas) — วาดเองใน pswrapPaintVerticalChat
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

// พื้นที่การ์ดแชทเริ่มต้น: Blur = เต็มแถบว่างใต้เกม (เกม 16:9 กินแถบ 0.342..0.658) · เลย์เอาต์อื่น = ทับส่วนล่าง
QRectF defaultChatArea(int mode)
{
	if (mode == PsWrapVerticalLayout::Blur)
		return QRectF(0.03, 0.675, 0.94, 0.30);
	return QRectF(0.03, 0.70, 0.94, 0.27);
}

QRectF clampChatArea(QRectF r)
{
	const qreal w = qBound(0.15, r.width(), 1.0), h = qBound(0.06, r.height(), 0.9);
	return QRectF(qBound(0.0, r.x(), 1.0 - w), qBound(0.0, r.y(), 1.0 - h), w, h);
}

// "x,y,w,h" → พื้นที่ (ว่าง/เสีย = ค่าเริ่มต้นของเลย์เอาต์)
QRectF parseChatArea(const QString &v, int mode)
{
	const QStringList parts = v.split(QLatin1Char(','));
	if (parts.size() == 4) {
		bool ok[4];
		const qreal x = parts[0].toDouble(&ok[0]), y = parts[1].toDouble(&ok[1]), w = parts[2].toDouble(&ok[2]), h = parts[3].toDouble(&ok[3]);
		if (ok[0] && ok[1] && ok[2] && ok[3] && w > 0 && h > 0)
			return clampChatArea(QRectF(x, y, w, h));
	}
	return defaultChatArea(mode);
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
		shared().chat_area = parseChatArea(settings->GetVerticalChatArea(shared().layout.mode), shared().layout.mode);
		shared().chat_on = settings->GetVerticalChat();
		if (!on)
			shared().latest = QImage();
	}
	pswrapScheduleVerticalChat();
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
		shared().chat_area = parseChatArea(settings->GetVerticalChatArea(mode), mode);
	}
	verticalLayers()->setMode(mode);   // รูป/GIF เก็บแยกต่อเลย์เอาต์
	pswrapScheduleVerticalChat();
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
	m[QStringLiteral("chat")] = shared().chat_on ? QVariant(rectMap(shared().chat_area)) : QVariant();
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
	}
}

// render thread: เลย์เอาต์ล่าสุด + กรอบ facecam แปลง logical → pixel ของ swapchain (คิดจากขนาดหน้าต่างจริง)
PsWrapVerticalLayout QmlMainWindow::pswrapVerticalLayoutNow(int sw, int sh) const
{
	PsWrapVerticalLayout layout;
	QRectF cam_logical;
	{
		QMutexLocker locker(&shared().mutex);
		layout = shared().layout;
		cam_logical = shared().cam_logical;
	}
	if (!cam_logical.isEmpty() && width() > 0 && height() > 0) {
		const qreal kx = qreal(sw) / width(), ky = qreal(sh) / height();
		layout.cam = QRectF(cam_logical.x() * kx, cam_logical.y() * ky, cam_logical.width() * kx, cam_logical.height() * ky);
	}
	return layout;
}

void QmlMainWindow::pswrapLoadVerticalLayout()
{
	verticalLayers();   // GUI thread: โหลดรูป/GIF ของเลย์เอาต์ปัจจุบัน (อัด/ไลฟ์แนวตั้งโดยไม่เปิด preview ก็ได้รูป)
	{
		QMutexLocker locker(&shared().mutex);
		shared().layout.mode = settings->GetVerticalLayout();
		shared().layout.crop_x = float(settings->GetVerticalCropX());
		applyCam(shared().layout, settings->GetVerticalCam(shared().layout.mode));
		shared().chat_area = parseChatArea(settings->GetVerticalChatArea(shared().layout.mode), shared().layout.mode);
		shared().chat_on = settings->GetVerticalChat();
	}
	pswrapScheduleVerticalChat();
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
	pswrapSyncChat();   // แชทแนวตั้งดึงข้อความเองได้ แม้ปิดการ์ดบนจอเกม
	pswrapScheduleVerticalChat();
	emit verticalChatChanged();
}

void QmlMainWindow::setVerticalChatArea(qreal x, qreal y, qreal w, qreal h)
{
	const int mode = settings->GetVerticalLayout();
	QRectF area = defaultChatArea(mode);
	QString v;
	if (w > 0 && h > 0) {
		area = clampChatArea(QRectF(x, y, w, h));
		v = QStringLiteral("%1,%2,%3,%4").arg(area.x(), 0, 'f', 4).arg(area.y(), 0, 'f', 4).arg(area.width(), 0, 'f', 4).arg(area.height(), 0, 'f', 4);
	}
	settings->SetVerticalChatArea(mode, v);
	{
		QMutexLocker locker(&shared().mutex);
		shared().chat_area = area;
	}
	pswrapScheduleVerticalChat();
}

void QmlMainWindow::pswrapScheduleVerticalChat()
{
	auto *t = findChild<QTimer *>(QStringLiteral("pswrapVerticalChatTimer"), Qt::FindDirectChildrenOnly);
	if (!t) {
		t = new QTimer(this);
		t->setObjectName(QStringLiteral("pswrapVerticalChatTimer"));
		t->setSingleShot(true);
		t->setInterval(60);
		connect(t, &QTimer::timeout, this, &QmlMainWindow::pswrapPaintVerticalChat);
	}
	t->start();
}

// การ์ดแชทในภาพแนวตั้ง — วาดเองตามขนาดพื้นที่จริง (ไม่ตัดจากการ์ดบนจอเกม): เต็มพื้นที่ว่างได้ทุกสัดส่วน ตัวหนังสือคม
// ขนาดภาพ = พื้นที่ × canvas 1080×1920 (preview ย่อเอง) · ข้อความใหม่อยู่ล่าง ของเก่าเลื่อนขึ้นแล้วจาง (แบบ ChatOverlay.qml)
void QmlMainWindow::pswrapPaintVerticalChat()
{
	QRectF area;
	bool on;
	{
		QMutexLocker locker(&shared().mutex);
		area = shared().chat_area;
		on = shared().chat_on;
	}
	PsWrapVerticalLayers *layers = verticalLayers();
	if (!on || area.isEmpty()) {
		layers->setChatImage(QImage(), QRectF());
		return;
	}
	const int W = qMax(64, qRound(area.width() * 1080.0)), H = qMax(64, qRound(area.height() * 1920.0));
	QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);
	const qreal radius = 28, pad = 28;

	// การ์ดกระจก + เส้น accent ล่าง (ชุดเดียวกับการ์ดบนจอ)
	p.setPen(QPen(QColor(255, 255, 255, 23), 2));
	p.setBrush(QColor(8, 12, 18, 158));
	p.drawRoundedRect(QRectF(1, 1, W - 2, H - 2), radius, radius);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 167, 255, 115));
	p.drawRect(QRectF(radius, H - 4, W - 2 * radius, 2));

	PsWrapLiveChat *chat = liveChat();
	QFont head = p.font();
	head.setPixelSize(22);
	head.setWeight(QFont::DemiBold);
	head.setLetterSpacing(QFont::AbsoluteSpacing, 3);
	p.setBrush(chat->isRunning() ? QColor(0xff, 0x4e, 0x45) : QColor(0x8b, 0x97, 0xa5));
	p.drawEllipse(QPointF(pad + 6, pad + 13), 7, 7);
	p.setFont(head);
	p.setPen(QColor(0xe8, 0xed, 0xf2));
	p.drawText(QPointF(pad + 24, pad + 21), tr("LIVE CHAT"));

	const qreal top = pad + 44;
	qreal bottom = H - pad;
	const QString status = chat->status();
	if (!status.isEmpty()) {
		QFont sf = head;
		sf.setPixelSize(19);
		sf.setWeight(QFont::Normal);
		sf.setLetterSpacing(QFont::AbsoluteSpacing, 0);
		p.setFont(sf);
		p.setPen(QColor(0x8b, 0x97, 0xa5));
		p.drawText(QRectF(pad, H - pad - 24, W - 2 * pad, 24), Qt::AlignLeft | Qt::AlignVCenter,
		           QFontMetrics(sf).elidedText(status, Qt::ElideRight, int(W - 2 * pad)));
		bottom -= 34;
	}

	QFont body = head;
	body.setPixelSize(30);
	body.setWeight(QFont::Normal);
	body.setLetterSpacing(QFont::AbsoluteSpacing, 0);
	const QVariantList msgs = chat->messages();
	if (msgs.isEmpty()) {
		p.setFont(body);
		p.setPen(QColor(0x8b, 0x97, 0xa5));
		p.drawText(QRectF(pad, top, W - 2 * pad, bottom - top), Qt::AlignCenter | Qt::TextWordWrap, tr("Waiting for chat messages…"));
	} else if (bottom > top) {
		p.save();
		p.setClipRect(QRectF(0, top, W, bottom - top));
		qreal y = bottom;
		const int n = int(msgs.size());
		for (int i = n - 1; i >= 0 && y > top; --i) {
			const QVariantMap m = msgs[i].toMap();
			const QString badge = m.value(QStringLiteral("badge")).toString();
			const QString mark = badge == QLatin1String("owner") ? QStringLiteral("★ ")
			                   : badge == QLatin1String("mod") ? QStringLiteral("MOD ")
			                   : badge == QLatin1String("super") ? QStringLiteral("$ ") : QString();
			QColor color(m.value(QStringLiteral("color")).toString());
			if (!color.isValid())
				color = QColor(0xe8, 0xed, 0xf2);
			const QString html = QStringLiteral("<span style='color:%1'>&#9679;</span> <b><span style='color:%2'>%3%4</span></b>&nbsp; <span style='color:#e8edf2'>%5</span>")
				.arg(m.value(QStringLiteral("platform")).toString() == QLatin1String("twitch") ? QStringLiteral("#a970ff") : QStringLiteral("#ff4e45"),
				     color.name(), mark.toHtmlEscaped(), m.value(QStringLiteral("author")).toString().toHtmlEscaped(),
				     m.value(QStringLiteral("text")).toString().toHtmlEscaped());
			QTextDocument doc;
			doc.setDefaultFont(body);
			doc.setDocumentMargin(0);
			doc.setTextWidth(W - 2 * pad);
			doc.setHtml(html);
			if (y - doc.size().height() < top)
				break;   // ไม่พอทั้งข้อความ — ไม่วาดครึ่งบรรทัดใต้หัวการ์ด
			y -= doc.size().height();
			p.setOpacity(qMax(0.35, 1.0 - qMax(0, n - 1 - i - 11) * 0.08));
			p.save();
			p.translate(pad, y);
			doc.drawContents(&p);
			p.restore();
			y -= 10;
		}
		p.restore();
	}
	p.end();
	layers->setChatImage(img.convertToFormat(QImage::Format_RGBA8888_Premultiplied), area);
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
	// แชทแนวตั้ง: ข้อความ/สถานะใหม่ → วาดการ์ดใหม่
	PsWrapLiveChat *chat = liveChat();
	connect(chat, &PsWrapLiveChat::messagesChanged, this, &QmlMainWindow::pswrapScheduleVerticalChat);
	connect(chat, &PsWrapLiveChat::statusChanged, this, &QmlMainWindow::pswrapScheduleVerticalChat);
	connect(chat, &PsWrapLiveChat::runningChanged, this, &QmlMainWindow::pswrapScheduleVerticalChat);
	pswrapScheduleVerticalChat();
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
