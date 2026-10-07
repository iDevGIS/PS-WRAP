// PS-WRAP: แชทไลฟ์บนจอ — ดึงข้อความจาก YouTube (Data API v3 + API key) และ Twitch (IRC อ่านอย่างเดียว ไม่ต้อง login)
//
// YouTube: แหล่ง = @handle / channel ID (UC…) / ลิงก์วิดีโอ หรือ video ID
//   → (handle → channel) → ไลฟ์ที่กำลังออนแอร์ของช่อง (search.list eventType=live — 100 quota ต่อครั้ง)
//   → videos.list liveStreamingDetails.activeLiveChatId → liveChatMessages.list ตาม pollingIntervalMillis (≥ 5 วิ, 5 quota ต่อครั้ง)
//   API key อยู่ใน Windows Credential Manager (PsWrapSecrets) ไม่ลง settings
// Twitch: IRC over TLS irc.chat.twitch.tv:6697 เป็น justinfan<เลขสุ่ม> (anonymous) + tags (ชื่อแสดง/สี)
// GUI thread ทั้งหมด (QNetworkAccessManager / QSslSocket อยู่บน event loop หลัก)
#ifndef PSWRAP_CHAT_H
#define PSWRAP_CHAT_H

#include <QElapsedTimer>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSslSocket>
#include <QTimer>
#include <QVariantList>

#include <functional>

class Settings;
class QNetworkReply;

class PsWrapLiveChat : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)   // [{platform, author, color, text, badge}] เก่า → ใหม่
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)             // สถานะสั้นๆ (เชื่อมต่อ / ไม่มีไลฟ์ / quota หมด …)
	Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
	Q_PROPERTY(QString youtubeSource READ youtubeSource WRITE setYoutubeSource NOTIFY sourcesChanged)
	Q_PROPERTY(QString twitchChannel READ twitchChannel WRITE setTwitchChannel NOTIFY sourcesChanged)
	Q_PROPERTY(bool youtubeKeySaved READ youtubeKeySaved NOTIFY sourcesChanged)

public:
	PsWrapLiveChat(QObject *parent, std::function<Settings *()> settings_getter);
	~PsWrapLiveChat() override;

	QVariantList messages() const { return message_list; }
	QString status() const { return status_value; }
	bool isRunning() const { return running; }
	QString youtubeSource() const;
	void setYoutubeSource(const QString &v);
	QString twitchChannel() const;
	void setTwitchChannel(const QString &v);
	bool youtubeKeySaved() const;

	Q_INVOKABLE bool setYoutubeApiKey(const QString &key);   // ว่าง = ลบ
	Q_INVOKABLE void start();   // ต่อทุกแหล่งที่ตั้งไว้ (ไม่มีแหล่งไหนเลย = status บอก)
	Q_INVOKABLE void stop();
	Q_INVOKABLE void clear();

signals:
	void messagesChanged();
	void statusChanged();
	void runningChanged();
	void sourcesChanged();

private:
	std::function<Settings *()> settings_getter;
	QVariantList message_list;
	QString status_value;
	bool running = false;
	int generation = 0;   // เพิ่มทุก start/stop — reply เก่าที่มาหลังหยุดถูกทิ้ง

	// YouTube
	QNetworkAccessManager nam;
	QTimer yt_timer;
	QString yt_key;
	QString yt_channel, yt_video, yt_chat_id, yt_page_token;
	bool yt_first_page = true;
	int yt_search_tries = 0;
	QString yt_status, tw_status;
	void ytBegin();
	void ytResolveHandle(const QString &handle);
	void ytFindLive();
	void ytFetchChatId();
	void ytPoll();
	void ytGet(const QString &path, const QList<QPair<QString, QString>> &query, std::function<void(const QJsonObject &)> ok);
	void ytFail(const QString &msg, bool retry_ms_valid = false, int retry_ms = 0);

	// Twitch
	QSslSocket twitch;
	QTimer tw_reconnect;
	QByteArray tw_buffer;
	QString tw_channel;
	void twConnect();
	void twLine(const QByteArray &line);

	QString targetName() const;
	void addMessage(const QString &platform, const QString &author, const QString &color, const QString &text, const QString &badge);
	void setYtStatus(const QString &s);
	void setTwStatus(const QString &s);
	void updateStatus();
};

#endif // PSWRAP_CHAT_H
