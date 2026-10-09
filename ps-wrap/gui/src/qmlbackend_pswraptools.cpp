// PS-WRAP: อัปเดตในแอป + ตรวจการเชื่อมต่อ — object สร้างครั้งแรกที่ QML ขอ (Chiaki.updater / Chiaki.netCheck)
#include "qmlbackend.h"
#include "pswrapnetcheck.h"
#include "pswrapupdater.h"

QObject *QmlBackend::updaterObject() const
{
    if (!pswrap_updater)
        pswrap_updater = new PsWrapUpdater(const_cast<QmlBackend *>(this));
    return pswrap_updater;
}

QObject *QmlBackend::netCheckObject() const
{
    if (!pswrap_netcheck)
        pswrap_netcheck = new PsWrapNetCheck(const_cast<QmlBackend *>(this));
    return pswrap_netcheck;
}
