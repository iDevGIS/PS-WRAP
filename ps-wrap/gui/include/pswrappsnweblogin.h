// PS-WRAP: หน้าต่างล็อกอิน PlayStation Network ในแอป (Windows WebView2 = Edge ที่มากับ Windows 10/11)
// build Windows ของเรา (MSYS2 mingw64) ไม่มี QtWebEngine → เดิมผู้ใช้ต้อง copy ลิงก์ไปเบราว์เซอร์แล้ว copy URL หลัง redirect กลับมาวางเอง
// คลาสนี้เปิดหน้า login ของ Sony ในหน้าต่างของเราเอง แล้วดัก navigation ไปหน้า redirect (มี ?code=) ให้อัตโนมัติ
// WebView2Loader.dll (BSD-3, third-party/webview2) โหลดตอนรันจากโฟลเดอร์โปรแกรม — ไม่มี DLL/ไม่มี runtime = available() เป็น false
#ifndef PSWRAP_PSNWEBLOGIN_H
#define PSWRAP_PSNWEBLOGIN_H

#include <QObject>
#include <QString>
#include <QUrl>

class PsWrapPsnWebLogin : public QObject
{
	Q_OBJECT

public:
	explicit PsWrapPsnWebLogin(QObject *parent = nullptr);
	~PsWrapPsnWebLogin() override;

	// มี WebView2Loader.dll + Edge WebView2 Runtime ในเครื่อง
	static bool available();
	// เปิดหน้าต่าง (หรือดึงขึ้นหน้าถ้าเปิดอยู่) · owner = หน้าต่างหลัก (HWND) · redirectPrefix = PSNAuth::REDIRECT_PAGE
	// forgetAccount = ลบคุกกี้ก่อนโหลด (เข้าด้วยบัญชีอื่น) — คุกกี้เก็บใน %LOCALAPPDATA%/PS-WRAP/PS-WRAP/psn-webview2
	bool start(const QUrl &url, const QString &redirectPrefix, quintptr ownerWindow, const QString &title, bool forgetAccount = false);
	void close();
	bool isOpen() const;

signals:
	void redirected(const QString &url);   // ได้ URL redirect ที่มี code แล้ว (หน้าต่างปิดเอง)
	void failed(const QString &error);      // เปิด WebView2 ไม่สำเร็จ
	void closed();                          // ผู้ใช้ปิดหน้าต่างเองก่อนล็อกอินเสร็จ

private:
	struct Impl;
	Impl *d;
};

#endif // PSWRAP_PSNWEBLOGIN_H
