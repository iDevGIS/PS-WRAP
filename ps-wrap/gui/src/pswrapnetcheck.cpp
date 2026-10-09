// PS-WRAP: ตรวจการเชื่อมต่อไปหาเครื่อง — ดู include/pswrapnetcheck.h
#include "pswrapnetcheck.h"

#include <QGuiApplication>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkDatagram>
#include <QPointer>
#include <QScreen>
#include <QThread>
#include <QUdpSocket>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <netioapi.h>
#include <wlanapi.h>
#endif

namespace {
QHostAddress resolve(const QString &address)
{
	QHostAddress a(address);
	if (!a.isNull())
		return a;
	const QHostInfo info = QHostInfo::fromName(address);
	for (const QHostAddress &h : info.addresses())
		if (h.protocol() == QAbstractSocket::IPv4Protocol)
			return h;
	return QHostAddress();
}

double percentile(QVector<double> v, double p)
{
	if (v.isEmpty())
		return -1;
	std::sort(v.begin(), v.end());
	const double idx = p * (v.size() - 1);
	const int lo = int(std::floor(idx)), hi = int(std::ceil(idx));
	return v[lo] + (v[hi] - v[lo]) * (idx - lo);
}
} // namespace

PsWrapNetCheck::PsWrapNetCheck(QObject *parent) : QObject(parent)
{
	srch_timeout.setSingleShot(true);
	connect(&srch_timeout, &QTimer::timeout, this, &PsWrapNetCheck::onSrchTimeout);
}

PsWrapNetCheck::~PsWrapNetCheck()
{
	if (cancel_flag_)
		*cancel_flag_ = true;
}

void PsWrapNetCheck::run(const QString &address, bool ps5)
{
	cancel();
	generation_++;
	address_ = address;
	port_ = ps5 ? 9302 : 987;
	probe_ = QByteArray("SRCH * HTTP/1.1\ndevice-discovery-protocol-version:") + (ps5 ? "00030010" : "00020020") + "\n";
	result_.clear();
	done_ = 0;
	srch_tries_ = 0;
	reachable_ = false;
	standby_ = false;
	const QHostAddress host = resolve(address);
	if (host.isNull() || host.protocol() != QAbstractSocket::IPv4Protocol) {
		fail(tr("Can't find the console's address"));
		return;
	}
	ipv4_ = host.toIPv4Address();
	socket = new QUdpSocket(this);
	if (!socket->bind(QHostAddress::AnyIPv4, 0)) {
		fail(socket->errorString());
		return;
	}
	connect(socket, &QUdpSocket::readyRead, this, &PsWrapNetCheck::onReadyRead);
	running_ = true;
	emit runningChanged();
	emit progressChanged();
	sendSrch();
}

void PsWrapNetCheck::cancel()
{
	srch_timeout.stop();
	if (cancel_flag_)
		*cancel_flag_ = true;
	cancel_flag_.reset();
	if (socket) {
		socket->deleteLater();
		socket = nullptr;
	}
	if (running_) {
		running_ = false;
		emit runningChanged();
	}
}

void PsWrapNetCheck::fail(const QString &error)
{
	cancel();
	QVariantMap r;
	r[QStringLiteral("ok")] = false;
	r[QStringLiteral("error")] = error;
	r[QStringLiteral("sent")] = 0;
	r[QStringLiteral("received")] = 0;
	result_ = r;
	emit finished();
}

// ---- ขั้น 1: SRCH — เครื่องอยู่ไหม ตื่นหรือพัก
void PsWrapNetCheck::sendSrch()
{
	if (!socket)
		return;
	srch_tries_++;
	socket->writeDatagram(probe_, QHostAddress(ipv4_), port_);
	srch_timeout.start(1200);
}

void PsWrapNetCheck::onReadyRead()
{
	while (socket && socket->hasPendingDatagrams()) {
		const QByteArray d = socket->receiveDatagram().data();
		if (reachable_ || !d.startsWith("HTTP/1.1"))
			continue;
		reachable_ = true;
		standby_ = d.startsWith("HTTP/1.1 620");
		srch_timeout.stop();
		startPings();
		return;
	}
}

void PsWrapNetCheck::onSrchTimeout()
{
	if (srch_tries_ < kSrchTries) {
		sendSrch();
		return;
	}
	// เครื่องไม่ตอบ discovery — ยังลอง ping ต่อ (อาจบล็อก UDP แต่ ping ได้) แล้วบอกในผล
	startPings();
}

// ---- ขั้น 2: ICMP echo ใน thread แยก (IcmpSendEcho บล็อก)
void PsWrapNetCheck::startPings()
{
	if (socket) {
		socket->deleteLater();
		socket = nullptr;
	}
	auto flag = std::make_shared<std::atomic_bool>(false);
	cancel_flag_ = flag;
	const quint64 gen = generation_;
	const quint32 ip = ipv4_;
	QPointer<PsWrapNetCheck> self(this);
	(void)QtConcurrent::run([self, flag, gen, ip]() {
		QVector<double> rtts;
		int sent = 0;
#ifdef Q_OS_WIN
		HANDLE icmp = IcmpCreateFile();
		if (icmp != INVALID_HANDLE_VALUE) {
			char payload[32] = "PS-WRAP connection check";
			const DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 8;
			QByteArray reply(int(replySize), 0);
			for (int i = 0; i < kPings && !*flag; ++i) {
				QElapsedTimer t;
				t.start();
				const DWORD n = IcmpSendEcho(icmp, htonl(ip), payload, sizeof(payload), nullptr, reply.data(), replySize, 1000);
				const double ms = t.nsecsElapsed() / 1.0e6;
				sent++;
				const auto *r = reinterpret_cast<const ICMP_ECHO_REPLY *>(reply.constData());
				if (n > 0 && r->Status == IP_SUCCESS)
					rtts << ms;
				QMetaObject::invokeMethod(self, [self, gen, sent]() {
					if (self && self->generation_ == gen) {
						self->done_ = sent;
						emit self->progressChanged();
					}
				}, Qt::QueuedConnection);
				QThread::msleep(50);
			}
			IcmpCloseHandle(icmp);
		}
#else
		Q_UNUSED(ip)
#endif
		QMetaObject::invokeMethod(self, [self, flag, gen, rtts, sent]() {
			if (self && !*flag && self->generation_ == gen)
				self->finish(rtts, sent);
		}, Qt::QueuedConnection);
	});
}

void PsWrapNetCheck::finish(const QVector<double> &rtts, int sent)
{
	QVariantMap r = linkInfo();
	r[QStringLiteral("ok")] = reachable_ || !rtts.isEmpty();
	if (!reachable_ && rtts.isEmpty())
		r[QStringLiteral("error")] = tr("The console did not answer. Check that it is on (or in rest mode) and on the same network.");
	r[QStringLiteral("standby")] = standby_;
	r[QStringLiteral("pingBlocked")] = rtts.isEmpty();   // ICMP ถูกบล็อก (เครื่อง/เราเตอร์) — ไม่มีตัวเลข ping
	r[QStringLiteral("sent")] = sent;
	r[QStringLiteral("received")] = rtts.size();
	r[QStringLiteral("lossPct")] = (sent && !rtts.isEmpty()) ? 100.0 * (sent - rtts.size()) / sent : 0.0;
	r[QStringLiteral("rttMin")] = rtts.isEmpty() ? -1 : *std::min_element(rtts.begin(), rtts.end());
	r[QStringLiteral("rttMedian")] = percentile(rtts, 0.5);
	r[QStringLiteral("rttP95")] = percentile(rtts, 0.95);
	r[QStringLiteral("jitter")] = rtts.isEmpty() ? -1 : percentile(rtts, 0.95) - percentile(rtts, 0.5);
	if (QScreen *s = QGuiApplication::primaryScreen()) {
		r[QStringLiteral("screenHz")] = qRound(s->refreshRate());
		r[QStringLiteral("screenHeight")] = qRound(s->size().height() * s->devicePixelRatio());
	}
	result_ = r;
	cancel_flag_.reset();
	running_ = false;
	emit runningChanged();
	emit finished();
}

QVariantMap PsWrapNetCheck::linkInfo() const
{
	QVariantMap m;
	m[QStringLiteral("linkType")] = QStringLiteral("unknown");
#ifdef Q_OS_WIN
	sockaddr_in sa = {};
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(ipv4_);
	DWORD ifIndex = 0;
	if (GetBestInterfaceEx(reinterpret_cast<sockaddr *>(&sa), &ifIndex) != NO_ERROR)
		return m;
	MIB_IF_ROW2 row = {};
	row.InterfaceIndex = ifIndex;
	if (GetIfEntry2(&row) != NO_ERROR)
		return m;
	m[QStringLiteral("adapter")] = QString::fromWCharArray(row.Description);
	m[QStringLiteral("linkMbps")] = double(qMin(row.ReceiveLinkSpeed, row.TransmitLinkSpeed)) / 1.0e6;
	if (row.Type == IF_TYPE_ETHERNET_CSMACD) {
		m[QStringLiteral("linkType")] = QStringLiteral("ethernet");
	} else if (row.Type == IF_TYPE_IEEE80211) {
		m[QStringLiteral("linkType")] = QStringLiteral("wifi");
		HANDLE wlan = nullptr;
		DWORD ver = 0;
		if (WlanOpenHandle(2, nullptr, &ver, &wlan) == ERROR_SUCCESS) {
			DWORD size = 0;
			WLAN_CONNECTION_ATTRIBUTES *conn = nullptr;
			if (WlanQueryInterface(wlan, &row.InterfaceGuid, wlan_intf_opcode_current_connection, nullptr, &size,
					reinterpret_cast<void **>(&conn), nullptr) == ERROR_SUCCESS && conn) {
				const auto &a = conn->wlanAssociationAttributes;
				m[QStringLiteral("wifiSignal")] = int(a.wlanSignalQuality);   // 0..100
				m[QStringLiteral("wifiRxMbps")] = a.ulRxRate / 1000.0;
				m[QStringLiteral("wifiTxMbps")] = a.ulTxRate / 1000.0;
				QString phy;
				switch (int(a.dot11PhyType)) {
				case 11: phy = QStringLiteral("Wi-Fi 7"); break;
				case 10: phy = QStringLiteral("Wi-Fi 6"); break;
				case 8: phy = QStringLiteral("Wi-Fi 5"); break;
				case 7: phy = QStringLiteral("Wi-Fi 4"); break;
				default: phy = QStringLiteral("Wi-Fi"); break;
				}
				m[QStringLiteral("wifiPhy")] = phy;
				m[QStringLiteral("linkMbps")] = qMin(a.ulRxRate, a.ulTxRate) / 1000.0;
				WlanFreeMemory(conn);
			}
			ULONG *channel = nullptr;
			if (WlanQueryInterface(wlan, &row.InterfaceGuid, wlan_intf_opcode_channel_number, nullptr, &size,
					reinterpret_cast<void **>(&channel), nullptr) == ERROR_SUCCESS && channel) {
				m[QStringLiteral("wifiChannel")] = int(*channel);
				m[QStringLiteral("wifiBand")] = *channel <= 14 ? QStringLiteral("2.4 GHz") : QStringLiteral("5 GHz+");
				WlanFreeMemory(channel);
			}
			WlanCloseHandle(wlan, nullptr);
		}
	} else {
		m[QStringLiteral("linkType")] = QStringLiteral("other");
	}
#endif
	return m;
}
