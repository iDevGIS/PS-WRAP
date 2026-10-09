// PS-WRAP: ตรวจการเชื่อมต่อไปหาเครื่อง แล้วให้ QML แนะนำค่าสตรีม (ConnectionCheckDialog.qml) — QML: Chiaki.netCheck
// วัด: (1) อะแดปเตอร์ที่ใช้ไปหาเครื่อง — สาย/Wi-Fi, ความเร็ว link, สัญญาณ/มาตรฐาน/ย่าน Wi-Fi (iphlpapi + wlanapi)
//      (2) SRCH (discovery) 1 ครั้ง → เครื่องอยู่ไหม / ตื่นหรือพัก ("620 Server Standby")
//          ไม่ใช้ SRCH วัด ping: PS5 หน่วงคำตอบ discovery ไว้เอง ~160 ms แม้ต่อสายและเครื่องตื่น (ICMP ได้ 1 ms — ตรวจ 2026-10-09)
//      (3) ping/jitter/loss = ICMP echo 30 ครั้ง (IcmpSendEcho — ไม่ต้อง admin) ใน thread แยก
// ไม่แตะ lib/
#ifndef PSWRAP_NETCHECK_H
#define PSWRAP_NETCHECK_H

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <memory>

class QUdpSocket;

class PsWrapNetCheck : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool running READ running NOTIFY runningChanged)
	Q_PROPERTY(int done READ done NOTIFY progressChanged)
	Q_PROPERTY(int total READ total CONSTANT)
	Q_PROPERTY(QVariantMap result READ result NOTIFY finished)

public:
	explicit PsWrapNetCheck(QObject *parent = nullptr);
	~PsWrapNetCheck() override;

	bool running() const { return running_; }
	int done() const { return done_; }
	int total() const { return kPings; }
	QVariantMap result() const { return result_; }

	Q_INVOKABLE void run(const QString &address, bool ps5);
	Q_INVOKABLE void cancel();

signals:
	void runningChanged();
	void progressChanged();
	void finished();

private:
	static constexpr int kPings = 30;
	static constexpr int kSrchTries = 3;
	void sendSrch();
	void onReadyRead();
	void onSrchTimeout();
	void startPings();
	void finish(const QVector<double> &rtts, int sent);
	void fail(const QString &error);
	QVariantMap linkInfo() const;

	QUdpSocket *socket = nullptr;
	QTimer srch_timeout;
	QString address_;
	quint32 ipv4_ = 0;
	quint16 port_ = 9302;
	QByteArray probe_;
	bool running_ = false;
	int srch_tries_ = 0;
	bool reachable_ = false;
	bool standby_ = false;
	int done_ = 0;
	quint64 generation_ = 0;
	std::shared_ptr<std::atomic_bool> cancel_flag_;
	QVariantMap result_;
};

#endif // PSWRAP_NETCHECK_H
