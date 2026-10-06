// PS-WRAP: ภาพแนวตั้ง 9:16 — preview สด (VerticalPreviewWindow.qml) · เลย์เอาต์อยู่ pswrapvertical.cpp
// GUI thread: เปิด/ปิด preview, เลย์เอาต์, ตำแหน่งตัด, กรอบ facecam (StreamView ส่งมาทุก 200ms)
// render thread: วาดภาพเล็ก 540x960 ไม่เกิน ~30 ภาพ/วิ → download async → image provider → QML Image
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
#include "qmlmainwindow.h"
#include "pswrapvertical.h"

#include <chiaki/time.h>

#include <QCoreApplication>
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

} // namespace

QQuickImageProvider *pswrapCreateVerticalProvider() { return new VerticalProvider; }

bool QmlMainWindow::verticalPreview() const
{
	QMutexLocker locker(&shared().mutex);
	return shared().enabled;
}

void QmlMainWindow::setVerticalPreview(bool on)
{
	{
		QMutexLocker locker(&shared().mutex);
		if (shared().enabled == on)
			return;
		shared().enabled = on;
		shared().layout.mode = settings->GetVerticalLayout();
		shared().layout.crop_x = float(settings->GetVerticalCropX());
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
	}
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
	PsWrapVerticalLayout layout;
	QRectF cam_logical;
	bool enabled;
	{
		QMutexLocker locker(&shared().mutex);
		enabled = shared().enabled;
		layout = shared().layout;
		cam_logical = shared().cam_logical;
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

	// logical → pixel ของ swapchain (คิดจากขนาดหน้าต่างจริง ไม่ใช่ DPR ตายตัว)
	const int sw = swapchain_size.width(), sh = swapchain_size.height();
	if (!cam_logical.isEmpty() && width() > 0 && height() > 0) {
		const qreal kx = qreal(sw) / width(), ky = qreal(sh) / height();
		layout.cam = QRectF(cam_logical.x() * kx, cam_logical.y() * ky, cam_logical.width() * kx, cam_logical.height() * ky);
	}

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
	if (started)
		pswrap_vertical_last_us = now_us;
}

void QmlMainWindow::pswrapDestroyVertical()
{
	delete pswrap_vertical;   // destructor รอ download ที่ค้าง
	pswrap_vertical = nullptr;
}
