// PS-WRAP: Discord Rich Presence (ดู pswrapdiscord.h)
#include <pswrapdiscord.h>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QUuid>
#include <QtEndian>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui)

namespace {
constexpr int kOpHandshake = 0;
constexpr int kOpFrame = 1;
constexpr int kOpClose = 2;
constexpr int kPipes = 10;          // discord-ipc-0 .. 9 (Discord / PTB / Canary เปิดพร้อมกันได้)
constexpr int kRetryMs = 15000;
}

PsWrapDiscord::PsWrapDiscord(const QString &client_id, QObject *parent)
	: QObject(parent), client_id(client_id)
{
	reconnect_timer.setSingleShot(true);
	connect(&reconnect_timer, &QTimer::timeout, this, &PsWrapDiscord::tryConnect);
}

PsWrapDiscord::~PsWrapDiscord()
{
	if (sock) {
		sock->disconnect(this);
		sock->abort();
	}
}

void PsWrapDiscord::setEnabled(bool on)
{
	if (on == enabled)
		return;
	enabled = on;
	if (on) {
		pipe_index = 0;
		tryConnect();
		return;
	}
	reconnect_timer.stop();
	if (sock && ready) {
		// ล้างสถานะก่อนตัด (ปิดแอปเฉยๆ Discord ก็ล้างเองเมื่อ pipe หลุด)
		QJsonObject args;
		args[QStringLiteral("pid")] = static_cast<qint64>(QCoreApplication::applicationPid());
		QJsonObject p;
		p[QStringLiteral("cmd")] = QStringLiteral("SET_ACTIVITY");
		p[QStringLiteral("args")] = args;
		p[QStringLiteral("nonce")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
		sendFrame(kOpFrame, p);
		sock->flush();
	}
	if (sock) {
		sock->disconnect(this);
		sock->abort();
		sock->deleteLater();
		sock = nullptr;
	}
	ready = false;
	sent.clear();
}

void PsWrapDiscord::setActivity(const QJsonObject &activity)
{
	wanted = activity;
	flush();
}

void PsWrapDiscord::tryConnect()
{
	if (!enabled || client_id.isEmpty())
		return;
	if (sock) {
		sock->disconnect(this);
		sock->abort();
		sock->deleteLater();
	}
	ready = false;
	buf.clear();
	sock = new QLocalSocket(this);
	connect(sock, &QLocalSocket::connected, this, &PsWrapDiscord::onConnected);
	connect(sock, &QLocalSocket::disconnected, this, &PsWrapDiscord::onDisconnected);
	connect(sock, &QLocalSocket::readyRead, this, &PsWrapDiscord::onReadyRead);
	connect(sock, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
		if (ready)
			return;   // หลุดหลังเชื่อมแล้ว → onDisconnected จัดการ
		// pipe นี้ไม่มี → ลองตัวถัดไป · ครบ 10 ตัว = ไม่ได้เปิด Discord → รอแล้วลองใหม่
		if (++pipe_index < kPipes) {
			QTimer::singleShot(0, this, &PsWrapDiscord::tryConnect);
		} else {
			pipe_index = 0;
			reconnect_timer.start(kRetryMs);
		}
	});
	sock->connectToServer(QStringLiteral("discord-ipc-%1").arg(pipe_index));
}

void PsWrapDiscord::onConnected()
{
	QJsonObject hs;
	hs[QStringLiteral("v")] = 1;
	hs[QStringLiteral("client_id")] = client_id;
	sendFrame(kOpHandshake, hs);
}

void PsWrapDiscord::onDisconnected()
{
	const bool was_ready = ready;
	ready = false;
	sent.clear();
	if (was_ready)
		qCInfo(chiakiGui) << "PSWRAP discord: disconnected";
	if (enabled) {
		pipe_index = 0;
		reconnect_timer.start(kRetryMs);
	}
}

void PsWrapDiscord::onReadyRead()
{
	buf += sock->readAll();
	while (buf.size() >= 8) {
		const int op = qFromLittleEndian<qint32>(buf.constData());
		const int len = qFromLittleEndian<qint32>(buf.constData() + 4);
		if (len < 0 || len > (1 << 20)) {   // ข้อมูลเพี้ยน → ตัดแล้วต่อใหม่
			sock->abort();
			return;
		}
		if (buf.size() < 8 + len)
			return;
		const QJsonObject msg = QJsonDocument::fromJson(buf.mid(8, len)).object();
		buf.remove(0, 8 + len);
		if (op == kOpClose) {
			if (!logged_error) {
				logged_error = true;
				qCWarning(chiakiGui) << "PSWRAP discord: closed by Discord:" << msg.value(QStringLiteral("message")).toString();
			}
			sock->abort();
			return;
		}
		const QString evt = msg.value(QStringLiteral("evt")).toString();
		if (evt == QLatin1String("READY")) {
			ready = true;
			qCInfo(chiakiGui) << "PSWRAP discord: connected (pipe" << pipe_index << ")";
			flush();
		} else if (evt == QLatin1String("ERROR") && !logged_error) {
			logged_error = true;
			qCWarning(chiakiGui) << "PSWRAP discord: error" << msg.value(QStringLiteral("data")).toObject().value(QStringLiteral("message")).toString();
		}
	}
}

void PsWrapDiscord::sendFrame(int op, const QJsonObject &payload)
{
	if (!sock)
		return;
	const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
	QByteArray frame(8, Qt::Uninitialized);
	qToLittleEndian<qint32>(op, frame.data());
	qToLittleEndian<qint32>(static_cast<qint32>(json.size()), frame.data() + 4);
	frame += json;
	sock->write(frame);
}

void PsWrapDiscord::flush()
{
	if (!enabled || !ready || !sock)
		return;
	const QByteArray json = QJsonDocument(wanted).toJson(QJsonDocument::Compact);
	if (json == sent)
		return;
	QJsonObject args;
	args[QStringLiteral("pid")] = static_cast<qint64>(QCoreApplication::applicationPid());
	if (!wanted.isEmpty())
		args[QStringLiteral("activity")] = wanted;   // ไม่มี activity = ล้างสถานะ
	QJsonObject p;
	p[QStringLiteral("cmd")] = QStringLiteral("SET_ACTIVITY");
	p[QStringLiteral("args")] = args;
	p[QStringLiteral("nonce")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
	sendFrame(kOpFrame, p);
	sent = json;
	qCInfo(chiakiGui).noquote() << "PSWRAP discord:" << wanted.value(QStringLiteral("details")).toString()
	                            << "·" << wanted.value(QStringLiteral("state")).toString();
}
