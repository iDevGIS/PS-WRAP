// PS-WRAP-Diagnostics — เครื่องมือวินิจฉัยปัญหาของ PS-WRAP ให้ user เปิดเองตอนติดปัญหา แล้วส่ง zip/สรุปกลับมา
// Win32 ล้วน + static (ไม่พึ่ง Qt/DLL ของแอป) เพื่อให้เปิดได้แม้ตัวแอปพัง
// - ตรวจเครื่อง: ไฟล์ในโฟลเดอร์, Windows, การ์ดจอ/ไดรเวอร์, Vulkan (process ลูกมี timeout เพราะ Vulkan ค้างได้), Vulkan layer,
//   โปรแกรมที่รันอยู่, settings (กรอง token/คีย์ทิ้ง), log ของแอป, Windows Error Reporting, Event Log
// - ทดสอบเปิดแอป: ดัก stdout/stderr (PSWRAP_DIAG → แอปไม่ buffer), verbose ได้, จับหน้าต่างค้าง → stack ทุก thread + minidump
// - ทุกข้อความผ่าน Redact() ก่อนแสดง/เขียนไฟล์ (path/ชื่อ user, ชื่อเครื่อง, IP สาธารณะ, MAC, token ยาว)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <winevt.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef PSWRAP_VERSION
#define PSWRAP_VERSION "dev"
#endif

using std::string;
using std::vector;
using std::wstring;

static const wchar_t *kIssuesUrl = L"https://github.com/iDevGIS/PS-WRAP/issues/new";
static const wchar_t *kSettingsBase = L"Software\\PS-WRAP\\PS-WRAP";
static const DWORD kVulkanTimeoutMs = 20000;
static const int kHungSecondsBeforeCapture = 10;   // IsHungAppWindow ตอบ true หลังไม่ตอบ 5 วิ → รวม ~15 วิ
static const int kNoWindowSecondsBeforeCapture = 60;

// ───────────────────────── string / file helpers ─────────────────────────

static wstring W(const string &s)
{
	if(s.empty())
		return {};
	UINT cp = CP_UTF8;
	DWORD flags = MB_ERR_INVALID_CHARS;
	int n = MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), nullptr, 0);
	if(n <= 0)
	{
		cp = CP_ACP;
		flags = 0;
		n = MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), nullptr, 0);
	}
	wstring r(n, L'\0');
	MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), &r[0], n);
	return r;
}

static string U8(const wstring &s)
{
	if(s.empty())
		return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
	string r(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n, nullptr, nullptr);
	return r;
}

static wstring Fmt(const wchar_t *fmt, ...)
{
	wchar_t buf[4096];
	va_list ap;
	va_start(ap, fmt);
	int n = vswprintf(buf, sizeof(buf) / sizeof(buf[0]), fmt, ap);
	va_end(ap);
	if(n < 0)
		buf[sizeof(buf) / sizeof(buf[0]) - 1] = 0;
	return buf;
}

static wstring Lower(wstring s)
{
	for(auto &c : s)
		c = (wchar_t)towlower(c);
	return s;
}

static bool IContains(const wstring &hay, const wstring &needle)
{
	return Lower(hay).find(Lower(needle)) != wstring::npos;
}

static bool IStarts(const wstring &s, const wstring &prefix)
{
	return s.size() >= prefix.size() && _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

static wstring Trim(const wstring &s)
{
	size_t a = s.find_first_not_of(L" \t\r\n");
	if(a == wstring::npos)
		return {};
	size_t b = s.find_last_not_of(L" \t\r\n");
	return s.substr(a, b - a + 1);
}

static vector<wstring> SplitLines(const wstring &s)
{
	vector<wstring> out;
	size_t start = 0;
	while(start <= s.size())
	{
		size_t nl = s.find(L'\n', start);
		wstring line = s.substr(start, nl == wstring::npos ? wstring::npos : nl - start);
		if(!line.empty() && line.back() == L'\r')
			line.pop_back();
		out.push_back(line);
		if(nl == wstring::npos)
			break;
		start = nl + 1;
	}
	if(!out.empty() && out.back().empty())
		out.pop_back();
	return out;
}

static wstring Crlf(const wstring &s)
{
	wstring r;
	r.reserve(s.size() + s.size() / 32);
	for(size_t i = 0; i < s.size(); i++)
	{
		if(s[i] == L'\n' && (i == 0 || s[i - 1] != L'\r'))
			r += L'\r';
		r += s[i];
	}
	return r;
}

// สีของ log ฝั่งแอป (\033[38;5;31m ...) ทิ้งก่อนแสดง
static wstring StripAnsi(const wstring &s)
{
	wstring r;
	r.reserve(s.size());
	for(size_t i = 0; i < s.size(); i++)
	{
		if(s[i] == 0x1b && i + 1 < s.size() && s[i + 1] == L'[')
		{
			i += 2;
			while(i < s.size() && !(s[i] >= L'@' && s[i] <= L'~'))
				i++;
			continue;
		}
		r += s[i];
	}
	return r;
}

static wstring Hex64(unsigned long long v)
{
	return Fmt(L"%llx", v);
}

static wstring FileName(const wstring &path)
{
	size_t p = path.find_last_of(L"\\/");
	return p == wstring::npos ? path : path.substr(p + 1);
}

static wstring Env(const wchar_t *name)
{
	wchar_t buf[32768];
	DWORD n = GetEnvironmentVariableW(name, buf, 32768);
	return (n && n < 32768) ? wstring(buf, n) : wstring();
}

static wstring KnownFolder(REFKNOWNFOLDERID id)
{
	PWSTR p = nullptr;
	wstring r;
	if(SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p)
		r = p;
	if(p)
		CoTaskMemFree(p);
	return r;
}

static bool FileExists(const wstring &p)
{
	DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool DirExists(const wstring &p)
{
	DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// อ่านไฟล์ (ถ้าใหญ่กว่า max_bytes เอาแค่ท้ายไฟล์)
static bool ReadFileBytes(const wstring &path, string &out, ULONGLONG max_bytes = 0)
{
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
	if(f == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER size{};
	GetFileSizeEx(f, &size);
	ULONGLONG want = (ULONGLONG)size.QuadPart;
	if(max_bytes && want > max_bytes)
	{
		LARGE_INTEGER off;
		off.QuadPart = (LONGLONG)(want - max_bytes);
		SetFilePointerEx(f, off, nullptr, FILE_BEGIN);
		want = max_bytes;
	}
	out.resize((size_t)want);
	DWORD got = 0;
	size_t pos = 0;
	while(pos < out.size() && ReadFile(f, &out[pos], (DWORD)std::min<size_t>(out.size() - pos, 1 << 24), &got, nullptr) && got)
		pos += got;
	out.resize(pos);
	CloseHandle(f);
	return true;
}

static wstring DecodeText(const string &bytes)
{
	if(bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE)
		return wstring((const wchar_t *)(bytes.data() + 2), (bytes.size() - 2) / 2);
	if(bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
		return W(bytes.substr(3));
	return W(bytes);
}

static bool WriteText(const wstring &path, const wstring &text)
{
	HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
	if(f == INVALID_HANDLE_VALUE)
		return false;
	string data = "\xEF\xBB\xBF" + U8(Crlf(text));
	DWORD w = 0;
	bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &w, nullptr) && w == data.size();
	CloseHandle(f);
	return ok;
}

static void RemoveTree(const wstring &dir)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if(h != INVALID_HANDLE_VALUE)
	{
		do
		{
			wstring n = fd.cFileName;
			if(n == L"." || n == L"..")
				continue;
			wstring p = dir + L"\\" + n;
			if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				RemoveTree(p);
			else
				DeleteFileW(p.c_str());
		} while(FindNextFileW(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryW(dir.c_str());
}

static wstring TimeText(const SYSTEMTIME &st)
{
	return Fmt(L"%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static wstring FileTimeText(const FILETIME &ft_utc)
{
	FILETIME lft;
	SYSTEMTIME st;
	FileTimeToLocalFileTime(&ft_utc, &lft);
	FileTimeToSystemTime(&lft, &st);
	return TimeText(st);
}

static wstring NowText()
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	return TimeText(st);
}

static wstring NowStamp()
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	return Fmt(L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static ULONGLONG FileTimeU64(const FILETIME &ft)
{
	return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static ULONGLONG NowU64()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return FileTimeU64(ft);
}

static const ULONGLONG kDay100ns = 864000000000ULL;

static wstring SizeText(ULONGLONG b)
{
	if(b >= (1ULL << 20))
		return Fmt(L"%.1f MB", b / 1048576.0);
	if(b >= 1024)
		return Fmt(L"%.0f KB", b / 1024.0);
	return Fmt(L"%llu B", b);
}

// ───────────────────────── registry ─────────────────────────

static bool RegStr(HKEY root, const wstring &sub, const wchar_t *name, wstring &out, REGSAM extra = 0)
{
	HKEY h;
	if(RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ | extra, &h) != ERROR_SUCCESS)
		return false;
	wchar_t buf[2048];
	DWORD type = 0, size = sizeof(buf) - sizeof(wchar_t);
	LONG r = RegQueryValueExW(h, name, nullptr, &type, (BYTE *)buf, &size);
	RegCloseKey(h);
	if(r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
		return false;
	buf[size / sizeof(wchar_t)] = 0;
	out = buf;
	return true;
}

static bool RegDword(HKEY root, const wstring &sub, const wchar_t *name, DWORD &out, REGSAM extra = 0)
{
	HKEY h;
	if(RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ | extra, &h) != ERROR_SUCCESS)
		return false;
	DWORD type = 0, size = sizeof(out);
	LONG r = RegQueryValueExW(h, name, nullptr, &type, (BYTE *)&out, &size);
	RegCloseKey(h);
	return r == ERROR_SUCCESS && type == REG_DWORD;
}

// ───────────────────────── redaction ─────────────────────────

static vector<std::pair<wstring, wstring>> g_redact_literals;   // (สิ่งที่ต้องซ่อน, แทนด้วย) — profile path ก่อน, ชื่อ user/เครื่องทีหลัง
static vector<std::pair<wstring, wstring>> g_redact_words;

static wstring IReplace(const wstring &s, const wstring &from, const wstring &to, bool word)
{
	if(from.empty())
		return s;
	wstring ls = Lower(s), lf = Lower(from), out;
	size_t pos = 0, i;
	while((i = ls.find(lf, pos)) != wstring::npos)
	{
		bool ok = true;
		if(word)
		{
			if(i > 0 && iswalnum(s[i - 1]))
				ok = false;
			size_t e = i + lf.size();
			if(e < s.size() && iswalnum(s[e]))
				ok = false;
		}
		out.append(s, pos, i - pos);
		out += ok ? to : s.substr(i, lf.size());
		pos = i + lf.size();
	}
	out.append(s, pos, wstring::npos);
	return out;
}

static void InitRedaction()
{
	wstring profile = Env(L"USERPROFILE");
	if(profile.size() > 3)
	{
		g_redact_literals.push_back({ profile, L"%USERPROFILE%" });
		wstring fwd = profile;
		std::replace(fwd.begin(), fwd.end(), L'\\', L'/');
		g_redact_literals.push_back({ fwd, L"%USERPROFILE%" });
	}
	wstring user = Env(L"USERNAME");
	if(user.size() >= 3)
		g_redact_words.push_back({ user, L"<user>" });
	wstring pc = Env(L"COMPUTERNAME");
	if(pc.size() >= 3)
		g_redact_words.push_back({ pc, L"<pc>" });
}

// เฉพาะ path/ชื่อ user/ชื่อเครื่อง — ใช้กับข้อความที่เราสร้างเอง (stack) ซึ่งชื่อฟังก์ชันยาวๆ ไม่ใช่ token
static wstring RedactPaths(const wstring &in)
{
	wstring s = in;
	for(auto &p : g_redact_literals)
		s = IReplace(s, p.first, p.second, false);
	for(auto &p : g_redact_words)
		s = IReplace(s, p.first, p.second, true);
	return s;
}

static bool PrivateIp(int a, int b)
{
	return a == 10 || a == 127 || a == 0 || a >= 224 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31) || (a == 169 && b == 254);
}

static bool IsHexDigit(wchar_t c)
{
	return iswxdigit(c) != 0;
}

static wstring Redact(const wstring &in)
{
	wstring s = in;
	for(auto &p : g_redact_literals)
		s = IReplace(s, p.first, p.second, false);
	for(auto &p : g_redact_words)
		s = IReplace(s, p.first, p.second, true);

	wstring out;
	out.reserve(s.size());
	size_t i = 0, n = s.size();
	while(i < n)
	{
		wchar_t c = s[i];
		bool boundary = i == 0 || !(iswalnum(s[i - 1]) || s[i - 1] == L'.' || s[i - 1] == L':');

		// IPv4 สาธารณะ (เก็บ IP ในบ้านไว้ เพราะช่วยดูปัญหาเครือข่าย)
		if(boundary && iswdigit(c))
		{
			int parts[4];
			size_t j = i;
			int k = 0;
			bool ok = true;
			for(; k < 4; k++)
			{
				int v = 0, d = 0;
				while(j < n && iswdigit(s[j]) && d < 4)
				{
					v = v * 10 + (s[j] - L'0');
					j++;
					d++;
				}
				if(d == 0 || d > 3 || v > 255)
				{
					ok = false;
					break;
				}
				parts[k] = v;
				if(k < 3)
				{
					if(j < n && s[j] == L'.')
						j++;
					else
					{
						ok = false;
						break;
					}
				}
			}
			if(ok && k == 4 && !(j < n && (iswalnum(s[j]) || (s[j] == L'.' && j + 1 < n && iswdigit(s[j + 1])))))
			{
				// เลขเวอร์ชัน (1.9.9.0, 6.11.0.0) หน้าตาเหมือน IP — ทุกส่วน < 50 และไม่มี :port ตามหลัง = ถือเป็นเวอร์ชัน
				bool port = j < n && s[j] == L':' && j + 1 < n && iswdigit(s[j + 1]);
				bool version_like = !port && parts[0] < 50 && parts[1] < 50 && parts[2] < 50 && parts[3] < 50;
				if(version_like || PrivateIp(parts[0], parts[1]))
					out.append(s, i, j - i);
				else
					out += L"<ip>";
				i = j;
				continue;
			}
		}

		// MAC aa:bb:cc:dd:ee:ff หรือ aa-bb-...
		if(boundary && i + 17 <= n && IsHexDigit(c))
		{
			wchar_t sep = s[i + 2];
			bool mac = (sep == L':' || sep == L'-');
			for(int g = 0; mac && g < 6; g++)
			{
				size_t p = i + g * 3;
				if(!IsHexDigit(s[p]) || !IsHexDigit(s[p + 1]))
					mac = false;
				if(g < 5 && s[p + 2] != sep)
					mac = false;
			}
			if(mac && !(i + 17 < n && (iswalnum(s[i + 17]) || s[i + 17] == sep)))
			{
				out += L"<mac>";
				i += 17;
				continue;
			}
		}

		// token ยาว (base64/hex ≥ 32 ตัว ที่ปนตัวเลข+ตัวพิมพ์ใหญ่เล็ก หรือเป็น hex ล้วน)
		if(iswalnum(c) && boundary)
		{
			size_t j = i;
			bool digit = false, upper = false, lower = false, hex = true;
			while(j < n && (iswalnum(s[j]) || s[j] == L'+' || s[j] == L'=' || s[j] == L'_' || s[j] == L'-'))
			{
				wchar_t d = s[j];
				if(iswdigit(d))
					digit = true;
				else if(iswupper(d))
					upper = true;
				else if(iswlower(d))
					lower = true;
				if(!(IsHexDigit(d) || d == L'-'))
					hex = false;
				j++;
			}
			size_t len = j - i;
			if(len >= 32 && ((digit && upper && lower) || (hex && digit)))
			{
				out += L"<redacted>";
				i = j;
				continue;
			}
			out.append(s, i, len);
			i = j;
			continue;
		}

		out += c;
		i++;
	}
	return out;
}

// ───────────────────────── report model ─────────────────────────

enum Level { LV_OK, LV_INFO, LV_WARN, LV_FAIL };

struct Finding
{
	Level level;
	wstring title;
	wstring fix;
};

struct Report
{
	std::mutex mu;
	vector<Finding> findings;          // จากการตรวจเครื่อง (ล้างทุกครั้งที่ตรวจใหม่)
	vector<Finding> launch_findings;   // จากทดสอบเปิดแอป
	wstring sections;
	wstring launch_section;
	vector<wstring> error_points;      // จาก log ของแอป
	vector<wstring> launch_errors;     // จาก output ตอนทดสอบเปิด
	wstring header_windows, header_gpu, header_renderer;
	bool vulkan_hang = false;
	bool checked = false;
	wstring last_zip;
};

static Report g_rep;
static wstring g_tool_path, g_app_dir, g_app_exe, g_work_dir;
static wstring g_launch_args;   // --profile <ชื่อ> ส่งต่อให้ PS-WRAP (ไว้ทดสอบโดยไม่แตะโปรไฟล์จริง)

static void AddFinding(Level lv, const wstring &title, const wstring &fix = L"")
{
	std::lock_guard<std::mutex> l(g_rep.mu);
	g_rep.findings.push_back({ lv, Redact(title), Redact(fix) });
}

static void AddSection(const wstring &title, const wstring &body)
{
	wstring red = Redact(body);
	std::lock_guard<std::mutex> l(g_rep.mu);
	g_rep.sections += L"\n== " + title + L" ==\n" + red;
	if(!red.empty() && red.back() != L'\n')
		g_rep.sections += L"\n";
}

static const wchar_t *LevelTag(Level lv)
{
	switch(lv)
	{
		case LV_OK: return L"[ OK ]";
		case LV_INFO: return L"[INFO]";
		case LV_WARN: return L"[WARN]";
		default: return L"[FAIL]";
	}
}

// ───────────────────────── UI plumbing ─────────────────────────

#define WM_APP_TEXT (WM_APP + 1)     // lParam = wstring* ต่อท้ายช่องข้อความ
#define WM_APP_STATUS (WM_APP + 2)   // lParam = wstring*
#define WM_APP_DONE (WM_APP + 3)     // wParam = job
#define WM_APP_HANG (WM_APP + 4)     // lParam = wstring* (จุดที่ค้าง)

enum Job { JOB_CHECK = 1, JOB_LAUNCH = 2 };

enum
{
	IDC_CHECK = 101,
	IDC_TEST,
	IDC_SAFE,
	IDC_VERBOSE,
	IDC_SAVE,
	IDC_COPY,
	IDC_GITHUB,
	IDC_RENDERER,
	IDC_EDIT,
	IDC_STATUS,
	IDC_INTRO,
};

static HWND g_hwnd, g_edit, g_status, g_intro, g_btn_check, g_btn_test, g_btn_safe, g_chk_verbose, g_btn_save, g_btn_copy, g_btn_github, g_btn_renderer;
static HFONT g_font, g_font_mono;
static UINT g_dpi = 96;
static std::atomic<bool> g_busy_check{ false };
static std::atomic<bool> g_busy_launch{ false };
static std::mutex g_proc_mu;
static HANDLE g_run_proc = nullptr;   // process ที่ทดสอบอยู่ (ใช้ร่วมกับ UI ตอนกดปิด — ถือ g_proc_mu ทุกครั้ง)

static void UiText(const wstring &s)
{
	PostMessageW(g_hwnd, WM_APP_TEXT, 0, (LPARAM) new wstring(s));
}

static void UiStatus(const wstring &s)
{
	PostMessageW(g_hwnd, WM_APP_STATUS, 0, (LPARAM) new wstring(s));
}

// ───────────────────────── child process with captured output ─────────────────────────

static vector<wchar_t> MakeEnvBlock(const vector<std::pair<wstring, wstring>> &overrides)
{
	vector<wstring> vars;
	LPWCH e = GetEnvironmentStringsW();
	for(LPWCH p = e; p && *p; p += wcslen(p) + 1)
		vars.push_back(p);
	if(e)
		FreeEnvironmentStringsW(e);
	auto name_of = [](const wstring &v) {
		size_t eq = v.find(L'=', 1);   // ตัวแปรแบบ "=C:" ขึ้นต้นด้วย '='
		return eq == wstring::npos ? v : v.substr(0, eq);
	};
	for(auto &o : overrides)
	{
		vars.erase(std::remove_if(vars.begin(), vars.end(), [&](const wstring &v) { return _wcsicmp(name_of(v).c_str(), o.first.c_str()) == 0; }), vars.end());
		if(!o.second.empty())
			vars.push_back(o.first + L"=" + o.second);
	}
	std::sort(vars.begin(), vars.end(), [&](const wstring &a, const wstring &b) { return _wcsicmp(name_of(a).c_str(), name_of(b).c_str()) < 0; });
	vector<wchar_t> block;
	for(auto &v : vars)
	{
		block.insert(block.end(), v.begin(), v.end());
		block.push_back(0);
	}
	block.push_back(0);
	return block;
}

struct Spawned
{
	HANDLE process = nullptr;
	HANDLE thread = nullptr;
	DWORD pid = 0;
	HANDLE read = nullptr;
	DWORD error = 0;
};

// สร้าง process ที่ stdout+stderr ไหลเข้า pipe เดียว และสืบทอดเฉพาะ handle ของ pipe (ไม่รั่ว handle อื่นของเรา)
static Spawned SpawnCaptured(const wstring &exe, const wstring &args, const vector<std::pair<wstring, wstring>> &env, const wstring &cwd)
{
	Spawned sp;
	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
	HANDLE rd = nullptr, wr = nullptr;
	if(!CreatePipe(&rd, &wr, &sa, 1 << 16))
	{
		sp.error = GetLastError();
		return sp;
	}
	SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
	HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

	SIZE_T attr_size = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
	vector<BYTE> attr_buf(attr_size);
	auto attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)attr_buf.data();
	InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size);
	HANDLE inherit[2] = { wr, nul };
	UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(HANDLE) * (nul != INVALID_HANDLE_VALUE ? 2 : 1), nullptr, nullptr);

	STARTUPINFOEXW si{};
	si.StartupInfo.cb = sizeof(si);
	si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	si.StartupInfo.hStdOutput = wr;
	si.StartupInfo.hStdError = wr;
	si.StartupInfo.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : nullptr;
	si.lpAttributeList = attrs;

	wstring cmd = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
	vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
	cmd_buf.push_back(0);
	vector<wchar_t> env_block = MakeEnvBlock(env);

	PROCESS_INFORMATION pi{};
	BOOL ok = CreateProcessW(exe.c_str(), cmd_buf.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
		env_block.data(), cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
	sp.error = ok ? 0 : GetLastError();
	DeleteProcThreadAttributeList(attrs);
	CloseHandle(wr);   // ต้องปิดฝั่งเขียนของเรา ไม่งั้น ReadFile ไม่มีวันจบ
	if(nul != INVALID_HANDLE_VALUE)
		CloseHandle(nul);
	if(!ok)
	{
		CloseHandle(rd);
		return sp;
	}
	sp.process = pi.hProcess;
	sp.thread = pi.hThread;
	sp.pid = pi.dwProcessId;
	sp.read = rd;
	return sp;
}

struct ChildResult
{
	bool started = false;
	bool timed_out = false;
	DWORD exit_code = 0;
	DWORD error = 0;
	string output;
};

static ChildResult RunCaptured(const wstring &exe, const wstring &args, const vector<std::pair<wstring, wstring>> &env, DWORD timeout_ms, const wstring &cwd)
{
	ChildResult res;
	Spawned sp = SpawnCaptured(exe, args, env, cwd);
	if(!sp.process)
	{
		res.error = sp.error;
		return res;
	}
	res.started = true;
	std::mutex mu;
	std::thread reader([&] {
		char buf[4096];
		DWORD got;
		while(ReadFile(sp.read, buf, sizeof(buf), &got, nullptr) && got)
		{
			std::lock_guard<std::mutex> l(mu);
			res.output.append(buf, got);
		}
	});
	if(WaitForSingleObject(sp.process, timeout_ms) == WAIT_TIMEOUT)
	{
		res.timed_out = true;
		TerminateProcess(sp.process, 0xDEAD);
		WaitForSingleObject(sp.process, 5000);
	}
	GetExitCodeProcess(sp.process, &res.exit_code);
	if(WaitForSingleObject(sp.process, 0) == WAIT_OBJECT_0)
	{
		// process จบแล้วแต่ถ้ามีลูกหลานถือ pipe อยู่ ReadFile จะค้าง → ยกเลิกหลังรอสั้นๆ
		Sleep(200);
		CancelIoEx(sp.read, nullptr);
	}
	reader.join();
	CloseHandle(sp.read);
	CloseHandle(sp.thread);
	CloseHandle(sp.process);
	return res;
}

static wstring ExitCodeText(DWORD code)
{
	switch(code)
	{
		case 0: return L"0 (normal)";
		case 3: return L"3 (aborted — fatal error inside PS-WRAP)";
		case 0xDEAD: return L"closed by PS-WRAP-Diagnostics";
		case 0xC0000005: return L"0xC0000005 (crash: access violation)";
		case 0xC0000135: return L"0xC0000135 (a DLL file is missing)";
		case 0xC0000139: return L"0xC0000139 (wrong DLL version — entry point not found)";
		case 0xC000007B: return L"0xC000007B (32/64-bit DLL mix-up)";
		case 0xC0000409: return L"0xC0000409 (crash: fast fail / stack buffer overrun)";
		case 0xC0000374: return L"0xC0000374 (crash: heap corruption)";
		case 0xC00000FD: return L"0xC00000FD (crash: stack overflow)";
		case 0xE06D7363: return L"0xE06D7363 (unhandled C++ exception)";
		case 0xC000013A: return L"0xC000013A (closed with Ctrl+C / logoff)";
		default: return Fmt(L"0x%08X", code);
	}
}

// ───────────────────────── Vulkan probe (process ลูก) ─────────────────────────

static void ProbeOut(const char *fmt, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if(n < 0)
		return;
	DWORD w;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, (DWORD)std::min<int>(n, sizeof(buf) - 1), &w, nullptr);
}

static string VkDriverVersion(uint32_t vendor, uint32_t v)
{
	char buf[64];
	if(vendor == 0x10DE)
		snprintf(buf, sizeof(buf), "%u.%02u", (v >> 22) & 0x3ff, (v >> 14) & 0xff);
	else if(vendor == 0x8086)
		snprintf(buf, sizeof(buf), "%u.%u", v >> 14, v & 0x3fff);
	else
		snprintf(buf, sizeof(buf), "%u.%u.%u", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
	return buf;
}

static int VkProbeMain()
{
	ProbeOut("step: load vulkan-1.dll\n");
	HMODULE m = LoadLibraryW(L"vulkan-1.dll");
	if(!m)
	{
		ProbeOut("error: vulkan-1.dll could not be loaded (error %lu)\n", GetLastError());
		return 2;
	}
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(m, path, MAX_PATH);
	ProbeOut("loader: %s\n", U8(path).c_str());
	auto gipa = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(m, "vkGetInstanceProcAddr");
	if(!gipa)
	{
		ProbeOut("error: vkGetInstanceProcAddr missing\n");
		return 2;
	}
	uint32_t api = VK_API_VERSION_1_0;
	auto enum_version = (PFN_vkEnumerateInstanceVersion)gipa(nullptr, "vkEnumerateInstanceVersion");
	if(enum_version)
		enum_version(&api);
	ProbeOut("instance-version: %u.%u.%u\n", VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api), VK_API_VERSION_PATCH(api));

	ProbeOut("step: vkEnumerateInstanceLayerProperties\n");
	auto enum_layers = (PFN_vkEnumerateInstanceLayerProperties)gipa(nullptr, "vkEnumerateInstanceLayerProperties");
	if(enum_layers)
	{
		uint32_t n = 0;
		enum_layers(&n, nullptr);
		vector<VkLayerProperties> layers(n);
		if(n && enum_layers(&n, layers.data()) >= 0)
			for(auto &l : layers)
				ProbeOut("layer: %s — %s\n", l.layerName, l.description);
	}

	ProbeOut("step: vkCreateInstance\n");
	if(GetEnvironmentVariableW(L"PSWRAP_DIAG_TEST_VKHANG", nullptr, 0))   // ทดสอบทาง "Vulkan ค้าง" โดยไม่ต้องมีเครื่องที่พังจริง
		for(;;)
			Sleep(1000);
	auto create_instance = (PFN_vkCreateInstance)gipa(nullptr, "vkCreateInstance");
	VkApplicationInfo app{};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "PS-WRAP-Diagnostics";
	app.apiVersion = api >= VK_API_VERSION_1_2 ? VK_API_VERSION_1_2 : api;
	const char *exts[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
	VkInstanceCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ci.pApplicationInfo = &app;
	ci.enabledExtensionCount = 2;
	ci.ppEnabledExtensionNames = exts;
	VkInstance inst = VK_NULL_HANDLE;
	VkResult r = create_instance ? create_instance(&ci, nullptr, &inst) : VK_ERROR_INITIALIZATION_FAILED;
	ProbeOut("result: vkCreateInstance = %d\n", (int)r);
	if(r != VK_SUCCESS)
		return 3;

	auto enum_devices = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
	auto get_props = (PFN_vkGetPhysicalDeviceProperties)gipa(inst, "vkGetPhysicalDeviceProperties");
	auto get_queues = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa(inst, "vkGetPhysicalDeviceQueueFamilyProperties");
	auto create_device = (PFN_vkCreateDevice)gipa(inst, "vkCreateDevice");
	auto destroy_instance = (PFN_vkDestroyInstance)gipa(inst, "vkDestroyInstance");
	auto get_device_proc = (PFN_vkGetDeviceProcAddr)gipa(inst, "vkGetDeviceProcAddr");

	ProbeOut("step: vkEnumeratePhysicalDevices\n");
	uint32_t count = 0;
	enum_devices(inst, &count, nullptr);
	vector<VkPhysicalDevice> devs(count);
	if(count)
		enum_devices(inst, &count, devs.data());
	ProbeOut("devices: %u\n", count);
	static const char *types[] = { "other", "integrated", "discrete", "virtual", "cpu" };
	for(auto d : devs)
	{
		VkPhysicalDeviceProperties p;
		get_props(d, &p);
		ProbeOut("gpu: %s | %s | Vulkan %u.%u.%u | driver %s | vendor 0x%04X device 0x%04X\n", p.deviceName, p.deviceType <= 4 ? types[p.deviceType] : "?",
			VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion),
			VkDriverVersion(p.vendorID, p.driverVersion).c_str(), p.vendorID, p.deviceID);

		uint32_t qn = 0;
		get_queues(d, &qn, nullptr);
		vector<VkQueueFamilyProperties> qs(qn);
		if(qn)
			get_queues(d, &qn, qs.data());
		uint32_t family = UINT32_MAX;
		for(uint32_t i = 0; i < qn; i++)
			if(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				family = i;
				break;
			}
		if(family == UINT32_MAX)
			continue;
		ProbeOut("step: vkCreateDevice %s\n", p.deviceName);
		float prio = 1.0f;
		VkDeviceQueueCreateInfo qci{};
		qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		qci.queueFamilyIndex = family;
		qci.queueCount = 1;
		qci.pQueuePriorities = &prio;
		VkDeviceCreateInfo dci{};
		dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		dci.queueCreateInfoCount = 1;
		dci.pQueueCreateInfos = &qci;
		VkDevice dev = VK_NULL_HANDLE;
		VkResult dr = create_device(d, &dci, nullptr, &dev);
		ProbeOut("result: vkCreateDevice = %d\n", (int)dr);
		if(dr == VK_SUCCESS && get_device_proc)
		{
			auto destroy_device = (PFN_vkDestroyDevice)get_device_proc(dev, "vkDestroyDevice");
			if(destroy_device)
				destroy_device(dev, nullptr);
		}
	}
	destroy_instance(inst, nullptr);
	ProbeOut("done\n");
	return 0;
}

// ───────────────────────── checks ─────────────────────────

static void CountFiles(const wstring &dir, size_t &files, ULONGLONG &bytes, int depth = 0)
{
	if(depth > 8)
		return;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE)
		return;
	do
	{
		wstring n = fd.cFileName;
		if(n == L"." || n == L"..")
			continue;
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			CountFiles(dir + L"\\" + n, files, bytes, depth + 1);
		else
		{
			files++;
			bytes += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
		}
	} while(FindNextFileW(h, &fd));
	FindClose(h);
}

static bool AnyMatch(const wstring &pattern)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE)
		return false;
	FindClose(h);
	return true;
}

static void CheckFiles()
{
	wstring b = L"Folder: " + g_app_dir + L"\n";
	wstring low = Lower(g_app_dir);
	bool in_temp = low.find(L"\\temp\\") != wstring::npos || low.find(L"\\tmp\\") != wstring::npos;
	bool in_archive = low.find(L".zip") != wstring::npos || low.find(L"rar$") != wstring::npos || low.find(L"7z") != wstring::npos;
	if(in_temp && in_archive)
		AddFinding(LV_FAIL, L"PS-WRAP is being run from inside the zip file (not extracted)",
			L"Right-click the zip > Extract All, then open PS-WRAP.exe from the extracted folder.");

	if(!FileExists(g_app_exe))
	{
		AddFinding(LV_FAIL, L"PS-WRAP.exe was not found next to PS-WRAP-Diagnostics.exe",
			L"Keep PS-WRAP-Diagnostics.exe in the PS-WRAP folder (the one with PS-WRAP.exe) and run it from there.");
		AddSection(L"Files", b + L"PS-WRAP.exe: missing\n");
		return;
	}

	WIN32_FILE_ATTRIBUTE_DATA fa;
	if(GetFileAttributesExW(g_app_exe.c_str(), GetFileExInfoStandard, &fa))
		b += L"PS-WRAP.exe: " + SizeText(((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow) + L", modified " + FileTimeText(fa.ftLastWriteTime) + L"\n";

	const wchar_t *needed[] = { L"qt.conf", L"Qt6Core.dll", L"Qt6Gui.dll", L"Qt6Qml.dll", L"Qt6Quick.dll", L"platforms\\qwindows.dll", L"vulkan-1.dll", L"SDL2.dll" };
	vector<wstring> missing;
	for(auto n : needed)
		if(!FileExists(g_app_dir + L"\\" + n))
			missing.push_back(n);
	if(!AnyMatch(g_app_dir + L"\\libplacebo-*.dll"))
		missing.push_back(L"libplacebo-*.dll");
	if(!AnyMatch(g_app_dir + L"\\avcodec-*.dll"))
		missing.push_back(L"avcodec-*.dll (FFmpeg)");
	if(!DirExists(g_app_dir + L"\\qml"))
		missing.push_back(L"qml\\ (folder)");

	size_t files = 0;
	ULONGLONG bytes = 0;
	CountFiles(g_app_dir, files, bytes);
	b += Fmt(L"Files in folder: %zu (%ls)\n", files, SizeText(bytes).c_str());

	if(!missing.empty())
	{
		wstring list;
		for(auto &m : missing)
			list += (list.empty() ? L"" : L", ") + m;
		b += L"Missing: " + list + L"\n";
		AddFinding(LV_FAIL, L"Files are missing from the PS-WRAP folder: " + list,
			L"Download the PS-WRAP zip again and extract ALL files into one folder (don't copy only PS-WRAP.exe). Check that your antivirus didn't remove files.");
	}
	else
		AddFinding(LV_OK, Fmt(L"PS-WRAP files are complete (%zu files)", files));
	AddSection(L"Files", b);
}

static void CheckSystem()
{
	wstring b;
	const wstring cv = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
	wstring product, display, build, edition;
	DWORD ubr = 0;
	RegStr(HKEY_LOCAL_MACHINE, cv, L"ProductName", product, KEY_WOW64_64KEY);
	RegStr(HKEY_LOCAL_MACHINE, cv, L"DisplayVersion", display, KEY_WOW64_64KEY);
	RegStr(HKEY_LOCAL_MACHINE, cv, L"CurrentBuild", build, KEY_WOW64_64KEY);
	RegDword(HKEY_LOCAL_MACHINE, cv, L"UBR", ubr, KEY_WOW64_64KEY);
	if(_wtoi(build.c_str()) >= 22000)
		product = IReplace(product, L"Windows 10", L"Windows 11", false);   // ProductName ยังเขียน 10 บน Windows 11
	wstring os = product + (display.empty() ? L"" : L" " + display) + L" (build " + build + Fmt(L".%lu)", ubr);
	b += L"Windows: " + os + L"\n";

	wstring cpu;
	RegStr(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", cpu, KEY_WOW64_64KEY);
	b += L"CPU: " + Trim(cpu) + L"\n";
	MEMORYSTATUSEX ms{ sizeof(ms) };
	GlobalMemoryStatusEx(&ms);
	b += Fmt(L"RAM: %.1f GB (%lu%% in use)\n", ms.ullTotalPhys / 1073741824.0, ms.dwMemoryLoad);

	DISPLAY_DEVICEW dd{ sizeof(dd) };
	for(DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); i++, dd = DISPLAY_DEVICEW{ sizeof(dd) })
	{
		if(!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
			continue;
		DEVMODEW dm{};
		dm.dmSize = sizeof(dm);
		wstring mode;
		if(EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm))
			mode = Fmt(L"%lux%lu @ %lu Hz", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
		b += Fmt(L"Display %lu: %ls on %ls%ls\n", i + 1, mode.c_str(), dd.DeviceString, (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? L" (main)" : L"");
	}
	{
		std::lock_guard<std::mutex> l(g_rep.mu);
		g_rep.header_windows = os;
	}
	AddSection(L"System", b);
}

// NVIDIA: DriverVersion 32.0.15.8129 → 581.29 (5 หลักท้ายของส่วนที่ 3+4)
static wstring NvidiaVersion(const wstring &dv)
{
	vector<wstring> parts;
	size_t s = 0, d;
	while((d = dv.find(L'.', s)) != wstring::npos)
	{
		parts.push_back(dv.substr(s, d - s));
		s = d + 1;
	}
	parts.push_back(dv.substr(s));
	if(parts.size() != 4)
		return {};
	wstring p4 = parts[3];
	while(p4.size() < 4)
		p4 = L"0" + p4;
	wstring digits = parts[2] + p4;
	if(digits.size() < 5)
		return {};
	digits = digits.substr(digits.size() - 5);
	return digits.substr(0, 3) + L"." + digits.substr(3);
}

static void CheckGpu()
{
	wstring b, header;
	const wstring cls = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
	HKEY h;
	int real_gpus = 0, basic = 0;
	if(RegOpenKeyExW(HKEY_LOCAL_MACHINE, cls.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &h) == ERROR_SUCCESS)
	{
		wchar_t name[256];
		for(DWORD i = 0;; i++)
		{
			DWORD len = 256;
			if(RegEnumKeyExW(h, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
				break;
			if(!iswdigit(name[0]))
				continue;
			wstring sub = cls + L"\\" + name, desc, provider, version, date;
			if(!RegStr(HKEY_LOCAL_MACHINE, sub, L"DriverDesc", desc, KEY_WOW64_64KEY))
				continue;
			RegStr(HKEY_LOCAL_MACHINE, sub, L"ProviderName", provider, KEY_WOW64_64KEY);
			RegStr(HKEY_LOCAL_MACHINE, sub, L"DriverVersion", version, KEY_WOW64_64KEY);
			RegStr(HKEY_LOCAL_MACHINE, sub, L"DriverDate", date, KEY_WOW64_64KEY);
			bool is_basic = IContains(desc, L"Basic Display") || IContains(desc, L"Basic Render");
			bool is_virtual = IContains(desc, L"Remote") || IContains(desc, L"Virtual") || IContains(desc, L"Parsec") || IContains(desc, L"IddSample");
			wstring friendly = version, short_version = version;
			if(IContains(provider, L"NVIDIA") || IContains(desc, L"NVIDIA"))
			{
				wstring nv = NvidiaVersion(version);
				if(!nv.empty())
				{
					friendly = nv + L" (" + version + L")";
					short_version = nv;
				}
			}
			b += desc + L" — driver " + friendly + L" by " + provider + L", dated " + date + (is_virtual ? L" [virtual]" : L"") + L"\n";
			if(is_basic)
			{
				basic++;
				continue;
			}
			if(is_virtual)
				continue;
			real_gpus++;
			if(header.empty())
				header = desc + L", driver " + short_version + L" (" + date + L")";
			// DriverDate = M-D-YYYY
			int mo = 0, dy = 0, yr = 0;
			if(swscanf(date.c_str(), L"%d-%d-%d", &mo, &dy, &yr) == 3 && yr > 2000)
			{
				SYSTEMTIME now;
				GetLocalTime(&now);
				int months = (now.wYear - yr) * 12 + (now.wMonth - mo);
				if(months > 18)
					AddFinding(LV_WARN, Fmt(L"Graphics driver for %ls is old (%d months)", desc.c_str(), months),
						L"Install the newest driver from NVIDIA / AMD / Intel.");
			}
		}
		RegCloseKey(h);
	}
	if(real_gpus == 0 && basic > 0)
		AddFinding(LV_FAIL, L"No graphics driver is installed (Windows is using \"Microsoft Basic Display Adapter\")",
			L"Install the driver for your graphics card from NVIDIA / AMD / Intel.");
	else if(real_gpus > 1)
		AddFinding(LV_INFO, Fmt(L"%d graphics cards found (laptop/hybrid). If the picture is black or slow, set PS-WRAP.exe to \"High performance\" in Windows Settings > System > Display > Graphics.", real_gpus));
	{
		std::lock_guard<std::mutex> l(g_rep.mu);
		g_rep.header_gpu = header.empty() ? L"(unknown)" : header;
	}
	AddSection(L"Graphics card", b.empty() ? L"(could not read graphics driver info)\n" : b);
}

static wstring LastLineStarting(const wstring &text, const wstring &prefix)
{
	wstring found;
	for(auto &l : SplitLines(text))
		if(IStarts(l, prefix))
			found = l;
	return found;
}

static void CheckVulkan()
{
	UiStatus(L"Testing Vulkan (up to 20 s)…");
	ChildResult r = RunCaptured(g_tool_path, L"--vkprobe", { { L"VK_LOADER_DEBUG", L"error,warn" } }, kVulkanTimeoutMs, g_app_dir);
	wstring out = StripAnsi(W(r.output));
	WriteText(g_work_dir + L"\\vulkan.txt", Redact(out));
	{
		// loader เตือน "Registry lookup failed" ทุกเครื่องที่ไม่มี layer บางประเภท — ไม่ใช่ปัญหา ไม่ต้องโชว์
		wstring kept;
		for(auto &l : SplitLines(out))
			if(!IContains(l, L"Registry lookup failed to get layer manifest files"))
				kept += l + L"\n";
		out = kept;
	}
	wstring b = out;
	if(!r.started)
	{
		AddFinding(LV_WARN, Fmt(L"Could not run the Vulkan test (error %lu)", r.error));
	}
	else if(r.timed_out)
	{
		wstring step = LastLineStarting(out, L"step: ");
		if(step.size() > 6)
			step = step.substr(6);
		{
			std::lock_guard<std::mutex> l(g_rep.mu);
			g_rep.vulkan_hang = true;
		}
		AddFinding(LV_FAIL, L"Vulkan freezes on this PC (stuck at " + step + L" for 20 s). This is what makes PS-WRAP hang at start (\"Not responding\").",
			L"Click \"Use OpenGL\" below, then open PS-WRAP again. To fix Vulkan itself: close overlay/recording apps listed under Vulkan layers, then clean-reinstall the graphics driver.");
		b += L"\n*** TIMED OUT after 20 s — last step above is where Vulkan froze ***\n";
	}
	else if(r.exit_code != 0)
	{
		wstring why = LastLineStarting(out, L"error: ");
		if(why.empty())
			why = LastLineStarting(out, L"result: ");
		AddFinding(LV_FAIL, L"Vulkan does not work on this PC (" + (why.empty() ? ExitCodeText(r.exit_code) : why) + L")",
			L"PS-WRAP should switch to OpenGL by itself. If it doesn't open, click \"Use OpenGL\". Update the graphics driver.");
	}
	else
	{
		wstring gpus;
		for(auto &l : SplitLines(out))
			if(IStarts(l, L"gpu: "))
			{
				wstring g = l.substr(5);
				size_t bar = g.find(L" | ");
				gpus += (gpus.empty() ? L"" : L", ") + (bar == wstring::npos ? g : g.substr(0, bar));
			}
		bool device_failed = false;
		for(auto &l : SplitLines(out))
			if(IStarts(l, L"result: vkCreateDevice = ") && l != L"result: vkCreateDevice = 0")
				device_failed = true;
		if(device_failed)
			AddFinding(LV_WARN, L"Vulkan starts but a graphics card refused to create a device (see Vulkan details)",
				L"Update the graphics driver. If PS-WRAP shows a black picture, click \"Use OpenGL\".");
		else
			AddFinding(LV_OK, L"Vulkan works (" + (gpus.empty() ? wstring(L"no GPU listed") : gpus) + L")");
	}
	AddSection(L"Vulkan test", b);
}

struct KnownTool
{
	const wchar_t *match;
	const wchar_t *label;
};

// overlay / ตัวอัด / ตัว hook ที่ทำให้แอป Vulkan ค้างหรือจอดำได้
static const KnownTool kHookers[] = {
	{ L"rtss", L"RivaTuner / MSI Afterburner overlay" },
	{ L"rivatuner", L"RivaTuner overlay" },
	{ L"overwolf", L"Overwolf" },
	{ L"bandicam", L"Bandicam" },
	{ L"reshade", L"ReShade" },
	{ L"eos_overlay", L"Epic Games overlay" },
	{ L"fraps", L"Fraps" },
	{ L"mirillis", L"Mirillis Action!" },
	{ L"nahimic", L"Nahimic" },
	{ L"fpsmon", L"FPS Monitor" },
	{ L"xsplit", L"XSplit" },
	{ L"medal", L"Medal" },
	{ L"outplayed", L"Outplayed" },
	{ L"vkbasalt", L"vkBasalt" },
	{ L"lsfg", L"Lossless Scaling" },
};

static void CheckLayers()
{
	wstring b;
	vector<wstring> suspects;
	struct Root
	{
		HKEY key;
		const wchar_t *name;
	} roots[] = { { HKEY_LOCAL_MACHINE, L"HKLM" }, { HKEY_CURRENT_USER, L"HKCU" } };
	for(auto &root : roots)
	{
		HKEY h;
		if(RegOpenKeyExW(root.key, L"SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers", 0, KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS)
			continue;
		wchar_t name[1024];
		for(DWORD i = 0;; i++)
		{
			DWORD len = 1024, type = 0, data = 1, dsize = sizeof(data);
			if(RegEnumValueW(h, i, name, &len, nullptr, &type, (BYTE *)&data, &dsize) != ERROR_SUCCESS)
				break;
			bool enabled = type == REG_DWORD && data == 0;
			wstring path = name;
			bool exists = FileExists(path);
			wstring layer_name;
			if(exists)
			{
				string json;
				if(ReadFileBytes(path, json, 1 << 20))
				{
					size_t lp = json.find("\"layer");
					size_t np = json.find("\"name\"", lp == string::npos ? 0 : lp);
					if(np != string::npos)
					{
						size_t q1 = json.find('"', json.find(':', np) + 1);
						size_t q2 = q1 == string::npos ? string::npos : json.find('"', q1 + 1);
						if(q2 != string::npos)
							layer_name = W(json.substr(q1 + 1, q2 - q1 - 1));
					}
				}
			}
			b += Fmt(L"%ls %ls %ls — %ls\n", root.name, enabled ? L"[on] " : L"[off]", layer_name.empty() ? L"(unknown)" : layer_name.c_str(), path.c_str());
			if(!enabled)
				continue;
			if(!exists)
			{
				AddFinding(LV_WARN, L"Broken Vulkan layer entry (its file is missing): " + path,
					L"Reinstall or uninstall the program it belongs to. Broken layers can stop Vulkan apps from starting.");
				continue;
			}
			wstring hay = Lower(layer_name + L" " + path);
			for(auto &k : kHookers)
				if(hay.find(k.match) != wstring::npos)
				{
					if(std::find(suspects.begin(), suspects.end(), k.label) == suspects.end())
						suspects.push_back(k.label);
					break;
				}
		}
		RegCloseKey(h);
	}
	for(auto v : { L"VK_INSTANCE_LAYERS", L"VK_LAYER_PATH", L"VK_ADD_LAYER_PATH", L"VK_LOADER_LAYERS_ENABLE" })
	{
		wstring val = Env(v);
		if(!val.empty())
			b += wstring(v) + L"=" + val + L"\n";
	}
	if(!suspects.empty())
	{
		wstring list;
		for(auto &s : suspects)
			list += (list.empty() ? L"" : L", ") + s;
		AddFinding(LV_WARN, L"Programs that hook into Vulkan games are installed: " + list,
			L"If PS-WRAP freezes or shows a black picture, close these programs (or turn off their overlay) and try again.");
	}
	AddSection(L"Vulkan layers (implicit)", b.empty() ? L"(none)\n" : b);
}

static HWND MainWindowOf(DWORD pid)
{
	struct Ctx
	{
		DWORD pid;
		HWND found;
	} ctx{ pid, nullptr };
	EnumWindows([](HWND h, LPARAM lp) -> BOOL {
		auto c = (Ctx *)lp;
		DWORD wp = 0;
		GetWindowThreadProcessId(h, &wp);
		if(wp == c->pid && IsWindowVisible(h) && !GetWindow(h, GW_OWNER))
		{
			c->found = h;
			return FALSE;
		}
		return TRUE;
	}, (LPARAM)&ctx);
	return ctx.found;
}

struct ProcInfo
{
	DWORD pid;
	DWORD parent;
	wstring exe;
};

static vector<ProcInfo> ListProcesses()
{
	vector<ProcInfo> r;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(snap == INVALID_HANDLE_VALUE)
		return r;
	PROCESSENTRY32W pe{ sizeof(pe) };
	if(Process32FirstW(snap, &pe))
		do
			r.push_back({ pe.th32ProcessID, pe.th32ParentProcessID, pe.szExeFile });
		while(Process32NextW(snap, &pe));
	CloseHandle(snap);
	return r;
}

static vector<DWORD> RunningPsWrap()
{
	vector<DWORD> r;
	for(auto &p : ListProcesses())
		if(_wcsicmp(p.exe.c_str(), L"PS-WRAP.exe") == 0)
			r.push_back(p.pid);
	return r;
}

static void CheckProcesses()
{
	static const KnownTool apps[] = {
		{ L"chiaki.exe", L"chiaki-ng" },
		{ L"rtss.exe", L"RivaTuner (RTSS)" },
		{ L"msiafterburner.exe", L"MSI Afterburner" },
		{ L"overwolf.exe", L"Overwolf" },
		{ L"obs64.exe", L"OBS Studio" },
		{ L"ds4windows.exe", L"DS4Windows" },
		{ L"dsx.exe", L"DSX (DualSenseX)" },
		{ L"steam.exe", L"Steam" },
		{ L"discord.exe", L"Discord" },
		{ L"nvidia overlay.exe", L"NVIDIA overlay" },
		{ L"bdcam.exe", L"Bandicam" },
		{ L"medal.exe", L"Medal" },
		{ L"lossless scaling.exe", L"Lossless Scaling" },
		{ L"x360ce.exe", L"x360ce" },
	};
	wstring b, seen;
	auto procs = ListProcesses();
	for(auto &a : apps)
		for(auto &p : procs)
			if(_wcsicmp(p.exe.c_str(), a.match) == 0)
			{
				seen += (seen.empty() ? L"" : L", ") + wstring(a.label);
				break;
			}
	b += L"Running programs of interest: " + (seen.empty() ? wstring(L"(none)") : seen) + L"\n";
	for(auto pid : RunningPsWrap())
	{
		HWND w = MainWindowOf(pid);
		bool hung = w && IsHungAppWindow(w);
		b += Fmt(L"PS-WRAP.exe running: pid %lu, window %ls\n", pid, !w ? L"none" : hung ? L"NOT RESPONDING" : L"ok");
		if(hung || !w)
			AddFinding(LV_WARN, Fmt(L"A PS-WRAP is already running in the background (pid %lu)%ls", pid, hung ? L" and is not responding" : L" without a window"),
				L"Close it in Task Manager. A stuck copy stops new PS-WRAP windows from opening. \"Test launch\" can close it for you.");
	}
	AddSection(L"Programs", b);
}

static bool SensitiveName(const wstring &name)
{
	wstring n = Lower(name);
	for(auto k : { L"psn", L"token", L"secret", L"password", L"passcode", L"regist", L"account", L"key", L"auth", L"cookie", L"rtmp", L"stream_url", L"mac", L"nickname", L"email",
	               L"youtube", L"twitch", L"kick", L"channel", L"url" })
		if(n.find(k) != wstring::npos)
			return true;
	return false;
}

static wstring DumpKey(const wstring &sub)
{
	HKEY h;
	if(RegOpenKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS)
		return L"  (not found)\n";
	wstring b;
	vector<wchar_t> name(16384);
	vector<BYTE> data(1 << 16);
	for(DWORD i = 0;; i++)
	{
		DWORD nlen = (DWORD)name.size(), dlen = (DWORD)data.size(), type = 0;
		LONG r = RegEnumValueW(h, i, name.data(), &nlen, nullptr, &type, data.data(), &dlen);
		if(r == ERROR_NO_MORE_ITEMS)
			break;
		if(r != ERROR_SUCCESS)
			continue;
		wstring n(name.data(), nlen);
		wstring v;
		if(SensitiveName(n))
			v = L"<hidden>";
		else if(type == REG_SZ || type == REG_EXPAND_SZ)
		{
			v.assign((const wchar_t *)data.data(), dlen / sizeof(wchar_t));
			while(!v.empty() && v.back() == 0)
				v.pop_back();
			if(v.size() > 160)
				v = Fmt(L"<long value, %zu chars>", v.size());
		}
		else if(type == REG_DWORD)
			v = Fmt(L"%lu", *(DWORD *)data.data());
		else if(type == REG_QWORD)
			v = Fmt(L"%llu", *(unsigned long long *)data.data());
		else
			v = Fmt(L"<binary %lu bytes>", dlen);
		b += L"  " + n + L" = " + v + L"\n";
	}
	RegCloseKey(h);
	return b;
}

static DWORD CountSubkeys(const wstring &sub)
{
	HKEY h;
	DWORD n = 0;
	if(RegOpenKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS)
	{
		RegQueryInfoKeyW(h, nullptr, nullptr, nullptr, &n, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		RegCloseKey(h);
	}
	return n;
}

// โปรไฟล์ที่แอปใช้จริง: settings/current_profile ว่าง = คีย์หลัก, ไม่ว่าง = PS-WRAP-<ชื่อ>
static wstring EffectiveSettingsKey()
{
	wstring profile;
	RegStr(HKEY_CURRENT_USER, wstring(kSettingsBase) + L"\\settings", L"current_profile", profile);
	if(profile.empty())
		return kSettingsBase;
	return L"Software\\PS-WRAP\\PS-WRAP-" + profile;
}

static wstring CurrentRenderer()
{
	wstring v;
	RegStr(HKEY_CURRENT_USER, EffectiveSettingsKey() + L"\\settings", L"render_backend", v);
	return v.empty() ? L"vulkan" : Lower(v);
}

static void CheckSettings()
{
	wstring b;
	wstring profile;
	RegStr(HKEY_CURRENT_USER, wstring(kSettingsBase) + L"\\settings", L"current_profile", profile);
	b += L"Profile in use: " + (profile.empty() ? wstring(L"(default)") : profile) + L"\n";
	b += L"[settings]\n" + DumpKey(wstring(kSettingsBase) + L"\\settings");
	b += L"[pswrap]\n" + DumpKey(wstring(kSettingsBase) + L"\\pswrap");
	if(!profile.empty())
		b += L"[profile " + profile + L" settings]\n" + DumpKey(EffectiveSettingsKey() + L"\\settings");
	b += Fmt(L"Registered consoles: %lu, manual hosts: %lu (details hidden)\n", CountSubkeys(wstring(kSettingsBase) + L"\\registered_hosts"),
		CountSubkeys(wstring(kSettingsBase) + L"\\manual_hosts"));
	wstring renderer = CurrentRenderer();
	{
		std::lock_guard<std::mutex> l(g_rep.mu);
		g_rep.header_renderer = renderer == L"opengl" ? L"OpenGL" : L"Vulkan";
	}
	HKEY hk;
	bool any = RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsBase, 0, KEY_READ, &hk) == ERROR_SUCCESS;
	if(any)
		RegCloseKey(hk);
	if(!any)
		AddFinding(LV_INFO, L"No PS-WRAP settings yet (PS-WRAP has never finished starting on this Windows account)");
	AddSection(L"Settings (passwords/tokens/keys hidden)", b);
}

static void CheckLogs()
{
	wstring dir = KnownFolder(FOLDERID_RoamingAppData) + L"\\PS-WRAP\\PS-WRAP\\log";
	wstring b = L"Log folder: " + dir + L"\n";
	struct LogFile
	{
		wstring name;
		ULONGLONG size, time;
		FILETIME ft;
	};
	vector<LogFile> logs;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*.log").c_str(), &fd);
	if(h != INVALID_HANDLE_VALUE)
	{
		do
			logs.push_back({ fd.cFileName, ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow, FileTimeU64(fd.ftLastWriteTime), fd.ftLastWriteTime });
		while(FindNextFileW(h, &fd));
		FindClose(h);
	}
	std::sort(logs.begin(), logs.end(), [](const LogFile &a, const LogFile &c) { return a.time > c.time; });
	if(logs.size() > 5)
		logs.resize(5);
	if(logs.empty())
	{
		AddFinding(LV_INFO, L"No stream logs yet (PS-WRAP writes a log only after a stream starts). Use \"Test launch\" to capture start-up output.");
		AddSection(L"Stream logs", b + L"(none)\n");
		return;
	}
	CreateDirectoryW((g_work_dir + L"\\logs").c_str(), nullptr);
	vector<wstring> points;
	size_t newest_errors = 0;
	for(size_t i = 0; i < logs.size(); i++)
	{
		auto &lf = logs[i];
		b += lf.name + L"  " + SizeText(lf.size) + L"  " + FileTimeText(lf.ft) + L"\n";
		string bytes;
		if(!ReadFileBytes(dir + L"\\" + lf.name, bytes, 16ULL << 20))
			continue;
		wstring text = StripAnsi(DecodeText(bytes));
		WriteText(g_work_dir + L"\\logs\\" + lf.name, Redact(text));
		if(i > 1)
			continue;
		vector<wstring> errs, warns;
		for(auto &l : SplitLines(text))
		{
			if(IStarts(l, L"[E]"))
				errs.push_back(l);
			else if(IStarts(l, L"[W]"))
				warns.push_back(l);
		}
		if(i == 0)
			newest_errors = errs.size();
		size_t from_e = errs.size() > 12 ? errs.size() - 12 : 0, from_w = warns.size() > 6 ? warns.size() - 6 : 0;
		for(size_t k = from_e; k < errs.size(); k++)
			points.push_back(lf.name + L": " + errs[k]);
		for(size_t k = from_w; k < warns.size(); k++)
			points.push_back(lf.name + L": " + warns[k]);
		b += Fmt(L"  errors: %zu, warnings: %zu\n", errs.size(), warns.size());
	}
	{
		std::lock_guard<std::mutex> l(g_rep.mu);
		for(auto &p : points)
			g_rep.error_points.push_back(Redact(p));
	}
	if(newest_errors)
		AddFinding(LV_WARN, Fmt(L"The newest stream log has %zu error line(s) — see ERROR POINTS", newest_errors));
	else
		AddFinding(LV_OK, L"The newest stream log has no error lines");
	AddSection(L"Stream logs", b);
}

static int g_wer_crash, g_wer_hang, g_wer_kernel;   // เขียน/อ่านใน thread ตรวจเท่านั้น (CheckWer → CheckEvents)
static wstring g_wer_module;

static void CheckWer()
{
	wstring b;
	vector<wstring> roots = {
		KnownFolder(FOLDERID_LocalAppData) + L"\\Microsoft\\Windows\\WER\\ReportArchive",
		KnownFolder(FOLDERID_LocalAppData) + L"\\Microsoft\\Windows\\WER\\ReportQueue",
		KnownFolder(FOLDERID_ProgramData) + L"\\Microsoft\\Windows\\WER\\ReportArchive",
		KnownFolder(FOLDERID_ProgramData) + L"\\Microsoft\\Windows\\WER\\ReportQueue",
	};
	struct Rep
	{
		wstring dir, name;
		ULONGLONG time;
		FILETIME ft;
	};
	vector<Rep> app, kernel;
	ULONGLONG cutoff = NowU64() - 30 * kDay100ns;
	for(auto &root : roots)
	{
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
		if(h == INVALID_HANDLE_VALUE)
			continue;
		do
		{
			wstring n = fd.cFileName;
			if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || n == L"." || n == L"..")
				continue;
			ULONGLONG t = FileTimeU64(fd.ftLastWriteTime);
			if(IStarts(n, L"AppCrash_PS-WRAP") || IStarts(n, L"AppHang_PS-WRAP") || IStarts(n, L"AppCrash_chiaki") || IStarts(n, L"AppHang_chiaki"))
				app.push_back({ root + L"\\" + n, n, t, fd.ftLastWriteTime });
			else if((IStarts(n, L"Kernel_141") || IStarts(n, L"Kernel_117") || IStarts(n, L"LiveKernelEvent_141") || IStarts(n, L"LiveKernelEvent_117")) && t >= cutoff)
				kernel.push_back({ root + L"\\" + n, n, t, fd.ftLastWriteTime });
		} while(FindNextFileW(h, &fd));
		FindClose(h);
	}
	std::sort(app.begin(), app.end(), [](const Rep &a, const Rep &c) { return a.time > c.time; });
	if(app.size() > 8)
		app.resize(8);
	int recent_crash = 0, recent_hang = 0;
	wstring newest_module;
	if(!app.empty())
		CreateDirectoryW((g_work_dir + L"\\wer").c_str(), nullptr);
	for(auto &r : app)
	{
		string bytes;
		wstring line = FileTimeText(r.ft) + L"  " + r.name.substr(0, r.name.find(L'_')) + L"  ";
		if(!ReadFileBytes(r.dir + L"\\Report.wer", bytes, 1 << 20))
			line += L"(details need admin rights — see Event Log below)";
		else
		{
			wstring text = DecodeText(bytes);
			WriteText(g_work_dir + L"\\wer\\" + r.name + L".txt", Redact(text));
			// Sig[n].Name / Sig[n].Value → "ชื่อ: ค่า"
			vector<wstring> names(16), values(16);
			for(auto &l : SplitLines(text))
			{
				int idx = -1;
				wchar_t kind[16] = {};
				if(swscanf(l.c_str(), L"Sig[%d].%15l[^=]", &idx, kind) == 2 && idx >= 0 && idx < 16)   // %l[ = wchar_t (mingw ใช้ C99: %[ เฉยๆ เป็น char)
				{
					wstring val = l.substr(l.find(L'=') + 1);
					if(wcscmp(kind, L"Name") == 0)
						names[idx] = val;
					else if(wcscmp(kind, L"Value") == 0)
						values[idx] = val;
				}
			}
			for(int k = 0; k < 16; k++)
				if(!names[k].empty() && (IContains(names[k], L"Module") || IContains(names[k], L"Exception") || IContains(names[k], L"Hang") || IContains(names[k], L"Version")))
				{
					line += names[k] + L"=" + values[k] + L"; ";
					if(newest_module.empty() && IContains(names[k], L"Fault Module Name"))
						newest_module = values[k];
				}
		}
		b += line + L"\n";
		if(r.time >= cutoff)
		{
			if(IStarts(r.name, L"AppHang"))
				recent_hang++;
			else
				recent_crash++;
		}
	}
	// นับรวมกับ Event Log ใน CheckEvents (เรื่องเดียวกันมักโผล่ทั้งสองที่ — ไม่ขึ้นซ้ำ)
	g_wer_crash = recent_crash;
	g_wer_hang = recent_hang;
	g_wer_module = newest_module;
	g_wer_kernel = (int)kernel.size();
	if(!kernel.empty())
		b += Fmt(L"Graphics driver reset reports (LiveKernelEvent 141/117) in 30 days: %zu\n", kernel.size());
	AddSection(L"Windows Error Reporting", b.empty() ? L"(no PS-WRAP reports)\n" : b);
}

struct EventRec
{
	wstring time, id, provider, data;
};

static wstring XmlAttr(const wstring &xml, const wstring &tag, const wstring &attr)
{
	size_t t = xml.find(L"<" + tag);
	if(t == wstring::npos)
		return {};
	size_t a = xml.find(attr + L"='", t);
	wchar_t q = L'\'';
	if(a == wstring::npos || a > xml.find(L'>', t))
	{
		a = xml.find(attr + L"=\"", t);
		q = L'"';
		if(a == wstring::npos || a > xml.find(L'>', t))
			return {};
	}
	a += attr.size() + 2;
	size_t e = xml.find(q, a);
	return xml.substr(a, e - a);
}

static wstring XmlUnescape(wstring s)
{
	s = IReplace(s, L"&lt;", L"<", false);
	s = IReplace(s, L"&gt;", L">", false);
	s = IReplace(s, L"&quot;", L"\"", false);
	s = IReplace(s, L"&apos;", L"'", false);
	s = IReplace(s, L"&amp;", L"&", false);
	return s;
}

static vector<EventRec> QueryEvents(const wchar_t *channel, const wstring &xpath, const vector<wstring> &must_contain, size_t max)
{
	vector<EventRec> out;
	EVT_HANDLE q = EvtQuery(nullptr, channel, xpath.c_str(), EvtQueryChannelPath | EvtQueryReverseDirection);
	if(!q)
		return out;
	EVT_HANDLE evs[16];
	DWORD got = 0;
	vector<wchar_t> buf(1 << 15);
	while(out.size() < max && EvtNext(q, 16, evs, 2000, 0, &got))
	{
		for(DWORD i = 0; i < got; i++)
		{
			DWORD used = 0, props = 0;
			if(!EvtRender(nullptr, evs[i], EvtRenderEventXml, (DWORD)(buf.size() * sizeof(wchar_t)), buf.data(), &used, &props))
			{
				if(GetLastError() == ERROR_INSUFFICIENT_BUFFER)
				{
					buf.resize(used / sizeof(wchar_t) + 16);
					if(!EvtRender(nullptr, evs[i], EvtRenderEventXml, (DWORD)(buf.size() * sizeof(wchar_t)), buf.data(), &used, &props))
						used = 0;
				}
				else
					used = 0;
			}
			EvtClose(evs[i]);
			if(!used || out.size() >= max)
				continue;
			wstring xml(buf.data());
			bool keep = must_contain.empty();
			for(auto &m : must_contain)
				if(IContains(xml, m))
					keep = true;
			if(!keep || IContains(xml, L"RADAR_PRE_LEAK"))   // RADAR = Windows ตรวจ memory leak ทั่วไป ไม่ใช่ crash
				continue;
			EventRec r;
			r.provider = XmlAttr(xml, L"Provider", L"Name");
			size_t idp = xml.find(L"<EventID");
			if(idp != wstring::npos)
			{
				size_t s = xml.find(L'>', idp) + 1;
				r.id = xml.substr(s, xml.find(L'<', s) - s);
			}
			wstring st = XmlAttr(xml, L"TimeCreated", L"SystemTime");
			SYSTEMTIME utc{};
			if(swscanf(st.c_str(), L"%hu-%hu-%huT%hu:%hu:%hu", &utc.wYear, &utc.wMonth, &utc.wDay, &utc.wHour, &utc.wMinute, &utc.wSecond) == 6)
			{
				SYSTEMTIME local;
				SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
				r.time = TimeText(local);
			}
			else
				r.time = st;
			size_t p = 0;
			while((p = xml.find(L"<Data", p)) != wstring::npos)
			{
				size_t s = xml.find(L'>', p);
				if(s == wstring::npos)
					break;
				if(xml[s - 1] == L'/')
				{
					p = s;
					continue;
				}
				size_t e = xml.find(L"</Data>", s);
				if(e == wstring::npos)
					break;
				wstring v = Trim(XmlUnescape(xml.substr(s + 1, e - s - 1)));
				if(IStarts(v, L"\\\\?\\"))   // รายการไฟล์แนบของ WER — ยาวและไม่ช่วยอะไร
					v.clear();
				std::replace(v.begin(), v.end(), L'\r', L' ');
				std::replace(v.begin(), v.end(), L'\n', L' ');
				if(!v.empty())
					r.data += (r.data.empty() ? L"" : L" | ") + v;
				p = e;
			}
			if(r.data.size() > 600)
				r.data = r.data.substr(0, 600) + L"…";
			out.push_back(r);
		}
	}
	EvtClose(q);
	return out;
}

static void CheckEvents()
{
	const wstring days30 = L"TimeCreated[timediff(@SystemTime) <= 2592000000]";
	auto app = QueryEvents(L"Application", L"*[System[(EventID=1000 or EventID=1001 or EventID=1002) and " + days30 + L"]]", { L"PS-WRAP.exe", L"chiaki.exe" }, 30);
	auto sys = QueryEvents(L"System", L"*[System[(EventID=4101 or Provider[@Name='nvlddmkm'] or Provider[@Name='amdkmdag'] or Provider[@Name='amdwddmg']) and " + days30 + L"]]", {}, 30);
	wstring b = L"Application events about PS-WRAP / chiaki (30 days):\n";
	int crashes = 0, hangs = 0;
	wstring crash_module;
	for(auto &e : app)
	{
		b += L"  " + e.time + L"  " + e.provider + L" " + e.id + L"  " + e.data + L"\n";
		if(e.id == L"1000")
		{
			crashes++;
			if(crash_module.empty())
			{
				// 1000: app | ver | ts | module | ...
				vector<wstring> f;
				size_t s = 0, d;
				while((d = e.data.find(L" | ", s)) != wstring::npos)
				{
					f.push_back(e.data.substr(s, d - s));
					s = d + 3;
				}
				f.push_back(e.data.substr(s));
				if(f.size() > 3)
					crash_module = f[3];
			}
		}
		else if(e.id == L"1002")
			hangs++;
	}
	if(app.empty())
		b += L"  (none)\n";
	b += L"\nGraphics driver events (30 days):\n";
	for(auto &e : sys)
		b += L"  " + e.time + L"  " + e.provider + L" " + e.id + L"  " + e.data + L"\n";
	if(sys.empty())
		b += L"  (none)\n";
	WriteText(g_work_dir + L"\\events.txt", Redact(b));
	crashes = std::max(crashes, g_wer_crash);
	hangs = std::max(hangs, g_wer_hang);
	if(crash_module.empty())
		crash_module = g_wer_module;
	if(crashes || hangs)
		AddFinding(LV_WARN, Fmt(L"PS-WRAP / chiaki-ng crashed %d time(s) and froze %d time(s) in the last 30 days%ls", crashes, hangs,
			crash_module.empty() ? L"" : (L" — last crash in " + crash_module).c_str()),
			L"If this keeps happening, use \"Test launch\" to catch it and send the report.");
	int tdr = 0;
	for(auto &e : sys)
		if(e.id == L"4101" || (e.provider == L"nvlddmkm" && (e.id == L"153" || e.id == L"13" || e.id == L"14")))
			tdr++;
	tdr = std::max(tdr, g_wer_kernel);
	if(tdr)
		AddFinding(LV_WARN, Fmt(L"The graphics driver reported %d error(s) or reset(s) in the last 30 days", tdr),
			L"Update or clean-reinstall the graphics driver, and turn off GPU overclocking. This can cause black screens or freezes.");
	AddSection(L"Event Log", b);
}

static void RunChecks()
{
	{
		std::lock_guard<std::mutex> l(g_rep.mu);
		g_rep.findings.clear();
		g_rep.sections.clear();
		g_rep.error_points.clear();
		g_rep.vulkan_hang = false;
	}
	struct Step
	{
		const wchar_t *label;
		void (*fn)();
	} steps[] = {
		{ L"files", CheckFiles }, { L"Windows", CheckSystem }, { L"graphics driver", CheckGpu }, { L"Vulkan", CheckVulkan },
		{ L"Vulkan layers", CheckLayers }, { L"running programs", CheckProcesses }, { L"settings", CheckSettings },
		{ L"logs", CheckLogs }, { L"error reports", CheckWer }, { L"Event Log", CheckEvents },
	};
	for(auto &s : steps)
	{
		UiStatus(wstring(L"Checking ") + s.label + L"…");
		UiText(wstring(L"Checking ") + s.label + L"…\n");
		s.fn();
	}
	{
		std::lock_guard<std::mutex> l(g_rep.mu);
		g_rep.checked = true;
	}
	PostMessageW(g_hwnd, WM_APP_DONE, JOB_CHECK, 0);
}

// ───────────────────────── hang capture (stack ของทุก thread + minidump) ─────────────────────────

static bool SystemModule(const wstring &m)
{
	wstring l = Lower(m);
	for(auto k : { L"ntdll", L"win32u", L"kernelbase", L"kernel32", L"user32", L"ucrtbase", L"msvcrt", L"gdi32", L"combase", L"?" })
		if(l.rfind(k, 0) == 0)
			return true;
	return false;
}

static wstring FrameName(HANDLE proc, DWORD64 pc, wstring *module_out)
{
	wstring mod = L"?";
	DWORD64 base = SymGetModuleBase64(proc, pc);
	IMAGEHLP_MODULEW64 mi{};
	mi.SizeOfStruct = sizeof(mi);
	if(SymGetModuleInfoW64(proc, pc, &mi))
		mod = FileName(mi.ImageName[0] ? mi.ImageName : mi.ModuleName);
	if(module_out)
		*module_out = mod;
	alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 512];
	auto si = (SYMBOL_INFO *)buf;
	si->SizeOfStruct = sizeof(SYMBOL_INFO);
	si->MaxNameLen = 511;
	DWORD64 disp = 0;
	if(SymFromAddr(proc, pc, &disp, si))
		return mod + L"!" + W(si->Name) + L"+0x" + Hex64(disp);
	return mod + L"+0x" + Hex64(base ? pc - base : pc);
}

static wstring WalkThread(HANDLE proc, DWORD tid, int max_frames, wstring *stuck_in)
{
	HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, tid);
	if(!th)
		return L"    (cannot open thread)\n";
	wstring out;
	if(SuspendThread(th) != (DWORD)-1)
	{
		CONTEXT ctx{};
		ctx.ContextFlags = CONTEXT_FULL;
		if(GetThreadContext(th, &ctx))
		{
			STACKFRAME64 sf{};
			sf.AddrPC.Offset = ctx.Rip;
			sf.AddrPC.Mode = AddrModeFlat;
			sf.AddrFrame.Offset = ctx.Rbp;
			sf.AddrFrame.Mode = AddrModeFlat;
			sf.AddrStack.Offset = ctx.Rsp;
			sf.AddrStack.Mode = AddrModeFlat;
			for(int i = 0; i < max_frames; i++)
			{
				if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, th, &sf, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
					break;
				if(!sf.AddrPC.Offset)
					break;
				wstring mod;
				wstring f = FrameName(proc, sf.AddrPC.Offset, &mod);
				out += Fmt(L"    #%02d %ls\n", i, f.c_str());
				if(stuck_in && stuck_in->empty() && !SystemModule(mod))
					*stuck_in = f;
			}
		}
		ResumeThread(th);
	}
	CloseHandle(th);
	return out.empty() ? L"    (no frames)\n" : out;
}

// คืนค่า "ค้างอยู่ใน" ของ UI thread · เขียน hang-stacks.txt + hang.dmp ลง work dir
static wstring CaptureHang(HANDLE proc, DWORD pid, DWORD ui_tid)
{
	wstring report = Fmt(L"PS-WRAP (pid %lu) not responding — captured %ls\n", pid, NowText().c_str());
	wstring stuck;
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
	bool sym = SymInitializeW(proc, g_app_dir.c_str(), TRUE);   // ใส่ path เองกัน _NT_SYMBOL_PATH ที่ชี้ symbol server (ช้า)
	vector<DWORD> tids;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if(snap != INVALID_HANDLE_VALUE)
	{
		THREADENTRY32 te{ sizeof(te) };
		if(Thread32First(snap, &te))
			do
				if(te.th32OwnerProcessID == pid)
					tids.push_back(te.th32ThreadID);
			while(Thread32Next(snap, &te));
		CloseHandle(snap);
	}
	if(!ui_tid && !tids.empty())
		ui_tid = tids.front();
	report += Fmt(L"\nUI thread %lu:\n", ui_tid);
	report += sym ? WalkThread(proc, ui_tid, 48, &stuck) : L"    (dbghelp unavailable)\n";
	int shown = 0;
	for(auto t : tids)
	{
		if(t == ui_tid || !sym)
			continue;
		if(++shown > 64)
			break;
		report += Fmt(L"\nThread %lu:\n", t) + WalkThread(proc, t, 16, nullptr);
	}
	if(sym)
		SymCleanup(proc);

	wstring dump = g_work_dir + L"\\hang.dmp";
	HANDLE f = CreateFileW(dump.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	if(f != INVALID_HANDLE_VALUE)
	{
		BOOL ok = MiniDumpWriteDump(proc, pid, f, (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules), nullptr, nullptr, nullptr);
		CloseHandle(f);
		report += ok ? L"\nSmall dump saved: hang.dmp (thread stacks + module list only)\n" : L"\nCould not write hang.dmp\n";
		if(!ok)
			DeleteFileW(dump.c_str());
	}
	report = L"Stuck in: " + (stuck.empty() ? wstring(L"(Windows system code — see UI thread)") : stuck) + L"\n" + report;
	WriteText(g_work_dir + L"\\hang-stacks.txt", RedactPaths(report));
	return RedactPaths(stuck);
}

// ───────────────────────── test launch ─────────────────────────

struct LaunchCapture
{
	std::mutex mu;
	std::deque<wstring> tail;   // บรรทัดท้ายๆ (ไว้ดูว่าก่อนค้าง/ก่อนปิดทำอะไรอยู่)
	vector<wstring> errors;
	size_t lines = 0;
	bool saw_fallback = false;
	HANDLE file = INVALID_HANDLE_VALUE;
	ULONGLONG t0 = 0;
	bool verbose = false;
	wstring ui_batch;
	ULONGLONG ui_last = 0;
};

static void FlushUiBatch(LaunchCapture &cap, bool force)
{
	ULONGLONG now = GetTickCount64();
	if(cap.ui_batch.empty() || (!force && now - cap.ui_last < 200))
		return;
	UiText(cap.ui_batch);
	cap.ui_batch.clear();
	cap.ui_last = now;
}

static void OnLaunchLine(LaunchCapture &cap, const wstring &raw)
{
	wstring line = Redact(StripAnsi(raw));
	double t = (GetTickCount64() - cap.t0) / 1000.0;
	wstring stamped = Fmt(L"+%7.3f  ", t) + line;
	std::lock_guard<std::mutex> l(cap.mu);
	cap.lines++;
	string u = U8(stamped + L"\r\n");
	DWORD w;
	if(cap.file != INVALID_HANDLE_VALUE)
		WriteFile(cap.file, u.data(), (DWORD)u.size(), &w, nullptr);
	// loader เตือนแบบนี้ทุกเครื่อง (ไม่มี layer บางชนิดใน registry) — เก็บในไฟล์แต่ไม่นับเป็น error และไม่โชว์
	if(IContains(line, L"Registry lookup failed to get layer manifest files"))
		return;
	cap.tail.push_back(stamped);
	if(cap.tail.size() > 40)
		cap.tail.pop_front();
	if(IStarts(line, L"[E]") || IContains(line, L"[Vulkan Loader] ERROR") || IContains(line, L"fatal") || IContains(line, L"failed"))
	{
		cap.errors.push_back(stamped);
		if(cap.errors.size() > 40)
			cap.errors.erase(cap.errors.begin());
	}
	if(IContains(line, L"falling back to OpenGL"))
		cap.saw_fallback = true;
	if(!(cap.verbose && IStarts(line, L"[V]")))   // verbose ล้นจอ → บนจอโชว์เฉพาะที่ไม่ใช่ [V] (ไฟล์เก็บครบ)
		cap.ui_batch += stamped + L"\n";
	FlushUiBatch(cap, false);
}

static void LaunchThread(bool safe, bool verbose)
{
	LaunchCapture cap;
	cap.verbose = verbose;
	wstring out_path = g_work_dir + (safe ? L"\\launch-output-opengl.txt" : L"\\launch-output.txt");
	cap.file = CreateFileW(out_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
	if(cap.file != INVALID_HANDLE_VALUE)
	{
		DWORD w;
		WriteFile(cap.file, "\xEF\xBB\xBF", 3, &w, nullptr);
	}

	vector<std::pair<wstring, wstring>> env = {
		{ L"PSWRAP_DIAG", L"1" },
		{ L"QT_FORCE_STDERR_LOGGING", L"1" },
		{ L"QT_LOGGING_RULES", L"qt.rhi.general=true;qt.scenegraph.general=true" },
		{ L"VK_LOADER_DEBUG", L"error,warn" },
	};
	if(verbose)
		env.push_back({ L"PSWRAP_DIAG_VERBOSE", L"1" });
	if(safe)
	{
		env.push_back({ L"CHIAKI_FORCE_VULKAN_FALLBACK", L"1" });
		env.push_back({ L"CHIAKI_FORCE_VULKAN_FALLBACK_REASON", L"Safe mode from PS-WRAP-Diagnostics" });
	}

	UiText(Fmt(L"\n──── Test launch%ls%ls — %ls ────\n", safe ? L" (OpenGL safe mode)" : L"", verbose ? L" (verbose)" : L"", NowText().c_str()));
	UiText(L"Use PS-WRAP as usual and do what causes the problem. Close PS-WRAP when done — this window keeps recording until then.\n");
	cap.t0 = GetTickCount64();
	Spawned sp = SpawnCaptured(g_app_exe, g_launch_args, env, g_app_dir);
	if(!sp.process)
	{
		std::lock_guard<std::mutex> l(g_rep.mu);
		g_rep.launch_findings.push_back({ LV_FAIL, Fmt(L"PS-WRAP.exe could not be started (Windows error %lu)", sp.error),
			L"Check that the file exists and isn't blocked by antivirus." });
		if(cap.file != INVALID_HANDLE_VALUE)
			CloseHandle(cap.file);
		PostMessageW(g_hwnd, WM_APP_DONE, JOB_LAUNCH, 0);
		return;
	}
	{
		std::lock_guard<std::mutex> l(g_proc_mu);
		g_run_proc = sp.process;
	}

	std::thread reader([&] {
		char buf[8192];
		DWORD got;
		string pending;
		while(ReadFile(sp.read, buf, sizeof(buf), &got, nullptr) && got)
		{
			pending.append(buf, got);
			size_t nl;
			while((nl = pending.find('\n')) != string::npos)
			{
				string line = pending.substr(0, nl);
				if(!line.empty() && line.back() == '\r')
					line.pop_back();
				pending.erase(0, nl + 1);
				OnLaunchLine(cap, W(line));
			}
		}
		if(!pending.empty())
			OnLaunchLine(cap, W(pending));
		std::lock_guard<std::mutex> l(cap.mu);
		FlushUiBatch(cap, true);
	});

	HANDLE proc = sp.process;
	DWORD pid = sp.pid;
	double first_window = -1, hang_at = -1;
	int hung_seconds = 0;
	bool captured = false, restarted = false;
	wstring stuck_in;
	DWORD exit_code = 0;
	ULONGLONG last_tick = GetTickCount64();
	for(;;)
	{
		if(WaitForSingleObject(proc, 500) == WAIT_OBJECT_0)
		{
			GetExitCodeProcess(proc, &exit_code);
			// แอป relaunch ตัวเองด้วย OpenGL (Vulkan พัง) → ตามไปดูตัวใหม่
			DWORD child = 0;
			for(int i = 0; i < 10 && !child; i++)
			{
				for(auto &p : ListProcesses())
					if(p.parent == pid && _wcsicmp(p.exe.c_str(), L"PS-WRAP.exe") == 0)
						child = p.pid;
				if(!child)
					Sleep(300);
			}
			HANDLE ch = child ? OpenProcess(PROCESS_ALL_ACCESS, FALSE, child) : nullptr;
			if(!ch)
				break;
			UiText(Fmt(L"PS-WRAP restarted itself (new pid %lu) — following it.\n", child));
			restarted = true;
			{
				std::lock_guard<std::mutex> l(g_proc_mu);
				g_run_proc = ch;
			}
			if(proc != sp.process)
				CloseHandle(proc);
			proc = ch;
			pid = child;
			hung_seconds = 0;
			continue;
		}
		ULONGLONG now = GetTickCount64();
		bool second = now - last_tick >= 1000;
		if(!second)
			continue;
		last_tick = now;
		double t = (now - cap.t0) / 1000.0;
		HWND w = MainWindowOf(pid);
		if(w && first_window < 0)
		{
			first_window = t;
			UiText(Fmt(L"+%7.3f  [Diagnostics] PS-WRAP window appeared\n", t));
		}
		bool hung = w ? IsHungAppWindow(w) != FALSE : (first_window < 0 && t >= kNoWindowSecondsBeforeCapture);
		hung_seconds = hung ? hung_seconds + 1 : 0;
		if(!captured && (hung_seconds >= kHungSecondsBeforeCapture || (!w && first_window < 0 && t >= kNoWindowSecondsBeforeCapture)))
		{
			captured = true;
			hang_at = t;
			UiText(Fmt(L"+%7.3f  [Diagnostics] PS-WRAP is NOT RESPONDING — saving where it is stuck…\n", t));
			DWORD ui_tid = w ? GetWindowThreadProcessId(w, nullptr) : 0;
			stuck_in = CaptureHang(proc, pid, ui_tid);
			UiText(L"Stuck in: " + (stuck_in.empty() ? wstring(L"(Windows system code)") : stuck_in) + L"\n");
			PostMessageW(g_hwnd, WM_APP_HANG, 0, (LPARAM) new wstring(stuck_in));
		}
	}
	double total = (GetTickCount64() - cap.t0) / 1000.0;

	// รอ output ที่ค้างใน pipe สั้นๆ แล้วตัด (ตัวที่ relaunch อาจถือ pipe ไว้)
	Sleep(500);
	CancelIoEx(sp.read, nullptr);
	reader.join();
	{
		std::lock_guard<std::mutex> l(g_proc_mu);
		g_run_proc = nullptr;
	}
	if(proc != sp.process)
		CloseHandle(proc);
	CloseHandle(sp.read);
	CloseHandle(sp.thread);
	CloseHandle(sp.process);

	vector<Finding> found;
	const wstring &stuck_red = stuck_in;   // CaptureHang ซ่อน path ให้แล้ว (ไม่ผ่าน Redact เต็ม เพราะชื่อฟังก์ชันจะโดนมองเป็น token)
	wstring stuck_lower = Lower(stuck_in);
	bool gpu_stuck = false;
	for(auto k : { L"vulkan", L"nvoglv", L"nvwgf", L"amdvlk", L"atiglpxx", L"atio6", L"igvk", L"ig9icd", L"ig11icd", L"ig12icd", L"libplacebo", L"nvgpucomp", L"amdxc" })
		if(stuck_lower.find(k) != wstring::npos)
			gpu_stuck = true;
	if(captured)
		found.push_back({ LV_FAIL, Fmt(L"PS-WRAP froze during the test (%.0f s after start)%ls", hang_at,
			first_window < 0 ? L", before its window appeared" : L"") + L". Stuck in: " + (stuck_red.empty() ? wstring(L"Windows system code") : stuck_red),
			gpu_stuck || first_window < 0 ? L"This points at the graphics driver / Vulkan. Click \"Use OpenGL\", then open PS-WRAP again. Update or clean-reinstall the graphics driver."
			                              : L"Send the report zip — hang-stacks.txt shows exactly where it stopped." });
	else if(exit_code != 0 && exit_code != 0xDEAD)
		found.push_back({ LV_FAIL, Fmt(L"PS-WRAP closed with an error after %.0f s: ", total) + ExitCodeText(exit_code),
			exit_code == 0xC0000135 || exit_code == 0xC0000139 || exit_code == 0xC000007B
				? L"Download the zip again and extract everything into a new, empty folder."
				: L"Send the report zip so we can see the last lines before it closed." });
	else
		found.push_back({ LV_OK, Fmt(L"PS-WRAP opened%ls and closed normally after %.0f s", first_window >= 0 ? Fmt(L" (window after %.1f s)", first_window).c_str() : L"", total) });
	if(cap.saw_fallback)
		found.push_back({ safe ? LV_INFO : LV_WARN, safe ? L"Started with OpenGL (safe mode) as requested" : L"Vulkan failed during start — PS-WRAP switched to OpenGL by itself",
			safe ? L"" : L"Update the graphics driver. You can keep OpenGL with \"Use OpenGL\"." });
	if(restarted)
		found.push_back({ LV_INFO, L"PS-WRAP restarted itself during the test (it does this after a graphics failure)" });

	wstring sec = Fmt(L"Mode: %ls%ls\nDuration: %.1f s, output lines: %zu\nWindow appeared: %ls\nExit code: %ls\n", safe ? L"OpenGL safe mode" : L"normal",
		verbose ? L", verbose" : L"", total, cap.lines, first_window >= 0 ? Fmt(L"after %.1f s", first_window).c_str() : L"never", ExitCodeText(exit_code).c_str());
	if(captured)
		sec += L"Froze: yes, see hang-stacks.txt" + wstring(stuck_red.empty() ? L"" : L" (stuck in " + stuck_red + L")") + L"\n";
	sec += L"Last lines before the end:\n";
	{
		std::lock_guard<std::mutex> l(cap.mu);
		size_t from = cap.tail.size() > 25 ? cap.tail.size() - 25 : 0;
		for(size_t i = from; i < cap.tail.size(); i++)
			sec += L"  " + cap.tail[i] + L"\n";
		std::lock_guard<std::mutex> l2(g_rep.mu);
		g_rep.launch_errors.assign(cap.errors.begin(), cap.errors.end());
	}
	if(cap.file != INVALID_HANDLE_VALUE)
		CloseHandle(cap.file);
	{
		std::lock_guard<std::mutex> l(g_rep.mu);
		g_rep.launch_findings = found;
		g_rep.launch_section = sec;
	}
	PostMessageW(g_hwnd, WM_APP_DONE, JOB_LAUNCH, 0);
}

// ───────────────────────── report text / zip / clipboard ─────────────────────────

static wstring BuildReport(bool full)
{
	std::lock_guard<std::mutex> l(g_rep.mu);
	wstring r = L"PS-WRAP Diagnostics (PS-WRAP-Diagnostics " + W(PSWRAP_VERSION) + L") — " + NowText() + L"\n";
	r += L"Windows: " + g_rep.header_windows + L"\n";
	r += L"GPU: " + g_rep.header_gpu + L"\n";
	r += L"Renderer: " + (g_rep.header_renderer.empty() ? wstring(L"?") : g_rep.header_renderer) + L"\n";
	if(full)
		r += L"PS-WRAP folder: " + Redact(g_app_dir) + L"\n";
	r += L"\nSUMMARY\n";
	vector<Finding> all = g_rep.launch_findings;
	all.insert(all.end(), g_rep.findings.begin(), g_rep.findings.end());
	std::stable_sort(all.begin(), all.end(), [](const Finding &a, const Finding &b) { return a.level > b.level; });
	for(auto &f : all)
	{
		if(!full && (f.level == LV_OK || f.level == LV_INFO))
			continue;
		r += L"  " + wstring(LevelTag(f.level)) + L" " + f.title + L"\n";
		if(full && !f.fix.empty())
			r += L"         → " + f.fix + L"\n";
	}
	if(!full && std::none_of(all.begin(), all.end(), [](const Finding &f) { return f.level >= LV_WARN; }))
		r += L"  no problems found by the checks\n";
	vector<wstring> points = g_rep.launch_errors;
	points.insert(points.end(), g_rep.error_points.begin(), g_rep.error_points.end());
	if(!points.empty())
	{
		r += L"\nERROR POINTS\n";
		size_t limit = full ? points.size() : 8;
		for(size_t i = 0; i < points.size() && i < limit; i++)
			r += L"  " + points[i] + L"\n";
	}
	if(full)
	{
		if(!g_rep.launch_section.empty())
			r += L"\nTEST LAUNCH\n" + g_rep.launch_section;
		r += L"\nDETAILS\n" + g_rep.sections;
	}
	return r;
}

static void PutU16(string &o, uint16_t v)
{
	o += (char)(v & 0xff);
	o += (char)(v >> 8);
}

static void PutU32(string &o, uint32_t v)
{
	for(int i = 0; i < 4; i++)
		o += (char)((v >> (8 * i)) & 0xff);
}

static uint32_t Crc32(const string &d)
{
	static uint32_t table[256];
	static bool init = false;
	if(!init)
	{
		for(uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for(int k = 0; k < 8; k++)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		init = true;
	}
	uint32_t c = 0xFFFFFFFFu;
	for(unsigned char b : d)
		c = table[(c ^ b) & 0xff] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

static void ListFiles(const wstring &dir, const wstring &rel, vector<std::pair<wstring, wstring>> &out)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE)
		return;
	do
	{
		wstring n = fd.cFileName;
		if(n == L"." || n == L"..")
			continue;
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			ListFiles(dir + L"\\" + n, rel + n + L"/", out);
		else
			out.push_back({ dir + L"\\" + n, rel + n });
	} while(FindNextFileW(h, &fd));
	FindClose(h);
}

// zip แบบ store (ไม่บีบอัด) — ไม่ต้องพึ่ง tar/PowerShell
static bool ZipFolder(const wstring &dir, const wstring &zip_path, const wstring &root_name)
{
	vector<std::pair<wstring, wstring>> files;
	ListFiles(dir, root_name + L"/", files);
	HANDLE f = CreateFileW(zip_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	if(f == INVALID_HANDLE_VALUE)
		return false;
	string central;
	uint32_t offset = 0;
	uint16_t count = 0;
	SYSTEMTIME st;
	GetLocalTime(&st);
	uint16_t dos_time = (uint16_t)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
	uint16_t dos_date = (uint16_t)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
	bool ok = true;
	DWORD w;
	for(auto &file : files)
	{
		string data;
		if(!ReadFileBytes(file.first, data) || data.size() > 0x7FFFFFFF)
			continue;
		string name = U8(file.second);
		uint32_t crc = Crc32(data), size = (uint32_t)data.size();
		string local;
		PutU32(local, 0x04034b50);
		PutU16(local, 20);
		PutU16(local, 0x0800);   // ชื่อไฟล์เป็น UTF-8
		PutU16(local, 0);
		PutU16(local, dos_time);
		PutU16(local, dos_date);
		PutU32(local, crc);
		PutU32(local, size);
		PutU32(local, size);
		PutU16(local, (uint16_t)name.size());
		PutU16(local, 0);
		local += name;
		ok = ok && WriteFile(f, local.data(), (DWORD)local.size(), &w, nullptr) && WriteFile(f, data.data(), (DWORD)data.size(), &w, nullptr);
		PutU32(central, 0x02014b50);
		PutU16(central, 20);
		PutU16(central, 20);
		PutU16(central, 0x0800);
		PutU16(central, 0);
		PutU16(central, dos_time);
		PutU16(central, dos_date);
		PutU32(central, crc);
		PutU32(central, size);
		PutU32(central, size);
		PutU16(central, (uint16_t)name.size());
		PutU16(central, 0);
		PutU16(central, 0);
		PutU16(central, 0);
		PutU16(central, 0);
		PutU32(central, 0);
		PutU32(central, offset);
		central += name;
		offset += (uint32_t)(local.size() + data.size());
		count++;
	}
	string end;
	PutU32(end, 0x06054b50);
	PutU16(end, 0);
	PutU16(end, 0);
	PutU16(end, count);
	PutU16(end, count);
	PutU32(end, (uint32_t)central.size());
	PutU32(end, offset);
	PutU16(end, 0);
	ok = ok && WriteFile(f, central.data(), (DWORD)central.size(), &w, nullptr) && WriteFile(f, end.data(), (DWORD)end.size(), &w, nullptr);
	CloseHandle(f);
	return ok;
}

static void ShowInExplorer(const wstring &path)
{
	PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str());
	if(pidl)
	{
		SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
		ILFree(pidl);
	}
}

static wstring SaveZip()
{
	WriteText(g_work_dir + L"\\report.txt", BuildReport(true));
	wstring stamp = NowStamp();
	wstring name = L"PS-WRAP-report-" + stamp + L".zip";
	for(auto dir : { KnownFolder(FOLDERID_Desktop), KnownFolder(FOLDERID_Documents), g_app_dir })
	{
		if(dir.empty())
			continue;
		wstring path = dir + L"\\" + name;
		if(ZipFolder(g_work_dir, path, L"PS-WRAP-report-" + stamp))
		{
			std::lock_guard<std::mutex> l(g_rep.mu);
			g_rep.last_zip = path;
			return path;
		}
	}
	return {};
}

static bool CopyToClipboard(const wstring &text)
{
	wstring t = Crlf(text);
	if(!OpenClipboard(g_hwnd))
		return false;
	EmptyClipboard();
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
	bool ok = false;
	if(mem)
	{
		memcpy(GlobalLock(mem), t.c_str(), (t.size() + 1) * sizeof(wchar_t));
		GlobalUnlock(mem);
		ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
		if(!ok)
			GlobalFree(mem);
	}
	CloseClipboard();
	return ok;
}

// Discord ส่งได้ 2000 ตัวอักษร → ตัดให้พอดีพร้อม code block
static wstring ShortSummary(size_t limit)
{
	wstring s = BuildReport(false);
	if(s.size() > limit)
		s = s.substr(0, limit) + L"\n…";
	return s;
}

static string UrlEncode(const string &s)
{
	static const char *hex = "0123456789ABCDEF";
	string o;
	for(unsigned char c : s)
	{
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			o += (char)c;
		else
		{
			o += '%';
			o += hex[c >> 4];
			o += hex[c & 15];
		}
	}
	return o;
}

// ───────────────────────── window ─────────────────────────

static int Px(int v)
{
	return MulDiv(v, (int)g_dpi, 96);
}

static void MakeFonts()
{
	if(g_font)
		DeleteObject(g_font);
	if(g_font_mono)
		DeleteObject(g_font_mono);
	g_font = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
	g_font_mono = CreateFontW(-Px(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
	for(HWND h : { g_intro, g_btn_check, g_btn_test, g_btn_safe, g_chk_verbose, g_btn_save, g_btn_copy, g_btn_github, g_btn_renderer, g_status })
		if(h)
			SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
	if(g_edit)
		SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font_mono, TRUE);
}

static void Layout()
{
	RECT rc;
	GetClientRect(g_hwnd, &rc);
	int m = Px(12), gap = Px(8), bh = Px(34), cw = rc.right - 2 * m;
	int intro_h = Px(40);
	MoveWindow(g_intro, m, m, cw, intro_h, TRUE);
	int y = m + intro_h + gap;
	HWND row1[] = { g_btn_check, g_btn_test, g_btn_safe, g_chk_verbose };
	HWND row2[] = { g_btn_save, g_btn_copy, g_btn_github, g_btn_renderer };
	int bw = (cw - 3 * gap) / 4;
	for(int i = 0; i < 4; i++)
		MoveWindow(row1[i], m + i * (bw + gap), y, bw, bh, TRUE);
	y += bh + gap;
	for(int i = 0; i < 4; i++)
		MoveWindow(row2[i], m + i * (bw + gap), y, bw, bh, TRUE);
	y += bh + gap;
	int status_h = Px(22);
	MoveWindow(g_edit, m, y, cw, rc.bottom - y - m - status_h - gap / 2, TRUE);
	MoveWindow(g_status, m, rc.bottom - m - status_h, cw, status_h, TRUE);
}

static void SetEditText(const wstring &t)
{
	SetWindowTextW(g_edit, Crlf(t).c_str());
}

static void AppendEdit(const wstring &t)
{
	int len = GetWindowTextLengthW(g_edit);
	if(len > 1500000)   // ไม่ให้ช่องข้อความโตไม่จำกัดตอน verbose (ไฟล์เก็บครบอยู่แล้ว)
	{
		SendMessageW(g_edit, EM_SETSEL, 0, len / 2);
		SendMessageW(g_edit, EM_REPLACESEL, FALSE, (LPARAM)L"[…older lines are in the report files…]\r\n");
		len = GetWindowTextLengthW(g_edit);
	}
	SendMessageW(g_edit, EM_SETSEL, len, len);
	SendMessageW(g_edit, EM_REPLACESEL, FALSE, (LPARAM)Crlf(t).c_str());
}

static void UpdateButtons()
{
	bool check = g_busy_check, launch = g_busy_launch;
	EnableWindow(g_btn_check, !check && !launch);
	EnableWindow(g_btn_test, !check);
	SetWindowTextW(g_btn_test, launch ? L"Stop test (close PS-WRAP)" : L"Test launch");
	EnableWindow(g_btn_safe, !check && !launch);
	EnableWindow(g_chk_verbose, !launch);
	EnableWindow(g_btn_save, !check && !launch);
	EnableWindow(g_btn_copy, !check && !launch);
	EnableWindow(g_btn_github, !check && !launch);
	EnableWindow(g_btn_renderer, !check && !launch);
	SetWindowTextW(g_btn_renderer, CurrentRenderer() == L"opengl" ? L"Use Vulkan again" : L"Use OpenGL (fix freeze)");
}

static void StartChecks()
{
	if(g_busy_check.exchange(true))
		return;
	UpdateButtons();
	SetEditText(L"Checking this PC…\n");
	std::thread(RunChecks).detach();
}

static bool CloseRunningPsWrap(const wchar_t *why)
{
	auto pids = RunningPsWrap();
	if(pids.empty())
		return true;
	wstring msg = Fmt(L"PS-WRAP is already running (%zu process%ls). %ls\n\nClose it now?", pids.size(), pids.size() > 1 ? L"es" : L"", why);
	if(MessageBoxW(g_hwnd, msg.c_str(), L"PS-WRAP Diagnostics", MB_YESNO | MB_ICONQUESTION) != IDYES)
		return false;
	for(auto pid : pids)
	{
		HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
		if(h)
		{
			TerminateProcess(h, 0xDEAD);
			WaitForSingleObject(h, 5000);
			CloseHandle(h);
		}
	}
	return true;
}

static void StartLaunch(bool safe)
{
	if(g_busy_launch)
	{
		if(MessageBoxW(g_hwnd, L"Close PS-WRAP and finish the test?", L"PS-WRAP Diagnostics", MB_YESNO | MB_ICONQUESTION) == IDYES)
		{
			std::lock_guard<std::mutex> l(g_proc_mu);
			if(g_run_proc)
				TerminateProcess(g_run_proc, 0xDEAD);
		}
		return;
	}
	if(!FileExists(g_app_exe))
	{
		MessageBoxW(g_hwnd, L"PS-WRAP.exe is not in this folder.\nPut PS-WRAP-Diagnostics.exe next to PS-WRAP.exe.", L"PS-WRAP Diagnostics", MB_ICONWARNING);
		return;
	}
	if(!CloseRunningPsWrap(L"It has to be closed for the test."))
		return;
	bool verbose = SendMessageW(g_chk_verbose, BM_GETCHECK, 0, 0) == BST_CHECKED;
	g_busy_launch = true;
	UpdateButtons();
	UiStatus(safe ? L"Test running in OpenGL safe mode — close PS-WRAP when you're done." : L"Test running — close PS-WRAP when you're done.");
	std::thread(LaunchThread, safe, verbose).detach();
}

static void ToggleRenderer()
{
	wstring cur = CurrentRenderer();
	wstring next = cur == L"opengl" ? L"vulkan" : L"opengl";
	wstring msg = next == L"opengl"
		? L"Make PS-WRAP always start with OpenGL instead of Vulkan?\n\nThis fixes PS-WRAP freezing at start on some PCs. Picture features that need Vulkan (Frame Gen, HDR) won't be available.\nYou can switch back here or in Settings > Video > Renderer Backend."
		: L"Switch PS-WRAP back to Vulkan (the default)?";
	if(MessageBoxW(g_hwnd, msg.c_str(), L"PS-WRAP Diagnostics", MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	if(!CloseRunningPsWrap(L"It must be closed first, or it would overwrite this setting when it exits."))
		return;
	HKEY h;
	wstring key = EffectiveSettingsKey() + L"\\settings";
	bool ok = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) == ERROR_SUCCESS;
	if(ok)
	{
		ok = RegSetValueExW(h, L"render_backend", 0, REG_SZ, (const BYTE *)next.c_str(), (DWORD)((next.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
		RegCloseKey(h);
	}
	if(ok)
	{
		{
			std::lock_guard<std::mutex> l(g_rep.mu);
			g_rep.header_renderer = next == L"opengl" ? L"OpenGL (changed by PS-WRAP-Diagnostics)" : L"Vulkan (changed by PS-WRAP-Diagnostics)";
		}
		UiStatus(next == L"opengl" ? L"Done — PS-WRAP will start with OpenGL. Open PS-WRAP normally now." : L"Done — PS-WRAP will use Vulkan again.");
		AppendEdit(next == L"opengl" ? L"\n[Diagnostics] Renderer set to OpenGL.\n" : L"\n[Diagnostics] Renderer set back to Vulkan.\n");
	}
	else
		MessageBoxW(g_hwnd, L"Could not change the setting.", L"PS-WRAP Diagnostics", MB_ICONWARNING);
	UpdateButtons();
}

static void OnCommand(int id)
{
	switch(id)
	{
		case IDC_CHECK:
			StartChecks();
			break;
		case IDC_TEST:
			StartLaunch(false);
			break;
		case IDC_SAFE:
			StartLaunch(true);
			break;
		case IDC_SAVE:
		{
			wstring zip = SaveZip();
			if(zip.empty())
				MessageBoxW(g_hwnd, L"Could not save the report.", L"PS-WRAP Diagnostics", MB_ICONWARNING);
			else
			{
				UiStatus(L"Saved: " + zip + L" — send this file (GitHub issue or Discord).");
				ShowInExplorer(zip);
			}
			break;
		}
		case IDC_COPY:
			if(CopyToClipboard(L"```\n" + ShortSummary(1880) + L"```"))
				UiStatus(L"Summary copied — paste it in Discord or a GitHub issue (Ctrl+V).");
			break;
		case IDC_GITHUB:
		{
			wstring zip = SaveZip();
			wstring first;
			{
				std::lock_guard<std::mutex> l(g_rep.mu);
				vector<Finding> all = g_rep.launch_findings;
				all.insert(all.end(), g_rep.findings.begin(), g_rep.findings.end());
				for(auto lv : { LV_FAIL, LV_WARN })
					for(auto &f : all)
						if(first.empty() && f.level == lv)
							first = f.title;
			}
			if(first.size() > 90)
				first = first.substr(0, 90) + L"…";
			wstring body = L"**What happened?**\n(describe the problem here)\n\n**Diagnostics**\n```\n" + ShortSummary(1500) + L"```\n\n"
				L"Please drag **" + FileName(zip) + L"** (on your Desktop) into this box to attach the full report.\n";
			string url = U8(kIssuesUrl) + "?title=" + UrlEncode(U8(first.empty() ? wstring(L"Problem report") : first)) + "&body=" + UrlEncode(U8(body));
			ShellExecuteW(g_hwnd, L"open", W(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			if(!zip.empty())
				ShowInExplorer(zip);
			UiStatus(L"Opened GitHub. Attach the zip from your Desktop to the issue.");
			break;
		}
		case IDC_RENDERER:
			ToggleRenderer();
			break;
	}
}

static HWND MakeButton(HWND parent, const wchar_t *text, int id, DWORD extra = BS_PUSHBUTTON)
{
	return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | extra, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}

static UINT WindowDpi(HWND h)
{
	typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
	static auto fn = (GetDpiForWindowFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
	if(fn)
		return fn(h);
	HDC dc = GetDC(h);
	UINT d = (UINT)GetDeviceCaps(dc, LOGPIXELSY);
	ReleaseDC(h, dc);
	return d;
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	switch(msg)
	{
		case WM_CREATE:
		{
			g_hwnd = h;
			g_dpi = WindowDpi(h);
			HINSTANCE inst = GetModuleHandleW(nullptr);
			g_intro = CreateWindowExW(0, L"STATIC",
				L"Your PC is checked automatically. \"Test launch\" opens PS-WRAP and records what happens.\n"
				L"Then \"Save report (.zip)\" and send the file, or \"Copy summary\" for Discord.",
				WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)IDC_INTRO, inst, nullptr);
			g_btn_check = MakeButton(h, L"Check again", IDC_CHECK);
			g_btn_test = MakeButton(h, L"Test launch", IDC_TEST);
			g_btn_safe = MakeButton(h, L"Test with OpenGL (safe mode)", IDC_SAFE);
			g_chk_verbose = MakeButton(h, L"Verbose log (very detailed)", IDC_VERBOSE, BS_AUTOCHECKBOX);
			SendMessageW(g_chk_verbose, BM_SETCHECK, BST_CHECKED, 0);
			g_btn_save = MakeButton(h, L"Save report (.zip)", IDC_SAVE);
			g_btn_copy = MakeButton(h, L"Copy summary", IDC_COPY);
			g_btn_github = MakeButton(h, L"Report on GitHub", IDC_GITHUB);
			g_btn_renderer = MakeButton(h, L"Use OpenGL (fix freeze)", IDC_RENDERER);
			g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10, 10, h,
				(HMENU)IDC_EDIT, inst, nullptr);
			SendMessageW(g_edit, EM_SETLIMITTEXT, 0, 0);
			g_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0, 0, 10, 10, h, (HMENU)IDC_STATUS, inst, nullptr);
			MakeFonts();
			Layout();
			UpdateButtons();
			StartChecks();
			return 0;
		}
		case WM_SIZE:
			Layout();
			return 0;
		case WM_GETMINMAXINFO:
		{
			auto mm = (MINMAXINFO *)lp;
			mm->ptMinTrackSize.x = Px(760);
			mm->ptMinTrackSize.y = Px(420);
			return 0;
		}
		case WM_DPICHANGED:
		{
			g_dpi = HIWORD(wp);
			MakeFonts();
			auto r = (RECT *)lp;
			SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
			Layout();
			return 0;
		}
		case WM_CTLCOLORSTATIC:
			if((HWND)lp == g_edit)
			{
				SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
				return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
			}
			break;
		case WM_COMMAND:
			if(HIWORD(wp) == BN_CLICKED)
				OnCommand(LOWORD(wp));
			return 0;
		case WM_APP_TEXT:
		{
			auto s = (wstring *)lp;
			AppendEdit(*s);
			delete s;
			return 0;
		}
		case WM_APP_STATUS:
		{
			auto s = (wstring *)lp;
			SetWindowTextW(g_status, s->c_str());
			delete s;
			return 0;
		}
		case WM_APP_DONE:
			if(wp == JOB_CHECK)
			{
				g_busy_check = false;
				SetEditText(BuildReport(true));
				bool hang;
				{
					std::lock_guard<std::mutex> l(g_rep.mu);
					hang = g_rep.vulkan_hang;
				}
				SetWindowTextW(g_status, hang ? L"Vulkan freezes on this PC — click \"Use OpenGL (fix freeze)\", then open PS-WRAP again."
				                               : L"Checks done. Click \"Test launch\" to watch PS-WRAP start, or \"Save report (.zip)\" to send this.");
			}
			else
			{
				g_busy_launch = false;
				AppendEdit(L"\n──── Test finished ────\n");
				SetEditText(BuildReport(true));
				SetWindowTextW(g_status, L"Test finished. Click \"Save report (.zip)\" and send the file, or \"Copy summary\".");
			}
			UpdateButtons();
			return 0;
		case WM_APP_HANG:
		{
			auto s = (wstring *)lp;
			wstring msg = L"PS-WRAP is not responding.\n\nThe place where it is stuck has been saved to the report"
				+ (s->empty() ? wstring(L".") : L":\n" + *s) + L"\n\nClose PS-WRAP now?";
			delete s;
			if(MessageBoxW(h, msg.c_str(), L"PS-WRAP Diagnostics", MB_YESNO | MB_ICONWARNING) == IDYES)
			{
				std::lock_guard<std::mutex> l(g_proc_mu);
				if(g_run_proc)
					TerminateProcess(g_run_proc, 0xDEAD);
			}
			return 0;
		}
		case WM_CLOSE:
			if(g_busy_launch && MessageBoxW(h, L"A test is still running. Quit anyway? (PS-WRAP keeps running.)", L"PS-WRAP Diagnostics", MB_YESNO | MB_ICONQUESTION) != IDYES)
				return 0;
			DestroyWindow(h);
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
	}
	return DefWindowProcW(h, msg, wp, lp);
}

static void EnableDpiAwareness()
{
	typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
	auto fn = (SetCtxFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
	if(!(fn && fn((HANDLE)-4)))   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
		SetProcessDPIAware();
}

// ลบผลตรวจเก่าใน %TEMP%\PS-WRAP-Diagnostics ที่เกิน 7 วัน (มี hang.dmp ได้)
static void CleanOldWorkDirs(const wstring &base)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((base + L"\\*").c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE)
		return;
	ULONGLONG cutoff = NowU64() - 7 * kDay100ns;
	do
	{
		wstring n = fd.cFileName;
		if((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n != L"." && n != L".." && FileTimeU64(fd.ftLastWriteTime) < cutoff)
			RemoveTree(base + L"\\" + n);
	} while(FindNextFileW(h, &fd));
	FindClose(h);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int show)
{
	if(cmd && wcsstr(cmd, L"--vkprobe"))
		return VkProbeMain();

	EnableDpiAwareness();
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&icc);
	InitRedaction();

	wchar_t path[MAX_PATH * 4];
	GetModuleFileNameW(nullptr, path, MAX_PATH * 4);
	g_tool_path = path;
	g_app_dir = g_tool_path.substr(0, g_tool_path.find_last_of(L'\\'));
	g_app_exe = g_app_dir + L"\\PS-WRAP.exe";

	// option สำหรับนักพัฒนา: --app <exe> (ทดสอบกับโปรแกรมอื่น) · --profile <ชื่อ> (ส่งต่อให้ PS-WRAP)
	int argc = 0;
	LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for(int i = 1; argv && i + 1 < argc; i++)
	{
		if(wcscmp(argv[i], L"--app") == 0)
			g_app_exe = argv[++i];
		else if(wcscmp(argv[i], L"--profile") == 0)
			g_launch_args = L"\"--profile=" + wstring(argv[++i]) + L"\"";
	}
	if(argv)
		LocalFree(argv);

	wchar_t tmp[MAX_PATH + 1];
	GetTempPathW(MAX_PATH + 1, tmp);
	wstring base = wstring(tmp) + L"PS-WRAP-Diagnostics";
	CreateDirectoryW(base.c_str(), nullptr);
	CleanOldWorkDirs(base);
	g_work_dir = base + L"\\" + NowStamp();
	CreateDirectoryW(g_work_dir.c_str(), nullptr);

	WNDCLASSEXW wc{ sizeof(wc) };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
	wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
	wc.lpszClassName = L"PsWrapDism";
	wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
	wc.hIconSm = wc.hIcon;
	RegisterClassExW(&wc);

	HDC dc = GetDC(nullptr);
	g_dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSY);
	ReleaseDC(nullptr, dc);
	HWND h = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"PS-WRAP Diagnostics", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, Px(980), Px(720), nullptr, nullptr,
		inst, nullptr);
	ShowWindow(h, show);
	UpdateWindow(h);

	MSG m;
	while(GetMessageW(&m, nullptr, 0, 0) > 0)
	{
		if(IsDialogMessageW(h, &m))
			continue;
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	return 0;
}
