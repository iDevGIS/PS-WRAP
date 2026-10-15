// PS-WRAP-MAC: ข้อมูลเครือข่ายสำหรับ pswrapnetcheck (แทน iphlpapi/wlanapi/IcmpSendEcho ของ Windows) — ดู src/macNetInfo.mm
#ifndef PSWRAP_MACNETINFO_H
#define PSWRAP_MACNETINFO_H

#include <QVariantMap>
#include <QtGlobal>

// อะแดปเตอร์ที่ใช้ไปหา ipv4 (route lookup ผ่าน UDP connect) — key เหมือนฝั่ง Windows: adapter, linkType (ethernet/wifi/other), linkMbps,
// wifiSignal (0..100), wifiRxMbps, wifiTxMbps, wifiPhy, wifiChannel, wifiBand
QVariantMap pswrapMacLinkInfo(quint32 ipv4);

// ICMP echo 1 ครั้ง (SOCK_DGRAM/IPPROTO_ICMP — ไม่ต้อง root บน macOS) · คืน true + *rtt_ms เมื่อได้ reply ใน timeout
bool pswrapMacPing(quint32 ipv4, quint16 seq, int timeout_ms, double *rtt_ms);

#endif
