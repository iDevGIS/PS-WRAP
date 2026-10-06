// PS-WRAP: ที่เก็บความลับ (stream key ของ Go Live ฯลฯ) — ไม่เขียนลง registry/QSettings เด็ดขาด
//
// Windows: Windows Credential Manager (CredWriteW/CredReadW/CredDeleteW) แบบ CRED_TYPE_GENERIC
//          persist = LOCAL_MACHINE (ไม่ roam ไปเครื่องอื่น) · เข้ารหัสด้วย DPAPI ของ user โดย OS
//          ดู/ลบเองได้ที่ Control Panel › Credential Manager › Windows Credentials › "PS-WRAP/..."
// อื่นๆ:   stub — isAvailable() = false, ทุกฟังก์ชันคืน Error (ยังไม่ทำ libsecret/Keychain)
//
// target name: "PS-WRAP/<namespace>/<id>" เช่น liveTarget("youtube") = "PS-WRAP/live/youtube"
// ค่าเก็บเป็น UTF-8 · ยาวสุด 2560 byte (CRED_MAX_CREDENTIAL_BLOB_SIZE)
// thread-safe (Cred* API เป็น stateless) · เรียกจาก thread ไหนก็ได้ แต่ห้าม log ค่าที่อ่านได้

#ifndef PSWRAP_SECRETS_H
#define PSWRAP_SECRETS_H

#include <QString>

namespace PsWrapSecrets
{
	enum class Result
	{
		Ok,
		NotFound,   // ยังไม่เคยเก็บ / ถูกลบไปแล้ว
		Error       // OS error (ข้อความอยู่ใน *error) หรือแพลตฟอร์มไม่รองรับ
	};

	// มีที่เก็บจริงไหม (Windows = true) — UI ใช้ตัดสินว่าจะยอมให้ "จำ key" หรือไม่
	bool isAvailable();

	// "PS-WRAP/live/<destinationId>" หรือ "PS-WRAP/live/<profile>/<destinationId>" ถ้าระบุ profile
	// (รันด้วย --profile pswrap-test ต้องส่ง profile มา ไม่งั้นจะเขียนทับ key จริงของ profile หลัก)
	// แต่ละส่วนใช้ [A-Za-z0-9._-] (ตัวอื่นถูกแทนด้วย '_')
	QString liveTarget(const QString &destination_id, const QString &profile = QString());

	// เขียนทับถ้ามีอยู่แล้ว · secret ว่าง = ลบ (เหมือน remove)
	Result write(const QString &target, const QString &secret, QString *error = nullptr);
	// *secret ถูกตั้งเฉพาะตอน Ok
	Result read(const QString &target, QString *secret, QString *error = nullptr);
	// ลบที่ไม่มีอยู่ = Ok (idempotent)
	Result remove(const QString &target, QString *error = nullptr);
	// มีค่าเก็บไว้ไหม (ไม่คืนค่าจริง — UI ใช้แสดง "••••••• saved")
	bool exists(const QString &target);
}

#endif // PSWRAP_SECRETS_H
