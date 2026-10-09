// PS-WRAP: Discord Rich Presence — สถานะ "Playing PS-WRAP · <เกม>" บนโปรไฟล์ Discord เหมือนเกมทั่วไป
//
// คุยกับ Discord desktop ในเครื่องผ่าน IPC (named pipe \\.\pipe\discord-ipc-0..9) ตรงๆ — ไม่ใช้ SDK/DLL ภายนอก
//   frame = [op:int32 LE][len:int32 LE][JSON] · op 0 handshake {v:1, client_id} → รอ READY → op 1 SET_ACTIVITY
// ไม่ได้เปิด Discord = เงียบ ลองต่อใหม่ทุก 15 วินาที · ส่งเฉพาะตอน activity เปลี่ยน (Discord จำกัด 5 ครั้ง/20 วิ)
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

class QLocalSocket;

class PsWrapDiscord : public QObject
{
	Q_OBJECT
public:
	PsWrapDiscord(const QString &client_id, QObject *parent = nullptr);
	~PsWrapDiscord() override;

	void setEnabled(bool on);              // ปิด = ล้างสถานะแล้วตัดการเชื่อมต่อ
	void setActivity(const QJsonObject &activity);   // ว่าง = ล้างสถานะ (ไม่โชว์อะไร)

private:
	void tryConnect();
	void onConnected();
	void onDisconnected();
	void onReadyRead();
	void sendFrame(int op, const QJsonObject &payload);
	void flush();                          // ส่ง activity ล่าสุดถ้ายังไม่ได้ส่ง

	QString client_id;
	QLocalSocket *sock = nullptr;
	QTimer reconnect_timer;
	int pipe_index = 0;
	bool enabled = false;
	bool ready = false;
	bool logged_error = false;
	QJsonObject wanted;
	QByteArray sent;                       // JSON ของ activity ที่ส่งล่าสุด (กันส่งซ้ำ)
	QByteArray buf;
};
