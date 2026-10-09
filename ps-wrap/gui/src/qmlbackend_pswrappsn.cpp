// PS-WRAP: ล็อกอิน PSN แบบง่าย — หน้าต่าง WebView2 ดัก redirect ให้เอง (pswrappsnweblogin.cpp)
// + ทางสำรองสำหรับเบราว์เซอร์ภายนอก: อ่าน URL redirect จาก clipboard ให้ ไม่ต้องกดวาง
#include "qmlbackend.h"
#include "qmlmainwindow.h"
#include "psnaccountid.h"
#include "pswrappsnweblogin.h"

#include <QClipboard>
#include <QGuiApplication>

bool QmlBackend::psnWebLoginAvailable() const
{
    return PsWrapPsnWebLogin::available();
}

bool QmlBackend::psnWebLogin(bool differentAccount)
{
    if (!psn_web_login) {
        psn_web_login = new PsWrapPsnWebLogin(this);
        connect(psn_web_login, &PsWrapPsnWebLogin::redirected, this, &QmlBackend::psnWebLoginRedirect);
        connect(psn_web_login, &PsWrapPsnWebLogin::failed, this, &QmlBackend::psnWebLoginFailed);
        connect(psn_web_login, &PsWrapPsnWebLogin::closed, this, &QmlBackend::psnWebLoginClosed);
    }
    return psn_web_login->start(psnLoginUrl(), QString::fromStdString(PSNAuth::REDIRECT_PAGE),
        window ? quintptr(window->winId()) : 0, tr("Sign in to PlayStation Network"), differentAccount);
}

void QmlBackend::psnWebLoginClose()
{
    if (psn_web_login)
        psn_web_login->close();
}

QString QmlBackend::psnClipboardRedirect() const
{
    const QString text = QGuiApplication::clipboard()->text().trimmed();
    if (text.startsWith(QString::fromStdString(PSNAuth::REDIRECT_PAGE)) && text.contains(QStringLiteral("code=")))
        return text;
    return QString();
}
