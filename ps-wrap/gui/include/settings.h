// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SETTINGS_H
#define CHIAKI_SETTINGS_H

#include <chiaki/session.h>

#include "host.h"

#include <QSettings>
#include <QList>
#include <QMap>
#include <QRect>

enum class ControllerButtonExt
{
	// must not overlap with ChiakiControllerButton and ChiakiControllerAnalogButton
	ANALOG_STICK_LEFT_X_UP = (1 << 18),
	ANALOG_STICK_LEFT_X_DOWN = (1 << 19),
	ANALOG_STICK_LEFT_Y_UP = (1 << 20),
	ANALOG_STICK_LEFT_Y_DOWN = (1 << 21),
	ANALOG_STICK_RIGHT_X_UP = (1 << 22),
	ANALOG_STICK_RIGHT_X_DOWN = (1 << 23),
	ANALOG_STICK_RIGHT_Y_UP = (1 << 24),
	ANALOG_STICK_RIGHT_Y_DOWN = (1 << 25),
	ANALOG_STICK_LEFT_X = (1 << 26),
	ANALOG_STICK_LEFT_Y = (1 << 27),
	ANALOG_STICK_RIGHT_X = (1 << 28),
	ANALOG_STICK_RIGHT_Y = (1 << 29),
	MISC1 = (1 << 30),
};

enum class RumbleHapticsIntensity
{
	Off,
	VeryWeak,
	Weak,
	Normal,
	Strong,
	VeryStrong
};

enum class DisconnectAction
{
	AlwaysNothing,
	AlwaysSleep,
	Ask
};

enum class SuspendAction
{
	Nothing,
	Sleep,
};

enum class Decoder
{
	Ffmpeg,
	Pi
};

enum class PlaceboPreset {
	Fast,
	Default,
	HighQuality,
	HighQualitySpatial,
	HighQualityAdvancedSpatial,
	Custom
};

enum class RenderBackend {
	Vulkan,
	OpenGL
};

enum class WindowType {
	SelectedResolution,
	CustomResolution,
	AdjustableResolution,
	Fullscreen,
	Zoom,
	Stretch
};

enum class PlaceboUpscaler {
	None,
	Nearest,
	Bilinear,
	Oversample,
	Bicubic,
	Gaussian,
	CatmullRom,
	Lanczos,
	EwaLanczos,
	EwaLanczosSharp,
	EwaLanczos4Sharpest,
	FSR,
	FSRCNNX8,
	FSRCNNX16
};

enum class PlaceboDownscaler {
	None,
	Box,
	Hermite,
	Bilinear,
	Bicubic,
	Gaussian,
	CatmullRom,
	Mitchell,
	Lanczos
};

enum class PlaceboFrameMixer {
	None,
	Oversample,
	Hermite,
	Linear,
	Cubic
};

enum class PlaceboDeinterlacePreset {
	Default,
};

enum class PlaceboDeinterlaceAlgorithm {
	Weave,
	Bob,
	Yadif,
	Bwdif,
};

enum class PlaceboDebandPreset {
	None,
	Default
};

enum class PlaceboSigmoidPreset {
	None,
	Default
};

enum class PlaceboColorAdjustmentPreset {
	None,
	Neutral
};

enum class PlaceboPeakDetectionPreset {
	None,
	Default,
	HighQuality
};

enum class PlaceboColorMappingPreset {
	None,
	Default,
	HighQuality
};

enum class PlaceboGamutMappingFunction {
	Clip,
	Perceptual,
	SoftClip,
	Relative,
	Saturation,
	Absolute,
	Desaturate,
	Darken,
	Highlight,
	Linear
};

enum class PlaceboToneMappingFunction {
	Clip,
	Spline,
	St209440,
	St209410,
	Bt2390,
	Bt2446a,
	Reinhard,
	Mobius,
	Hable,
	Gamma,
	Linear,
	LinearLight
};

enum class PlaceboToneMappingMetadata {
	Any,
	None,
	Hdr10,
	Hdr10Plus,
	CieY
};

class Settings : public QObject
{
	Q_OBJECT

	private:
		QSettings settings;
		QSettings default_settings;
		QSettings placebo_settings;
		QString time_format;
		QMap<HostMAC, HiddenHost> hidden_hosts;

		QMap<HostMAC, RegisteredHost> registered_hosts;
		QMap<QString, RegisteredHost> nickname_registered_hosts;
		QMap<QString, QString> controller_mappings;
		QList<QString> profiles;
		size_t ps4s_registered;
		QMap<int, ManualHost> manual_hosts;
		int manual_hosts_id_next;

		void LoadRegisteredHosts(QSettings *qsettings = nullptr);
		void SaveRegisteredHosts(QSettings *qsettings = nullptr);

		void LoadHiddenHosts(QSettings *qsettings = nullptr);
		void SaveHiddenHosts(QSettings *qsettings = nullptr);

		void LoadManualHosts(QSettings *qsettings = nullptr);
		void SaveManualHosts(QSettings *qsettings = nullptr);

		void LoadControllerMappings(QSettings *qsettings = nullptr);
		void SaveControllerMappings(QSettings *qsettings = nullptr);

		void LoadProfiles();
		void SaveProfiles();

	public:
		explicit Settings(const QString &conf, QObject *parent = nullptr);

		void ExportSettings(QString filepath);
		void ImportSettings(QString filepath);

		void ExportPlaceboSettings(QString filepath);
		void ImportPlaceboSettings(QString filepath);

		QMap<QString, QString> GetPlaceboValues();

		ChiakiDisableAudioVideo GetAudioVideoDisabled() const       { return static_cast<ChiakiDisableAudioVideo>(settings.value("settings/audio_video_disabled", 0).toInt()); }
		void SetAudioVideoDisabled(ChiakiDisableAudioVideo disabled) { settings.setValue("settings/audio_video_disabled", disabled); }

		bool GetDiscoveryEnabled() const		{ return settings.value("settings/auto_discovery", true).toBool(); }
		void SetDiscoveryEnabled(bool enabled)	{ settings.setValue("settings/auto_discovery", enabled); }

		QRect GetGeometry() const;
		void SetGeometry(QRect geometry);
		// PS-WRAP: จำสถานะ maximize ของหน้าต่างหลักแยกจาก geometry (upstream เซฟ geometry ตอน maximize ทับค่าปกติ)
		// PS-WRAP: hide to tray / always on top
		bool GetHideToTray() const { return settings.value("settings/pswrap_hide_to_tray", false).toBool(); }
		void SetHideToTray(bool v) { settings.setValue("settings/pswrap_hide_to_tray", v); }
		// PS-WRAP: overlay toggles (key เดียวกับที่ QML Settings{category:"pswrap"} เคยเขียน — ค่าเก่าใช้ต่อได้)
		bool GetPadOverlay() const { return settings.value("pswrap/controllerOverlay", true).toBool(); }
		void SetPadOverlay(bool v) { settings.setValue("pswrap/controllerOverlay", v); }
		bool GetCamOverlay() const { return settings.value("pswrap/webcam", false).toBool(); }
		void SetCamOverlay(bool v) { settings.setValue("pswrap/webcam", v); }
		int GetCamFx() const { return settings.value("pswrap/camFx", 0).toInt(); }   // PS-WRAP: เอฟเฟกต์ facecam (key เดียวกับ QML เดิม)
		void SetCamFx(int v) { settings.setValue("pswrap/camFx", v); }
		int GetCamKey() const { return settings.value("pswrap/camKey", 0).toInt(); }   // 0 keep · 1 green · 2 blue · 3 AI
		void SetCamKey(int v) { settings.setValue("pswrap/camKey", v); }
		bool GetMicOverlay() const { return settings.value("pswrap/micOverlay", false).toBool(); }   // PS-WRAP: spectrum ไมค์
		void SetMicOverlay(bool v) { settings.setValue("pswrap/micOverlay", v); }
		bool GetClockOverlay() const { return settings.value("pswrap/clockOverlay", false).toBool(); }   // PS-WRAP: นาฬิกา + เวลาเล่น
		void SetClockOverlay(bool v) { settings.setValue("pswrap/clockOverlay", v); }
		// PS-WRAP: ภาพสวยขึ้นตอนสตรีม (qmlmainwindow_pswrapvisual.cpp) — ค่าเริ่มต้นปิดทั้งหมด
		bool GetAmbientLight() const { return settings.value("pswrap/ambientLight", false).toBool(); }   // ขอบว่างรอบภาพ = แสงเบลอจากขอบเกม
		void SetAmbientLight(bool v) { settings.setValue("pswrap/ambientLight", v); }
		bool GetFrameGen() const { return settings.value("pswrap/frameGen", false).toBool(); }           // เฟรมกลางระหว่างเฟรมจริง (60 → 120)
		void SetFrameGen(bool v) { settings.setValue("pswrap/frameGen", v); }
		bool GetLightbarHalo() const { return settings.value("pswrap/lightbarHalo", false).toBool(); }   // แสงเรืองขอบจอตามสีไฟจอย
		void SetLightbarHalo(bool v) { settings.setValue("pswrap/lightbarHalo", v); }
		// PS-WRAP: แชทไลฟ์บนจอ (pswrapchat.cpp) — แหล่ง YouTube (@handle / channel / ลิงก์) + ช่อง Twitch · API key อยู่ Credential Manager
		bool GetChatOverlay() const { return settings.value("pswrap/chatOverlay", false).toBool(); }
		void SetChatOverlay(bool v) { settings.setValue("pswrap/chatOverlay", v); }
		QString GetChatYouTube() const { return settings.value("pswrap/chatYouTube").toString(); }
		void SetChatYouTube(const QString &v) { settings.setValue("pswrap/chatYouTube", v); }
		QString GetChatTwitch() const { return settings.value("pswrap/chatTwitch").toString(); }
		void SetChatTwitch(const QString &v) { settings.setValue("pswrap/chatTwitch", v); }
		// PS-WRAP: ไมค์ — เพิ่มเสียง (dB) + noise gate (ดู PsWrapVoiceProc::setDynamics)
		double GetMicGainDb() const { return settings.value("pswrap/micGainDb", 0.0).toDouble(); }
		void SetMicGainDb(double v) { settings.setValue("pswrap/micGainDb", v); }
		bool GetMicGateEnabled() const { return settings.value("pswrap/micGateEnabled", false).toBool(); }
		void SetMicGateEnabled(bool v) { settings.setValue("pswrap/micGateEnabled", v); }
		double GetMicGateThresholdDb() const { return settings.value("pswrap/micGateThresholdDb", -50.0).toDouble(); }
		void SetMicGateThresholdDb(double v) { settings.setValue("pswrap/micGateThresholdDb", v); }
		QString GetRecordingFolder() const { return settings.value("pswrap/recordingFolder").toString(); }   // ว่าง = <Videos>/PS-WRAP
		void SetRecordingFolder(const QString &v) { settings.setValue("pswrap/recordingFolder", v); }
		// PS-WRAP: ความสูงภาพของคลิป/Replay/ไลฟ์ (16:9) · 0 = เท่าความละเอียดสตรีม · 1440 / 2160 = upscale
		int GetCaptureHeight() const { const int h = settings.value("pswrap/captureHeight", 0).toInt(); return h == 1440 || h == 2160 ? h : 0; }
		void SetCaptureHeight(int v) { settings.setValue("pswrap/captureHeight", v == 1440 || v == 2160 ? v : 0); }
		// PS-WRAP: ภาพแนวตั้ง 9:16 — เลย์เอาต์ (0 Split · 1 Center · 2 Blur) + ตำแหน่งตัดแนวนอน 0..1
		int GetVerticalLayout() const { return qBound(0, settings.value("pswrap/verticalLayout", 0).toInt(), 2); }
		void SetVerticalLayout(int v) { settings.setValue("pswrap/verticalLayout", qBound(0, v, 2)); }
		double GetVerticalCropX() const { return qBound(0.0, settings.value("pswrap/verticalCropX", 0.5).toDouble(), 1.0); }
		void SetVerticalCropX(double v) { settings.setValue("pswrap/verticalCropX", qBound(0.0, v, 1.0)); }
		// ตำแหน่ง facecam ที่ลากเองต่อเลย์เอาต์ "cx,cy,w" (สัดส่วน canvas) · ว่าง = ค่าเริ่มต้น
		QString GetVerticalCam(int mode) const { return settings.value(QStringLiteral("pswrap/verticalCam%1").arg(mode)).toString(); }
		void SetVerticalCam(int mode, const QString &v) { if (v.isEmpty()) settings.remove(QStringLiteral("pswrap/verticalCam%1").arg(mode)); else settings.setValue(QStringLiteral("pswrap/verticalCam%1").arg(mode), v); }
		// รูป/GIF ที่วางในภาพแนวตั้งต่อเลย์เอาต์ (JSON array — pswrapverticallayers.cpp) · การ์ดแชทในภาพแนวตั้ง + ตำแหน่งต่อเลย์เอาต์ "cx,cy,w"
		QString GetVerticalLayers(int mode) const { return settings.value(QStringLiteral("pswrap/verticalLayers%1").arg(mode)).toString(); }
		void SetVerticalLayers(int mode, const QString &v) { if (v.isEmpty()) settings.remove(QStringLiteral("pswrap/verticalLayers%1").arg(mode)); else settings.setValue(QStringLiteral("pswrap/verticalLayers%1").arg(mode), v); }
		bool GetVerticalChat() const { return settings.value("pswrap/verticalChat", false).toBool(); }
		void SetVerticalChat(bool v) { settings.setValue("pswrap/verticalChat", v); }
		// พื้นที่การ์ดแชทในภาพแนวตั้งต่อเลย์เอาต์ "x,y,w,h" (สัดส่วน canvas) · ว่าง = ค่าเริ่มต้น (เต็มพื้นที่ว่างใต้เกม)
		QString GetVerticalChatArea(int mode) const { return settings.value(QStringLiteral("pswrap/verticalChatArea%1").arg(mode)).toString(); }
		void SetVerticalChatArea(int mode, const QString &v) { if (v.isEmpty()) settings.remove(QStringLiteral("pswrap/verticalChatArea%1").arg(mode)); else settings.setValue(QStringLiteral("pswrap/verticalChatArea%1").arg(mode), v); }
		// PS-WRAP: Instant Replay — เก็บ N วินาทีล่าสุดไว้ในแรมระหว่างสตรีม (ดู PsWrapRecorder::startReplay)
		bool GetReplayEnabled() const { return settings.value("pswrap/replayEnabled", false).toBool(); }
		void SetReplayEnabled(bool v) { settings.setValue("pswrap/replayEnabled", v); }
		int GetReplaySeconds() const { return qBound(30, settings.value("pswrap/replaySeconds", 60).toInt(), 120); }
		void SetReplaySeconds(int v) { settings.setValue("pswrap/replaySeconds", qBound(30, v, 120)); }
		// PS-WRAP: preset รายเกม (JSON — ดู PsWrapGameProfiles)
		QString GetPsWrapGameProfiles() const { return settings.value("pswrap/gameProfiles").toString(); }
		void SetPsWrapGameProfiles(const QString &json) { settings.setValue("pswrap/gameProfiles", json); }
		// PS-WRAP: Go Live — ปลายทาง (JSON array, ไม่มี stream key — key อยู่ Credential Manager · ดู PsWrapGoLive)
		QString GetPsWrapLiveDestinations() const { return settings.value("pswrap_live/destinations").toString(); }
		void SetPsWrapLiveDestinations(const QString &json) { settings.setValue("pswrap_live/destinations", json); }
		bool GetAlwaysOnTop() const { return settings.value("settings/pswrap_always_on_top", false).toBool(); }
		void SetAlwaysOnTop(bool v) { settings.setValue("settings/pswrap_always_on_top", v); }
		bool GetWindowMaximized() const { return settings.value("settings/window_maximized", false).toBool(); }
		void SetWindowMaximized(bool maximized) { settings.setValue("settings/window_maximized", maximized); }

		QRect GetStreamGeometry() const;
		void SetStreamGeometry(QRect geometry);

		bool GetRemotePlayAsk() const           { return settings.value("settings/remote_play_ask", true).toBool(); }
		void SetRemotePlayAsk(bool asked)       { settings.setValue("settings/remote_play_ask", asked); }

		bool GetAddSteamShortcutAsk() const           { return settings.value("settings/add_steam_shortcut_ask", true).toBool(); }
		void SetAddSteamShortcutAsk(bool asked)       { settings.setValue("settings/add_steam_shortcut_ask", asked); }

		bool GetLogVerbose() const 				{ return settings.value("settings/log_verbose", false).toBool(); }
		void SetLogVerbose(bool enabled)		{ settings.setValue("settings/log_verbose", enabled); }
		bool GetLogSanitize() const            { return settings.value("settings/log_sanitize", true).toBool(); }
		void SetLogSanitize(bool enabled)      { settings.setValue("settings/log_sanitize", enabled); }
		bool GetVSyncEnabled() const           { return settings.value("settings/vsync", false).toBool(); }
		void SetVSyncEnabled(bool enabled)     { settings.setValue("settings/vsync", enabled); }
		uint32_t GetLogLevelMask();

		bool GetHideCursor() const				{ return settings.value("settings/hide_cursor", true).toBool(); }
		void SetHideCursor(bool enabled)		{ settings.setValue("settings/hide_cursor", enabled); }

		RumbleHapticsIntensity GetRumbleHapticsIntensity() const;
		void SetRumbleHapticsIntensity(RumbleHapticsIntensity intensity);

		bool GetShowStreamStats() const            { return settings.value("settings/show_stream_stats", false).toBool(); }
		void SetShowStreamStats(bool enabled)      { settings.setValue("settings/show_stream_stats", enabled); }

		bool GetStreamerMode() const		{ return settings.value("settings/streamer_mode", false).toBool(); }
		void SetStreamerMode(bool enabled)	{ settings.setValue("settings/streamer_mode", enabled); }

		bool GetButtonsByPosition() const 		{ return settings.value("settings/buttons_by_pos", false).toBool(); }
		void SetButtonsByPosition(bool enabled) { settings.setValue("settings/buttons_by_pos", enabled); }

		bool GetAllowJoystickBackgroundEvents() const { return settings.value("settings/allow_joystick_background_events", true).toBool(); }
		void SetAllowJoystickBackgroundEvents(bool enabled) { settings.setValue("settings/allow_joystick_background_events", enabled); }

		bool GetStartMicUnmuted() const          { return settings.value("settings/start_mic_unmuted", false).toBool(); }
		void SetStartMicUnmuted(bool unmuted) { return settings.setValue("settings/start_mic_unmuted", unmuted); }

#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
		bool GetVerticalDeckEnabled() const       { return settings.value("settings/gyro_inverted", false).toBool(); }
		void SetVerticalDeckEnabled(bool enabled) { settings.setValue("settings/gyro_inverted", enabled); }

		bool GetSteamDeckHapticsEnabled() const   { return settings.value("settings/steamdeck_haptics", false).toBool(); }
		void SetSteamDeckHapticsEnabled(bool enabled) { settings.setValue("settings/steamdeck_haptics", enabled); }
#endif

		bool GetAutomaticConnect() const         { return settings.value("settings/automatic_connect", false).toBool(); }
		void SetAutomaticConnect(bool autoconnect)    { settings.setValue("settings/automatic_connect", autoconnect); }

		bool GetFullscreenDoubleClickEnabled() const	   { return settings.value("settings/fullscreen_doubleclick", false).toBool(); }
		void SetFullscreenDoubleClickEnabled(bool enabled) { settings.setValue("settings/fullscreen_doubleclick", enabled); }

		bool GetIDROnFECFailureEnabled() const	   { return settings.value("settings/idr_on_fec_failure", false).toBool(); }
		void SetIDROnFECFailureEnabled(bool enabled) { settings.setValue("settings/idr_on_fec_failure", enabled); }

		float GetHapticOverride() const 			{ return settings.value("settings/haptic_override", 1.0).toFloat(); }
		void SetHapticOverride(float override)	{ settings.setValue("settings/haptic_override", override); }

		ChiakiVideoResolutionPreset GetResolutionLocalPS4() const;
		ChiakiVideoResolutionPreset GetResolutionRemotePS4() const;
		ChiakiVideoResolutionPreset GetResolutionLocalPS5() const;
		ChiakiVideoResolutionPreset GetResolutionRemotePS5() const;
		void SetResolutionLocalPS4(ChiakiVideoResolutionPreset resolution);
		void SetResolutionRemotePS4(ChiakiVideoResolutionPreset resolution);
		void SetResolutionLocalPS5(ChiakiVideoResolutionPreset resolution);
		void SetResolutionRemotePS5(ChiakiVideoResolutionPreset resolution);

		/**
		 * @return 0 if set to "automatic"
		 */
		ChiakiVideoFPSPreset GetFPSLocalPS4() const;
		ChiakiVideoFPSPreset GetFPSRemotePS4() const;
		ChiakiVideoFPSPreset GetFPSLocalPS5() const;
		ChiakiVideoFPSPreset GetFPSRemotePS5() const;
		void SetFPSLocalPS4(ChiakiVideoFPSPreset fps);
		void SetFPSRemotePS4(ChiakiVideoFPSPreset fps);
		void SetFPSLocalPS5(ChiakiVideoFPSPreset fps);
		void SetFPSRemotePS5(ChiakiVideoFPSPreset fps);

		unsigned int GetBitrateLocalPS4() const;
		unsigned int GetBitrateRemotePS4() const;
		unsigned int GetBitrateLocalPS5() const;
		unsigned int GetBitrateRemotePS5() const;
		void SetBitrateLocalPS4(unsigned int bitrate);
		void SetBitrateRemotePS4(unsigned int bitrate);
		void SetBitrateLocalPS5(unsigned int bitrate);
		void SetBitrateRemotePS5(unsigned int bitrate);

		ChiakiCodec GetCodecPS4() const;
		ChiakiCodec GetCodecLocalPS5() const;
		ChiakiCodec GetCodecRemotePS5() const;
		void SetCodecPS4(ChiakiCodec codec);
		void SetCodecLocalPS5(ChiakiCodec codec);
		void SetCodecRemotePS5(ChiakiCodec codec);

		int GetDisplayTargetContrast() const;
		void SetDisplayTargetContrast(int contrast);

		int GetDisplayTargetPeak() const;
		void SetDisplayTargetPeak(int peak);

		int GetDisplayTargetTrc() const;
		void SetDisplayTargetTrc(int trc);

		int GetDisplayTargetPrim() const;
		void SetDisplayTargetPrim(int prim);

		Decoder GetDecoder() const;
		void SetDecoder(Decoder decoder);

		QString GetHardwareDecoder() const;
		void SetHardwareDecoder(const QString &hw_decoder);
		bool GetUseZeroCopy() const { return settings.value("settings/use_zero_copy", true).toBool(); }
		void SetUseZeroCopy(bool enabled) { settings.setValue("settings/use_zero_copy", enabled); }
		bool GetVulkanDeferredSwap() const { return settings.value("settings/vulkan_deferred_swap", false).toBool(); }
		void SetVulkanDeferredSwap(bool enabled) { settings.setValue("settings/vulkan_deferred_swap", enabled); }

		WindowType GetWindowType() const;
		void SetWindowType(WindowType type);

		uint GetCustomResolutionWidth() const;
		void SetCustomResolutionWidth(uint width);

		uint GetCustomResolutionHeight() const;
		void SetCustomResolutionHeight(uint length);

		PlaceboPreset GetPlaceboPreset() const;
		void SetPlaceboPreset(PlaceboPreset preset);

		RenderBackend GetRenderBackend() const;
		void SetRenderBackend(RenderBackend backend);
		void Sync();

		float GetZoomFactor() const;
		void SetZoomFactor(float factor);

		float GetPacketLossReportedMax() const;
		void SetPacketLossReportedMax(float reported_max);

		RegisteredHost GetAutoConnectHost() const;
		void SetAutoConnectHost(const QByteArray &mac);

		int GetAudioVolume() const;
		void SetAudioVolume(int volume);

		unsigned int GetAudioBufferSizeDefault() const;

		/**
		 * @return 0 if set to "automatic"
		 */
		unsigned int GetAudioBufferSizeRaw() const;

		/**
		 * @return actual size to be used, default value if GetAudioBufferSizeRaw() would return 0
		 */
		unsigned int GetAudioBufferSize() const;
		void SetAudioBufferSize(unsigned int size);
		
		QString GetAudioOutDevice() const;
		void SetAudioOutDevice(QString device_name);

		QString GetAudioInDevice() const;
		void SetAudioInDevice(QString device_name);

		uint GetWifiDroppedNotif() const;
		void SetWifiDroppedNotif(uint percent);

		bool GetPortGuessingEnabled() const;
		void SetPortGuessingEnabled(bool enabled);
		int GetPortGuessCount() const;
		void SetPortGuessCount(int count);
		int GetPortGuessSocketCount() const;
		void SetPortGuessSocketCount(int count);

		QString GetPsnAuthToken() const;
		void SetPsnAuthToken(QString auth_token);

		QString GetPsnRefreshToken() const;
		void SetPsnRefreshToken(QString refresh_token);

		QString GetPsnAuthTokenExpiry() const;
		void SetPsnAuthTokenExpiry(QString expiry_date);

		QString GetCurrentProfile() const;
		void SetCurrentProfile(QString profile);

		bool GetKeyboardEnabled() const;
		void SetKeyboardEnabled(bool enabled);

		bool GetMouseTouchEnabled() const;
		void SetMouseTouchEnabled(bool enabled);

		bool GetDpadTouchEnabled() const;
		void SetDpadTouchEnabled(bool enabled);

		uint16_t GetDpadTouchIncrement() const;
		void SetDpadTouchIncrement(uint16_t increment);

		uint GetDpadTouchShortcut1() const;
		void SetDpadTouchShortcut1(uint button);

		uint GetDpadTouchShortcut2() const;
		void SetDpadTouchShortcut2(uint button);

		uint GetDpadTouchShortcut3() const;
		void SetDpadTouchShortcut3(uint button);

		uint GetDpadTouchShortcut4() const;
		void SetDpadTouchShortcut4(uint button);

		bool GetStreamMenuEnabled() const;
		void SetStreamMenuEnabled(bool enabled);

		uint GetStreamMenuShortcut1() const;
		void SetStreamMenuShortcut1(uint button);

		uint GetStreamMenuShortcut2() const;
		void SetStreamMenuShortcut2(uint button);

		uint GetStreamMenuShortcut3() const;
		void SetStreamMenuShortcut3(uint button);

		uint GetStreamMenuShortcut4() const;
		void SetStreamMenuShortcut4(uint button);

		void DeleteProfile(QString profile);

		QString GetPsnAccountId() const;
		void SetPsnAccountId(QString account_id);

		QString GetTimeFormat() const     { return time_format; }
		void ClearKeyMapping();

#if CHIAKI_GUI_ENABLE_SPEEX
		bool GetSpeechProcessingEnabled() const;
		int GetNoiseSuppressLevel() const;
		int GetEchoSuppressLevel() const;

		void SetSpeechProcessingEnabled(bool enabled);
		void SetNoiseSuppressLevel(int reduceby);
		void SetEchoSuppressLevel(int reduceby);
#endif

		ChiakiConnectVideoProfile GetVideoProfileLocalPS4();
		ChiakiConnectVideoProfile GetVideoProfileRemotePS4();
		ChiakiConnectVideoProfile GetVideoProfileLocalPS5();
		ChiakiConnectVideoProfile GetVideoProfileRemotePS5();

		DisconnectAction GetDisconnectAction();
		void SetDisconnectAction(DisconnectAction action);

		SuspendAction GetSuspendAction();
		void SetSuspendAction(SuspendAction action);

		PlaceboUpscaler GetPlaceboUpscaler() const;
		void SetPlaceboUpscaler(PlaceboUpscaler upscaler);

		PlaceboUpscaler GetPlaceboPlaneUpscaler() const;
		void SetPlaceboPlaneUpscaler(PlaceboUpscaler upscaler);
		
		PlaceboDownscaler GetPlaceboDownscaler() const;
		void SetPlaceboDownscaler(PlaceboDownscaler downscaler);

		PlaceboDownscaler GetPlaceboPlaneDownscaler() const;
		void SetPlaceboPlaneDownscaler(PlaceboDownscaler downscaler);

		PlaceboFrameMixer GetPlaceboFrameMixer() const;
		bool GetDirectFrameMapping() const { return settings.value("settings/direct_frame_mapping", true).toBool(); }
		void SetDirectFrameMapping(bool enabled) { settings.setValue("settings/direct_frame_mapping", enabled); }
		void SetPlaceboFrameMixer(PlaceboFrameMixer frame_mixer);

		bool GetPlaceboDeinterlaceEnabled() const;
		void SetPlaceboDeinterlaceEnabled(bool enabled);
		PlaceboDeinterlacePreset GetPlaceboDeinterlacePreset() const;
		void SetPlaceboDeinterlacePreset(PlaceboDeinterlacePreset preset);
		PlaceboDeinterlaceAlgorithm GetPlaceboDeinterlaceAlgorithm() const;
		void SetPlaceboDeinterlaceAlgorithm(PlaceboDeinterlaceAlgorithm algorithm);
		bool GetPlaceboDeinterlaceSkipSpatial() const;
		void SetPlaceboDeinterlaceSkipSpatial(bool skip);
		QString GetPlaceboDeinterlacePresetValue() const;
		QString GetPlaceboDeinterlaceAlgorithmValue() const;

		float GetPlaceboAntiringingStrength() const;
		void SetPlaceboAntiringingStrength(float strength);

		bool GetPlaceboDebandEnabled() const;
		void SetPlaceboDebandEnabled(bool enabled);

		PlaceboDebandPreset GetPlaceboDebandPreset() const;
		void SetPlaceboDebandPreset(PlaceboDebandPreset preset);

		int GetPlaceboDebandIterations() const;
		void SetPlaceboDebandIterations(int iterations);

		float GetPlaceboDebandThreshold() const;
		void SetPlaceboDebandThreshold(float threshold);

		float GetPlaceboDebandRadius() const;
		void SetPlaceboDebandRadius(float radius);
		
		float GetPlaceboDebandGrain() const;
		void SetPlaceboDebandGrain(float grain);
		
		bool GetPlaceboSigmoidEnabled() const;
		void SetPlaceboSigmoidEnabled(bool enabled);

		PlaceboSigmoidPreset GetPlaceboSigmoidPreset() const;
		void SetPlaceboSigmoidPreset(PlaceboSigmoidPreset preset);
		
		float GetPlaceboSigmoidCenter() const;
		void SetPlaceboSigmoidCenter(float center);

		float GetPlaceboSigmoidSlope() const;
		void SetPlaceboSigmoidSlope(float slope);

		bool GetPlaceboColorAdjustmentEnabled() const;
		void SetPlaceboColorAdjustmentEnabled(bool enabled);

		PlaceboColorAdjustmentPreset GetPlaceboColorAdjustmentPreset() const;
		void SetPlaceboColorAdjustmentPreset(PlaceboColorAdjustmentPreset preset);

		float GetPlaceboColorAdjustmentBrightness() const;
		void SetPlaceboColorAdjustmentBrightness(float brightness);

		float GetPlaceboColorAdjustmentContrast() const;
		void SetPlaceboColorAdjustmentContrast(float contrast);

		float GetPlaceboColorAdjustmentSaturation() const;
		void SetPlaceboColorAdjustmentSaturation(float saturation);

		float GetPlaceboColorAdjustmentHue() const;
		void SetPlaceboColorAdjustmentHue(float hue);

		float GetPlaceboColorAdjustmentGamma() const;
		void SetPlaceboColorAdjustmentGamma(float gamma);

		float GetPlaceboColorAdjustmentTemperature() const;
		void SetPlaceboColorAdjustmentTemperature(float temperature);

		bool GetPlaceboPeakDetectionEnabled() const;
		void SetPlaceboPeakDetectionEnabled(bool enabled);

		PlaceboPeakDetectionPreset GetPlaceboPeakDetectionPreset() const;
		void SetPlaceboPeakDetectionPreset(PlaceboPeakDetectionPreset preset);

		float GetPlaceboPeakDetectionPeakSmoothingPeriod() const;
		void SetPlaceboPeakDetectionPeakSmoothingPeriod(float period);

		float GetPlaceboPeakDetectionSceneThresholdLow() const;
		void SetPlaceboPeakDetectionSceneThresholdLow(float threshold_low);

		float GetPlaceboPeakDetectionSceneThresholdHigh() const;
		void SetPlaceboPeakDetectionSceneThresholdHigh(float threshold_high);

		float GetPlaceboPeakDetectionPeakPercentile() const;
		void SetPlaceboPeakDetectionPeakPercentile(float percentile);

		float GetPlaceboPeakDetectionBlackCutoff() const;
		void SetPlaceboPeakDetectionBlackCutoff(float cutoff);

		bool GetPlaceboPeakDetectionAllowDelayedPeak() const;
		void SetPlaceboPeakDetectionAllowDelayedPeak(bool allowed);

		bool GetPlaceboColorMappingEnabled() const;
		void SetPlaceboColorMappingEnabled(bool enabled);

		PlaceboColorMappingPreset GetPlaceboColorMappingPreset() const;
		void SetPlaceboColorMappingPreset(PlaceboColorMappingPreset preset);

		PlaceboGamutMappingFunction GetPlaceboGamutMappingFunction() const;
		void SetPlaceboGamutMappingFunction(PlaceboGamutMappingFunction function);

		float GetPlaceboGamutMappingPerceptualDeadzone() const;
		void SetPlaceboGamutMappingPerceptualDeadzone(float deadzone);

		float GetPlaceboGamutMappingPerceptualStrength() const;
		void SetPlaceboGamutMappingPerceptualStrength(float strength);

		float GetPlaceboGamutMappingColorimetricGamma() const;
		void SetPlaceboGamutMappingColorimetricGamma(float gamma);

		float GetPlaceboGamutMappingSoftClipKnee() const;
		void SetPlaceboGamutMappingSoftClipKnee(float knee);

		float GetPlaceboGamutMappingSoftClipDesat() const;
		void SetPlaceboGamutMappingSoftClipDesat(float strength);

		int GetPlaceboGamutMappingLut3dSizeI() const;
		void SetPlaceboGamutMappingLut3dSizeI(int size);

		int GetPlaceboGamutMappingLut3dSizeC() const;
		void SetPlaceboGamutMappingLut3dSizeC(int size);

		int GetPlaceboGamutMappingLut3dSizeH() const;
		void SetPlaceboGamutMappingLut3dSizeH(int size);

		bool GetPlaceboGamutMappingLut3dTricubicEnabled() const;
		void SetPlaceboGamutMappingLut3dTricubicEnabled(bool enabled);

		bool GetPlaceboGamutMappingGamutExpansionEnabled() const;
		void SetPlaceboGamutMappingGamutExpansionEnabled(bool enabled);

		PlaceboToneMappingFunction GetPlaceboToneMappingFunction() const;
		void SetPlaceboToneMappingFunction(PlaceboToneMappingFunction function);

		float GetPlaceboToneMappingKneeAdaptation() const;
		void SetPlaceboToneMappingKneeAdaptation(float knee);

		float GetPlaceboToneMappingKneeMinimum() const;
		void SetPlaceboToneMappingKneeMinimum(float knee);

		float GetPlaceboToneMappingKneeMaximum() const;
		void SetPlaceboToneMappingKneeMaximum(float knee);

		float GetPlaceboToneMappingKneeDefault() const;
		void SetPlaceboToneMappingKneeDefault(float knee);

		float GetPlaceboToneMappingKneeOffset() const;
		void SetPlaceboToneMappingKneeOffset(float knee);

		float GetPlaceboToneMappingSlopeTuning() const;
		void SetPlaceboToneMappingSlopeTuning(float slope);

		float GetPlaceboToneMappingSlopeOffset() const;
		void SetPlaceboToneMappingSlopeOffset(float offset);

		float GetPlaceboToneMappingSplineContrast() const;
		void SetPlaceboToneMappingSplineContrast(float contrast);

		float GetPlaceboToneMappingReinhardContrast() const;
		void SetPlaceboToneMappingReinhardContrast(float contrast);

		float GetPlaceboToneMappingLinearKnee() const;
		void SetPlaceboToneMappingLinearKnee(float knee);

		float GetPlaceboToneMappingExposure() const;
		void SetPlaceboToneMappingExposure(float exposure);

		bool GetPlaceboToneMappingInverseToneMappingEnabled() const;
		void SetPlaceboToneMappingInverseToneMappingEnabled(bool enabled);

		PlaceboToneMappingMetadata GetPlaceboToneMappingMetadata() const;
		void SetPlaceboToneMappingMetadata(PlaceboToneMappingMetadata function);

		int GetPlaceboToneMappingToneLutSize() const;
		void SetPlaceboToneMappingToneLutSize(int size);

		float GetPlaceboToneMappingContrastRecovery() const;
		void SetPlaceboToneMappingContrastRecovery(float recovery);

		float GetPlaceboToneMappingContrastSmoothness() const;
		void SetPlaceboToneMappingContrastSmoothness(float smoothness);

		void PlaceboSettingsApply();

		QList<RegisteredHost> GetRegisteredHosts() const			{ return registered_hosts.values(); }
		void AddRegisteredHost(const RegisteredHost &host);
		void RemoveRegisteredHost(const HostMAC &mac);
		bool GetRegisteredHostRegistered(const HostMAC &mac) const	{ return registered_hosts.contains(mac); }
		RegisteredHost GetRegisteredHost(const HostMAC &mac) const	{ return registered_hosts[mac]; }
		QList<HiddenHost> GetHiddenHosts() const 					{ return hidden_hosts.values(); }
		void AddHiddenHost(const HiddenHost &host);
		void RemoveHiddenHost(const HostMAC &mac);
		bool GetHiddenHostHidden(const HostMAC &mac) const			{ return hidden_hosts.contains(mac); }
		HiddenHost GetHiddenHost(const HostMAC &mac) const 			{ return hidden_hosts[mac]; }
		bool GetNicknameRegisteredHostRegistered(const QString &nickname) const { return nickname_registered_hosts.contains(nickname); }
		RegisteredHost GetNicknameRegisteredHost(const QString &nickname) const { return nickname_registered_hosts[nickname]; }
		size_t GetPS4RegisteredHostsRegistered() const { return ps4s_registered; }
		QList<QString> GetProfiles() const                          { return profiles; }

		QList<ManualHost> GetManualHosts() const 					{ return manual_hosts.values(); }
		int SetManualHost(const ManualHost &host);
		void RemoveManualHost(int id);
		bool GetManualHostExists(int id)							{ return manual_hosts.contains(id); }
		ManualHost GetManualHost(int id) const						{ return manual_hosts[id]; }

		QMap<QString, QString> GetControllerMappings() const		{ return controller_mappings; }
		void SetControllerMapping(const QString &vidpid, const QString &mapping);
		void RemoveControllerMapping(const QString &vidpid);

		static QString GetChiakiControllerButtonName(int);
		void SetControllerButtonMapping(int, Qt::Key);
		QMap<int, Qt::Key> GetControllerMapping();
		QMap<Qt::Key, int> GetControllerMappingForDecoding();

	signals:
		void RegisteredHostsUpdated();
		void HiddenHostsUpdated();
		void ManualHostsUpdated();
		void ControllerMappingsUpdated();
		void CurrentProfileChanged();
		void ProfilesUpdated();
		void PlaceboSettingsUpdated();
};

#endif // CHIAKI_SETTINGS_H
