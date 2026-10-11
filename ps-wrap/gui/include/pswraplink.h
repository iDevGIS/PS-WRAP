// PS-WRAP Link (spike) — ให้เพื่อนดูภาพ PS-WRAP และกดจอยแทนได้ผ่าน WebRTC (libdatachannel)
//
// ยังเป็นตัวทดลองวัด latency กับเกมจริง (repo PS-WRAP-LINK, ผลการทดลอง.md) — ยังไม่ใช่ฟีเจอร์ให้ผู้ใช้:
//   - build: -DPSWRAP_LINK=ON -DPSWRAP_LIBDATACHANNEL_DIR=<PS-WRAP-LINK/spike/deps/libdatachannel>
//   - เปิดด้วย env PSWRAP_LINK_DEV=1 (ส่งภาพ) หรือ 2 (ส่งภาพ + รับจอยเพื่อน) · ไม่ตั้ง = ไม่เปิดพอร์ตใดๆ
//   - signaling มี 2 แบบ:
//       ตั้ง PSWRAP_LINK_SIGNAL=wss://link.ps-wrap.com/ws/host + PSWRAP_LINK_HOST_KEY → ต่อออกไปที่เซิร์ฟเวอร์ (ไม่เปิดพอร์ต)
//         ได้ลิงก์เชิญ → คัดลอกลง clipboard · เซิร์ฟเวอร์ให้ TURN credential · หน้าเว็บ = PS-WRAP-LINK/web
//       ไม่ตั้ง → WebSocket server :18800 ใน LAN ไม่มี auth (ทดสอบเท่านั้น) · หน้าเว็บ = PS-WRAP-LINK/spike/latency/web
//
// ภาพ: raw tap ของ recorder (ภาพที่เห็นบนจอ รวม overlay) → NVENC low-latency → WebRTC H.264
//   แปะบาร์โค้ดเวลา "ตอน render thread จับภาพ" ไว้แถบบนสุด → หน้าเว็บวัด latency จากจอเราถึงจอเพื่อน
// จอย: data channel "input" ข้อความ pad (Gamepad API standard mapping) → StreamSession::PsWrapSetLinkState
//   OR รวมกับจอยของเรา · ไม่มีข้อความ 500 ms / หลุด = ปล่อยทุกปุ่ม
#ifndef PSWRAP_LINK_H
#define PSWRAP_LINK_H

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>
#include <string>

class PsWrapRecorder;
class QJsonObject;
class StreamSession;
struct PsWrapRecConfig;

class PsWrapLink : public QObject
{
	Q_OBJECT

public:
	enum class Mode { Off, Video, VideoInput };
	static Mode envMode();

	// อายุเท่าหน้าต่างหลัก (ลูกของ QmlMainWindow)
	PsWrapLink(QObject *parent, PsWrapRecorder *recorder, std::function<bool(PsWrapRecConfig *, QString *)> config_getter, Mode mode);
	~PsWrapLink() override;

public slots:
	void setSession(StreamSession *session);   // GUI thread · nullptr = สตรีมจบ → หยุด

private:
	struct Impl;
	std::unique_ptr<Impl> d;
	bool tryStart();
	void stop();
	void pollPad();
	// signaling: openPeer สร้าง PeerConnection + track + data channel แล้วส่ง offer ผ่าน d->signal_out (thread ใดก็ได้)
	void openPeer(int height, int kbps, double pace);
	void onRemoteAnswer(const std::string &sdp);
	void connectSignal();   // โหมดเซิร์ฟเวอร์ (PSWRAP_LINK_SIGNAL) — GUI thread
	void sendSignal(const QJsonObject &msg);   // thread ใดก็ได้
	void onSignalMessage(const QString &text); // GUI thread
};

#endif // PSWRAP_LINK_H
