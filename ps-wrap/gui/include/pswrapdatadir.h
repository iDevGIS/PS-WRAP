// PS-WRAP: โฟลเดอร์ไฟล์ข้อมูลที่วางมากับโปรแกรม (models/, fx/, THIRD-PARTY-NOTICES.txt, licenses/)
// Windows/Linux = ข้าง exe (applicationDirPath) · macOS = Contents/Resources (ไฟล์ที่ไม่ใช่โค้ดอยู่ใน Contents/MacOS ทำ codesign ไม่ผ่าน)
#ifndef PSWRAP_DATADIR_H
#define PSWRAP_DATADIR_H

#include <QCoreApplication>
#include <QDir>
#include <QString>

inline QString pswrapDataDir()
{
#ifdef Q_OS_MACOS
	const QDir app(QCoreApplication::applicationDirPath());
	if (app.absolutePath().endsWith(QStringLiteral("/Contents/MacOS")))
		return QDir::cleanPath(app.absolutePath() + QStringLiteral("/../Resources"));
#endif
	return QCoreApplication::applicationDirPath();
}

#endif
