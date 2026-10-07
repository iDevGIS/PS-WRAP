// PS-WRAP: หน้า About — เปิดไฟล์ license / third-party notices ที่วางข้าง PS-WRAP.exe ในแพ็กเกจ release
// (build ที่รันจากโฟลเดอร์ build ไม่มีไฟล์พวกนี้ → QML ซ่อนปุ่ม)
#include "qmlmainwindow.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

static QString pswrapAppFilePath(const QString &name)
{
	// ชื่อไฟล์/โฟลเดอร์เดียวข้าง exe เท่านั้น — กันการส่ง path ออกนอกโฟลเดอร์โปรแกรม
	if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name.contains(QLatin1String("..")))
		return QString();
	return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

bool QmlMainWindow::appFileExists(const QString &name) const
{
	const QString path = pswrapAppFilePath(name);
	return !path.isEmpty() && QFileInfo::exists(path);
}

void QmlMainWindow::openAppFile(const QString &name)
{
	if (!appFileExists(name))
		return;
	QDesktopServices::openUrl(QUrl::fromLocalFile(pswrapAppFilePath(name)));
}
