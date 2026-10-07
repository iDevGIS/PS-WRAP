// PS-WRAP: แชทไลฟ์บนจอ (ดู pswrapchat.h)
#include <pswrapchat.h>
#include <pswrapsecrets.h>
#include <settings.h>

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(pswrapChat, "pswrap.chat", QtInfoMsg)

namespace {

constexpr int kMaxMessages = 60;
constexpr int kMinPollMs = 5000;        // ประหยัด quota (liveChatMessages.list = 5 unit/ครั้ง, วันละ 10,000)
constexpr int kNoLiveRetryMs = 60000;   // ช่องยังไม่ไลฟ์ → ลองใหม่ทุก 1 นาที (search.list = 100 unit/ครั้ง)
constexpr int kMaxSearchTries = 15;     // ≈ 15 นาที แล้วหยุด (กัน quota หมดทั้งวัน)
const char *const kApi = "https://www.googleapis.com/youtube/v3/";

// สีชื่อ Twitch ที่ไม่ได้ตั้งเอง — คงที่ต่อชื่อ (เหมือนเว็บ Twitch)
QString fallbackColor(const QString &name)
{
	static const char *const palette[] = {"#ff6b6b", "#4dabf7", "#69db7c", "#ffd43b", "#da77f2", "#ff922b", "#38d9a9", "#f783ac"};
	return QString::fromLatin1(palette[qHash(name) % (sizeof(palette) / sizeof(palette[0]))]);
}

} // namespace

PsWrapLiveChat::PsWrapLiveChat(QObject *parent, std::function<Settings *()> getter)
	: QObject(parent), settings_getter(std::move(getter))
{
	yt_timer.setSingleShot(true);
	tw_reconnect.setSingleShot(true);
	tw_reconnect.setInterval(5000);
	connect(&tw_reconnect, &QTimer::timeout, this, [this]() { if (running) twConnect(); });
	connect(&twitch, &QSslSocket::encrypted, this, [this]() {
		const QByteArray nick = "justinfan" + QByteArray::number(QRandomGenerator::global()->bounded(10000, 99999));
		twitch.write("CAP REQ :twitch.tv/tags\r\nPASS SCHMOOPIIE\r\nNICK " + nick + "\r\nJOIN #" + tw_channel.toUtf8() + "\r\n");
		setTwStatus(tr("Twitch: #%1").arg(tw_channel));
	});
	connect(&twitch, &QSslSocket::readyRead, this, [this]() {
		tw_buffer += twitch.readAll();
		int nl;
		while ((nl = tw_buffer.indexOf("\r\n")) >= 0) {
			const QByteArray line = tw_buffer.left(nl);
			tw_buffer.remove(0, nl + 2);
			twLine(line);
		}
	});
	connect(&twitch, &QSslSocket::disconnected, this, [this]() {
		if (running && !tw_channel.isEmpty()) {
			setTwStatus(tr("Twitch: reconnecting…"));
			tw_reconnect.start();
		}
	});
	connect(&twitch, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		qCWarning(pswrapChat) << "twitch socket error:" << twitch.errorString();
		if (running && !tw_channel.isEmpty()) {
			setTwStatus(tr("Twitch: can't connect — retrying"));
			tw_reconnect.start();
		}
	});
}

PsWrapLiveChat::~PsWrapLiveChat()
{
	stop();
}

QString PsWrapLiveChat::targetName() const
{
	Settings *s = settings_getter ? settings_getter() : nullptr;
	return PsWrapSecrets::liveTarget(QStringLiteral("chat-youtube-api"), s ? s->GetCurrentProfile() : QString());
}

QString PsWrapLiveChat::youtubeSource() const
{
	Settings *s = settings_getter ? settings_getter() : nullptr;
	return s ? s->GetChatYouTube() : QString();
}

void PsWrapLiveChat::setYoutubeSource(const QString &v)
{
	Settings *s = settings_getter ? settings_getter() : nullptr;
	if (!s || s->GetChatYouTube() == v.trimmed())
		return;
	s->SetChatYouTube(v.trimmed());
	emit sourcesChanged();
}

QString PsWrapLiveChat::twitchChannel() const
{
	Settings *s = settings_getter ? settings_getter() : nullptr;
	return s ? s->GetChatTwitch() : QString();
}

void PsWrapLiveChat::setTwitchChannel(const QString &v)
{
	Settings *s = settings_getter ? settings_getter() : nullptr;
	// รับทั้ง "name", "#name", "twitch.tv/name"
	QString c = v.trimmed();
	c.remove(QRegularExpression(QStringLiteral("^(https?://)?(www\\.)?twitch\\.tv/"), QRegularExpression::CaseInsensitiveOption));
	c.remove(QLatin1Char('#'));
	c = c.section(QLatin1Char('/'), 0, 0).toLower();
	if (!s || s->GetChatTwitch() == c)
		return;
	s->SetChatTwitch(c);
	emit sourcesChanged();
}

bool PsWrapLiveChat::youtubeKeySaved() const
{
	return PsWrapSecrets::exists(targetName());
}

bool PsWrapLiveChat::setYoutubeApiKey(const QString &key)
{
	const PsWrapSecrets::Result r = PsWrapSecrets::write(targetName(), key.trimmed());
	emit sourcesChanged();
	return r == PsWrapSecrets::Result::Ok;
}

void PsWrapLiveChat::start()
{
	if (running)
		stop();
	generation++;
	running = true;
	emit runningChanged();
	yt_status.clear();
	tw_status.clear();
	const QString yt = youtubeSource();
	tw_channel = twitchChannel();
	if (yt.isEmpty() && tw_channel.isEmpty()) {
		setYtStatus(tr("Add a YouTube channel or Twitch channel in Settings › Go Live › Chat on screen."));
	}
	if (!yt.isEmpty())
		ytBegin();
	if (!tw_channel.isEmpty())
		twConnect();
	qCInfo(pswrapChat) << "chat start: youtube" << !yt.isEmpty() << "twitch" << tw_channel;
}

void PsWrapLiveChat::stop()
{
	generation++;
	yt_timer.stop();
	tw_reconnect.stop();
	yt_timer.disconnect();
	if (twitch.state() != QAbstractSocket::UnconnectedState)
		twitch.abort();
	yt_key.fill(QChar(u'\0'));
	yt_key.clear();
	yt_chat_id.clear();
	yt_page_token.clear();
	if (running) {
		running = false;
		emit runningChanged();
	}
}

void PsWrapLiveChat::clear()
{
	message_list.clear();
	emit messagesChanged();
}

void PsWrapLiveChat::addMessage(const QString &platform, const QString &author, const QString &color, const QString &text, const QString &badge)
{
	if (text.trimmed().isEmpty())
		return;
	QVariantMap m;
	m[QStringLiteral("platform")] = platform;
	m[QStringLiteral("author")] = author;
	// สีชื่อที่ผู้ใช้ตั้งเอง (Twitch) อาจมืดจนอ่านไม่ออกบนการ์ดดำ → ยกความสว่างขึ้นโดยคงเฉดสีเดิม
	QColor c(color);
	if (c.isValid() && c.lightnessF() < 0.6)
		c = QColor::fromHslF(c.hslHueF() < 0 ? 0 : c.hslHueF(), c.hslSaturationF(), 0.66);
	m[QStringLiteral("color")] = c.isValid() ? c.name() : color;
	m[QStringLiteral("text")] = text;
	m[QStringLiteral("badge")] = badge;
	message_list.append(m);
	while (message_list.size() > kMaxMessages)
		message_list.removeFirst();
	emit messagesChanged();
}

void PsWrapLiveChat::setYtStatus(const QString &s) { yt_status = s; updateStatus(); }
void PsWrapLiveChat::setTwStatus(const QString &s) { tw_status = s; updateStatus(); }

void PsWrapLiveChat::updateStatus()
{
	QStringList parts;
	if (!yt_status.isEmpty())
		parts << yt_status;
	if (!tw_status.isEmpty())
		parts << tw_status;
	const QString s = parts.join(QStringLiteral(" · "));
	if (s == status_value)
		return;
	status_value = s;
	emit statusChanged();
}

// ---------------------------------------------------------------- YouTube

void PsWrapLiveChat::ytBegin()
{
	QString key;
	if (PsWrapSecrets::read(targetName(), &key) != PsWrapSecrets::Result::Ok || key.trimmed().isEmpty()) {
		setYtStatus(tr("YouTube: add an API key in Settings › Go Live › Chat on screen."));
		return;
	}
	yt_key = key.trimmed();
	key.fill(QChar(u'\0'));
	yt_channel.clear();
	yt_video.clear();
	yt_chat_id.clear();
	yt_page_token.clear();
	yt_first_page = true;
	yt_search_tries = 0;

	const QString src = youtubeSource();
	static const QRegularExpression video_re(QStringLiteral("(?:v=|youtu\\.be/|/live/|/shorts/)([A-Za-z0-9_-]{11})"));
	static const QRegularExpression id_re(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
	static const QRegularExpression channel_re(QStringLiteral("(UC[A-Za-z0-9_-]{22})"));
	static const QRegularExpression handle_re(QStringLiteral("@([A-Za-z0-9._-]+)"));
	QRegularExpressionMatch m;
	if ((m = video_re.match(src)).hasMatch()) {
		yt_video = m.captured(1);
		ytFetchChatId();
	} else if ((m = channel_re.match(src)).hasMatch()) {
		yt_channel = m.captured(1);
		ytFindLive();
	} else if ((m = handle_re.match(src)).hasMatch()) {
		ytResolveHandle(QLatin1Char('@') + m.captured(1));
	} else if (id_re.match(src).hasMatch()) {
		yt_video = src;
		ytFetchChatId();
	} else {
		setYtStatus(tr("YouTube: use @handle, a channel ID (UC…) or a live video link."));
	}
}

void PsWrapLiveChat::ytGet(const QString &path, const QList<QPair<QString, QString>> &query, std::function<void(const QJsonObject &)> ok)
{
	QUrl url(QString::fromLatin1(kApi) + path);
	QUrlQuery q;
	for (const auto &kv : query)
		q.addQueryItem(kv.first, kv.second);
	q.addQueryItem(QStringLiteral("key"), yt_key);
	url.setQuery(q);
	QNetworkRequest req(url);
	req.setTransferTimeout(15000);
	QNetworkReply *reply = nam.get(req);
	const int gen = generation;
	connect(reply, &QNetworkReply::finished, this, [this, reply, gen, ok, path]() {
		reply->deleteLater();
		if (gen != generation)
			return;   // หยุด/เริ่มใหม่ไปแล้ว
		const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
		if (reply->error() == QNetworkReply::NoError && http == 200) {
			ok(obj);
			return;
		}
		// ข้อความ error ของ Google (ไม่มี key ในข้อความ — url ไม่ถูก log)
		const QJsonObject err = obj.value(QStringLiteral("error")).toObject();
		const QString reason = err.value(QStringLiteral("errors")).toArray().at(0).toObject().value(QStringLiteral("reason")).toString();
		qCWarning(pswrapChat) << "youtube" << path << "http" << http << "reason" << reason << err.value(QStringLiteral("message")).toString();
		if (reason == QLatin1String("quotaExceeded") || reason == QLatin1String("dailyLimitExceeded"))
			ytFail(tr("YouTube: API quota used up for today (resets at midnight Pacific time)."));
		else if (reason == QLatin1String("keyInvalid") || http == 400)
			ytFail(tr("YouTube: the API key isn't valid — check it in Settings › Go Live."));
		else if (reason == QLatin1String("liveChatEnded") || reason == QLatin1String("liveChatNotFound") || http == 404)
			ytFail(tr("YouTube: live chat ended — looking for the next live…"), true, kNoLiveRetryMs);
		else if (reason == QLatin1String("liveChatDisabled"))
			ytFail(tr("YouTube: live chat is turned off for this broadcast."));
		else if (reason == QLatin1String("forbidden") || http == 403)
			ytFail(tr("YouTube: access denied (%1).").arg(reason.isEmpty() ? QString::number(http) : reason));
		else
			ytFail(tr("YouTube: network problem — retrying"), true, 10000);
	});
}

void PsWrapLiveChat::ytFail(const QString &msg, bool retry, int retry_ms)
{
	setYtStatus(msg);
	if (!retry || !running)
		return;
	yt_chat_id.clear();
	yt_page_token.clear();
	yt_timer.disconnect();
	connect(&yt_timer, &QTimer::timeout, this, [this]() {
		if (!running)
			return;
		if (!yt_channel.isEmpty())
			ytFindLive();
		else if (!yt_video.isEmpty())
			ytFetchChatId();
	});
	yt_timer.start(retry_ms);
}

void PsWrapLiveChat::ytResolveHandle(const QString &handle)
{
	setYtStatus(tr("YouTube: finding %1…").arg(handle));
	ytGet(QStringLiteral("channels"), {{QStringLiteral("part"), QStringLiteral("id")}, {QStringLiteral("forHandle"), handle}},
	      [this, handle](const QJsonObject &o) {
		const QJsonArray items = o.value(QStringLiteral("items")).toArray();
		if (items.isEmpty()) {
			ytFail(tr("YouTube: no channel called %1.").arg(handle));
			return;
		}
		yt_channel = items.at(0).toObject().value(QStringLiteral("id")).toString();
		ytFindLive();
	});
}

void PsWrapLiveChat::ytFindLive()
{
	if (++yt_search_tries > kMaxSearchTries) {
		setYtStatus(tr("YouTube: no live broadcast found — turn Chat off and on again once you are live."));
		return;
	}
	setYtStatus(tr("YouTube: looking for a live broadcast…"));
	ytGet(QStringLiteral("search"), {{QStringLiteral("part"), QStringLiteral("id")}, {QStringLiteral("channelId"), yt_channel},
	                                 {QStringLiteral("eventType"), QStringLiteral("live")}, {QStringLiteral("type"), QStringLiteral("video")},
	                                 {QStringLiteral("maxResults"), QStringLiteral("1")}},
	      [this](const QJsonObject &o) {
		const QJsonArray items = o.value(QStringLiteral("items")).toArray();
		if (items.isEmpty()) {
			ytFail(tr("YouTube: the channel isn't live yet — checking every minute"), true, kNoLiveRetryMs);
			return;
		}
		yt_video = items.at(0).toObject().value(QStringLiteral("id")).toObject().value(QStringLiteral("videoId")).toString();
		ytFetchChatId();
	});
}

void PsWrapLiveChat::ytFetchChatId()
{
	ytGet(QStringLiteral("videos"), {{QStringLiteral("part"), QStringLiteral("liveStreamingDetails,snippet")}, {QStringLiteral("id"), yt_video}},
	      [this](const QJsonObject &o) {
		const QJsonArray items = o.value(QStringLiteral("items")).toArray();
		const QJsonObject v = items.isEmpty() ? QJsonObject() : items.at(0).toObject();
		yt_chat_id = v.value(QStringLiteral("liveStreamingDetails")).toObject().value(QStringLiteral("activeLiveChatId")).toString();
		if (yt_chat_id.isEmpty()) {
			ytFail(tr("YouTube: this video has no live chat right now — checking every minute"), true, kNoLiveRetryMs);
			return;
		}
		const QString title = v.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("channelTitle")).toString();
		setYtStatus(tr("YouTube: %1").arg(title.isEmpty() ? yt_video : title));
		qCInfo(pswrapChat) << "youtube chat connected, video" << yt_video;
		yt_first_page = true;
		ytPoll();
	});
}

void PsWrapLiveChat::ytPoll()
{
	if (!running || yt_chat_id.isEmpty())
		return;
	QList<QPair<QString, QString>> q = {{QStringLiteral("liveChatId"), yt_chat_id},
	                                    {QStringLiteral("part"), QStringLiteral("snippet,authorDetails")},
	                                    {QStringLiteral("maxResults"), QStringLiteral("200")}};
	if (!yt_page_token.isEmpty())
		q.append({QStringLiteral("pageToken"), yt_page_token});
	ytGet(QStringLiteral("liveChat/messages"), q, [this](const QJsonObject &o) {
		const QJsonArray items = o.value(QStringLiteral("items")).toArray();
		// หน้าแรก = ประวัติย้อนหลัง → เอาแค่ 8 ข้อความล่าสุด ไม่ให้ท่วมจอ
		const int from = yt_first_page ? qMax(0, int(items.size()) - 8) : 0;
		yt_first_page = false;
		for (int i = from; i < items.size(); i++) {
			const QJsonObject it = items.at(i).toObject();
			const QJsonObject sn = it.value(QStringLiteral("snippet")).toObject();
			const QJsonObject au = it.value(QStringLiteral("authorDetails")).toObject();
			const QString type = sn.value(QStringLiteral("type")).toString();
			QString text = sn.value(QStringLiteral("displayMessage")).toString();
			QString badge;
			QString color = QStringLiteral("#e8edf2");
			if (au.value(QStringLiteral("isChatOwner")).toBool()) {
				badge = QStringLiteral("owner");
				color = QStringLiteral("#ffd43b");
			} else if (au.value(QStringLiteral("isChatModerator")).toBool()) {
				badge = QStringLiteral("mod");
				color = QStringLiteral("#5e9eff");
			} else if (au.value(QStringLiteral("isChatSponsor")).toBool()) {
				badge = QStringLiteral("member");
				color = QStringLiteral("#2ba640");
			}
			if (type == QLatin1String("superChatEvent") || type == QLatin1String("superStickerEvent"))
				badge = QStringLiteral("super");
			addMessage(QStringLiteral("youtube"), au.value(QStringLiteral("displayName")).toString(), color, text, badge);
		}
		yt_page_token = o.value(QStringLiteral("nextPageToken")).toString();
		const int wait = qMax(kMinPollMs, o.value(QStringLiteral("pollingIntervalMillis")).toInt());
		yt_timer.disconnect();
		connect(&yt_timer, &QTimer::timeout, this, &PsWrapLiveChat::ytPoll);
		yt_timer.start(wait);
	});
}

// ---------------------------------------------------------------- Twitch

void PsWrapLiveChat::twConnect()
{
	if (tw_channel.isEmpty())
		return;
	if (twitch.state() != QAbstractSocket::UnconnectedState)
		twitch.abort();
	tw_buffer.clear();
	setTwStatus(tr("Twitch: connecting…"));
	twitch.connectToHostEncrypted(QStringLiteral("irc.chat.twitch.tv"), 6697);
}

void PsWrapLiveChat::twLine(const QByteArray &raw)
{
	const QString line = QString::fromUtf8(raw);
	if (line.startsWith(QLatin1String("PING"))) {
		twitch.write("PONG :tmi.twitch.tv\r\n");
		return;
	}
	// @tags :nick!nick@nick.tmi.twitch.tv PRIVMSG #channel :text
	const int priv = line.indexOf(QLatin1String(" PRIVMSG #"));
	if (priv < 0)
		return;
	QString tags, prefix = line;
	if (line.startsWith(QLatin1Char('@'))) {
		const int sp = line.indexOf(QLatin1Char(' '));
		tags = line.mid(1, sp - 1);
		prefix = line.mid(sp + 1);
	}
	const int text_at = prefix.indexOf(QLatin1String(" :"), prefix.indexOf(QLatin1String(" PRIVMSG #")));
	if (text_at < 0)
		return;
	QString text = prefix.mid(text_at + 2);
	if (text.startsWith(QStringLiteral("\x01" "ACTION ")))   // /me
		text = text.mid(8).chopped(text.endsWith(QChar(0x01)) ? 1 : 0);
	QString name = prefix.mid(1, prefix.indexOf(QLatin1Char('!')) - 1);
	QString color, badge;
	for (const QString &kv : tags.split(QLatin1Char(';'))) {
		const int eq = kv.indexOf(QLatin1Char('='));
		const QString k = kv.left(eq), v = kv.mid(eq + 1);
		if (k == QLatin1String("display-name") && !v.isEmpty())
			name = v;
		else if (k == QLatin1String("color") && !v.isEmpty())
			color = v;
		else if (k == QLatin1String("badges")) {
			if (v.contains(QLatin1String("broadcaster")))
				badge = QStringLiteral("owner");
			else if (v.contains(QLatin1String("moderator")))
				badge = QStringLiteral("mod");
			else if (v.contains(QLatin1String("subscriber")))
				badge = QStringLiteral("member");
		}
	}
	if (color.isEmpty())
		color = fallbackColor(name);
	addMessage(QStringLiteral("twitch"), name, color, text, badge);
}
