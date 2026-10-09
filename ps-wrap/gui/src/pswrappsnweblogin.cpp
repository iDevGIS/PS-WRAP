// PS-WRAP: หน้าต่างล็อกอิน PSN ด้วย WebView2 — ดู include/pswrappsnweblogin.h
#include "pswrappsnweblogin.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QMetaObject>
#include <QPointer>

#ifdef Q_OS_WIN

#include <windows.h>
#include <dwmapi.h>
#include <functional>
#include "WebView2.h"

namespace {

// IID ของ handler ที่เราสร้างเอง (จาก MIDL_INTERFACE ใน WebView2.h) — mingw ไม่มี WebView2Guid.lib ให้ link
const GUID kIidEnvCompleted   = {0x4e8a3389, 0xc9d8, 0x4bd2, {0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d}};
const GUID kIidCtrlCompleted  = {0x6c4819f3, 0xc9b7, 0x4260, {0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c}};
const GUID kIidNavStarting    = {0x9adbe429, 0xf36d, 0x432b, {0x9d, 0xdc, 0xf8, 0x88, 0x1f, 0xbd, 0x76, 0xe3}};
const GUID kIidTitleChanged   = {0xf5f2b923, 0x953e, 0x4042, {0x9f, 0x95, 0xf3, 0xa1, 0x18, 0xe1, 0xaf, 0xd4}};
const GUID kIidWebView2_2     = {0x9e8f0cf8, 0xe670, 0x4b5e, {0xb2, 0xbc, 0x73, 0xe0, 0x61, 0xe3, 0x18, 0x4c}};   // ICoreWebView2_2 (CookieManager)

// COM callback ทั่วไป: ห่อ std::function เป็น handler ของ WebView2 (ไม่มี WRL ใน mingw)
template <typename I, typename... A>
class Callback : public I
{
	LONG ref = 1;
	GUID iid;
	std::function<HRESULT(A...)> fn;
public:
	Callback(const GUID &id, std::function<HRESULT(A...)> f) : iid(id), fn(std::move(f)) {}
	virtual ~Callback() = default;
	ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const LONG r = InterlockedDecrement(&ref);
		if (!r)
			delete this;
		return r;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
	{
		if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid)) {
			*out = static_cast<I *>(this);
			AddRef();
			return S_OK;
		}
		*out = nullptr;
		return E_NOINTERFACE;
	}
	HRESULT STDMETHODCALLTYPE Invoke(A... args) override { return fn(args...); }
};

// สร้าง callback (ref = 1) → ส่งให้ WebView2 (AddRef เอง) → คืน ref ของเราหลังเรียก
template <typename T, typename F>
HRESULT withHandler(T *handler, F &&call)
{
	const HRESULT hr = call(handler);
	handler->Release();
	return hr;
}

using CreateEnvFn = HRESULT(STDAPICALLTYPE *)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *,
	ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);

const wchar_t *kWindowClass = L"PsWrapPsnLoginWindow";

QString loaderPath()
{
	return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("WebView2Loader.dll"));
}

QString dataFolder()
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("psn-webview2"));
}

} // namespace

struct PsWrapPsnWebLogin::Impl
{
	PsWrapPsnWebLogin *q = nullptr;
	HWND hwnd = nullptr;
	HMODULE loader = nullptr;
	ICoreWebView2Controller *controller = nullptr;
	ICoreWebView2 *webview = nullptr;
	QString prefix;
	QString title;
	bool done = false;
	bool forget = false;
	quint64 generation = 0;   // callback ของหน้าต่างเก่า (ปิดไปแล้ว) ไม่ต้องทำอะไร

	void fitBounds()
	{
		if (!controller || !hwnd)
			return;
		RECT rc;
		GetClientRect(hwnd, &rc);
		controller->put_Bounds(rc);
	}

	void releaseWeb()
	{
		if (webview) {
			webview->Release();
			webview = nullptr;
		}
		if (controller) {
			controller->Close();
			controller->Release();
			controller = nullptr;
		}
	}

	static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		auto *self = reinterpret_cast<Impl *>(GetWindowLongPtrW(h, GWLP_USERDATA));
		switch (msg) {
		case WM_SIZE:
			if (self)
				self->fitBounds();
			return 0;
		case WM_MOVE:
			if (self && self->controller)
				self->controller->NotifyParentWindowPositionChanged();
			break;
		case WM_SETFOCUS:
			if (self && self->controller)
				self->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
			return 0;
		case WM_CLOSE:
			DestroyWindow(h);
			return 0;
		case WM_DESTROY:
			if (self) {
				self->releaseWeb();
				self->hwnd = nullptr;
				self->generation++;
				SetWindowLongPtrW(h, GWLP_USERDATA, 0);
				if (!self->done) {
					QPointer<PsWrapPsnWebLogin> q = self->q;
					QMetaObject::invokeMethod(q, [q]() { if (q) emit q->closed(); }, Qt::QueuedConnection);
				}
			}
			return 0;
		}
		return DefWindowProcW(h, msg, wp, lp);
	}

	bool ensureClass()
	{
		static bool registered = false;
		if (registered)
			return true;
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = &Impl::wndProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hbrBackground = CreateSolidBrush(RGB(11, 15, 20));   // Theme.bg ระหว่างหน้าเว็บยังไม่ขึ้น
		wc.lpszClassName = kWindowClass;
		registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
		return registered;
	}

	void fail(const QString &error)
	{
		QPointer<PsWrapPsnWebLogin> qp = q;
		done = true;   // ไม่ส่ง closed() ซ้ำตอนทำลายหน้าต่าง
		if (hwnd)
			DestroyWindow(hwnd);
		QMetaObject::invokeMethod(qp, [qp, error]() { if (qp) emit qp->failed(error); }, Qt::QueuedConnection);
	}

	void onController(ICoreWebView2Controller *ctrl, const QUrl &url)
	{
		controller = ctrl;
		controller->AddRef();
		controller->get_CoreWebView2(&webview);
		if (!webview) {
			fail(QStringLiteral("WebView2: no CoreWebView2"));
			return;
		}
		ICoreWebView2Settings *settings = nullptr;
		if (SUCCEEDED(webview->get_Settings(&settings)) && settings) {
			settings->put_AreDevToolsEnabled(FALSE);
			settings->put_IsStatusBarEnabled(FALSE);
			settings->Release();
		}
		fitBounds();
		const quint64 gen = generation;
		EventRegistrationToken token;
		withHandler(new Callback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *>(
			kIidNavStarting, [this, gen](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
				if (gen != generation || done)
					return S_OK;
				LPWSTR uri = nullptr;
				if (FAILED(args->get_Uri(&uri)) || !uri)
					return S_OK;
				const QString u = QString::fromWCharArray(uri);
				CoTaskMemFree(uri);
				if (!u.startsWith(prefix))
					return S_OK;
				// ได้ redirect พร้อม code แล้ว — ไม่ต้องโหลดหน้านั้น ปิดหน้าต่างแล้วส่ง URL ให้ QML ทำต่อ
				args->put_Cancel(TRUE);
				done = true;
				QPointer<PsWrapPsnWebLogin> qp = q;
				QMetaObject::invokeMethod(qp, [qp, u]() {
					if (!qp)
						return;
					qp->close();
					emit qp->redirected(u);
				}, Qt::QueuedConnection);
				return S_OK;
			}), [&](auto *hd) { return webview->add_NavigationStarting(hd, &token); });
		// ชื่อหน้าต่าง = ชื่อเรา · ชื่อหน้าเว็บ (เห็นว่าอยู่หน้าไหนของ Sony)
		withHandler(new Callback<ICoreWebView2DocumentTitleChangedEventHandler, ICoreWebView2 *, IUnknown *>(
			kIidTitleChanged, [this, gen](ICoreWebView2 *sender, IUnknown *) -> HRESULT {
				if (gen != generation || !hwnd)
					return S_OK;
				LPWSTR t = nullptr;
				if (SUCCEEDED(sender->get_DocumentTitle(&t)) && t) {
					const QString page = QString::fromWCharArray(t);
					CoTaskMemFree(t);
					const QString full = page.isEmpty() ? title : title + QStringLiteral(" — ") + page;
					SetWindowTextW(hwnd, reinterpret_cast<const wchar_t *>(full.utf16()));
				}
				return S_OK;
			}), [&](auto *hd) { return webview->add_DocumentTitleChanged(hd, &token); });
		// เข้าด้วยบัญชีอื่น: ลบคุกกี้ (session PSN ที่จำไว้) ก่อนโหลดหน้าล็อกอิน
		if (forget) {
			ICoreWebView2_2 *wv2 = nullptr;
			if (SUCCEEDED(webview->QueryInterface(kIidWebView2_2, reinterpret_cast<void **>(&wv2))) && wv2) {
				ICoreWebView2CookieManager *cookies = nullptr;
				if (SUCCEEDED(wv2->get_CookieManager(&cookies)) && cookies) {
					cookies->DeleteAllCookies();
					cookies->Release();
				}
				wv2->Release();
			}
		}
		const QString target = url.toString(QUrl::FullyEncoded);
		webview->Navigate(reinterpret_cast<const wchar_t *>(target.utf16()));
		controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
	}
};

PsWrapPsnWebLogin::PsWrapPsnWebLogin(QObject *parent) : QObject(parent), d(new Impl)
{
	d->q = this;
}

PsWrapPsnWebLogin::~PsWrapPsnWebLogin()
{
	d->done = true;
	close();
	if (d->loader)
		FreeLibrary(d->loader);
	delete d;
}

bool PsWrapPsnWebLogin::available()
{
	if (!QFileInfo::exists(loaderPath()))
		return false;
	// Evergreen WebView2 Runtime (มากับ Windows 11 / Edge) — ตรวจ version ที่ลงไว้ทั้งแบบเครื่องและแบบผู้ใช้
	static const char *keys[] = {
		"HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
		"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
		"HKEY_CURRENT_USER\\Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
	};
	for (const char *k : keys) {
		const QString pv = QSettings(QString::fromLatin1(k), QSettings::NativeFormat).value(QStringLiteral("pv")).toString();
		if (!pv.isEmpty() && pv != QStringLiteral("0.0.0.0"))
			return true;
	}
	return false;
}

bool PsWrapPsnWebLogin::isOpen() const
{
	return d->hwnd != nullptr;
}

void PsWrapPsnWebLogin::close()
{
	if (d->hwnd)
		DestroyWindow(d->hwnd);
}

bool PsWrapPsnWebLogin::start(const QUrl &url, const QString &redirectPrefix, quintptr ownerWindow, const QString &title, bool forgetAccount)
{
	if (d->hwnd && forgetAccount) {
		d->done = true;   // ปิดเพื่อเปิดใหม่ — ไม่ใช่ผู้ใช้ปิดเอง (ไม่ส่ง closed())
		close();
	}
	if (d->hwnd) {
		ShowWindow(d->hwnd, SW_RESTORE);
		SetForegroundWindow(d->hwnd);
		return true;
	}
	if (!d->loader)
		d->loader = LoadLibraryW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(loaderPath()).utf16()));
	auto createEnv = d->loader ? reinterpret_cast<CreateEnvFn>(reinterpret_cast<void *>(GetProcAddress(d->loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
	if (!createEnv || !d->ensureClass()) {
		emit failed(QStringLiteral("WebView2Loader.dll not found"));
		return false;
	}
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // Qt เปิด OLE ไว้แล้วบน GUI thread — เรียกซ้ำได้

	d->prefix = redirectPrefix;
	d->title = title;
	d->done = false;
	d->forget = forgetAccount;
	d->generation++;

	// ขนาดตามหน้าต่างหลัก (DPI) · วางกลางหน้าต่างหลัก
	HWND owner = reinterpret_cast<HWND>(ownerWindow);
	const UINT dpi = owner ? GetDpiForWindow(owner) : 96;
	const int w = MulDiv(560, dpi, 96), h = MulDiv(760, dpi, 96);
	int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
	RECT orc;
	if (owner && GetWindowRect(owner, &orc)) {
		x = orc.left + ((orc.right - orc.left) - w) / 2;
		y = orc.top + qMax(0L, ((orc.bottom - orc.top) - h) / 2);
	}
	d->hwnd = CreateWindowExW(0, kWindowClass, reinterpret_cast<const wchar_t *>(title.utf16()),
		WS_OVERLAPPEDWINDOW, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!d->hwnd) {
		emit failed(QStringLiteral("CreateWindow failed"));
		return false;
	}
	SetWindowLongPtrW(d->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
	BOOL dark = TRUE;
	DwmSetWindowAttribute(d->hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
	if (owner) {
		if (HICON big = reinterpret_cast<HICON>(SendMessageW(owner, WM_GETICON, ICON_BIG, 0)))
			SendMessageW(d->hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
		if (HICON small = reinterpret_cast<HICON>(SendMessageW(owner, WM_GETICON, ICON_SMALL, 0)))
			SendMessageW(d->hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
	}
	ShowWindow(d->hwnd, SW_SHOW);
	SetForegroundWindow(d->hwnd);

	QDir().mkpath(dataFolder());
	const QString folder = QDir::toNativeSeparators(dataFolder());
	const quint64 gen = d->generation;
	Impl *impl = d;
	HRESULT hr = withHandler(new Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment *>(
			kIidEnvCompleted, [impl, gen, url](HRESULT err, ICoreWebView2Environment *env) -> HRESULT {
				if (gen != impl->generation || !impl->hwnd)
					return S_OK;   // ผู้ใช้ปิดหน้าต่างไปก่อน
				if (FAILED(err) || !env) {
					impl->fail(QStringLiteral("WebView2 environment failed (0x%1)").arg(quint32(err), 8, 16, QLatin1Char('0')));
					return S_OK;
				}
				withHandler(new Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller *>(
						kIidCtrlCompleted, [impl, gen, url](HRESULT err2, ICoreWebView2Controller *ctrl) -> HRESULT {
							if (gen != impl->generation || !impl->hwnd) {
								if (ctrl)
									ctrl->Close();
								return S_OK;
							}
							if (FAILED(err2) || !ctrl) {
								impl->fail(QStringLiteral("WebView2 controller failed (0x%1)").arg(quint32(err2), 8, 16, QLatin1Char('0')));
								return S_OK;
							}
							impl->onController(ctrl, url);
							return S_OK;
						}), [&](auto *hd) { return env->CreateCoreWebView2Controller(impl->hwnd, hd); });
				return S_OK;
			}), [&](auto *hd) { return createEnv(nullptr, reinterpret_cast<const wchar_t *>(folder.utf16()), nullptr, hd); });
	if (FAILED(hr)) {
		d->fail(QStringLiteral("WebView2 runtime not available (0x%1)").arg(quint32(hr), 8, 16, QLatin1Char('0')));
		return false;
	}
	return true;
}

#else // !Q_OS_WIN

struct PsWrapPsnWebLogin::Impl {};
PsWrapPsnWebLogin::PsWrapPsnWebLogin(QObject *parent) : QObject(parent), d(new Impl) {}
PsWrapPsnWebLogin::~PsWrapPsnWebLogin() { delete d; }
bool PsWrapPsnWebLogin::available() { return false; }
bool PsWrapPsnWebLogin::isOpen() const { return false; }
void PsWrapPsnWebLogin::close() {}
bool PsWrapPsnWebLogin::start(const QUrl &, const QString &, quintptr, const QString &, bool) { return false; }

#endif
