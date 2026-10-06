// PS-WRAP: ส่วนของ QmlMainWindow ที่เกี่ยวกับไมค์ — เพิ่มเสียง (gain) + noise gate
// ค่าเก็บใน Settings (pswrap/micGainDb, pswrap/micGateEnabled, pswrap/micGateThresholdDb) → ของกลางของ PsWrapVoiceProc
// มีผลทันทีทั้งสตรีมที่เล่นอยู่และหน้าทดสอบไมค์ (thread ไมค์อ่าน atomics ทุกเฟรม)
// แยกไฟล์จาก qmlmainwindow.cpp / qmlmainwindow_pswraprec.cpp เพื่อลด conflict (ดู docs/04-upstream-sync.md)
#include "qmlmainwindow.h"

#include <pswrapvoiceproc.h>

#include <QtGlobal>

#include <cmath>

namespace {

// ปัดเป็นขั้น 0.5 dB — slider ลากละเอียดเกินไม่มีประโยชน์ และกัน registry เต็มไปด้วยเลขยาว
double roundHalfDb(double db) { return std::round(db * 2.0) / 2.0; }

} // namespace

void QmlMainWindow::pswrapInitMic()
{
	PsWrapVoiceProc::setDynamics(float(micGainDb()), micGateEnabled(), float(micGateThresholdDb()));
}

qreal QmlMainWindow::micGainDb() const
{
	return qBound(-12.0, settings->GetMicGainDb(), 24.0);
}

void QmlMainWindow::setMicGainDb(qreal db)
{
	db = roundHalfDb(qBound(-12.0, db, 24.0));
	if (std::abs(db - micGainDb()) < 1e-6)
		return;
	settings->SetMicGainDb(db);
	pswrapInitMic();
	emit micGainDbChanged();
}

bool QmlMainWindow::micGateEnabled() const
{
	return settings->GetMicGateEnabled();
}

void QmlMainWindow::setMicGateEnabled(bool on)
{
	if (on == micGateEnabled())
		return;
	settings->SetMicGateEnabled(on);
	pswrapInitMic();
	emit micGateEnabledChanged();
}

qreal QmlMainWindow::micGateThresholdDb() const
{
	return qBound(-80.0, settings->GetMicGateThresholdDb(), -20.0);
}

void QmlMainWindow::setMicGateThresholdDb(qreal db)
{
	db = roundHalfDb(qBound(-80.0, db, -20.0));
	if (std::abs(db - micGateThresholdDb()) < 1e-6)
		return;
	settings->SetMicGateThresholdDb(db);
	pswrapInitMic();
	emit micGateThresholdDbChanged();
}
