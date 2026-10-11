// PS-WRAP Link (spike) — ให้เพื่อนดูภาพ PS-WRAP และกดจอยแทนได้ผ่าน WebRTC (libdatachannel)
//
// ยังเป็นตัวทดลองวัด latency กับเกมจริง (repo PS-WRAP-LINK, ผลการทดลอง.md) — ยังไม่ใช่ฟีเจอร์ให้ผู้ใช้:
//   - build: -DPSWRAP_LINK=ON -DPSWRAP_LIBDATACHANNEL_DIR=<PS-WRAP-LINK/spike/deps/libdatachannel>
//   - เปิดด้วย env PSWRAP_LINK_DEV=1 (ส่งภาพ) หรือ 2 (ส่งภาพ + รับจอยเพื่อน) · ไม่ตั้ง = ไม่เปิดพอร์ตใดๆ
//   - signaling = WebSocket :18800 ไม่มี auth (ใช้ใน LAN/ทดสอบเท่านั้น) · หน้าเว็บ = PS-WRAP-LINK/spike/latency/web/index.html
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

class PsWrapRecorder;
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
};

#endif // PSWRAP_LINK_H
