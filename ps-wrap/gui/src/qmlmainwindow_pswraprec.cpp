// PS-WRAP: ส่วนของ QmlMainWindow ที่เกี่ยวกับอัดวิดีโอ + overlay spectrum ไมค์
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
#include "qmlmainwindow.h"
#include "qmlbackend.h"
#include "qmlsettings.h"

#include <pswraprecorder.h>
#include <pswraplive.h>
#include <pswrapreccapture.h>
#include <pswrapmicmeter.h>
#include <pswrapvoiceproc.h>

#include <SDL.h>

#include <QAction>
#include <QActionGroup>
#include <QDateTime>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

// หน้าทดสอบไมค์: ไมค์ mono → PsWrapVoiceProc (ไม่มีเสียงเกม = ไม่มี echo reference, ได้แค่ลดเสียงรบกวน) → สเตอริโอเข้า meter
struct MicPreviewState
{
	PsWrapVoiceProc voice;
	int16_t frame[PsWrapVoiceProc::kFrame] = {};
	int16_t stereo[PsWrapVoiceProc::kFrame * 2] = {};
	int fill = 0;
};
MicPreviewState *g_mic_preview_state = nullptr;   // ใช้คู่กับ pswrap_mic_preview (สร้างก่อนเปิด ลบหลังปิดอุปกรณ์)

QString defaultRecordingFolder()
{
	QString base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
	if (base.isEmpty())
		base = QDir::homePath();
	return QDir(base).filePath(QStringLiteral("PS-WRAP"));
}

// โฟลเดอร์สำรองเมื่อเขียนโฟลเดอร์ที่ตั้งไว้ไม่ได้ (Windows Security "Controlled folder access" บล็อก exe ที่ไม่รู้จักไม่ให้เขียน Videos)
QString fallbackRecordingFolder()
{
	return QDir(QDir::homePath()).filePath(QStringLiteral("PS-WRAP Recordings"));
}

// Instant Replay: ภาพต้องนิ่งก่อนเริ่ม pipeline (ขนาดหน้าต่าง/HDR ของสตรีมตั้งตัวแล้ว)
QElapsedTimer g_replay_video_since;
constexpr qint64 kReplayStartDelayMs = 3000;
int g_test_replay_s = -1;   // PSWRAP_TEST_REPLAY (อ่านครั้งเดียว)

} // namespace

void QmlMainWindow::pswrapInitRecording()
{
	pswrap_recorder = new PsWrapRecorder(this);
	pswrap_mic_meter = new PsWrapMicMeter(this);
	pswrapInitMic();   // gain/noise gate ที่จำไว้ → PsWrapVoiceProc (qmlmainwindow_pswrapmic.cpp) · ไม่ขึ้นกับ speex
	connect(pswrap_recorder, &PsWrapRecorder::recordingChanged, this, [this]() { pswrapRefreshTray(); });
#if CHIAKI_GUI_ENABLE_SPEEX
	// ลดเสียงรบกวน/ตัด echo: ค่าใน Settings → ของกลางของ PsWrapVoiceProc ทันที (สตรีมที่เล่นอยู่ + หน้าทดสอบไมค์รับไปเฟรมถัดไป)
	auto sync_voice = [this]() {
		PsWrapVoiceProc::setParams(settings->GetSpeechProcessingEnabled(), settings->GetNoiseSuppressLevel(), settings->GetEchoSuppressLevel());
	};
	sync_voice();
	if (QmlSettings *qs = backend->qmlSettings()) {
		connect(qs, &QmlSettings::speechProcessingChanged, this, sync_voice);
		connect(qs, &QmlSettings::noiseSuppressLevelChanged, this, sync_voice);
		connect(qs, &QmlSettings::echoSuppressLevelChanged, this, sync_voice);
	}
#endif
	// สตรีมจบ = ปิดไฟล์ให้เรียบร้อยทันที (ก่อน render หยุด) · tray ตามสถานะ mute ของ session
	connect(backend, &QmlBackend::sessionChanged, this, [this](StreamSession *s) {
		if (!s) {
			pswrap_recorder->stop();
			pswrap_recorder->stopReplay(); // pipeline ปิดเองเมื่อไม่มีผู้รับเหลือ
		}
		pswrap_recorder->clearReplayFault(); // สตรีมใหม่ = ให้ replay ลองเริ่มใหม่ได้
		g_replay_video_since.invalidate();
		if (s)
			connect(s, &StreamSession::MutedChanged, this, [this]() { pswrapRefreshTray(); });
		QMetaObject::invokeMethod(this, [this]() { pswrapRefreshTray(); pswrapSyncReplay(); }, Qt::QueuedConnection); // หลัง lambda ใน init ตั้ง session แล้ว
	});

	// Instant Replay: เริ่ม/หยุด pipeline เองตามสตรีม — เช็คทุก 1 วิ (ภาพขึ้น/สตรีมจบ/เปิดปิดใน Settings)
	g_test_replay_s = qEnvironmentVariableIntValue("PSWRAP_TEST_REPLAY");
	pswrap_recorder->setReplaySeconds(settings->GetReplaySeconds());
	connect(this, &QmlMainWindow::hasVideoChanged, this, [this]() { pswrapSyncReplay(); });
	auto *replay_timer = new QTimer(this);
	replay_timer->setInterval(1000);
	connect(replay_timer, &QTimer::timeout, this, [this]() { pswrapSyncReplay(); });
	replay_timer->start();
	if (g_test_replay_s > 0) {
		// ทดสอบอัตโนมัติ: PSWRAP_TEST_REPLAY=<วินาที> → เปิด replay (ไม่เซฟค่าลง Settings) · marker ที่ครึ่งทาง · เซฟ replay เมื่อครบ
		auto *t = new QTimer(this);
		t->setInterval(250);
		auto since = std::make_shared<QElapsedTimer>();
		auto marked = std::make_shared<bool>(false);
		connect(t, &QTimer::timeout, this, [this, t, since, marked]() {
			if (!pswrap_recorder->isReplayActive()) {
				since->invalidate();
				return;
			}
			if (!since->isValid())
				since->start();
			if (!*marked && since->elapsed() >= g_test_replay_s * 500LL) {
				*marked = true;
				qCInfo(chiakiGui) << "PSWRAP test replay: marker";
				addMarker();
			}
			if (since->elapsed() >= g_test_replay_s * 1000LL) {
				qCInfo(chiakiGui) << "PSWRAP test replay: save";
				saveReplay();
				t->stop();
			}
		});
		t->start();
	}

	// ทดสอบอัตโนมัติ: PSWRAP_TEST_AUTOREC=<วินาที> → ภาพขึ้นแล้ว 3 วิ เริ่มอัดเอง ครบแล้วหยุดเอง (ไม่มีผลถ้าไม่ตั้ง env)
	const int autorec_s = qEnvironmentVariableIntValue("PSWRAP_TEST_AUTOREC");
	if (autorec_s > 0) {
		auto *timer = new QTimer(this);
		timer->setInterval(250);
		auto video_since = std::make_shared<QElapsedTimer>();
		auto rec_since = std::make_shared<QElapsedTimer>();
		// PSWRAP_TEST_MARKERS=1 → ใส่ marker ที่ 1/3 และ 2/3 ของคลิป (ทดสอบ chapter)
		auto markers_left = std::make_shared<int>(qEnvironmentVariableIntValue("PSWRAP_TEST_MARKERS") > 0 ? 2 : 0);
		connect(timer, &QTimer::timeout, this, [this, timer, autorec_s, video_since, rec_since, markers_left]() {
			if (rec_since->isValid() && *markers_left > 0 && rec_since->elapsed() >= autorec_s * 1000LL * (3 - *markers_left) / 3) {
				(*markers_left)--;
				addMarker();
			}
			if (!rec_since->isValid()) {
				if (!session || !has_video) {
					video_since->invalidate();
					return;
				}
				if (!video_since->isValid())
					video_since->start();
				if (video_since->elapsed() < 3000)
					return;
				qCInfo(chiakiGui) << "PSWRAP test autorec: start" << autorec_s << "s";
				toggleRecording();
				rec_since->start();
			} else if (rec_since->elapsed() >= autorec_s * 1000LL) {
				qCInfo(chiakiGui) << "PSWRAP test autorec: stop";
				if (pswrap_recorder->isRecording())
					toggleRecording();
				timer->stop();
			}
		});
		timer->start();
	}
	goLive();   // PS-WRAP: สร้าง Go Live พร้อม hook สตรีมจบ/สลับ profile (qmlmainwindow_pswraplive.cpp)
}

static void pswrapMicPreviewCb(void *userdata, Uint8 *stream, int len)
{
	auto *st = static_cast<MicPreviewState *>(userdata);
	const int16_t *in = reinterpret_cast<const int16_t *>(stream);
	const size_t n = static_cast<size_t>(len) / sizeof(int16_t);
	for (size_t i = 0; i < n; i++) {
		st->frame[st->fill++] = in[i];
		if (st->fill < PsWrapVoiceProc::kFrame)
			continue;
		st->fill = 0;
		st->voice.process(st->frame, 0);
		for (int k = 0; k < PsWrapVoiceProc::kFrame; k++)
			st->stereo[2 * k] = st->stereo[2 * k + 1] = st->frame[k];
		PsWrapMicMeter::tap(st->stereo, PsWrapVoiceProc::kFrame, 2);
	}
}

bool QmlMainWindow::startMicPreview(const QString &device)
{
	stopMicPreview();
	SDL_AudioSpec want = {}, have = {};
	want.freq = 48000;
	want.format = AUDIO_S16SYS;
	want.channels = 1;
	want.samples = 480;
	want.callback = pswrapMicPreviewCb;
	g_mic_preview_state = new MicPreviewState;
	want.userdata = g_mic_preview_state;
	const QByteArray name = device.toUtf8();
	SDL_AudioDeviceID id = SDL_OpenAudioDevice(device.isEmpty() ? nullptr : name.constData(), 1, &want, &have, 0);
	if (!id && !device.isEmpty())
		id = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0); // ถอดอยู่ → ลอง default
	if (!id) {
		qCWarning(chiakiGui) << "PSWRAP mic preview: open failed" << device << SDL_GetError();
		delete g_mic_preview_state;
		g_mic_preview_state = nullptr;
		return false;
	}
	pswrap_mic_preview = id;
	SDL_PauseAudioDevice(id, 0);
	qCInfo(chiakiGui) << "PSWRAP mic preview:" << (device.isEmpty() ? QStringLiteral("Auto") : device) << have.freq << "Hz" << have.channels << "ch";
	return true;
}

void QmlMainWindow::stopMicPreview()
{
	if (pswrap_mic_preview) {
		SDL_CloseAudioDevice(pswrap_mic_preview); // รอ callback ที่กำลังรันจบก่อนคืน
		pswrap_mic_preview = 0;
	}
	delete g_mic_preview_state;
	g_mic_preview_state = nullptr;
}

void QmlMainWindow::pswrapStopRecordingForTeardown()
{
	stopMicPreview();
	if (auto *g = findChild<PsWrapGoLive *>(QString(), Qt::FindDirectChildrenOnly))
		g->stop();   // PS-WRAP: ปิด socket ไลฟ์ + ถอด tap ก่อน recorder ปิด pipeline
	if (pswrap_recorder)
		pswrap_recorder->shutdown(); // ไฟล์อัด + replay + job เซฟ/remux ที่ค้าง (ยกเลิก — ไฟล์เดิมยังอยู่)
	if (pswrap_mic_meter)
		pswrap_mic_meter->setEnabled(false);
}

void QmlMainWindow::pswrapDestroyCapture()
{
	delete pswrap_rec_capture;
	pswrap_rec_capture = nullptr;
	if (pswrap_rec_dot_tex)
		if (pl_gpu gpu = placeboGpu())
			pl_tex_destroy(gpu, &pswrap_rec_dot_tex);
}

// จุดแดงกระพริบมุมซ้ายบนของวิดีโอ วาดเป็น overlay ของ "จอ" เท่านั้น — target ของไฟล์ใช้ quick_tex อย่างเดียว เลยไม่ติดคลิป
void QmlMainWindow::pswrapDecorateScreen(pl_frame &target_frame)
{
	PsWrapRecorder *rec = PsWrapRecorder::instance();
	if (!rec || !rec->isRecording() || target_frame.num_overlays > 1)
		return;
	pl_gpu gpu = placeboGpu();
	if (!gpu)
		return;
	constexpr int kDot = 64;
	if (!pswrap_rec_dot_tex) {
		pl_fmt fmt = pl_find_fmt(gpu, PL_FMT_UNORM, 1, 8, 8, static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_LINEAR));
		if (!fmt)
			return;
		static uint8_t mask[kDot * kDot];
		for (int y = 0; y < kDot; y++)
			for (int x = 0; x < kDot; x++) {
				const float dx = x + 0.5f - kDot / 2.0f, dy = y + 0.5f - kDot / 2.0f;
				const float d = std::sqrt(dx * dx + dy * dy);
				mask[y * kDot + x] = static_cast<uint8_t>(std::clamp((kDot / 2.0f - 1.0f - d) * 255.0f, 0.0f, 255.0f)); // ขอบ AA 1px
			}
		pl_tex_params tp = {};
		tp.w = kDot;
		tp.h = kDot;
		tp.format = fmt;
		tp.sampleable = true;
		tp.initial_data = mask;
		tp.debug_tag = PL_DEBUG_TAG;
		pswrap_rec_dot_tex = pl_tex_create(gpu, &tp);
		if (!pswrap_rec_dot_tex)
			return;
	}
	const pl_rect2df c = target_frame.crop;
	const float x0 = std::min(c.x0, c.x1), y0 = std::min(c.y0, c.y1);
	const float size = 14.0f * float(devicePixelRatio());
	const float margin = 18.0f * float(devicePixelRatio());
	const double t = QDateTime::currentMSecsSinceEpoch() % 1400 / 1400.0;
	const float alpha = 0.6f + 0.4f * float(std::cos(t * 2.0 * M_PI));
	pswrap_rec_dot_part = {};
	pswrap_rec_dot_part.src = {0, 0, float(kDot), float(kDot)};
	pswrap_rec_dot_part.dst = {x0 + margin, y0 + margin, x0 + margin + size, y0 + margin + size};
	pswrap_rec_dot_part.color[0] = 1.0f;
	pswrap_rec_dot_part.color[1] = 0.23f;
	pswrap_rec_dot_part.color[2] = 0.23f;
	pswrap_rec_dot_part.color[3] = alpha;
	if (target_frame.num_overlays == 1)
		pswrap_screen_overlays[0] = target_frame.overlays[0];
	pl_overlay &dot = pswrap_screen_overlays[target_frame.num_overlays];
	dot = {};
	dot.tex = pswrap_rec_dot_tex;
	dot.mode = PL_OVERLAY_MONOCHROME;
	dot.repr = pl_color_repr_rgb;
	dot.color = pl_color_space_srgb;
	dot.parts = &pswrap_rec_dot_part;
	dot.num_parts = 1;
	target_frame.overlays = pswrap_screen_overlays;
	target_frame.num_overlays += 1;
}

QObject *QmlMainWindow::recorderObject() const { return pswrap_recorder; }
QObject *QmlMainWindow::micMeterObject() const { return pswrap_mic_meter; }

bool QmlMainWindow::micOverlay() const { return settings->GetMicOverlay(); }
void QmlMainWindow::setMicOverlay(bool v)
{
	if (v == settings->GetMicOverlay())
		return;
	settings->SetMicOverlay(v);
	if (tray_mic_action && tray_mic_action->isChecked() != v)
		tray_mic_action->setChecked(v);
	emit micOverlayChanged();
}

QString QmlMainWindow::recordingFolder() const
{
	const QString f = settings->GetRecordingFolder();
	return f.isEmpty() ? QDir::toNativeSeparators(defaultRecordingFolder()) : f;
}

void QmlMainWindow::setRecordingFolder(const QString &folder)
{
	QString f = folder.trimmed();
	if (f.startsWith(QStringLiteral("file:")))
		f = QUrl(f).toLocalFile();
	f = QDir::toNativeSeparators(QDir::cleanPath(f));
	if (QDir::cleanPath(f) == QDir::cleanPath(defaultRecordingFolder()))
		f.clear();
	if (f == settings->GetRecordingFolder())
		return;
	settings->SetRecordingFolder(f);
	emit recordingFolderChanged();
}

void QmlMainWindow::toggleRecording()
{
	if (!pswrap_recorder || pswrap_recorder->isBusy())
		return;
	if (pswrap_recorder->isRecording()) {
		pswrap_recorder->stop();
		return;
	}
	PsWrapRecConfig cfg;
	QString err;
	if (!pswrapBuildRecConfig(&cfg, &err)) {
		emit pswrap_recorder->failed(err);
		return;
	}
	const QString folder = QDir::fromNativeSeparators(recordingFolder());
	cfg.path = QDir(folder).filePath(QStringLiteral("PS-WRAP %1.mp4")
		.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss"))));
	if (pswrap_recorder->start(cfg, &err))
		return;
	// เขียนโฟลเดอร์ที่ตั้งไว้ไม่ได้ (เครื่องลูกพี่: Windows Security "Controlled folder access" บล็อก exe ที่ไม่รู้จักไม่ให้เขียน Videos)
	// → ย้ายไปโฟลเดอร์สำรองใน home ที่ระบบไม่คุ้มครอง แล้วแจ้งผู้ใช้ แทนที่จะอัดไม่ได้เลย
	const QString fallback = fallbackRecordingFolder();
	if (pswrap_recorder->lastStartFileError() && QDir::cleanPath(folder) != QDir::cleanPath(fallback)) {
		const QString blocked = QDir::toNativeSeparators(folder);
		cfg.path = QDir(fallback).filePath(QFileInfo(cfg.path).fileName());
		QString err2;
		if (pswrap_recorder->start(cfg, &err2)) {
			qCInfo(chiakiGui) << "PSWRAP recording: fallback folder" << fallback << "because" << err;
			emit pswrap_recorder->notice(tr("Windows blocked saving to %1, so this recording is saved to %2. To keep using your folder, allow PS-WRAP in Windows Security → Ransomware protection → Allow an app, or pick another folder in Settings.")
				.arg(blocked, QDir::toNativeSeparators(fallback)), cfg.path);
			return;
		}
	}
	emit pswrap_recorder->failed(err);
}

// สเปคของ pipeline (ใช้ร่วมกันทั้งไฟล์อัดและ Instant Replay — ต้องเหมือนกันทุกจุด)
bool QmlMainWindow::pswrapBuildRecConfig(PsWrapRecConfig *cfg, QString *error)
{
	if (!session || stream_session_active.loadAcquire() == 0) {
		*error = tr("Start a stream before recording.");
		return false;
	}
	bool hdr = false;
	PsWrapRecHdrInfo hdr_info;
	int src_h = 0;
	if (!PsWrapRecCapture::sourceInfo(&hdr, &hdr_info, &src_h)) {
		*error = tr("No video yet — wait for the stream to start.");
		return false;
	}

	// ไฟล์ = ทั้งหน้าต่างตามสัดส่วนจริง สูงไม่เกินความละเอียดสตรีม (หน้าต่าง 5K ultrawide + สตรีม 1080p → 2632x1080)
	const qreal dpr = devicePixelRatio();
	const int sw = qMax(2, qRound(width() * dpr));
	const int sh = qMax(2, qRound(height() * dpr));
	int th = qMin(sh, qMax(720, src_h > 0 ? src_h : 1080));
	int tw = qRound(double(sw) * th / sh);
	const int max_w = hdr ? 8192 : 4096; // H.264 NVENC กว้างได้สุด 4096
	if (tw > max_w) {
		th = qRound(double(th) * max_w / tw);
		tw = max_w;
	}
	const double interval_ms = stream_configured_frame_interval_ms;

	cfg->width = qMax(2, tw & ~1);
	cfg->height = qMax(2, th & ~1);
	cfg->fps = interval_ms > 0.0 ? qBound(24, qRound(1000.0 / interval_ms), 120) : 60;
	cfg->hdr = hdr;
	cfg->hdr_info = hdr_info;
	return true;
}

bool QmlMainWindow::replayEnabled() const { return settings->GetReplayEnabled(); }
void QmlMainWindow::setReplayEnabled(bool on)
{
	if (on == settings->GetReplayEnabled())
		return;
	settings->SetReplayEnabled(on);
	if (pswrap_recorder)
		pswrap_recorder->clearReplayFault();
	emit replayEnabledChanged();
	pswrapSyncReplay();
}

int QmlMainWindow::replaySeconds() const { return settings->GetReplaySeconds(); }
void QmlMainWindow::setReplaySeconds(int seconds)
{
	seconds = qBound(30, seconds, 120);
	if (seconds == settings->GetReplaySeconds())
		return;
	settings->SetReplaySeconds(seconds);
	if (pswrap_recorder)
		pswrap_recorder->setReplaySeconds(seconds);
	emit replaySecondsChanged();
}

void QmlMainWindow::pswrapSyncReplay()
{
	if (!pswrap_recorder || pswrap_recorder->isBusy())
		return;
	const bool enabled = settings->GetReplayEnabled() || g_test_replay_s > 0;
	const bool streaming = session && stream_session_active.loadAcquire() != 0 && has_video;
	if (!enabled || !streaming) {
		g_replay_video_since.invalidate();
		if (pswrap_recorder->isReplayActive())
			pswrap_recorder->stopReplay();
		return;
	}
	if (pswrap_recorder->isReplayActive() || pswrap_recorder->replayFaulted())
		return;
	if (!g_replay_video_since.isValid())
		g_replay_video_since.start();
	if (g_replay_video_since.elapsed() < kReplayStartDelayMs)
		return;
	PsWrapRecConfig cfg;
	QString err;
	if (!pswrapBuildRecConfig(&cfg, &err))
		return; // ยังไม่มีภาพ — รอบหน้าลองใหม่
	if (!pswrap_recorder->startReplay(cfg, settings->GetReplaySeconds(), &err)) {
		// replayFaulted() กันไม่ให้ลองซ้ำทุกวินาที จนกว่าสตรีมใหม่/เปิดปิด Instant Replay ใหม่
		qCWarning(chiakiGui) << "PSWRAP replay: start failed" << err;
		emit pswrap_recorder->failed(tr("Instant Replay couldn't start: %1").arg(err));
	}
}

void QmlMainWindow::saveReplay()
{
	if (!pswrap_recorder)
		return;
	if (!pswrap_recorder->isReplayActive()) {
		emit pswrap_recorder->failed(settings->GetReplayEnabled() ? tr("Instant Replay starts a few seconds after the stream shows video.")
		                                                          : tr("Turn on Instant Replay in Settings first."));
		return;
	}
	const QString name = QStringLiteral("PS-WRAP Replay %1.mp4")
		.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss")));
	const QString folder = QDir::fromNativeSeparators(recordingFolder());
	pswrap_recorder->saveReplay(QDir(folder).filePath(name), QDir(fallbackRecordingFolder()).filePath(name));
}

void QmlMainWindow::addMarker()
{
	if (pswrap_recorder)
		pswrap_recorder->addMarker();
}

void QmlMainWindow::openRecordingsFolder()
{
	const QString folder = QDir::fromNativeSeparators(recordingFolder());
	QDir().mkpath(folder);
	QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void QmlMainWindow::revealRecording(const QString &path)
{
	const QString p = path.startsWith(QStringLiteral("file:")) ? QUrl(path).toLocalFile() : path;
	if (!QFileInfo::exists(p)) {
		openRecordingsFolder();
		return;
	}
#ifdef Q_OS_WIN
	QProcess proc;
	proc.setProgram(QStringLiteral("explorer.exe"));
	proc.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(p)));
	if (proc.startDetached())
		return;
#endif
	QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p).absolutePath()));
}

void QmlMainWindow::pswrapSetupTrayRecording(QMenu *menu)
{
	auto makeSubmenu = [menu](const QString &icon, const QString &title) {
		QMenu *sub = menu->addMenu(QIcon(icon), title);
		sub->setStyleSheet(menu->styleSheet());
		return sub;
	};

	// ---- overlay: Mic visualizer + เอฟเฟกต์/พื้นหลัง facecam
	const QString mic_label = tr("Mic visualizer");
	tray_mic_action = menu->addAction(QIcon(QStringLiteral(":/icons/menu/spectrum.svg")), mic_label);
	tray_mic_action->setCheckable(true);
	tray_mic_action->setChecked(settings->GetMicOverlay());
	auto refresh = [a = tray_mic_action, mic_label]() {
		a->setText(mic_label + (a->isChecked() ? QStringLiteral("   ●  ON") : QStringLiteral("   ○  OFF")));
	};
	refresh();
	connect(tray_mic_action, &QAction::toggled, this, [this, refresh](bool on) { setMicOverlay(on); refresh(); });

	// รายชื่อตรงกับ WebcamOverlay.fx / SettingsDialog
	const QStringList fx_names = {
		tr("None"), tr("Sunglasses"), tr("Mustache"), tr("Clown nose"), tr("Crown"), tr("Bane mask"),
		tr("Party (glasses + mustache + crown)"), tr("Samurai mask"), tr("Ninja"), tr("Ghost (Tsushima)"),
		tr("Samurai armor (kabuto + mask)"), tr("Samurai armor (photo)"), tr("Jin mask (private)"), tr("Jin mask + headband (private)")};
	tray_fx_menu = makeSubmenu(QStringLiteral(":/icons/menu/cam.svg"), tr("Facecam effect"));
	auto *fx_group = new QActionGroup(tray_fx_menu);
	for (int i = 0; i < fx_names.size(); i++) {
		QAction *a = tray_fx_menu->addAction(fx_names[i]);
		a->setCheckable(true);
		a->setData(i);
		fx_group->addAction(a);
		connect(a, &QAction::triggered, this, [this, i]() { setCamFx(i); });
	}
	const QStringList bg_names = {tr("Keep"), tr("Remove green screen"), tr("Remove blue screen"), tr("AI remove (no green screen)")};
	tray_bg_menu = makeSubmenu(QStringLiteral(":/icons/menu/cam.svg"), tr("Facecam background"));
	auto *bg_group = new QActionGroup(tray_bg_menu);
	for (int i = 0; i < bg_names.size(); i++) {
		QAction *a = tray_bg_menu->addAction(bg_names[i]);
		a->setCheckable(true);
		a->setData(i);
		bg_group->addAction(a);
		connect(a, &QAction::triggered, this, [this, i]() { setCamBackground(i); });
	}

	// ---- ไมค์: mute/unmute + เลือกอุปกรณ์ · อัดคลิป
	menu->addSeparator();
	tray_mute_action = menu->addAction(QIcon(QStringLiteral(":/icons/menu/mic.svg")), tr("Microphone"));
	connect(tray_mute_action, &QAction::triggered, this, [this]() {
		if (session)
			session->SetMuted(!session->GetMuted());
		pswrapRefreshTray();
	});
	tray_mic_menu = makeSubmenu(QStringLiteral(":/icons/menu/mic.svg"), tr("Microphone device"));
	auto rebuildMicMenu = [this]() {
		tray_mic_menu->clear();
		QmlSettings *qs = backend ? backend->qmlSettings() : nullptr;
		const QString current = session ? session->GetAudioInDevice() : settings->GetAudioInDevice();
		auto *group = new QActionGroup(tray_mic_menu);
		QStringList names = {QString()};
		if (qs)
			names += qs->availableAudioInDevices();
		if (!current.isEmpty() && !names.contains(current))
			names << current; // อุปกรณ์ที่เลือกไว้แต่ถอดอยู่ — ยังให้เห็นว่าเลือกตัวไหน
		for (const QString &name : names) {
			QAction *a = tray_mic_menu->addAction(name.isEmpty() ? tr("Auto (Windows default)") : name);
			a->setCheckable(true);
			a->setChecked(name == current);
			group->addAction(a);
			connect(a, &QAction::triggered, this, [this, name]() {
				if (QmlSettings *qs2 = backend ? backend->qmlSettings() : nullptr)
					qs2->setAudioInDevice(name); // จำไว้ใช้สตรีมครั้งหน้า + หน้า Settings เห็นค่าเดียวกัน
				else
					settings->SetAudioInDevice(name);
				if (session)
					session->SetAudioInDevice(name); // สลับทันทีถ้ากำลังสตรีม
			});
		}
	};
	rebuildMicMenu();
	connect(tray_mic_menu, &QMenu::aboutToShow, this, [this, rebuildMicMenu]() {
		rebuildMicMenu();
		if (backend && backend->qmlSettings())
			backend->qmlSettings()->refreshAudioDevices();
	});
	if (backend && backend->qmlSettings()) {
		connect(backend->qmlSettings(), &QmlSettings::audioDevicesChanged, this, rebuildMicMenu);
		connect(backend->qmlSettings(), &QmlSettings::audioInDeviceChanged, this, rebuildMicMenu);
		backend->qmlSettings()->refreshAudioDevices();
	}

	tray_record_action = menu->addAction(QIcon(QStringLiteral(":/icons/menu/record.svg")), tr("Start recording"));
	connect(tray_record_action, &QAction::triggered, this, [this]() { toggleRecording(); });
	// Instant Replay / Screenshot — ใช้ API เดียวกับเมนูสตรีมและ hotkey
	tray_replay_action = menu->addAction(QIcon(QStringLiteral(":/icons/menu/replay.svg")), tr("Instant Replay"));
	tray_replay_action->setCheckable(true);
	connect(tray_replay_action, &QAction::triggered, this, [this](bool on) { setReplayEnabled(on); pswrapRefreshTray(); });
	tray_save_replay_action = menu->addAction(QIcon(QStringLiteral(":/icons/menu/save.svg")), tr("Save replay"));
	connect(tray_save_replay_action, &QAction::triggered, this, [this]() { saveReplay(); });
	tray_shot_action = menu->addAction(QIcon(QStringLiteral(":/icons/menu/screenshot.svg")), tr("Screenshot"));
	connect(tray_shot_action, &QAction::triggered, this, [this]() { takeScreenshot(); });
	// Go Live — เริ่ม/หยุดไลฟ์ไปทุกปลายทางที่เปิดไว้ใน Settings › Go Live (ข้อความสถานะต่อท้าย)
	tray_live_action = menu->addAction(QIcon(QStringLiteral(":/icons/menu/live.svg")), tr("Go Live"));
	connect(tray_live_action, &QAction::triggered, this, [this]() { goLive()->toggle(); pswrapRefreshTray(); });
	if (pswrap_recorder)
		connect(pswrap_recorder, &PsWrapRecorder::replayActiveChanged, this, [this]() { pswrapRefreshTray(); });
	connect(this, &QmlMainWindow::replayEnabledChanged, this, [this]() { pswrapRefreshTray(); });
	connect(menu, &QMenu::aboutToShow, this, [this]() { pswrapRefreshTray(); });
	pswrapRefreshTray();
}

void QmlMainWindow::pswrapRefreshTray()
{
	{
		const bool streaming = session && stream_session_active.loadAcquire() != 0;
		if (tray_replay_action) {
			const bool on = replayEnabled();
			tray_replay_action->setChecked(on);
			tray_replay_action->setText(tr("Instant Replay") + (on ? QStringLiteral("   ●  ON") : QStringLiteral("   ○  OFF")));
		}
		if (tray_save_replay_action)
			tray_save_replay_action->setEnabled(pswrap_recorder && pswrap_recorder->isReplayActive());
		if (tray_shot_action)
			tray_shot_action->setEnabled(streaming);
		if (tray_live_action) {
			PsWrapGoLive *g = goLive();
			if (g->isLive()) {
				tray_live_action->setEnabled(true);
				tray_live_action->setText(tr("Stop live") + QStringLiteral("   ●  ") + (g->summary().isEmpty() ? tr("LIVE") : g->summary()));
			} else {
				tray_live_action->setEnabled(streaming && g->enabledCount() > 0);
				tray_live_action->setText(g->enabledCount() > 0 ? tr("Go Live") : tr("Go Live") + QStringLiteral("   ") + tr("(add a destination in Settings)"));
			}
		}
	}
	if (tray_mute_action) {
		const bool live = session && stream_session_active.loadAcquire() != 0;
		tray_mute_action->setEnabled(live);
		if (!live)
			tray_mute_action->setText(tr("Microphone") + QStringLiteral("   ") + tr("(not streaming)"));
		else if (session->GetMuted())
			tray_mute_action->setText(tr("Unmute microphone") + QStringLiteral("   ○  MUTED"));
		else
			tray_mute_action->setText(tr("Mute microphone") + QStringLiteral("   ●  LIVE"));
	}
	if (tray_record_action && pswrap_recorder) {
		tray_record_action->setText(pswrap_recorder->isRecording() ? tr("Stop recording") + QStringLiteral("   ●  REC") : tr("Start recording"));
		tray_record_action->setEnabled(!pswrap_recorder->isBusy());
	}
	const int fx = camFx(), bg = camBackground();
	if (tray_fx_menu)
		for (QAction *a : tray_fx_menu->actions())
			a->setChecked(a->data().toInt() == fx);
	if (tray_bg_menu)
		for (QAction *a : tray_bg_menu->actions())
			a->setChecked(a->data().toInt() == bg);
}

int QmlMainWindow::camFx() const { return qBound(0, settings->GetCamFx(), 13); }
void QmlMainWindow::setCamFx(int fx)
{
	fx = qBound(0, fx, 13);
	if (fx == settings->GetCamFx())
		return;
	settings->SetCamFx(fx);
	pswrapRefreshTray();
	emit camFxChanged();
}

int QmlMainWindow::camBackground() const { return qBound(0, settings->GetCamKey(), 3); }
void QmlMainWindow::setCamBackground(int mode)
{
	mode = qBound(0, mode, 3);
	if (mode == settings->GetCamKey())
		return;
	settings->SetCamKey(mode);
	pswrapRefreshTray();
	emit camBackgroundChanged();
}

void QmlMainWindow::pswrapRecordCapture(const pl_frame_mix *mix, const pl_frame *single, const pl_render_params &params,
                                        const pl_frame &screen_target, const pl_overlay *overlay, const pl_frame *hint)
{
	Q_ASSERT(QThread::currentThread() == render_thread);
	if (hint)
		PsWrapRecCapture::noteSource(hint);
	PsWrapRecorder *rec = PsWrapRecorder::instance();
	if (!rec || !rec->isCapturing()) {   // อัดไฟล์ หรือ Instant Replay (pipeline เดียวกัน)
		if (pswrap_rec_capture && pswrap_rec_capture->canDestroy()) {
			delete pswrap_rec_capture;
			pswrap_rec_capture = nullptr;
		}
		return;
	}
	if (!pswrap_rec_capture) {
		pl_gpu gpu = placeboGpu();
		if (!gpu)
			return;
		pswrap_rec_capture = new PsWrapRecCapture(gpu, placebo_log);
	}
	pswrap_rec_capture->capture(rec, mix, single, params, screen_target, overlay,
	                            swapchain_size.width(), swapchain_size.height());
}
