// PS-WRAP: ส่วนของ QmlMainWindow ที่เกี่ยวกับ Go Live (Chiaki.window.goLive = Chiaki.goLive) — engine อยู่ใน pswraplive.cpp
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
#include "qmlmainwindow.h"
#include "qmlbackend.h"
#include "qmlsettings.h"

#include <pswraplive.h>
#include <pswraprecorder.h>

PsWrapGoLive *QmlMainWindow::goLive()
{
	auto *g = findChild<PsWrapGoLive *>(QString(), Qt::FindDirectChildrenOnly);
	if (g)
		return g;
	// สเปคภาพ = สเปคเดียวกับไฟล์อัด/replay (หน้าต่าง + สตรีม) · Settings ตาม profile ปัจจุบัน (--profile ทดสอบไม่ทับ key จริง)
	g = new PsWrapGoLive(this, pswrap_recorder, [this]() { return settings; },
	                     [this](PsWrapRecConfig *cfg, QString *error) { return pswrapBuildRecConfig(cfg, error); });
	if (backend) {
		// สตรีมจบ = หยุดไลฟ์ (pipeline ปิดตามเมื่อไม่มีใครใช้)
		connect(backend, &QmlBackend::sessionChanged, g, [g](StreamSession *s) {
			if (!s)
				g->stop();
		});
		// เปลี่ยน profile = Settings ตัวใหม่ → QmlSettings::setSettings ยิง remotePlayAskChanged (แบบเดียวกับ PsWrapGameProfiles)
		if (QmlSettings *qs = backend->qmlSettings())
			connect(qs, &QmlSettings::remotePlayAskChanged, g, &PsWrapGoLive::reloadIfSettingsChanged);
	}
	connect(g, &PsWrapGoLive::liveChanged, this, [this]() { pswrapRefreshTray(); });
	connect(g, &PsWrapGoLive::stateChanged, this, [this]() { pswrapRefreshTray(); });
	connect(g, &PsWrapGoLive::destinationsChanged, this, [this]() { pswrapRefreshTray(); });
	return g;
}

QObject *QmlMainWindow::goLiveObject()
{
	return goLive();
}
