// PS-WRAP: ส่วนของ QmlMainWindow ที่เกี่ยวกับ PS-WRAP Link (spike) — engine อยู่ใน pswraplink.cpp
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
#include "qmlmainwindow.h"
#include "qmlbackend.h"

#include <pswraplink.h>
#include <pswraprecorder.h>

void QmlMainWindow::pswrapLinkInit()
{
	const PsWrapLink::Mode mode = PsWrapLink::envMode();
	if (mode == PsWrapLink::Mode::Off || !backend || findChild<PsWrapLink *>(QString(), Qt::FindDirectChildrenOnly))
		return;
	// สเปคภาพ = สเปคเดียวกับไฟล์อัด/Go Live (หน้าต่าง + สตรีม)
	auto *link = new PsWrapLink(this, pswrap_recorder, [this](PsWrapRecConfig *cfg, QString *error) { return pswrapBuildRecConfig(cfg, error); }, mode);
	connect(backend, &QmlBackend::sessionChanged, link, &PsWrapLink::setSession);
}
