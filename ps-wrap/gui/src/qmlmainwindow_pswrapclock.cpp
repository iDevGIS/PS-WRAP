// PS-WRAP: overlay นาฬิกา + เวลาเล่น (ClockOverlay.qml) — เจ้าของค่า on/off อยู่ C++ เหมือน pad/cam/mic overlay
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
// เวลา/เวลาเล่นคำนวณใน QML ทั้งหมด — ฝั่ง C++ เก็บแค่ค่าเปิด/ปิด (settings key pswrap/clockOverlay, ค่าเริ่มต้นปิด)
#include "qmlmainwindow.h"

bool QmlMainWindow::clockOverlay() const { return settings->GetClockOverlay(); }

void QmlMainWindow::setClockOverlay(bool v)
{
	if (v == settings->GetClockOverlay())
		return;
	settings->SetClockOverlay(v);
	emit clockOverlayChanged();
}
