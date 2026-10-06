// PS-WRAP: ส่วนของ QmlMainWindow ที่เกี่ยวกับ preset รายเกม (Chiaki.window.gameProfiles)
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
// วางไว้ที่ window (ไม่ใช่ QmlBackend) เพราะค่าที่ preset คุมส่วนใหญ่เป็น property ของ window (overlay/facecam/replay)
// และ window ถือ Settings ปัจจุบันเสมอ (setSettings ตอนเปลี่ยน profile)
#include "qmlmainwindow.h"
#include "qmlbackend.h"

#include <pswrapgameprofiles.h>

PsWrapGameProfiles *QmlMainWindow::gameProfiles()
{
	// ไม่เพิ่ม member ใน header (ลด conflict) — เก็บเป็นลูกตรงของ window แล้วหาด้วย findChild
	auto *gp = findChild<PsWrapGameProfiles *>(QString(), Qt::FindDirectChildrenOnly);
	if (!gp)
		gp = new PsWrapGameProfiles(this, backend, [this]() { return settings; });
	return gp;
}

QObject *QmlMainWindow::gameProfilesObject()
{
	return gameProfiles();
}
