// PS-WRAP: แชทไลฟ์บนจอ — overlay (ChatOverlay.qml) + ตัวดึงข้อความ (pswrapchat.cpp)
// แชทต่อเองเมื่อเปิด overlay ระหว่างสตรีม · ปิด overlay / สตรีมจบ = หยุด (ไม่กิน quota YouTube ตอนไม่ได้ใช้)
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
#include "qmlmainwindow.h"
#include "qmlbackend.h"

#include <pswrapchat.h>

bool QmlMainWindow::chatOverlay() const { return settings->GetChatOverlay(); }

void QmlMainWindow::setChatOverlay(bool v)
{
	if (v == settings->GetChatOverlay())
		return;
	settings->SetChatOverlay(v);
	emit chatOverlayChanged();
	pswrapSyncChat();
}

PsWrapLiveChat *QmlMainWindow::liveChat()
{
	auto *c = findChild<PsWrapLiveChat *>(QString(), Qt::FindDirectChildrenOnly);
	if (c)
		return c;
	c = new PsWrapLiveChat(this, [this]() { return settings; });
	if (backend)
		connect(backend, &QmlBackend::sessionChanged, this, [this](StreamSession *) {
			QMetaObject::invokeMethod(this, [this]() { pswrapSyncChat(); }, Qt::QueuedConnection);
		});
	// เปลี่ยนแหล่งระหว่างที่ต่ออยู่ → ต่อใหม่ด้วยค่าใหม่
	connect(c, &PsWrapLiveChat::sourcesChanged, this, [this, c]() {
		if (c->isRunning())
			c->start();
	});
	// สร้างตอนสตรีมเริ่มไปแล้ว (QML แตะ liveChat ครั้งแรกใน StreamView) → sync สถานะตอนนี้เลย
	QMetaObject::invokeMethod(this, [this]() { pswrapSyncChat(); }, Qt::QueuedConnection);
	return c;
}

QObject *QmlMainWindow::liveChatObject() { return liveChat(); }

void QmlMainWindow::pswrapSyncChat()
{
	PsWrapLiveChat *c = liveChat();
	const bool want = (settings->GetChatOverlay() || settings->GetVerticalChat()) && session;   // การ์ดบนจอ หรือแชทในภาพแนวตั้ง
	if (want && !c->isRunning()) {
		c->clear();
		c->start();
	} else if (!want && c->isRunning()) {
		c->stop();
	}
}
