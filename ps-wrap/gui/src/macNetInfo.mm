// PS-WRAP-MAC: ดู include/macNetInfo.h
#include "macNetInfo.h"

#import <CoreWLAN/CoreWLAN.h>
#import <Foundation/Foundation.h>

#include <QElapsedTimer>
#include <QString>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstring>

namespace {

// interface ที่ route ไปหา ip (ไม่ส่งอะไรจริง — UDP connect แค่เลือก source address)
QString routeInterface(quint32 ipv4)
{
	const int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return QString();
	sockaddr_in dst = {};
	dst.sin_len = sizeof(dst);
	dst.sin_family = AF_INET;
	dst.sin_port = htons(9);
	dst.sin_addr.s_addr = htonl(ipv4);
	sockaddr_in local = {};
	socklen_t len = sizeof(local);
	QString name;
	if (connect(s, reinterpret_cast<sockaddr *>(&dst), sizeof(dst)) == 0 && getsockname(s, reinterpret_cast<sockaddr *>(&local), &len) == 0) {
		ifaddrs *list = nullptr;
		if (getifaddrs(&list) == 0) {
			for (ifaddrs *a = list; a; a = a->ifa_next) {
				if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET)
					continue;
				if (reinterpret_cast<sockaddr_in *>(a->ifa_addr)->sin_addr.s_addr == local.sin_addr.s_addr) {
					name = QString::fromUtf8(a->ifa_name);
					break;
				}
			}
			freeifaddrs(list);
		}
	}
	close(s);
	return name;
}

double linkBaudMbps(const QString &ifname)
{
	ifaddrs *list = nullptr;
	double mbps = 0;
	if (getifaddrs(&list) != 0)
		return 0;
	for (ifaddrs *a = list; a; a = a->ifa_next) {
		if (!a->ifa_addr || a->ifa_addr->sa_family != AF_LINK || !a->ifa_data || ifname != QString::fromUtf8(a->ifa_name))
			continue;
		const auto *d = static_cast<const if_data *>(a->ifa_data);
		mbps = double(d->ifi_baudrate) / 1.0e6;
		break;
	}
	freeifaddrs(list);
	return mbps;
}

QString phyName(CWPHYMode mode)
{
	switch (mode) {
	case kCWPHYMode11n: return QStringLiteral("Wi-Fi 4");
	case kCWPHYMode11ac: return QStringLiteral("Wi-Fi 5");
	case kCWPHYMode11ax: return QStringLiteral("Wi-Fi 6");
	default:
		if (int(mode) >= 7)   // kCWPHYMode11be (macOS 14+)
			return QStringLiteral("Wi-Fi 7");
		return QStringLiteral("Wi-Fi");
	}
}

} // namespace

QVariantMap pswrapMacLinkInfo(quint32 ipv4)
{
	QVariantMap m;
	m[QStringLiteral("linkType")] = QStringLiteral("unknown");
	const QString ifname = routeInterface(ipv4);
	if (ifname.isEmpty())
		return m;
	m[QStringLiteral("adapter")] = ifname;
	const double baud = linkBaudMbps(ifname);
	if (baud > 0)
		m[QStringLiteral("linkMbps")] = baud;

	@autoreleasepool {
		CWInterface *wifi = nil;
		for (CWInterface *i in [[CWWiFiClient sharedWiFiClient] interfaces]) {
			if (QString::fromNSString(i.interfaceName) == ifname) {
				wifi = i;
				break;
			}
		}
		if (wifi) {
			m[QStringLiteral("linkType")] = QStringLiteral("wifi");
			if (wifi.ssid)
				m[QStringLiteral("adapter")] = QStringLiteral("%1 (%2)").arg(ifname, QString::fromNSString(wifi.ssid));
			// RSSI dBm → 0..100 แบบเดียวกับ wlanSignalQuality ของ Windows (-100 = 0, -50 = 100)
			const int rssi = int(wifi.rssiValue);
			if (rssi != 0)
				m[QStringLiteral("wifiSignal")] = qBound(0, (rssi + 100) * 2, 100);
			const double tx = wifi.transmitRate;   // Mbps
			if (tx > 0) {
				m[QStringLiteral("wifiTxMbps")] = tx;
				m[QStringLiteral("wifiRxMbps")] = tx;   // CoreWLAN ให้แค่ tx rate
				m[QStringLiteral("linkMbps")] = tx;
			}
			m[QStringLiteral("wifiPhy")] = phyName(wifi.activePHYMode);
			if (CWChannel *ch = wifi.wlanChannel) {
				m[QStringLiteral("wifiChannel")] = int(ch.channelNumber);
				switch (ch.channelBand) {
				case kCWChannelBand2GHz: m[QStringLiteral("wifiBand")] = QStringLiteral("2.4 GHz"); break;
				case kCWChannelBand5GHz: m[QStringLiteral("wifiBand")] = QStringLiteral("5 GHz"); break;
				default: m[QStringLiteral("wifiBand")] = int(ch.channelBand) >= 3 ? QStringLiteral("6 GHz") : QStringLiteral("5 GHz+"); break;
				}
			}
			return m;
		}
	}
	// ไม่ใช่ Wi-Fi: en* ที่มี baudrate = สาย (Ethernet/Thunderbolt/USB) · อื่นๆ (utun/bridge) = other
	m[QStringLiteral("linkType")] = (ifname.startsWith(QStringLiteral("en")) && baud > 0) ? QStringLiteral("ethernet") : QStringLiteral("other");
	return m;
}

bool pswrapMacPing(quint32 ipv4, quint16 seq, int timeout_ms, double *rtt_ms)
{
	const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	if (s < 0)
		return false;
	struct Echo { icmp hdr; char payload[32]; } req = {};
	req.hdr.icmp_type = ICMP_ECHO;
	req.hdr.icmp_code = 0;
	req.hdr.icmp_id = htons(quint16(getpid() & 0xffff));   // kernel อาจแทน id ให้เอง (SOCK_DGRAM)
	req.hdr.icmp_seq = htons(seq);
	memcpy(req.payload, "PS-WRAP connection check\0\0\0\0\0\0\0", sizeof(req.payload));
	// checksum (kernel ไม่คำนวณให้บน SOCK_DGRAM ICMP ของ macOS)
	{
		const quint16 *w = reinterpret_cast<const quint16 *>(&req);
		quint32 sum = 0;
		for (size_t i = 0; i < sizeof(req) / 2; ++i)
			sum += w[i];
		while (sum >> 16)
			sum = (sum & 0xffff) + (sum >> 16);
		req.hdr.icmp_cksum = quint16(~sum);
	}
	sockaddr_in dst = {};
	dst.sin_len = sizeof(dst);
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(ipv4);
	QElapsedTimer t;
	t.start();
	bool ok = false;
	if (sendto(s, &req, sizeof(req), 0, reinterpret_cast<sockaddr *>(&dst), sizeof(dst)) == ssize_t(sizeof(req))) {
		while (!ok) {
			const int left = timeout_ms - int(t.elapsed());
			if (left <= 0)
				break;
			pollfd p = {s, POLLIN, 0};
			if (poll(&p, 1, left) <= 0)
				break;
			char buf[512];
			sockaddr_in from = {};
			socklen_t fl = sizeof(from);
			const ssize_t n = recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr *>(&from), &fl);
			if (n <= 0)
				break;
			// SOCK_DGRAM ICMP บน macOS คืนมาพร้อม IP header
			const char *p0 = buf;
			size_t len = size_t(n);
			if (len >= sizeof(ip) && (quint8(buf[0]) >> 4) == 4) {
				const size_t ihl = size_t(quint8(buf[0]) & 0x0f) * 4;
				if (ihl <= len) { p0 += ihl; len -= ihl; }
			}
			if (len < sizeof(icmp))
				continue;
			const icmp *r = reinterpret_cast<const icmp *>(p0);
			if (r->icmp_type == ICMP_ECHOREPLY && ntohs(r->icmp_seq) == seq && from.sin_addr.s_addr == dst.sin_addr.s_addr) {
				if (rtt_ms)
					*rtt_ms = t.nsecsElapsed() / 1.0e6;
				ok = true;
			}
		}
	}
	close(s);
	return ok;
}
