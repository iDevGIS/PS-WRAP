// PS-WRAP: Discord Rich Presence ฝั่งหน้าต่าง — คิดว่ากำลังทำอะไรแล้วส่งให้ PsWrapDiscord (pswrapdiscord.cpp)
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
//
// สตรีม: "Playing PS-WRAP" · <ชื่อเกมจาก discovery ของเครื่อง> · "PS5 Remote Play · Live/Recording" · เวลาเล่น
// หน้าแรก: "Browsing consoles" · ปิด/ซ่อนชื่อเกมได้ใน Settings › General (pswrap/discordPresence, pswrap/discordShowGame)
// คำนวณทุก 5 วินาที + ตอน session เปลี่ยน · ส่งเฉพาะตอนเปลี่ยนจริง (PsWrapDiscord กันซ้ำ)
// ชื่อเกมระหว่างสตรีม: upstream ปิด discovery ตอนต่อแล้ว (qmlbackend.cpp) → ถามเครื่องที่สตรีมอยู่ตรงๆ ด้วย SRCH แบบ unicast
//   ทุก 30 วินาที (UDP แพ็กเก็ตเดียว ~60 ไบต์ ไปเครื่องเดียว — ไม่ broadcast) แล้วอ่าน running-app-name จากคำตอบ
#include "qmlmainwindow.h"
#include "qmlbackend.h"
#include "streamsession.h"
#include "pswrapdiscord.h"
#include "pswraplive.h"
#include "pswraprecorder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QHostAddress>
#include <QTimer>
#include <QUdpSocket>

namespace {
// เกมที่เครื่อง address นี้กำลังรัน จากรายการเครื่องของ backend · เครื่องเดียวกันอาจมีทั้งรายการ manual (ไม่มีข้อมูลเกม)
// และรายการที่ discovery เจอ → ใช้ตัวที่ discovery เจอ · คืน false ถ้าไม่เจอเครื่องเลย
bool findGame(const QVariantList &hosts, const QString &address, QString *game, bool *ps5)
{
	bool found = false;
	for (const QVariant &hv : hosts) {
		const QVariantMap h = hv.toMap();
		if (h.value(QStringLiteral("address")).toString() != address)
			continue;
		found = true;
		*ps5 = h.value(QStringLiteral("ps5"), *ps5).toBool();
		if (h.value(QStringLiteral("discovered")).toBool()) {
			*game = h.value(QStringLiteral("app")).toString().trimmed();
			break;
		}
	}
	return found;
}

// Discord Developer Portal › Applications › PS-WRAP (ไม่ใช่ความลับ — client id ของ Rich Presence ฝังในแอปได้)
const char *kDiscordAppId = "1557916650534346772";
}

bool QmlMainWindow::discordPresence() const { return settings->GetDiscordPresence(); }

void QmlMainWindow::setDiscordPresence(bool v)
{
	if (v == settings->GetDiscordPresence())
		return;
	settings->SetDiscordPresence(v);
	if (pswrap_discord)
		pswrap_discord->setEnabled(v);
	pswrapUpdateDiscord();
	emit discordPresenceChanged();
}

bool QmlMainWindow::discordShowGame() const { return settings->GetDiscordShowGame(); }

void QmlMainWindow::setDiscordShowGame(bool v)
{
	if (v == settings->GetDiscordShowGame())
		return;
	settings->SetDiscordShowGame(v);
	pswrapUpdateDiscord();
	emit discordPresenceChanged();
}

void QmlMainWindow::pswrapInitDiscord()
{
	pswrap_discord = new PsWrapDiscord(QString::fromLatin1(kDiscordAppId), this);
	pswrap_discord->setEnabled(settings->GetDiscordPresence());
	auto *timer = new QTimer(this);
	timer->setInterval(5000);
	connect(timer, &QTimer::timeout, this, &QmlMainWindow::pswrapUpdateDiscord);
	timer->start();
	connect(backend, &QmlBackend::sessionChanged, this, [this](StreamSession *s) {
		pswrap_discord_session_start = 0;
		pswrap_discord_last_probe = 0;
		// จำเกมไว้ตอน session เริ่ม — ระหว่างสตรีมรายการเครื่องไม่มีข้อมูล discovery แล้ว (พบ 2026-10-09)
		pswrap_discord_game.clear();
		pswrap_discord_ps5 = true;
		if (s)
			findGame(backend->hosts(), s->GetHost(), &pswrap_discord_game, &pswrap_discord_ps5);
		QTimer::singleShot(500, this, &QmlMainWindow::pswrapUpdateDiscord);
	});
	pswrap_discord_udp = new QUdpSocket(this);
	pswrap_discord_udp->bind(QHostAddress(QHostAddress::AnyIPv4), 0);
	connect(pswrap_discord_udp, &QUdpSocket::readyRead, this, [this]() {
		while (pswrap_discord_udp->hasPendingDatagrams()) {
			QHostAddress from;
			QByteArray data(int(pswrap_discord_udp->pendingDatagramSize()), Qt::Uninitialized);
			pswrap_discord_udp->readDatagram(data.data(), data.size(), &from);
			StreamSession *s = backend ? backend->qmlSession() : nullptr;
			if (!s || QHostAddress(from.toIPv4Address()) != QHostAddress(s->GetHost()))
				continue;
			// คำตอบ = HTTP-like "200 Ok" + header ทีละบรรทัด · ไม่มี running-app-name = อยู่หน้า Home (ไม่มีเกม)
			static bool logged_reply = false;
			if (!logged_reply) {
				logged_reply = true;
				qCInfo(chiakiGui) << "PSWRAP discord: console replied" << data.left(data.indexOf('\n')).trimmed();
			}
			QString game;
			for (const QByteArray &line : data.split('\n')) {
				if (line.startsWith("running-app-name:"))
					game = QString::fromUtf8(line.mid(17)).trimmed();
			}
			if (game != pswrap_discord_game) {
				pswrap_discord_game = game;
				pswrapUpdateDiscord();
			}
		}
	});
	pswrap_discord_app_start = QDateTime::currentSecsSinceEpoch();
	pswrapUpdateDiscord();
}

// SRCH ไปเครื่องที่สตรีมอยู่ (เฉพาะ address แบบ IP — ชื่อโฮสต์/PSN remote ข้าม)
void QmlMainWindow::pswrapProbeDiscordGame(const QString &host, bool ps5)
{
	const QHostAddress addr(host);
	if (!pswrap_discord_udp || addr.isNull() || addr.protocol() != QAbstractSocket::IPv4Protocol)
		return;
	QByteArray pkt = QByteArrayLiteral("SRCH * HTTP/1.1\ndevice-discovery-protocol-version:")
		+ (ps5 ? QByteArrayLiteral("00030010") : QByteArrayLiteral("00020020")) + QByteArrayLiteral("\n");
	pkt.append('\0');   // เหมือน chiaki_discovery_send (ส่ง NUL ปิดท้ายด้วย)
	pswrap_discord_udp->writeDatagram(pkt, addr, ps5 ? 9302 : 987);
}

void QmlMainWindow::pswrapUpdateDiscord()
{
	if (!pswrap_discord || !settings->GetDiscordPresence())
		return;
	QJsonObject a;
	a[QStringLiteral("type")] = 0;   // Playing
	QJsonObject assets;
	assets[QStringLiteral("large_image")] = QStringLiteral("pswrap");   // Art Assets key ใน Developer Portal
	assets[QStringLiteral("large_text")] = QStringLiteral("PS-WRAP %1 — PS4/PS5 Remote Play").arg(QCoreApplication::applicationVersion());
	a[QStringLiteral("assets")] = assets;
	QJsonObject button;
	button[QStringLiteral("label")] = QStringLiteral("Get PS-WRAP");
	button[QStringLiteral("url")] = QStringLiteral("https://github.com/iDevGIS/PS-WRAP");
	a[QStringLiteral("buttons")] = QJsonArray{button};

	StreamSession *s = backend ? backend->qmlSession() : nullptr;
	QJsonObject ts;
	if (s && s->GetConnected()) {
		// เกม + รุ่นเครื่อง: ค่าที่จำตอน session เริ่ม · ถ้า discovery ยังเห็นเครื่องอยู่ (เปลี่ยนเกมกลางสตรีม) ใช้ค่าล่าสุด
		// PSN remote = ไม่มีข้อมูล discovery → ไม่มีชื่อเกม
		QString game;
		bool ps5 = pswrap_discord_ps5;
		if (findGame(backend->hosts(), s->GetHost(), &game, &ps5) && !game.isEmpty())
			pswrap_discord_game = game;
		game = pswrap_discord_game;
		const qint64 now = QDateTime::currentSecsSinceEpoch();
		if (now - pswrap_discord_last_probe >= 30) {
			pswrap_discord_last_probe = now;
			pswrapProbeDiscordGame(s->GetHost(), ps5);
		}
		const QString console = ps5 ? QStringLiteral("PS5") : QStringLiteral("PS4");
		a[QStringLiteral("details")] = (settings->GetDiscordShowGame() && game.size() >= 2)
			? game.left(128)
			: QStringLiteral("Playing on %1").arg(console);
		QString state = QStringLiteral("%1 Remote Play").arg(console);
		const auto *live = findChild<PsWrapGoLive *>(QString(), Qt::FindDirectChildrenOnly);
		if (live && live->isLive())
			state += QStringLiteral(" · 🔴 Live");
		else if (pswrap_recorder && pswrap_recorder->isRecording())
			state += QStringLiteral(" · Recording");
		a[QStringLiteral("state")] = state;
		if (!pswrap_discord_session_start)
			pswrap_discord_session_start = QDateTime::currentSecsSinceEpoch();
		ts[QStringLiteral("start")] = pswrap_discord_session_start;
	} else {
		pswrap_discord_session_start = 0;
		a[QStringLiteral("details")] = QStringLiteral("Browsing consoles");
		a[QStringLiteral("state")] = QStringLiteral("PS4/PS5 Remote Play");
		ts[QStringLiteral("start")] = pswrap_discord_app_start;
	}
	a[QStringLiteral("timestamps")] = ts;
	pswrap_discord->setActivity(a);
}
