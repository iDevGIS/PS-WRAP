// PS-WRAP Link — ให้เพื่อนดูเกมจากเบราว์เซอร์ และส่งจอยให้เพื่อนเล่นแทนได้ (WebRTC ผ่าน libdatachannel)
//
// build: -DPSWRAP_LINK=ON -DPSWRAP_LIBDATACHANNEL_DIR=<PS-WRAP-LINK/spike/deps/libdatachannel> (ปิดไว้เป็นค่าเริ่มต้น)
// ใช้: เมนูสตรีม › CAPTURE › Link (Ctrl+Shift+J) → ต่อออกไปที่ link.ps-wrap.com → ได้ลิงก์เชิญ (คัดลอกให้ + QR)
//   เพื่อนเปิดลิงก์ในเบราว์เซอร์ = ดูอย่างเดียว · กด "ขอจอย" → เราอนุญาต/ดึงคืน/เตะได้จากการ์ด Link
//   PS5 รับจอยตัวเดียว → ถือจอยได้ทีละคน (OR รวมกับจอยของเรา) · เงียบ 500 ms / หลุด = ปล่อยทุกปุ่ม
//
// ภาพ: raw tap ของ recorder (ภาพที่เห็นบนจอ) → ย่อตามความสูงที่เพื่อนขอ → NVENC low-latency → H.264
//   encoder 1 ตัวต่อความสูง (1080 8M / 720 5M / 480 2.5M — เพื่อนเลือกได้) ส่งให้ทุกคนในกลุ่ม · ไม่มีคนในกลุ่ม = ถอด tap (ไม่กิน GPU)
// เสียง: "Game + Mic" ของ recorder → Opus 10 ms → ทุกคน (คนละ stream กับภาพ — เบราว์เซอร์ไม่หน่วงภาพรอ sync เสียง)
// signaling: QWebSocket (ตรวจใบรับรอง TLS) — rtc::WebSocket บน Windows ไม่ตรวจ
// host key: Windows Credential Manager "PS-WRAP/link/host-key" (env PSWRAP_LINK_HOST_KEY ครั้งแรก = นำเข้าให้)
// ทดลอง/วัด: env PSWRAP_LINK_DEV=1 แปะบาร์โค้ดเวลาไว้แถบบนของภาพ + log ผลวัดจากหน้าเว็บ (PS-WRAP-LINK/ผลการทดลอง.md)
#ifndef PSWRAP_LINK_H
#define PSWRAP_LINK_H

#include <QObject>
#include <QString>
#include <QVariantList>

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
	Q_PROPERTY(bool available READ available NOTIFY changed)        // มี host key — ไม่มี = ปุ่มกดไม่ได้
	Q_PROPERTY(bool active READ active NOTIFY changed)              // เปิด Link อยู่ (ต่อ/พร้อม)
	Q_PROPERTY(QString state READ state NOTIFY changed)             // off · connecting · ready · error
	Q_PROPERTY(QString room READ room NOTIFY changed)
	Q_PROPERTY(QString inviteUrl READ inviteUrl NOTIFY changed)     // มีรหัสเชิญ — อย่า log
	Q_PROPERTY(QString inviteQr READ inviteQr NOTIFY changed)       // data:image/svg+xml;base64,…
	Q_PROPERTY(QVariantList friends READ friends NOTIFY changed)    // [{peer, name, status, controller, wantsController, quality, latency, avatar, color}]
	Q_PROPERTY(int friendCount READ friendCount NOTIFY changed)
	Q_PROPERTY(QString controllerPeer READ controllerPeer NOTIFY changed)

public:
	// อายุเท่าหน้าต่างหลัก (ลูกของ QmlMainWindow)
	PsWrapLink(QObject *parent, PsWrapRecorder *recorder, std::function<bool(PsWrapRecConfig *, QString *)> config_getter);
	~PsWrapLink() override;

	bool available() const;
	bool active() const;
	QString state() const;
	QString room() const;
	QString inviteUrl() const;
	QString inviteQr() const;
	QVariantList friends() const;
	int friendCount() const;
	QString controllerPeer() const;

	Q_INVOKABLE void toggle();
	Q_INVOKABLE void start();
	Q_INVOKABLE void stop();
	Q_INVOKABLE void copyInvite();
	Q_INVOKABLE void resetInvite();
	Q_INVOKABLE void giveController(const QString &peer);
	Q_INVOKABLE void takeController();
	Q_INVOKABLE void kick(const QString &peer);
	// ไมค์ของเพื่อน: ปิด/เปิดเสียงทีละคน (เพื่อนเห็นว่าถูกปิด)
	Q_INVOKABLE void setMicMuted(const QString &peer, bool muted);

public slots:
	void setSession(StreamSession *session);   // GUI thread · nullptr = สตรีมจบ → ปิด Link

signals:
	void changed();
	void toast(const QString &kind, const QString &title, const QString &detail);
	// แชทจากเพื่อน (ตัด/จำกัดความถี่แล้ว) → QmlMainWindow ส่งเข้า Live Chat overlay · avatar = PNG data URI ที่ encode เอง หรือว่าง
	void chatMessage(const QString &author, const QString &color, const QString &text, const QString &avatar);
	// รีแอคชันที่เพื่อนกดรัวๆ (แบบ TikTok) — kind = heart | flower | rock | wai · รวมเป็นชุด จำกัดความถี่แล้ว
	// → HeartsOverlay.qml ลอยตามจำนวน
	void reaction(const QString &name, const QString &color, const QString &kind, int count);

private:
	struct Impl;
	struct Peer;
	std::unique_ptr<Impl> d;

	void connectSignal();
	void onSignalMessage(const QString &text);
	void sendSignal(const QJsonObject &msg);
	void addPeer(const QString &peer, const QString &name, int height, int kbps, double pace);
	// allow_resume: เพื่อนหลุด/รีเฟรช (ไม่ใช่ถูกเตะ) → ถ้าถือจอยอยู่ จองคืนให้คนเดิม 15 วิ
	void removePeer(const QString &peer, bool notify_server, bool allow_resume = true);
	void tryReclaim(const QString &peer);   // control channel ของเพื่อนเปิด → คืนจอยถ้าเป็นคนเดิมที่เพิ่งหลุด
	void setPeerHeight(const QString &peer, int height);   // เพื่อนเลือกคุณภาพใหม่ (1080/720/480)
	void onPeerMessage(const QString &peer, const QJsonObject &msg);   // GUI thread (จาก data channel)
	bool ensureVideoTap(int height);
	void ensureAudioTap();
	void dropUnusedTaps();
	void releaseController(bool notify);
	void pollPad();
	void setState(const QString &state);
	void setInvite(const QString &room, const QString &url);
};

#endif // PSWRAP_LINK_H
