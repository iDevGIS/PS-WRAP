#pragma once
#include <limits>

#include "streamsession.h"
#include "settings.h"

#include <QMutex>
#include <QTimer>
#include <QAtomicInteger>
#include <QWindow>
#include <QQuickWindow>
#include <QLoggingCategory>
#include <deque>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libplacebo/opengl.h>
#include <libplacebo/options.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/utils/frame_queue.h>
#include <libplacebo/log.h>
#include <libplacebo/cache.h>
}

#include <vulkan/vulkan.h>
#if defined(Q_OS_LINUX)
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_wayland.h>
#elif defined(Q_OS_MACOS)
#include <vulkan/vulkan_metal.h>
#elif defined(Q_OS_WIN)
#include <vulkan/vulkan_win32.h>
#endif

Q_DECLARE_LOGGING_CATEGORY(chiakiGui);

class Settings;
class StreamSession;
class QmlBackend;
class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QWidget;
class QLabel;
class QVBoxLayout;
class QHBoxLayout;
class QFrame;
class StatsOverlayWidget;
class BufferedPlaybackPacerThread;
class DeferredPresentPacerThread;
class DeferredSwapThread;
class PsWrapRecorder;    // PS-WRAP: อัดวิดีโอ
class PsWrapRecCapture;
class PsWrapMicMeter;   // PS-WRAP: spectrum ไมค์
class PsWrapGameProfiles;   // PS-WRAP: preset รายเกม
class PsWrapGoLive;        // PS-WRAP: Go Live (pswraplive.h)

class QmlMainWindow : public QWindow
{
    Q_OBJECT
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY droppedFramesChanged)
    Q_PROPERTY(bool keepVideo READ keepVideo WRITE setKeepVideo NOTIFY keepVideoChanged)
    Q_PROPERTY(bool loadingTransitionComplete READ loadingTransitionComplete NOTIFY loadingTransitionCompleteChanged)
    Q_PROPERTY(VideoMode videoMode READ videoMode WRITE setVideoMode NOTIFY videoModeChanged)
    Q_PROPERTY(float ZoomFactor READ zoomFactor WRITE setZoomFactor NOTIFY zoomFactorChanged)
    Q_PROPERTY(VideoPreset videoPreset READ videoPreset WRITE setVideoPreset NOTIFY videoPresetChanged)
    Q_PROPERTY(bool directStream READ directStream NOTIFY directStreamChanged)
    Q_PROPERTY(int runtimeRendererBackend READ runtimeRendererBackend CONSTANT)
    // PS-WRAP: tray + always on top (ค่าเก็บใน Settings)
    Q_PROPERTY(bool hideToTray READ hideToTray WRITE setHideToTray NOTIFY hideToTrayChanged)
    Q_PROPERTY(bool padOverlay READ padOverlay WRITE setPadOverlay NOTIFY padOverlayChanged)
    Q_PROPERTY(bool camOverlay READ camOverlay WRITE setCamOverlay NOTIFY camOverlayChanged)
    Q_PROPERTY(bool statsOverlay READ statsOverlay WRITE setStatsOverlay NOTIFY statsOverlayChanged)
    // PS-WRAP: อัดวิดีโอ + overlay spectrum ไมค์ (ดู pswraprecorder.h / pswrapmicmeter.h)
    Q_PROPERTY(bool micOverlay READ micOverlay WRITE setMicOverlay NOTIFY micOverlayChanged)
    Q_PROPERTY(bool clockOverlay READ clockOverlay WRITE setClockOverlay NOTIFY clockOverlayChanged)   // PS-WRAP: นาฬิกา + เวลาเล่น (qmlmainwindow_pswrapclock.cpp)
    Q_PROPERTY(bool chatOverlay READ chatOverlay WRITE setChatOverlay NOTIFY chatOverlayChanged)       // PS-WRAP: แชทไลฟ์บนจอ (qmlmainwindow_pswrapchat.cpp)
    Q_PROPERTY(QObject *liveChat READ liveChatObject CONSTANT)                                         // PS-WRAP: PsWrapLiveChat (ข้อความ/สถานะ/แหล่ง)
    Q_PROPERTY(int camFx READ camFx WRITE setCamFx NOTIFY camFxChanged)                          // PS-WRAP: 0..13 (ดู WebcamOverlay.fx)
    Q_PROPERTY(int camBackground READ camBackground WRITE setCamBackground NOTIFY camBackgroundChanged) // 0 keep · 1 green · 2 blue · 3 AI
    Q_PROPERTY(QObject *recorder READ recorderObject CONSTANT)
    Q_PROPERTY(QObject *micMeter READ micMeterObject CONSTANT)
    Q_PROPERTY(QObject *gameProfiles READ gameProfilesObject CONSTANT)   // PS-WRAP: preset รายเกม (qmlmainwindow_pswrapprofiles.cpp)
    Q_PROPERTY(QObject *goLive READ goLiveObject CONSTANT)               // PS-WRAP: Go Live (qmlmainwindow_pswraplive.cpp) — Chiaki.goLive ชี้ตัวเดียวกัน
    // PS-WRAP: ไมค์ gain/noise gate (qmlmainwindow_pswrapmic.cpp)
    Q_PROPERTY(qreal micGainDb READ micGainDb WRITE setMicGainDb NOTIFY micGainDbChanged)                       // -12..+24
    Q_PROPERTY(bool micGateEnabled READ micGateEnabled WRITE setMicGateEnabled NOTIFY micGateEnabledChanged)
    Q_PROPERTY(qreal micGateThresholdDb READ micGateThresholdDb WRITE setMicGateThresholdDb NOTIFY micGateThresholdDbChanged) // -80..-20
    Q_PROPERTY(QString recordingFolder READ recordingFolder WRITE setRecordingFolder NOTIFY recordingFolderChanged)
    Q_PROPERTY(int captureHeight READ captureHeight WRITE setCaptureHeight NOTIFY captureHeightChanged)   // PS-WRAP: 0 = สตรีม · 1440 · 2160
    // PS-WRAP: ภาพแนวตั้ง 9:16 (qmlmainwindow_pswrapvertical.cpp) — preview = image://pswrapvertical/<verticalFrame>
    Q_PROPERTY(bool verticalPreview READ verticalPreview WRITE setVerticalPreview NOTIFY verticalPreviewChanged)
    Q_PROPERTY(int verticalLayout READ verticalLayout WRITE setVerticalLayout NOTIFY verticalLayoutChanged)   // 0 Split · 1 Center · 2 Blur
    Q_PROPERTY(qreal verticalCropX READ verticalCropX WRITE setVerticalCropX NOTIFY verticalCropXChanged)    // 0..1
    Q_PROPERTY(int verticalFrame READ verticalFrame NOTIFY verticalFrameChanged)
    Q_PROPERTY(QObject *verticalRecorder READ verticalRecorderObject CONSTANT)   // PS-WRAP: อัดคลิปแนวตั้ง (recording/seconds/saved/failed)
    Q_PROPERTY(bool verticalChat READ verticalChat WRITE setVerticalChat NOTIFY verticalChatChanged)   // PS-WRAP: การ์ดแชทในภาพแนวตั้ง
    Q_PROPERTY(QObject *verticalLayers READ verticalLayersObject CONSTANT)        // PS-WRAP: รูป/GIF ในภาพแนวตั้ง (pswrapverticallayers.h)
    // PS-WRAP: Instant Replay (qmlmainwindow_pswraprec.cpp) — เดินเองระหว่างสตรีมเมื่อเปิด · saveReplay() → recorder.replaySaved
    Q_PROPERTY(bool replayEnabled READ replayEnabled WRITE setReplayEnabled NOTIFY replayEnabledChanged)
    Q_PROPERTY(int replaySeconds READ replaySeconds WRITE setReplaySeconds NOTIFY replaySecondsChanged)   // 30..120
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY alwaysOnTopChanged)
    Q_PROPERTY(double queueDepthAverage READ queueDepthAverage NOTIFY queueDepthAverageChanged)
    Q_PROPERTY(double pendingFrameAge READ pendingFrameAge NOTIFY pendingFrameAgeChanged)

public:
    enum class UpdateRequestReason {
        Unknown,
        SceneChanged,
        RenderRequested,
        Timer,
        QueueStoredFrame,
        QueueReset,
        PendingFrame,
        Replay,
        PlaceboReset,
        FinalizePending,
        NoSwapchain,
        VSync
    };
    Q_ENUM(UpdateRequestReason);

    enum class VideoMode {
        Normal,
        Stretch,
        Zoom
    };
    Q_ENUM(VideoMode);

    enum class VideoPreset {
        Fast,
        Default,
        HighQuality,
        HighQualitySpatial,
        HighQualityAdvancedSpatial,
        Custom
    };
    Q_ENUM(VideoPreset);

    QmlMainWindow(Settings *settings,  bool exit_app_on_stream_exit = false);
    QmlMainWindow(const StreamSessionConnectInfo &connect_info);
    ~QmlMainWindow();
    void updateWindowType(WindowType type);
    void setSettings(Settings *new_settings);

    bool hasVideo() const;
    int droppedFrames() const;
    void increaseDroppedFrames();

    bool directStream() const;
    int runtimeRendererBackend() const { return static_cast<int>(render_backend); }
    bool loadingTransitionComplete() const { return loading_transition_complete.loadAcquire() != 0; }

    bool keepVideo() const;
    void setKeepVideo(bool keep);

    VideoMode videoMode() const;
    void setVideoMode(VideoMode mode);

    float zoomFactor() const;
    void setZoomFactor(float factor);

    double queueDepthAverage() const;
    double pendingFrameAge() const;
    QString describePlaceboDiscardReason(qint64 now_us) const;

    bool amdCard() const;
    bool nvidiaCard() const;
    bool wasMaximized() const { return was_maximized; };
    bool isWindowAdjustable() const { return is_window_adjustable; }
    void setWindowAdjustable(bool adjustable) { is_window_adjustable = adjustable; }

    void fullscreenTime();
    void normalTime();

    bool isStreamWindowAdjustable() { return is_stream_window_adjustable; }
    // PS-WRAP: เซฟ stream geometry เฉพาะช่วงที่ผู้ใช้ปรับเอง — arm หลังเริ่มสตรีม, lock ทันทีเมื่อจบ (กัน resize ตอนเปลี่ยน session เซฟทับ)
    void armStreamGeometrySave(int delay_ms);
    void restoreMainGeometry();   // PS-WRAP: คืน geometry + maximize ของหน้าต่างหลัก (ตอนเปิดและหลังสตรีม)
    bool hideToTray() const;
    void setHideToTray(bool v);
    bool alwaysOnTop() const;
    void setAlwaysOnTop(bool v);
    void applyAlwaysOnTop();
    void setupTray();
    Q_INVOKABLE void hideToTrayNow();
    Q_INVOKABLE void restoreFromTray();   // PS-WRAP
    // PS-WRAP: พื้นที่ overlay ที่คลิกได้ระหว่างสตรีม (logical px ของหน้าต่าง) — เมาส์ในพื้นที่นี้ส่งให้ QML แทนเกม
    Q_INVOKABLE void setOverlayHitRects(const QVariantList &rects);
    // PS-WRAP: overlay toggles — เจ้าของค่าอยู่ C++ ให้ tray menu / QML ใช้ร่วมกันและ sync สด
    bool padOverlay() const;
    void setPadOverlay(bool v);
    bool camOverlay() const;
    void setCamOverlay(bool v);
    bool statsOverlay() const;
    void setStatsOverlay(bool v);
    bool micOverlay() const;            // PS-WRAP
    int camFx() const;
    void setCamFx(int fx);
    int camBackground() const;
    void setCamBackground(int mode);
    void setMicOverlay(bool v);
    bool clockOverlay() const;          // PS-WRAP: qmlmainwindow_pswrapclock.cpp
    void setClockOverlay(bool v);
    bool chatOverlay() const;           // PS-WRAP: qmlmainwindow_pswrapchat.cpp
    void setChatOverlay(bool v);
    class PsWrapLiveChat *liveChat();   // สร้างครั้งแรกที่เรียก (ลูกของ window)
    QObject *liveChatObject();
    void pswrapSyncChat();              // เปิด overlay + มีสตรีม = ต่อแชท · ไม่งั้นหยุด
    QObject *recorderObject() const;
    QObject *micMeterObject() const;
    PsWrapGameProfiles *gameProfiles();   // PS-WRAP: สร้างครั้งแรกที่เรียก (ลูกของ window) — qmlmainwindow_pswrapprofiles.cpp
    QObject *gameProfilesObject();
    PsWrapGoLive *goLive();               // PS-WRAP: สร้างครั้งแรกที่เรียก (ลูกของ window) — qmlmainwindow_pswraplive.cpp
    QObject *goLiveObject();
    QString recordingFolder() const;
    void setRecordingFolder(const QString &folder);
    int captureHeight() const;              // PS-WRAP: qmlmainwindow_pswraprec.cpp
    bool verticalPreview() const;           // PS-WRAP: qmlmainwindow_pswrapvertical.cpp
    void setVerticalPreview(bool on);
    int verticalLayout() const;
    void setVerticalLayout(int mode);
    qreal verticalCropX() const;
    void setVerticalCropX(qreal x);
    int verticalFrame() const { return pswrap_vertical_frame; }
    QObject *verticalRecorderObject() const;
    Q_INVOKABLE void toggleVerticalRecording();   // อัดคลิป 9:16 (1080x1920) ตามเลย์เอาต์ปัจจุบัน
    Q_INVOKABLE void setVerticalCamRect(qreal x, qreal y, qreal w, qreal h);   // logical px ของหน้าต่าง · w<=0 = ไม่มี facecam
    // เฟส 2: ลากใน preview — กรอบล่าสุด (สัดส่วน 0..1 ของ canvas) {game:{x,y,w,h}, cam:{x,y,w,h}|null}
    Q_INVOKABLE QVariantMap verticalHitRects() const;
    Q_INVOKABLE void setVerticalCam(qreal cx, qreal cy, qreal w);   // ตำแหน่ง facecam ของเลย์เอาต์ปัจจุบัน · w<=0 = คืนค่าเริ่มต้น
    bool verticalChat() const;
    void setVerticalChat(bool on);   // เปิดแล้วเปิด overlay แชทบนจอให้ด้วย (ภาพตัดมาจากการ์ดบนจอ)
    Q_INVOKABLE void setVerticalChatRect(qreal x, qreal y, qreal w, qreal h);   // logical px ของการ์ดแชทบนจอ · w<=0 = ไม่มี
    Q_INVOKABLE void setVerticalChatPos(qreal cx, qreal cy, qreal w);           // ตำแหน่งการ์ดแชทในภาพแนวตั้ง · w<=0 = ค่าเริ่มต้น
    class PsWrapVerticalLayers *verticalLayers();   // สร้างครั้งแรกที่เรียก (ลูกของ window)
    QObject *verticalLayersObject();
    void setCaptureHeight(int height);
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void openRecordingsFolder();
    Q_INVOKABLE void openScreenshotsFolder();   // PS-WRAP: เลือกภาพล่าสุดใน Explorer (รวมโฟลเดอร์สำรอง)
    Q_INVOKABLE void revealRecording(const QString &path);
    bool replayEnabled() const;         // PS-WRAP: Instant Replay (qmlmainwindow_pswraprec.cpp)
    void setReplayEnabled(bool on);
    int replaySeconds() const;
    void setReplaySeconds(int seconds);
    Q_INVOKABLE void saveReplay();      // → recorder.replaySaved(path) / recorder.failed(msg)
    Q_INVOKABLE void addMarker();       // → recorder.markerAdded(seconds) / recorder.failed(msg)
    // PS-WRAP: ภาพหน้าจอ 1 ปุ่ม (qmlmainwindow_pswrapshot.cpp) → screenshotSaved / screenshotFailed
    Q_INVOKABLE void takeScreenshot();
    // PS-WRAP: ขนาดพื้นที่ภาพ 16:9 ตาม preset (qmlmainwindow_pswrapsize.cpp) — physical px · width/height -1 = Fullscreen
    Q_INVOKABLE QVariantList playerSizes();   // [{width, height, stream, current}] เฉพาะที่วางบนจอได้ + Fullscreen ท้ายสุด
    Q_INVOKABLE void setPlayerSize(int width, int height);
    // PS-WRAP: หน้า About (qmlmainwindow_pswrapabout.cpp) — ไฟล์ license ที่วางข้าง exe ในแพ็กเกจ release
    Q_INVOKABLE bool appFileExists(const QString &name) const;
    Q_INVOKABLE void openAppFile(const QString &name);
    // PS-WRAP: ทดสอบไมค์นอกสตรีม (หน้า preview) — เปิดอุปกรณ์ capture ส่ง PCM เข้า micMeter · "" = Auto
    Q_INVOKABLE bool startMicPreview(const QString &device);
    Q_INVOKABLE void stopMicPreview();
    qreal micGainDb() const;            // PS-WRAP: qmlmainwindow_pswrapmic.cpp
    void setMicGainDb(qreal db);
    bool micGateEnabled() const;
    void setMicGateEnabled(bool on);
    qreal micGateThresholdDb() const;
    void setMicGateThresholdDb(qreal db);
    Q_INVOKABLE void quitApp();
    void saveMainGeometryDeferred();   // PS-WRAP: เซฟหลังหยุดขยับ 400ms (กัน Resize ที่มาก่อน WindowStateChange ตอน maximize)
    void saveMainGeometryNow();
    bool looksMaximized(const QRect &g) const;
    void lockStreamGeometrySave() { stream_geometry_save_after_ms = std::numeric_limits<qint64>::max(); }
    bool canSaveStreamGeometry() const;
    void setStreamWindowAdjustable(bool adjustable) { is_stream_window_adjustable = adjustable; }

    QmlBackend *getBackend();

    VideoPreset videoPreset() const;
    void setVideoPreset(VideoPreset mode);

    Q_INVOKABLE void grabInput();
    Q_INVOKABLE void releaseInput();
    Q_INVOKABLE void requestOverlayUpdate();
    Q_INVOKABLE void setOverlayInteractionActive(bool active);
    Q_INVOKABLE void setStatsOverlayActive(bool active);
    Q_INVOKABLE void noteLoadingTransitionComplete();
    Q_INVOKABLE void presentStartupWarmupFrame(unsigned width, unsigned height, bool hdr);
    void armVerbosePlaceboQuietWindow();
    bool startupWarmupFrameActive() const { return startup_warmup_frame_active; }

public slots:
    void resetPlaceboQueue();
    void schedulePlaceboReset();
    void queuePlaceboReset(bool preserve_timeline);

    void updatePlacebo();
    void updateVSync();
    void updateVulkanDeferredSwap();
    void show();
    void presentFrame(ChiakiFfmpegFrame frame, int32_t frames_lost, qint64 decoder_delivery_us = 0);

    AVBufferRef *vulkanHwDeviceCtx();

signals:
    void screenshotSaved(QString path);      // PS-WRAP: GUI thread · path = ภาพ SDR (.png)
    void screenshotFailed(QString message);
    void hideToTrayChanged();
    void padOverlayChanged();
    void camOverlayChanged();
    void statsOverlayChanged();
    void micOverlayChanged();          // PS-WRAP
    void clockOverlayChanged();        // PS-WRAP
    void chatOverlayChanged();         // PS-WRAP
    void captureHeightChanged();       // PS-WRAP
    void verticalPreviewChanged();     // PS-WRAP
    void verticalLayoutChanged();
    void verticalChatChanged();        // PS-WRAP
    void verticalCropXChanged();
    void verticalFrameChanged();
    void micGainDbChanged();
    void micGateEnabledChanged();
    void micGateThresholdDbChanged();
    void camFxChanged();
    void camBackgroundChanged();
    void recordingFolderChanged();
    void replayEnabledChanged();       // PS-WRAP
    void replaySecondsChanged();
    void alwaysOnTopChanged();
    void hasVideoChanged();
    void droppedFramesChanged();
    void keepVideoChanged();
    void videoModeChanged();
    void zoomFactorChanged();
    void videoPresetChanged();
    void menuRequested();
    void directStreamChanged();
    void queueDepthAverageChanged();
    void pendingFrameAgeChanged();
    void loadingTransitionCompleteChanged();
    void statsOverlayActiveChanged();

private:
    friend class BufferedPlaybackPacerThread;
    friend class DeferredPresentPacerThread;
    friend class DeferredSwapThread;

    struct PendingFrameEntry;

    bool makeOpenGLContextCurrent();
    void doneOpenGLContextCurrent();

    void init(Settings *settings, bool exit_app_on_stream_exit = false);
    pl_gpu placeboGpu() const;
    void update();
    void scheduleUpdate(bool force = false);
    void scheduleUpdate(bool force, UpdateRequestReason reason);
    void scheduleBufferedUpdate();
    void scheduleBufferedUpdate(UpdateRequestReason reason);
    void updateStatsOverlayGeometry();
    bool statsOverlayActive() const { return stats_overlay_visible; }
    void armQuickNeedSync(const char *reason);
    void scheduleRenderIfBacklog(UpdateRequestReason reason = UpdateRequestReason::PendingFrame);
    void handleBufferedPlaybackWake(qint64 timer_fire_us);
    void completeStartupVideoVisibility(quint64 generation);
    bool throttleFramePresentation(double interval_s);
    void handleDeferredPresentWake(qint64 timer_fire_us);
    bool enqueueDeferredSwap(qint64 submit_begin_us,
                             int queue_depth_at_submit,
                             int depth_limit,
                             bool pending_frame_waiting,
                             bool pending_overflow_waiting,
                             qint64 present_interval_us,
                             qint64 present_submit_interval_us);
    void finalizeDeferredPresentIfIdle();
    void processDeferredSwapTask(qint64 submit_begin_us,
                                 int queue_depth_at_submit,
                                 int depth_limit,
                                 bool pending_frame_waiting,
                                 bool pending_overflow_waiting,
                                 qint64 present_interval_us,
                                 qint64 present_submit_interval_us);
    void drainDeferredSwaps();
    void setStreamMaxFPS(unsigned int max_fps);
    void createSwapchain();
    void destroySwapchain();
    void resizeSwapchain();
    void updateSwapchain();
    void drainRenderThread();
    void sync();
    void beginFrame();
    void endFrame();
    void render();
    void handleVulkanDeviceLost(const QString &reason);
    void handleVulkanRendererFallback(const QString &title, const QString &message, const QString &fallback_reason);
    void applyPendingFrame();
    void queuePendingFrameRelease();
    bool applyPendingFrameIfQueueHasCapacity();
    bool hasPendingFrame() const;
    bool hasPendingFrameOverflow();
    bool pendingFrameOverflowEnabled() const;
    int effectiveQueueDepthLimit() const;
    int pendingFrameOverflowLimit(int submission_depth_limit) const;
    void clearPendingFrameStateLocked();
    void prunePendingFramesBeforeLocked(double cutoff_pts);
    void insertPendingOverflowLocked(PendingFrameEntry entry);
    bool takePendingFrameLocked(PendingFrameEntry &entry);
    bool dropPendingFrameLocked();
    void resetQueueDepthTracking();
    bool promotePendingFrameFromOverflowLocked();
    bool storePendingFrame(ChiakiFfmpegFrame &frame, bool take_ownership = false, bool synthetic_warmup = false);
    bool storeResetSeedFrame(const AVFrame *frame, double pts, float duration, quint64 generation);
    bool storeResetSeedFromPendingFrame(quint64 generation);
    bool cloneNewestPendingFrameLocked(AVFrame *&clone, double &pts, float &duration, uint64_t *stored_us = nullptr);
    bool applyKeptFrameSnapshot();
    bool applyResetSeedFrame(quint64 generation);
    bool queueStoredFrame(AVFrame *frame, double pts, float duration,
                          void (*discard_cb)(const struct pl_source_frame *),
                          bool synthetic_warmup = false);
    void refreshPendingFrameAge();
    void snapshotPendingFrame();
    bool handleShortcut(QKeyEvent *event);
    bool event(QEvent *event) override;
    QObject *focusObject() const override;
    void updateQueueDepthAverage(int depth);
    void updatePendingFrameAge(double age);
    void snapshotLastFrame(AVFrame *frame, double pts, float duration, bool take_ownership = false);
    void clearSnapshotFrame();
    bool hasBufferedWork();
    bool enqueueKeptFrame(double queue_pts_origin_hint, bool deinterlace_enabled, double &used_origin);
    double queuePtsOriginCached() const;
    void setQueuePtsOriginCached(double queue_pts_origin);
    const struct pl_filter_config *effectiveFrameMixerConfig(const struct pl_render_params *render_params = nullptr) const;
    bool effectiveFrameMixerEnabled(const struct pl_render_params *render_params = nullptr) const;
    bool configuredFrameMixerEnabledForScheduling() const;
    bool has_video = false;
    struct pl_queue_params qparams;
    struct pl_frame_mix frame_mix;
    // Direct rendering retains the displayed mapping and replaces only pending input.
    bool bypass_frame_queue = true;
    QMutex direct_frame_mutex;
    AVFrame *direct_pending_frame = nullptr;
    struct pl_frame direct_frame = {};
    QAtomicInteger<int> direct_frame_reset = 0;
    uint64_t ts_start = 0;
    double queue_pts_origin = -1.0;
    double newest_queued_frame_pts = -1.0;
    bool playback_started = false;
    bool preserve_playback_timeline = false;
    bool was_maximized = false;
    bool amd_card = false;
    bool nvidia_card = false;
    bool direct_stream = false;
    std::atomic<qint64> queue_pts_origin_cached_us = -1;
    uint64_t next_frame_target_us = 0;
    double last_throttle_interval_s = 0.0;
    QAtomicInteger<int> present_backpressure_active = 0;
    QAtomicInteger<int> present_pace_timer_rearm = 0;
    QAtomicInteger<int> present_pacing_reset_pending = 0;
    QAtomicInteger<int> ui_priority_update_pending = 0;
    QAtomicInteger<int> overlay_interaction_active = 0;
    bool keep_video = false;
    RenderBackend render_backend = RenderBackend::Vulkan;
    int grab_input = 0;
    int dropped_frames = 0;
    bool is_window_adjustable = false;
    bool is_stream_window_adjustable = false;
    qint64 stream_geometry_save_after_ms = std::numeric_limits<qint64>::max();
    QTimer *geometry_save_timer = nullptr;
    class QSystemTrayIcon *tray_icon = nullptr;
    class QAction *tray_on_top_action = nullptr;
    QList<QRectF> overlay_hit_rects;   // PS-WRAP
    bool overlay_mouse_captured = false;   // PS-WRAP: press เริ่มบน overlay → move/release ตามไป QML
    QTimer tray_click_timer;   // PS-WRAP: แยก single/double click ของ tray
    class QAction *tray_pad_action = nullptr;
    class QAction *tray_cam_action = nullptr;
    class QAction *tray_stats_action = nullptr;
    class QAction *tray_mic_action = nullptr;      // PS-WRAP
    class QAction *tray_record_action = nullptr;
    class QAction *tray_replay_action = nullptr;       // PS-WRAP: Instant Replay ON/OFF
    class QAction *tray_save_replay_action = nullptr;
    class QAction *tray_shot_action = nullptr;
    class QAction *tray_live_action = nullptr;         // PS-WRAP: Go Live เริ่ม/หยุด
    class QAction *tray_mute_action = nullptr;
    class QMenu *tray_mic_menu = nullptr;
    class QMenu *tray_fx_menu = nullptr;
    class QMenu *tray_bg_menu = nullptr;
    void pswrapRefreshTray();
    quint32 pswrap_mic_preview = 0;   // SDL_AudioDeviceID
    PsWrapRecorder *pswrap_recorder = nullptr;     // GUI thread เป็นเจ้าของ
    PsWrapMicMeter *pswrap_mic_meter = nullptr;
    PsWrapRecCapture *pswrap_rec_capture = nullptr; // render thread เท่านั้น (ลบตอน destructor หลัง render thread จบ)
    PsWrapRecorder *pswrap_vrec = nullptr;            // PS-WRAP: pipeline แนวตั้ง 9:16 (คลิป/ไลฟ์แนวตั้ง) — PsWrapRecorder::secondary()
    PsWrapRecCapture *pswrap_vrec_capture = nullptr;  // render thread เท่านั้น
    struct PsWrapVerticalLayout pswrapVerticalLayoutNow(int screen_w, int screen_h) const;   // render thread: เลย์เอาต์ + กรอบ facecam (px)
    void pswrapLoadVerticalLayout();                  // GUI thread: settings → ค่าที่ render thread ใช้
    void pswrapInitRecording();
    void pswrapInitMic();              // PS-WRAP: ส่งค่า gain/gate ที่จำไว้เข้า PsWrapVoiceProc (qmlmainwindow_pswrapmic.cpp)
    void pswrapStopRecordingForTeardown();
    void pswrapDestroyCapture();
    void pswrapDecorateScreen(struct pl_frame &target_frame);   // render thread: จุด REC กระพริบบนจอ (ไม่ติดไฟล์)
    pl_tex pswrap_rec_dot_tex = nullptr;
    pl_tex pswrap_rec_label_tex = nullptr;     // ป้าย "4K" / "1440p" ต่อท้ายจุด REC (อัดแบบ upscale)
    int pswrap_rec_label_height = 0;           // ความสูงไฟล์ที่ป้ายปัจจุบันวาดไว้
    pl_overlay pswrap_screen_overlays[3] = {};
    pl_overlay_part pswrap_rec_dot_part = {};
    pl_overlay_part pswrap_rec_label_part = {};
    void pswrapSetupTrayRecording(class QMenu *menu);
    void pswrapSetupTrayView(class QMenu *menu);   // PS-WRAP: Stream menu / Settings / Picture size (qmlmainwindow_pswrapsize.cpp)
    bool pswrapBuildRecConfig(struct PsWrapRecConfig *cfg, QString *error);   // PS-WRAP: สเปคไฟล์อัด/replay จากหน้าต่าง + สตรีม
    void pswrapSyncReplay();            // PS-WRAP: เปิด/ปิด pipeline replay ตาม replayEnabled + สถานะสตรีม
    void pswrapRecordCapture(const struct pl_frame_mix *mix, const struct pl_frame *single, const struct pl_render_params &params,
                             const struct pl_frame &screen_target, const struct pl_overlay *overlay, const struct pl_frame *hint);
    const struct pl_hook *pswrapCaptureUpscaler() const;   // PS-WRAP: upscaler ของไฟล์ตามปุ่ม QUALITY (qmlmainwindow_pswraprec.cpp)
    // PS-WRAP: ภาพหน้าจอ (qmlmainwindow_pswrapshot.cpp) — request id จาก GUI thread, render thread หยิบไปถ่ายเฟรมถัดไป
    QAtomicInteger<int> pswrap_shot_request = 0;
    class PsWrapShotCapture *pswrap_shot_capture = nullptr;   // render thread เท่านั้น (ลบตอน destructor หลัง render thread จบ)
    qint64 pswrap_shot_last_us = 0;                            // render thread: ปล่อย GPU resource เมื่อว่างนาน
    void pswrapShotCapture(const struct pl_frame_mix *mix, const struct pl_frame *single, const struct pl_render_params &params,
                           const struct pl_frame &screen_target, const struct pl_overlay *overlay);
    void pswrapDestroyShot();
    // PS-WRAP: preview 9:16 (qmlmainwindow_pswrapvertical.cpp) — render thread เท่านั้น
    void pswrapVerticalCapture(const struct pl_frame_mix *mix, const struct pl_frame *single, const struct pl_render_params &params,
                               const struct pl_frame &screen_target, const struct pl_overlay *overlay);
    void pswrapDestroyVertical();
    class PsWrapVerticalPreview *pswrap_vertical = nullptr;
    qint64 pswrap_vertical_last_us = 0;
    int pswrap_vertical_frame = 0;   // GUI thread
    bool quit_requested = false;
    QAtomicInteger<int> dropped_frames_current = 0;
    bool going_full = false;
    VideoMode video_mode = VideoMode::Normal;
    float zoom_factor = 0;
    VideoPreset video_preset = VideoPreset::HighQuality;
    Settings *settings = {};

    QmlBackend *backend = {};
    StreamSession *session = {};
    AVBufferRef *vulkan_hw_dev_ctx = nullptr;
    std::atomic<double> queue_depth_average{0.0};
    QAtomicInteger<int> queue_depth_cached = 0;
    double pending_frame_age = 0.0;
    uint64_t last_placebo_reset_ts = 0;
    uint64_t pending_frame_stored_us = 0;
    mutable QMutex pending_frame_age_mutex;
    bool startup_warmup_preserve_next_session_change = false;

    pl_cache placebo_cache = {};
    pl_log placebo_log = {};
    pl_vk_inst placebo_vk_inst = {};
    pl_vulkan placebo_vulkan = {};
    pl_opengl placebo_opengl = {};
    pl_swapchain placebo_swapchain = {};
    pl_renderer placebo_renderer = {};
    pl_queue placebo_queue = {};
    std::array<pl_tex, 8> placebo_tex{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    int vk_decode_queue_index = -1;
    QSize swapchain_size;
    QThread *render_thread = {};
    bool owns_render_thread = false;
    QMutex render_schedule_mutex;
    QMutex placebo_state_mutex;
    QMutex placebo_swapchain_mutex;
    bool render_scheduled = false;
    bool render_pending = false;
    QAtomicInteger<int> render_pending_during_cycle = 0;
    QAtomicInteger<int> last_update_request_reason = 0;
    QAtomicInteger<int> queue_stored_frame_pending = 0;
    QAtomicInteger<int> pending_frame_submission_active = 0;
    QMutex pending_frame_mutex;
    struct PendingFrameEntry {
        AVFrame *frame = nullptr;
        double pts = 0.0;
        float duration = 0.0f;
        double queue_origin = 0.0;
        uint64_t stored_us = 0;
        bool synthetic_warmup = false;
    };
    AVFrame *pending_frame = nullptr;
    std::deque<PendingFrameEntry> pending_frame_overflow;
    bool pending_frame_synthetic_warmup = false;
    double pending_pts = 0.0;
    float pending_duration = 0.0f;
    double pending_frame_queue_origin = 0.0;
    QAtomicInteger<int> pending_frame_present = 0;
    QAtomicInteger<int> pending_frame_release_queued = 0;
    QAtomicInteger<qint64> pending_frame_release_queued_us = 0;
    QMutex reset_seed_mutex;
    AVFrame *reset_seed_frame = nullptr;
    double reset_seed_pts = 0.0;
    float reset_seed_duration = 0.0f;
    quint64 reset_seed_generation = 0;
    QAtomicInteger<quint64> snapshot_generation = 0;
    QAtomicInteger<quint64> pending_reset_snapshot_generation = 0;
    QAtomicInteger<quint64> last_reset_snapshot_generation = 0;
    QAtomicInteger<int> stream_session_active = 0;
    QMutex kept_frame_mutex;
    AVFrame *kept_frame = nullptr;
    double kept_frame_pts = 0.0;
    float kept_frame_duration = 0.0f;
    AVFrame *fallback_frame = nullptr;
    QAtomicInteger<int> swapchain_recreate_pending = 0;
    QAtomicInteger<int> vulkan_device_lost = 0;
    QAtomicInteger<int> renderer_cache_flush_pending = 0;
    QAtomicInteger<int> placebo_reset_pending = 0;
    QAtomicInteger<int> placebo_reset_preserve_timeline = 0;
    QAtomicInteger<int> reset_seed_capture_active = 0;
    QAtomicInteger<quint64> reset_seed_capture_generation = 0;
    QAtomicInteger<quint64> placebo_reset_throttle_generation = 0;
    QAtomicInteger<int> render_active = 0;
    QAtomicInteger<int> deferred_present_in_flight = 0;
    QAtomicInteger<int> startup_video_visible_pending = 0;
    QAtomicInteger<int> startup_first_real_frame_queued_pending_visibility = 0;
    QAtomicInteger<int> loading_transition_complete = 0;
    QAtomicInteger<int> startup_video_visible_refresh_pending = 0;
    QAtomicInteger<int> stats_overlay_active = 0;
    StatsOverlayWidget *stats_overlay_widget = nullptr;
    bool stats_overlay_visible = false;
    QAtomicInteger<quint64> startup_video_visible_generation = 0;
    bool startup_warmup_frame_active = false;
    bool vulkan_deferred_swap_enabled = false;
    bool present_vsync_enabled = true;

    QVulkanInstance *qt_vk_inst = {};
    QOpenGLContext *qt_gl_context = {};
    QOffscreenSurface *qt_gl_offscreen_surface = {};
    QQmlEngine *qml_engine = {};
    QQuickWindow *quick_window = {};
    QQuickRenderControl *quick_render = {};
    QQuickItem *quick_item = {};
    BufferedPlaybackPacerThread *buffered_pace_thread = {};
    DeferredPresentPacerThread *present_pace_thread = {};
    DeferredSwapThread *deferred_swap_thread = {};
    VkFormat quick_vk_format = VK_FORMAT_UNDEFINED;
    pl_tex quick_tex = {};
    QOpenGLFramebufferObject *quick_fbo = {};
    VkSemaphore quick_sem = VK_NULL_HANDLE;
    uint64_t quick_sem_value = 0;
    VkImage quick_vk_image = VK_NULL_HANDLE;
    bool quick_frame = false;
    void pswrapNoteSyncBlock(qint64 block_us);
    QAtomicInteger<int> quick_need_sync = 0;
    QAtomicInteger<int> quick_need_render = 0;
    qint64 quick_begin_wait_last_us = 0;
    int quick_render_skip_next = 0;
    QAtomicInteger<int> update_pending = 0;
    QAtomicInteger<int> schedule_frame_mixer_active = 0;
    double source_frame_interval_ms = 16.6667;
    double stream_configured_frame_interval_ms = 16.6667;
    QAtomicInteger<qint64> last_schedule_update_us = 0;
    QAtomicInteger<qint64> last_render_dispatch_us = 0;
    QAtomicInteger<qint64> last_render_entry_us = 0;
    QAtomicInteger<qint64> last_render_idle_us = 0;
    QAtomicInteger<qint64> last_swap_return_us = 0;
    QAtomicInteger<qint64> last_present_complete_us = 0;
    QAtomicInteger<qint64> last_present_target_us = 0;
    QAtomicInteger<qint64> last_present_wakeup_target_us = 0;
    QAtomicInteger<qint64> swap_to_start_gap_estimate_us = 0;
    QAtomicInteger<qint64> start_frame_block_estimate_us = 0;
    QAtomicInteger<qint64> render_submit_estimate_us = 0;
    QAtomicInteger<int> surface_create_failures = 0;
    qint64 surface_create_failure_first_us = 0;
    QAtomicInteger<int> swapchain_start_failures = 0;
    qint64 swapchain_start_failure_first_us = 0;
    QAtomicInteger<int> buffered_timer_fired = 0;
    qint64 last_update_us = 0;
    qint64 next_buffered_update_us = 0;
    qint64 last_buffered_interval_us = 0;
    QString pending_renderer_fallback_reason;
    VkPhysicalDeviceProperties vk_device_props = {};
    VkPhysicalDeviceDriverProperties vk_device_driver_props = {};
    pl_options renderparams_opts = {};
    bool renderparams_changed = false;
    const struct pl_hook *fsr_hook = nullptr;
    const struct pl_hook *fsrcnnx_hook_8 = nullptr;
    const struct pl_hook *fsrcnnx_hook_16 = nullptr;

    struct {
        PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
#if defined(Q_OS_LINUX)
        PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR;
        PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
#elif defined(Q_OS_MACOS)
        PFN_vkCreateMetalSurfaceEXT vkCreateMetalSurfaceEXT;
#elif defined(Q_OS_WIN)
        PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
#endif
        PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
        PFN_vkWaitSemaphores vkWaitSemaphores;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
        PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
    } vk_funcs;

    friend class QmlBackend;
};
