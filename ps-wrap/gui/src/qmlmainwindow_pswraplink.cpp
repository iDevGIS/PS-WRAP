// PS-WRAP: ส่วนของ QmlMainWindow ที่เกี่ยวกับ PS-WRAP Link (Chiaki.link) — engine อยู่ใน pswraplink.cpp
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
// คอมไพล์เสมอ · build ที่ไม่เปิด PSWRAP_LINK → linkObject() = nullptr (QML ซ่อนปุ่ม Link)
#include "qmlmainwindow.h"
#include "qmlbackend.h"

#ifdef PSWRAP_LINK
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <pswrapchat.h>
#include <pswraplink.h>
#include <pswraprecorder.h>
#endif

void QmlMainWindow::pswrapLinkInit()
{
#ifdef PSWRAP_LINK
	if (!backend || findChild<PsWrapLink *>(QString(), Qt::FindDirectChildrenOnly))
		return;
	// สเปคภาพ = สเปคเดียวกับไฟล์อัด/Go Live (หน้าต่าง + สตรีม)
	auto *link = new PsWrapLink(this, pswrap_recorder, [this](PsWrapRecConfig *cfg, QString *error) { return pswrapBuildRecConfig(cfg, error); });
	connect(backend, &QmlBackend::sessionChanged, link, &PsWrapLink::setSession);
	if (qml_engine)
		qml_engine->addImageProvider(QStringLiteral("linkcam"), link->createCamProvider());   // facecam เพื่อน (FacecamOverlay.qml)
	// แชทจากเพื่อน → Live Chat overlay (จุดฟ้า + รูปโปรไฟล์) · ปิด overlay อยู่ = เด้งเป็น toast แทน (ไม่ให้ข้อความหายเงียบ)
	connect(link, &PsWrapLink::chatMessage, this, [this, link](const QString &author, const QString &color, const QString &text, const QString &avatar) {
		liveChat()->addExternal(QStringLiteral("link"), author, color, text, avatar);
		if (!chatOverlay())
			emit link->toast(QStringLiteral("info"), author, text);
	});
#endif
}

QObject *QmlMainWindow::linkObject()
{
#ifdef PSWRAP_LINK
	return findChild<PsWrapLink *>(QString(), Qt::FindDirectChildrenOnly);
#else
	return nullptr;
#endif
}
