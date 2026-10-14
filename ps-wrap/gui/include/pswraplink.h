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
// แบบห้อง (เจ้าของเลือก · จำไว้): direct = Direct P2P (ส่งตรงหาเพื่อน ≤ 4) · stage = Stage Broadcast (ส่งครั้งเดียวผ่านเซิร์ฟเวอร์
//   ด้วย WHIP → คนดูหลายคนดึง WHEP) · both = 4 คนแรกส่งตรง ที่เหลือดูผ่าน Stage · ให้จอยคนดู Stage = ย้ายเขามาส่งตรงเอง
// ทดลอง/วัด: env PSWRAP_LINK_DEV=1 แปะบาร์โค้ดเวลาไว้แถบบนของภาพ + log ผลวัดจากหน้าเว็บ (PS-WRAP-LINK/ผลการทดลอง.md)
#ifndef PSWRAP_LINK_H
#define PSWRAP_LINK_H

#include <QObject>

class QQuickImageProvider;
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
	Q_PROPERTY(QVariantList friends READ friends NOTIFY changed)    // [{peer, name, status, controller, wantsController, quality, latency, avatar, color, staff}]
	Q_PROPERTY(int friendCount READ friendCount NOTIFY changed)
	Q_PROPERTY(QString controllerPeer READ controllerPeer NOTIFY changed)
	// ทีม PS-WRAP ขอเข้าดูห้อง (จากหน้า Monitor) — เจ้าของห้องต้องกดอนุญาต · หมดอายุเอง 60 วิ
	Q_PROPERTY(bool viewRequest READ viewRequest NOTIFY changed)
	// ความดังเสียงเพื่อนแต่ละคน {peer: 0..1} ~15 ครั้ง/วิ (แยก signal — ไม่ทำให้รายชื่อสร้างใหม่) → คลื่นเสียงในการ์ด
	Q_PROPERTY(QVariantMap levels READ levels NOTIFY levelsChanged)
	// facecam ของเพื่อน {peer: เลขเฟรม} — QML แสดงผ่าน image://linkcam/<peer>?<เลข> (ภาพกลมแล้ว)
	Q_PROPERTY(QVariantMap cams READ cams NOTIFY camsChanged)
	// ปุ่มที่เพื่อน (ทุกคนที่ถือจอย) กดอยู่ รวมกันแล้ว — overlay จอยของเจ้าของเครื่องแสดงด้วย
	// {active, buttons (bitmask chiaki), lx, ly, rx, ry (-1..1), l2, r2 (0..1)}
	Q_PROPERTY(QVariantMap pad READ pad NOTIFY padChanged)
	// แบบห้อง: "direct" | "stage" | "both" — เปลี่ยนได้ระหว่างเปิด (ห้อง/ลิงก์เดิม)
	Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
	Q_PROPERTY(QString stageState READ stageState NOTIFY changed)   // off · connecting · live · error
	Q_PROPERTY(int stageViewers READ stageViewers NOTIFY changed)
	Q_PROPERTY(int stageMax READ stageMax NOTIFY changed)

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
	QVariantMap levels() const;
	QVariantMap cams() const;
	QVariantMap pad() const;
	// image provider ของ facecam (QML engine เป็นเจ้าของ) — QmlMainWindow ลงทะเบียนชื่อ "linkcam"
	QQuickImageProvider *createCamProvider();
	int friendCount() const;
	QString controllerPeer() const;
	bool viewRequest() const;
	QString mode() const;
	void setMode(const QString &mode);
	QString stageState() const;
	int stageViewers() const;
	int stageMax() const;

	Q_INVOKABLE void toggle();
	Q_INVOKABLE void start();
	Q_INVOKABLE void stop();
	Q_INVOKABLE void copyInvite();
	Q_INVOKABLE void resetInvite();
	Q_INVOKABLE void giveController(const QString &peer);
	Q_INVOKABLE void takeController();
	// ดึงจอยคืนจากคนเดียว (กลุ่มดาวถือร่วมกันได้) — คนอื่นที่ถืออยู่ยังเล่นต่อ
	Q_INVOKABLE void takeControllerFrom(const QString &peer);
	Q_INVOKABLE void kick(const QString &peer);
	Q_INVOKABLE void answerViewRequest(bool allow);
	// ติดดาวเพื่อน (จำไว้ในเครื่อง ด้วย hash ของรหัสสุ่มที่หน้าเว็บเพื่อนสร้าง) · ดาว = ขอจอยแล้วได้เลยถ้าไม่มีใครถืออยู่
	Q_INVOKABLE void setStarred(const QString &peer, bool on);
	// ไมค์ของเพื่อน: ปิด/เปิดเสียงทีละคน (เพื่อนเห็นว่าถูกปิด)
	Q_INVOKABLE void setMicMuted(const QString &peer, bool muted);
	// ปิดกล้องเพื่อน (เหมือนปิดไมค์): หน้าเว็บเขาปิดกล้อง + เราไม่รับภาพ จนกว่าจะอนุญาตใหม่
	Q_INVOKABLE void setCamBlocked(const QString &peer, bool blocked);

public slots:
	void setSession(StreamSession *session);   // GUI thread · nullptr = สตรีมจบ → ปิด Link

signals:
	void changed();
	void levelsChanged();
	void camsChanged();
	void padChanged();
	void toast(const QString &kind, const QString &title, const QString &detail);
	// แชทจากเพื่อน (ตัด/จำกัดความถี่แล้ว) → QmlMainWindow ส่งเข้า Live Chat overlay · avatar = PNG data URI ที่ encode เอง หรือว่าง
	void chatMessage(const QString &author, const QString &color, const QString &text, const QString &avatar);
	// รีแอคชันที่เพื่อนกดรัวๆ (แบบ TikTok) — kind = heart | flower | rock | wai · รวมเป็นชุด จำกัดความถี่แล้ว
	// → HeartsOverlay.qml ลอยตามจำนวน
	void reaction(const QString &name, const QString &color, const QString &kind, int count);

private:
	struct Impl;
	struct Peer;
	struct StageLeg;
	std::unique_ptr<Impl> d;

	void connectSignal();
	void onSignalMessage(const QString &text);
	void sendSignal(const QJsonObject &msg);
	void addPeer(const QString &peer, const QString &name, int height, int kbps, double pace, bool stage = false, bool staff = false);
	void setupPeerConnection(const std::shared_ptr<Peer> &p, double pace);   // offer ไปหาเพื่อน (ส่งตรง)
	void sendControl(const std::shared_ptr<Peer> &p, const QJsonObject &msg);  // data channel หรือ relay (คนดู Stage)
	void upgradeStagePeer(const QString &peer);   // คนดู Stage ได้จอย → ย้ายมาส่งตรง
	void startStage();
	void stopStage();
	std::shared_ptr<StageLeg> makeStageLeg(bool main);
	void postWhip(const std::shared_ptr<StageLeg> &leg);
	void scheduleStageRetry();
	// allow_resume: เพื่อนหลุด/รีเฟรช (ไม่ใช่ถูกเตะ) → ถ้าถือจอยอยู่ จองคืนให้คนเดิม 15 วิ
	void removePeer(const QString &peer, bool notify_server, bool allow_resume = true);
	void tryReclaim(const QString &peer);   // control channel ของเพื่อนเปิด → คืนจอยถ้าเป็นคนเดิมที่เพิ่งหลุด
	void shareAvatars(const QString &peer);   // รูปโปรไฟล์ → ทีมที่เจ้าของห้องอนุญาตให้ดู (ส่งตรง ไม่ผ่านเซิร์ฟเวอร์)
	void setPeerHeight(const QString &peer, int height);   // เพื่อนเลือกคุณภาพใหม่ (1080/720/480)
	void onPeerMessage(const QString &peer, const QJsonObject &msg);   // GUI thread (จาก data channel)
	bool ensureVideoTap(int height);
	void ensureAudioTap();
	void dropUnusedTaps();
	void releaseController(bool notify);
	void addCoHolder(const QString &peer);      // เพื่อนติดดาวขอจอยตอนมีคนถืออยู่ = ถือร่วม (ปุ่มรวมกัน)
	void pushMergedPad();                       // รวมปุ่มของทุกคนที่ถือ → StreamSession
	void pollPad();
	void setState(const QString &state);
	void setInvite(const QString &room, const QString &url);
};

#endif // PSWRAP_LINK_H
