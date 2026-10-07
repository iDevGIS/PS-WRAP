// PS-WRAP: "Size" ในเมนูสตรีม — ตั้งพื้นที่ภาพ (client area) เป็น 16:9 พอดีตาม preset ไม่ต้องลากหน้าต่างเอง
// ชื่อ preset = ขนาดจริง (physical px) ที่ได้ → คลิปอัด/ภาพหน้าจอได้ขนาดนั้นตรงตามชื่อ และไม่มีขอบดำ (สตรีม PS เป็น 16:9 เสมอ)
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
#include "qmlmainwindow.h"
#include "pswrapreccapture.h"

#include <QScreen>
#include <QGuiApplication>
#include <QTimer>
#include <QVariantMap>
#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMenu>
#include <QQuickItem>
#include <QtMath>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

constexpr int kPresetHeights[] = {720, 900, 1080, 1440, 2160};

int widthFor169(int h) { return (h * 16 / 9 + 1) & ~1; }   // 900 → 1600, 720 → 1280

// พื้นที่ใช้ได้ของจอ (physical) + ขนาดกรอบหน้าต่าง (physical) — ใช้ตัดสินว่า preset ไหนวางได้
struct ScreenRoom { QSize avail; QSize frame; };

ScreenRoom screenRoom(QWindow *w)
{
	ScreenRoom r;
#ifdef Q_OS_WIN
	HWND hwnd = reinterpret_cast<HWND>(w->winId());
	MONITORINFO mi{};
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
		r.avail = QSize(mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top);
	RECT rc{0, 0, 100, 100};
	const DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE)) & ~WS_MAXIMIZE;
	const DWORD ex = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
	if (AdjustWindowRectExForDpi(&rc, style, FALSE, ex, GetDpiForWindow(hwnd)))
		r.frame = QSize(rc.right - rc.left - 100, rc.bottom - rc.top - 100);
#endif
	if (r.avail.isEmpty()) {
		QScreen *scr = w->screen() ? w->screen() : QGuiApplication::primaryScreen();
		r.avail = scr->availableGeometry().size() * w->devicePixelRatio();
		const QMargins m = w->frameMargins();
		r.frame = QSize(m.left() + m.right(), m.top() + m.bottom()) * w->devicePixelRatio();
	}
	return r;
}

// client area ปัจจุบัน (physical)
QSize clientSize(QWindow *w)
{
#ifdef Q_OS_WIN
	RECT rc{};
	if (GetClientRect(reinterpret_cast<HWND>(w->winId()), &rc))
		return QSize(rc.right - rc.left, rc.bottom - rc.top);
#endif
	return w->size() * w->devicePixelRatio();
}

} // namespace

QVariantList QmlMainWindow::playerSizes()
{
	bool hdr = false;
	PsWrapRecHdrInfo hdr_info;
	int stream_h = 0;
	if (!session || !PsWrapRecCapture::sourceInfo(&hdr, &hdr_info, &stream_h))
		stream_h = 0;

	const ScreenRoom room = screenRoom(this);
	const QSize cur = clientSize(this);
	const bool full = windowState() == Qt::WindowFullScreen;
	const bool maxed = windowState() == Qt::WindowMaximized;

	QList<int> heights(std::begin(kPresetHeights), std::end(kPresetHeights));
	if (stream_h > 0 && !heights.contains(stream_h)) {
		heights.append(stream_h);
		std::sort(heights.begin(), heights.end());
	}

	QVariantList out;
	for (int h : heights) {
		const int w = widthFor169(h);
		if (w + room.frame.width() > room.avail.width() || h + room.frame.height() > room.avail.height())
			continue;   // ใหญ่กว่าจอ → ใช้ Fullscreen แทน
		QVariantMap m;
		m[QStringLiteral("width")] = w;
		m[QStringLiteral("height")] = h;
		m[QStringLiteral("stream")] = h == stream_h;
		m[QStringLiteral("current")] = !full && !maxed && cur == QSize(w, h);
		out.append(m);
	}
	QVariantMap fs;
	fs[QStringLiteral("width")] = -1;
	fs[QStringLiteral("height")] = -1;
	fs[QStringLiteral("stream")] = false;
	fs[QStringLiteral("current")] = full;
	out.append(fs);
	return out;
}

// เมนู tray: เปิดเมนูสตรีม (แทนกด L1+R1+L3+R3 / Ctrl+O) · Settings (หน้าแรก) · ขนาดภาพ (เหมือนปุ่ม Size ในเมนูสตรีม)
void QmlMainWindow::pswrapSetupTrayView(QMenu *menu)
{
	auto *stream_menu = menu->addAction(QIcon(QStringLiteral(":/icons/menu/menu.svg")), tr("Stream menu"));
	connect(stream_menu, &QAction::triggered, this, [this]() {
		if (!session)
			return;
		restoreFromTray();
		requestActivate();
		// หลังหน้าต่างกลับมา (คืนจาก tray/minimize) ค่อยเปิด — StreamView เปิด/ปิดสลับตาม menuRequested
		QTimer::singleShot(150, this, [this]() { if (session) emit menuRequested(); });
	});

	auto *settings_action = menu->addAction(QIcon(QStringLiteral(":/icons/menu/settings.svg")), tr("Settings"));
	connect(settings_action, &QAction::triggered, this, [this]() {
		restoreFromTray();
		requestActivate();
		if (quick_item)
			QMetaObject::invokeMethod(quick_item, "openSettingsFromTray");
	});

	QMenu *size_menu = menu->addMenu(QIcon(QStringLiteral(":/icons/menu/display.svg")), tr("Picture size"));
	size_menu->setStyleSheet(menu->styleSheet());
	connect(size_menu, &QMenu::aboutToShow, this, [this, size_menu]() {
		size_menu->clear();
		auto *group = new QActionGroup(size_menu);
		for (const QVariant &v : playerSizes()) {
			const QVariantMap m = v.toMap();
			const int w = m.value(QStringLiteral("width")).toInt();
			const int h = m.value(QStringLiteral("height")).toInt();
			QString label = h < 0 ? tr("Fullscreen") : tr("%1p   %2 × %3").arg(h).arg(w).arg(h);
			if (m.value(QStringLiteral("stream")).toBool())
				label += tr("   · stream");
			const bool current = m.value(QStringLiteral("current")).toBool();
			auto *a = size_menu->addAction((current ? QStringLiteral("✓  ") : QStringLiteral("     ")) + label);
			a->setCheckable(true);
			a->setChecked(current);
			group->addAction(a);
			connect(a, &QAction::triggered, this, [this, w, h]() {
				restoreFromTray();
				setPlayerSize(w, h);
			});
		}
	});

	// preview ภาพแนวตั้ง 9:16 (VerticalPreviewWindow.qml — มีเฉพาะระหว่างสตรีม)
	auto *vertical_action = menu->addAction(QIcon(QStringLiteral(":/icons/menu/vertical.svg")), tr("9:16 preview"));
	connect(vertical_action, &QAction::triggered, this, [this]() { setVerticalPreview(!verticalPreview()); });
	auto *vrec_action = menu->addAction(QIcon(QStringLiteral(":/icons/menu/record.svg")), tr("Record 9:16"));
	connect(vrec_action, &QAction::triggered, this, [this]() { toggleVerticalRecording(); });
	auto *chat_action = menu->addAction(QIcon(QStringLiteral(":/icons/menu/chat.svg")), tr("Live chat"));
	connect(chat_action, &QAction::triggered, this, [this]() { setChatOverlay(!chatOverlay()); });

	// สถานะตามตอนเปิดเมนู: Stream menu / 9:16 ใช้ได้เฉพาะระหว่างสตรีม · Settings เฉพาะนอกสตรีม
	connect(menu, &QMenu::aboutToShow, this, [this, stream_menu, settings_action, vertical_action, vrec_action, chat_action]() {
		chat_action->setText(tr("Live chat") + (chatOverlay() ? QStringLiteral("   ●  ON") : QStringLiteral("   ○  OFF")));
		const bool vrec_on = pswrap_vrec && pswrap_vrec->isRecording();
		vrec_action->setEnabled(session != nullptr && pswrap_vrec && !pswrap_vrec->isBusy());
		vrec_action->setText(vrec_on ? tr("Stop 9:16 recording") + QStringLiteral("   ●  REC") : tr("Record 9:16"));
		stream_menu->setEnabled(session != nullptr);
		settings_action->setEnabled(session == nullptr);
		vertical_action->setEnabled(session != nullptr);
		vertical_action->setText(tr("9:16 preview") + (verticalPreview() ? QStringLiteral("   ●  ON") : QStringLiteral("   ○  OFF")));
	});
}

void QmlMainWindow::setPlayerSize(int width, int height)
{
	if (width <= 0 || height <= 0) {
		fullscreenTime();
		return;
	}

	auto apply = [this, width, height]() {
#ifdef Q_OS_WIN
		// ตั้งเป็น physical px ตรงๆ — QWindow::resize รับ logical ซึ่งที่ scale 150% ปัด 1600x900 ไม่ลง (1066.67)
		HWND hwnd = reinterpret_cast<HWND>(winId());
		RECT rc{0, 0, width, height};
		const DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
		const DWORD ex = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
		AdjustWindowRectExForDpi(&rc, style, FALSE, ex, GetDpiForWindow(hwnd));
		const int fw = rc.right - rc.left, fh = rc.bottom - rc.top;
		RECT cur{};
		GetWindowRect(hwnd, &cur);
		MONITORINFO mi{};
		mi.cbSize = sizeof(mi);
		GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
		const RECT &wa = mi.rcWork;
		// คงมุมซ้ายบนไว้ ถ้าล้นจอค่อยเลื่อนเข้ามา (ล้นทั้งก้อน → ชิดซ้าย/บน)
		const int x = std::max<int>(wa.left, std::min<int>(cur.left, wa.right - fw));
		const int y = std::max<int>(wa.top, std::min<int>(cur.top, wa.bottom - fh));
		SetWindowPos(hwnd, nullptr, x, y, fw, fh, SWP_NOZORDER | SWP_NOACTIVATE);
#else
		const qreal dpr = devicePixelRatio();
		resize(qCeil(width / dpr), qCeil(height / dpr));
#endif
		qCInfo(chiakiGui) << "PSWRAP player size:" << width << "x" << height << "client now" << clientSize(this);
	};

	if (windowState() == Qt::WindowFullScreen) {
		was_maximized = false;   // ออกจาก fullscreen เป็นหน้าต่างปกติ ไม่ใช่ maximize
		normalTime();
		QTimer::singleShot(150, this, apply);   // รอ Windows คืนกรอบหน้าต่างก่อน
	} else if (windowState() == Qt::WindowMaximized) {
		showNormal();
		QTimer::singleShot(150, this, apply);
	} else {
		apply();
	}
}
