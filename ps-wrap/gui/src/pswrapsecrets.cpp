// PS-WRAP: ที่เก็บความลับ — Windows Credential Manager / macOS Keychain (ดู pswrapsecrets.h)
#include <pswrapsecrets.h>

#include <QByteArray>

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <cstring>
#endif

namespace PsWrapSecrets
{

// กัน id แปลกๆ ทำ target ชนกัน/อ่านยาก — เหลือแค่ตัวที่ปลอดภัย
static QString sanitizePart(const QString &part)
{
	QString out;
	out.reserve(part.size());
	for (const QChar c : part)
	{
		const bool ok = (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
			|| (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
			|| (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
			|| c == QLatin1Char('.') || c == QLatin1Char('_') || c == QLatin1Char('-');
		out.append(ok ? c : QLatin1Char('_'));
	}
	if (out.isEmpty())
		out = QStringLiteral("_");
	return out;
}

QString liveTarget(const QString &destination_id, const QString &profile)
{
	QString target = QStringLiteral("PS-WRAP/live/");
	if (!profile.isEmpty())
		target += sanitizePart(profile) + QLatin1Char('/');
	return target + sanitizePart(destination_id);
}

#ifdef _WIN32

static QString lastErrorText(const char *what)
{
	const DWORD code = GetLastError();
	wchar_t *buf = nullptr;
	const DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, code, 0, reinterpret_cast<wchar_t *>(&buf), 0, nullptr);
	QString msg = len && buf ? QString::fromWCharArray(buf, int(len)).trimmed() : QString();
	if (buf)
		LocalFree(buf);
	return QStringLiteral("%1 failed (0x%2)%3").arg(QLatin1String(what))
		.arg(qulonglong(code), 8, 16, QLatin1Char('0'))
		.arg(msg.isEmpty() ? QString() : QStringLiteral(": ") + msg);
}

// Windows SDK: CRED_MAX_CREDENTIAL_BLOB_SIZE = 5*512 แต่ header ของ mingw-w64 ประกาศเป็น 512 → ใช้ค่าจริงเอง
static constexpr int kMaxBlob = 5 * 512;

static void setError(QString *error, const QString &msg)
{
	if (error)
		*error = msg;
}

bool isAvailable()
{
	return true;
}

Result write(const QString &target, const QString &secret, QString *error)
{
	if (target.isEmpty())
	{
		setError(error, QStringLiteral("empty target"));
		return Result::Error;
	}
	if (secret.isEmpty())
		return remove(target, error);

	QByteArray blob = secret.toUtf8();
	if (blob.size() > kMaxBlob)
	{
		SecureZeroMemory(blob.data(), size_t(blob.size()));
		setError(error, QStringLiteral("secret too long (%1 bytes, max %2)").arg(blob.size()).arg(kMaxBlob));
		return Result::Error;
	}

	std::wstring target_w = target.toStdWString();
	std::wstring user_w = L"PS-WRAP";
	CREDENTIALW cred = {};
	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = target_w.data();
	cred.CredentialBlobSize = DWORD(blob.size());
	cred.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;   // ไม่ roam ไปกับ domain profile
	cred.UserName = user_w.data();

	const BOOL ok = CredWriteW(&cred, 0);
	const QString err = ok ? QString() : lastErrorText("CredWriteW");
	SecureZeroMemory(blob.data(), size_t(blob.size()));
	if (!ok)
	{
		setError(error, err);
		return Result::Error;
	}
	return Result::Ok;
}

Result read(const QString &target, QString *secret, QString *error)
{
	if (target.isEmpty())
	{
		setError(error, QStringLiteral("empty target"));
		return Result::Error;
	}
	const std::wstring target_w = target.toStdWString();
	PCREDENTIALW cred = nullptr;
	if (!CredReadW(target_w.c_str(), CRED_TYPE_GENERIC, 0, &cred))
	{
		if (GetLastError() == ERROR_NOT_FOUND)
			return Result::NotFound;
		setError(error, lastErrorText("CredReadW"));
		return Result::Error;
	}
	if (secret)
		*secret = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob), int(cred->CredentialBlobSize));
	if (cred->CredentialBlob && cred->CredentialBlobSize)
		SecureZeroMemory(cred->CredentialBlob, cred->CredentialBlobSize);
	CredFree(cred);
	return Result::Ok;
}

Result remove(const QString &target, QString *error)
{
	if (target.isEmpty())
	{
		setError(error, QStringLiteral("empty target"));
		return Result::Error;
	}
	const std::wstring target_w = target.toStdWString();
	if (!CredDeleteW(target_w.c_str(), CRED_TYPE_GENERIC, 0))
	{
		if (GetLastError() == ERROR_NOT_FOUND)
			return Result::Ok;
		setError(error, lastErrorText("CredDeleteW"));
		return Result::Error;
	}
	return Result::Ok;
}

bool exists(const QString &target)
{
	return read(target, nullptr) == Result::Ok;
}

#elif defined(__APPLE__) // macOS Keychain (Security.framework) — generic password: service "PS-WRAP", account = target

static constexpr int kMaxBlob = 5 * 512;   // เท่ากับฝั่ง Windows

static void setError(QString *error, const QString &msg)
{
	if (error)
		*error = msg;
}

static QString osStatusText(const char *what, OSStatus st)
{
	QString msg;
	if (CFStringRef cf = SecCopyErrorMessageString(st, nullptr))
	{
		msg = QString::fromCFString(cf);
		CFRelease(cf);
	}
	return QStringLiteral("%1 failed (%2)%3").arg(QLatin1String(what)).arg(int(st))
		.arg(msg.isEmpty() ? QString() : QStringLiteral(": ") + msg);
}

static CFMutableDictionaryRef baseQuery(const QString &target)
{
	CFMutableDictionaryRef q = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
	CFStringRef service = CFSTR("PS-WRAP");
	CFDictionarySetValue(q, kSecAttrService, service);
	CFStringRef account = target.toCFString();
	CFDictionarySetValue(q, kSecAttrAccount, account);
	CFRelease(account);
	return q;
}

bool isAvailable()
{
	return true;
}

Result write(const QString &target, const QString &secret, QString *error)
{
	if (target.isEmpty())
	{
		setError(error, QStringLiteral("empty target"));
		return Result::Error;
	}
	if (secret.isEmpty())
		return remove(target, error);

	QByteArray blob = secret.toUtf8();
	if (blob.size() > kMaxBlob)
	{
		memset(blob.data(), 0, size_t(blob.size()));
		setError(error, QStringLiteral("secret too long (%1 bytes, max %2)").arg(blob.size()).arg(kMaxBlob));
		return Result::Error;
	}
	CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(blob.constData()), blob.size());
	memset(blob.data(), 0, size_t(blob.size()));

	// มีอยู่แล้ว → update · ไม่มี → add (เข้าถึงได้เฉพาะเครื่องนี้ ไม่ sync iCloud)
	CFMutableDictionaryRef q = baseQuery(target);
	CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(attrs, kSecValueData, data);
	OSStatus st = SecItemUpdate(q, attrs);
	if (st == errSecItemNotFound)
	{
		CFDictionarySetValue(q, kSecValueData, data);
		CFDictionarySetValue(q, kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
		CFStringRef label = CFSTR("PS-WRAP");
		CFDictionarySetValue(q, kSecAttrLabel, label);
		st = SecItemAdd(q, nullptr);
	}
	CFRelease(attrs);
	CFRelease(q);
	CFRelease(data);
	if (st != errSecSuccess)
	{
		setError(error, osStatusText(st == errSecItemNotFound ? "SecItemAdd" : "SecItemUpdate", st));
		return Result::Error;
	}
	return Result::Ok;
}

Result read(const QString &target, QString *secret, QString *error)
{
	if (target.isEmpty())
	{
		setError(error, QStringLiteral("empty target"));
		return Result::Error;
	}
	CFMutableDictionaryRef q = baseQuery(target);
	CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
	CFTypeRef result = nullptr;
	const OSStatus st = SecItemCopyMatching(q, &result);
	CFRelease(q);
	if (st == errSecItemNotFound)
		return Result::NotFound;
	if (st != errSecSuccess)
	{
		setError(error, osStatusText("SecItemCopyMatching", st));
		return Result::Error;
	}
	if (result && CFGetTypeID(result) == CFDataGetTypeID())
	{
		CFDataRef data = static_cast<CFDataRef>(result);
		if (secret)
			*secret = QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), int(CFDataGetLength(data)));
	}
	if (result)
		CFRelease(result);
	return Result::Ok;
}

Result remove(const QString &target, QString *error)
{
	if (target.isEmpty())
	{
		setError(error, QStringLiteral("empty target"));
		return Result::Error;
	}
	CFMutableDictionaryRef q = baseQuery(target);
	const OSStatus st = SecItemDelete(q);
	CFRelease(q);
	if (st == errSecSuccess || st == errSecItemNotFound)
		return Result::Ok;
	setError(error, osStatusText("SecItemDelete", st));
	return Result::Error;
}

bool exists(const QString &target)
{
	return read(target, nullptr) == Result::Ok;
}

#else // อื่นๆ — stub: ยังไม่มีที่เก็บที่ปลอดภัย (TODO: libsecret)

static Result unsupported(QString *error)
{
	if (error)
		*error = QStringLiteral("secure credential storage is not supported on this platform");
	return Result::Error;
}

bool isAvailable() { return false; }
Result write(const QString &, const QString &, QString *error) { return unsupported(error); }
Result read(const QString &, QString *, QString *error) { return unsupported(error); }
Result remove(const QString &, QString *error) { return unsupported(error); }
bool exists(const QString &) { return false; }

#endif

} // namespace PsWrapSecrets
