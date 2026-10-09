// PS-WRAP: เช็ค/ติดตั้งเวอร์ชันใหม่จาก GitHub Releases (iDevGIS/PS-WRAP) — QML: Chiaki.updater
// เช็คตอนเปิดแอป (หน่วง 8 วิ) + ทุก 12 ชม. ถ้าเปิด autoCheck · รวม pre-release (เราออกแบบนั้นทุกตัว)
// ติดตั้ง: ดาวน์โหลด zip → ตรวจ SHA-256 จาก release notes → แตกด้วย tar.exe ของ Windows → สคริปต์ cmd รอแอปปิด
//         แล้ว robocopy ทับโฟลเดอร์โปรแกรม → เปิดตัวใหม่ · โฟลเดอร์เขียนไม่ได้/ไม่ใช่ตัว portable (ไม่มี qt.conf) = เปิดหน้า release แทน
#ifndef PSWRAP_UPDATER_H
#define PSWRAP_UPDATER_H

#include <QObject>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

class PsWrapUpdater : public QObject
{
	Q_OBJECT
	// idle · checking · uptodate · available · downloading · ready · error
	Q_PROPERTY(QString state READ state NOTIFY changed)
	Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
	Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
	Q_PROPERTY(QString notes READ notes NOTIFY changed)              // markdown ของ release (ตัดรูป/ส่วน Download ออก)
	Q_PROPERTY(QString pageUrl READ pageUrl NOTIFY changed)
	Q_PROPERTY(qint64 downloadSize READ downloadSize NOTIFY changed)
	Q_PROPERTY(double progress READ progress NOTIFY progressChanged) // 0..1 ตอน downloading
	Q_PROPERTY(QString error READ error NOTIFY changed)
	Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
	Q_PROPERTY(bool dismissed READ dismissed NOTIFY changed)         // ผู้ใช้กด "Later" กับเวอร์ชันนี้แล้ว (ซ่อนชิป)
	Q_PROPERTY(bool canInstall READ canInstall CONSTANT)
	Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged)

public:
	explicit PsWrapUpdater(QObject *parent = nullptr);

	QString state() const { return state_; }
	QString currentVersion() const;
	QString latestVersion() const { return latest_; }
	QString notes() const { return notes_; }
	QString pageUrl() const { return page_url_; }
	qint64 downloadSize() const { return asset_size_; }
	double progress() const { return progress_; }
	QString error() const { return error_; }
	bool updateAvailable() const;
	bool dismissed() const;
	bool canInstall() const;
	bool autoCheck() const;
	void setAutoCheck(bool on);

	Q_INVOKABLE void check(bool manual = true);
	Q_INVOKABLE void download();
	Q_INVOKABLE void install();      // ปิดแอปแล้วติดตั้ง (หลัง state = ready)
	Q_INVOKABLE void openPage();
	Q_INVOKABLE void dismiss();      // Later: ไม่เตือนเวอร์ชันนี้อีก (เวอร์ชันถัดไปเตือนใหม่)

	static int compareVersions(const QString &a, const QString &b);

signals:
	void changed();
	void progressChanged();
	void autoCheckChanged();

private:
	void setState(const QString &s, const QString &err = QString());
	void onCheckFinished(QNetworkReply *reply, bool manual);
	void onDownloadFinished();
	void extract();

	QNetworkAccessManager *net = nullptr;
	QNetworkReply *download_reply = nullptr;
	QFile *download_file = nullptr;
	QTimer periodic;
	QString state_ = QStringLiteral("idle");
	QString latest_;
	QString notes_;
	QString page_url_;
	QString asset_url_;
	QString asset_name_;
	QString asset_sha256_;
	qint64 asset_size_ = 0;
	double progress_ = 0;
	QString error_;
	QString work_dir_;
	QString extracted_dir_;
};

#endif // PSWRAP_UPDATER_H
