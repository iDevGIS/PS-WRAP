// PS-WRAP: Go Live — ไลฟ์ไปหลายแพลตฟอร์มพร้อมกัน (RTMP/RTMPS) จากภาพเดียวกับที่อัด (ดู docs/06-go-live.md)
//
// โครงสร้าง:
//   PsWrapRecorder (pipeline เดิม: จับภาพ + mix เสียงครั้งเดียว)
//     └─ PsWrapRawTap ─► LiveRendition (thread ของตัวเอง)
//           ย่อ/ใส่ขอบดำให้เป็น 16:9 → H.264 CBR (NVENC → AMF → x264) + AAC 160 kbps
//           GOP คงที่ 2 วิ · ปลายทางที่ bitrate เท่ากันใช้ packet ชุดเดียว (ค่าเริ่มต้น 6 Mbps ทุกเจ้า = encode ครั้งเดียว)
//           └─ PsWrapLiveSink × N (PsWrapPacketSink) — thread ต่อปลายทาง + คิวมีเพดาน ~4 วิ
//                 คิวเต็ม = ทิ้งทั้ง GOP เก่าสุด (ไม่ทิ้งกลาง GOP) · encoder ไม่เคยรอเน็ต
//                 libavformat "flv" → rtmp:// / rtmps:// · AVIOInterruptCB (timeout 10 วิ + stop ตัดทันที)
//                 หลุด → ต่อใหม่ backoff 1→2→4→…→30 วิ (+jitter) · key ผิด/server ปฏิเสธ = หยุด ไม่ลองซ้ำ
//
// ทำไมไม่ใช้ packet ของไฟล์อัดตรงๆ: ไฟล์อัดเป็น VBR ~20 Mbps ตามสัดส่วนหน้าต่าง (เช่น 2632×1080) และเป็น HEVC 10-bit ตอน HDR
//   ทุก ingest ต้องการ CBR ≤ 6–8 Mbps 16:9 H.264 (Twitch ≤ 6 Mbps) → encode แยก (NVENC session ที่ 2 — 4090 รับสบาย)
//   ระหว่างไลฟ์อย่างเดียว (ไม่อัด/ไม่มี replay) pipeline ข้ามการ encode ภาพของไฟล์อัดเอง (ไม่เปลือง GPU)
// HDR: ยังไม่รองรับ — สตรีม HDR = start() ปฏิเสธพร้อมข้อความ (ต้อง tone-map เป็น SDR ใน render thread ก่อน — งานต่อ)
// 9:16 (TikTok/IG): ยังไม่มี — ปลายทางพวกนี้ได้ภาพ 16:9 ไปก่อน · จุดต่อขยาย: LiveRendition::Spec (canvas/codec)
//
// stream key: อ่านจาก Windows Credential Manager (PsWrapSecrets::liveTarget(destId, profile ปัจจุบัน)) ตอน start() เท่านั้น
//   URL เต็มอยู่ในแรมของ sink เท่านั้น — log/สถานะใช้ server (ไม่มี key) เสมอ
// ค่าที่ไม่ลับ: QSettings ของ profile ปัจจุบัน key "pswrap_live/destinations" (JSON array)

#ifndef PSWRAP_LIVE_H
#define PSWRAP_LIVE_H

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>

#include <functional>
#include <memory>
#include <vector>

class Settings;
class PsWrapRecorder;
struct PsWrapRecConfig;

namespace PsWrapLiveInternal { class Rendition; class Sink; }

// สถานะต่อปลายทาง (ตรงกับ role "status")
enum class PsWrapLiveState { Idle, Connecting, Live, Reconnecting, Error };

class PsWrapLiveDestinations : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
	enum Role
	{
		DestIdRole = Qt::UserRole + 1,
		PlatformRole,
		EnabledRole,
		ServerRole,
		KeySavedRole,
		VideoBitrateRole,
		AudioBitrateRole,
		CodecRole,
		VerticalRole,
		StatusRole,
		StatusTextRole
	};

	struct Row
	{
		QString id;
		QString platform;
		bool enabled = true;
		QString server;
		bool key_saved = false;
		int video_kbps = 6000;
		int audio_kbps = 160;
		QString codec = QStringLiteral("h264");
		bool vertical = false;
		PsWrapLiveState status = PsWrapLiveState::Idle;
		QString status_text;
	};

	explicit PsWrapLiveDestinations(QObject *parent = nullptr) : QAbstractListModel(parent) {}
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;
	int count() const { return static_cast<int>(rows.size()); }

	const std::vector<Row> &all() const { return rows; }
	int indexOf(const QString &id) const;
	const Row *find(const QString &id) const;
	void reset(std::vector<Row> new_rows);
	void append(const Row &row);
	void remove(const QString &id);
	// แก้แถวเดียว → dataChanged แถวนั้น (ห้าม reset — delegate ถูกสร้างใหม่ = focus จอยหลุด)
	bool update(const QString &id, const std::function<void(Row &)> &fn);

signals:
	void countChanged();

private:
	std::vector<Row> rows;
};

class PsWrapGoLive : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QVariantList platforms READ platforms CONSTANT)
	Q_PROPERTY(QObject *destinations READ destinationsObject CONSTANT)
	Q_PROPERTY(bool live READ isLive NOTIFY liveChanged)                         // กำลังไลฟ์ (ตั้งแต่ start จน stop) → หน้า settings ล็อก
	Q_PROPERTY(QString state READ state NOTIFY stateChanged)                     // "off" / "connecting" / "live" / "reconnecting" / "error" (รวมทุกปลายทาง)
	Q_PROPERTY(QString summary READ summary NOTIFY stateChanged)                 // เช่น "Live on 2 of 3"
	Q_PROPERTY(int seconds READ seconds NOTIFY secondsChanged)                   // วินาทีตั้งแต่ start
	Q_PROPERTY(int enabledCount READ enabledCount NOTIFY destinationsChanged)    // ปลายทางที่เปิดอยู่ (0 = ปุ่ม Live ไม่มีอะไรให้ทำ)
	Q_PROPERTY(bool secureStoreAvailable READ secureStoreAvailable CONSTANT)
	Q_PROPERTY(int totalKbps READ totalKbps NOTIFY destinationsChanged)
	Q_PROPERTY(bool twitchParityWarning READ twitchParityWarning NOTIFY destinationsChanged)
	Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
	// settings_getter: Settings ของ profile ปัจจุบัน (เปลี่ยนได้ตอนสลับ profile)
	// config_getter: สเปค pipeline จากหน้าต่าง + สตรีม (QmlMainWindow::pswrapBuildRecConfig) — false + ข้อความ = ยังสตรีมไม่ได้
	PsWrapGoLive(QObject *parent, PsWrapRecorder *recorder, std::function<Settings *()> settings_getter,
	             std::function<bool(PsWrapRecConfig *, QString *)> config_getter);
	~PsWrapGoLive() override;

	QVariantList platforms() const;
	QObject *destinationsObject() { return &model; }
	bool isLive() const { return active; }
	QString state() const { return state_value; }
	QString summary() const { return summary_value; }
	int seconds() const { return seconds_value; }
	int enabledCount() const;
	bool secureStoreAvailable() const;
	int totalKbps() const;
	bool twitchParityWarning() const;
	QString lastError() const { return last_error; }

	Q_INVOKABLE QString addDestination(const QString &platform_id);
	Q_INVOKABLE void removeDestination(const QString &dest_id);
	Q_INVOKABLE void setDestinationValue(const QString &dest_id, const QString &key, const QVariant &value);
	Q_INVOKABLE bool setStreamKey(const QString &dest_id, const QString &key);
	Q_INVOKABLE void clearStreamKey(const QString &dest_id);
	Q_INVOKABLE QString revealStreamKey(const QString &dest_id);

	Q_INVOKABLE bool start();
	Q_INVOKABLE void stop();
	Q_INVOKABLE void toggle();

	void reloadIfSettingsChanged();   // สลับ profile → โหลดปลายทางของ profile ใหม่ (ถ้าไม่ได้ไลฟ์อยู่)

signals:
	void liveChanged();
	void stateChanged();
	void secondsChanged();
	void destinationsChanged();
	void lastErrorChanged();
	// แจ้งผู้ใช้แบบสั้น (Main.qml → toast) · kind = "success" / "info" / "error"
	void toast(const QString &kind, const QString &title, const QString &detail);

private:
	struct Active;
	PsWrapRecorder *recorder = nullptr;
	std::function<Settings *()> settings_getter;
	std::function<bool(PsWrapRecConfig *, QString *)> config_getter;
	Settings *loaded_from = nullptr;
	PsWrapLiveDestinations model;
	bool active = false;
	std::unique_ptr<Active> run;
	QTimer poll_timer;
	qint64 start_ms = 0;
	int seconds_value = 0;
	QString state_value = QStringLiteral("off");
	QString summary_value;
	QString last_error;

	void load();
	void store();
	QString profileName() const;
	QString target(const QString &dest_id) const;
	void setLastError(const QString &e);
	void poll();
	void tapDetached(PsWrapLiveInternal::Rendition *r);
	void finishStop(bool notify_failure, const QString &reason);
};

#endif // PSWRAP_LIVE_H
