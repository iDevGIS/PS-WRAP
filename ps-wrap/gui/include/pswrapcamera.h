// PS-WRAP: กล้อง DirectShow ผ่าน libavdevice — ให้ facecam ใช้ virtual camera ได้ (NVIDIA Broadcast, OBS Virtual Camera, Streamlabs)
// Qt Multimedia backend WMF ไม่ enumerate DirectShow software device พวกนี้ จึงดึงเองด้วย ffmpeg ที่ chiaki-ng ใช้อยู่แล้ว
// 2026-10-06: ใช้กับกล้อง USB จริงด้วย (WebcamOverlay) — ปิดกล้องแบบไม่บล็อก GUI thread:
//   - stop = ตั้ง flag แล้วปล่อย worker ไปปิด device เอง (avformat_close_input บน worker) แล้ว deleteLater ตัวเอง ไม่ wait/terminate บน GUI
//   - เปิดซ้ำ device เดิมระหว่างตัวเก่ายังปิดไม่เสร็จ → worker ใหม่รอ (ต่อชื่อ device) จนตัวเก่าปล่อย
//   - ตอนออกจากแอป: post routine รอ worker ที่ยังปิดอยู่ (สูงสุด ~4 s) กัน process ตายกลางสตรีมกล้อง (UVC ค้าง)
#pragma once

#include <QObject>
#include <QtGlobal>
#include <QStringList>
#include <QThread>
#include <QVideoFrame>
#include <QPointer>
#include <QVideoSink>
#include <QElapsedTimer>
#include <atomic>

class PswrapCameraWorker;
class QTimer;
struct AVFormatContext;

class PswrapCamera : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)          // device เปิดสำเร็จ (ยังไม่แปลว่ามีเฟรม — ดู state)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)             // รายละเอียดดิบ (ชื่อ device + FFmpeg error) สำหรับ log/tooltip
    // สถานะสำหรับ UI: "idle" (ไม่ได้เปิด) · "opening" (กำลังเปิด/รอเฟรมแรก) · "live" (มีเฟรมเข้า) · "error" (เปิดไม่ได้ / ไม่มีเฟรมใน ~3 s / เฟรมหยุด)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged) // ข้อความให้ผู้ใช้ (แปลได้) — ว่างเมื่อไม่รู้สาเหตุ
    Q_PROPERTY(int framesReceived READ framesReceived NOTIFY framesReceivedChanged)   // นับตั้งแต่เปิดรอบล่าสุด · notify เฟรมแรก + ทุก ~0.5 s (ไม่ทุกเฟรม)
    Q_PROPERTY(QStringList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString fakeSource READ fakeSource CONSTANT)
    Q_PROPERTY(QString appDir READ appDir CONSTANT)   // PS-WRAP: โฟลเดอร์ exe (โหลด fx/*.png ภาพจริง)   // PS-WRAP test: env PSWRAP_FAKE_CAM = ไฟล์วิดีโอแทนกล้อง
    Q_PROPERTY(bool forceQtBackend READ forceQtBackend CONSTANT)   // env PSWRAP_CAM_BACKEND=qt → WebcamOverlay ใช้ Qt Camera (WMF) กับกล้องที่ Qt เห็น แบบเดิม

public:
    explicit PswrapCamera(QObject *parent = nullptr);
    ~PswrapCamera() override;

    QString deviceName() const { return device_name; }
    void setDeviceName(const QString &name);
    bool active() const { return is_active; }
    void setActive(bool v);
    QVideoSink *videoSink() const { return sink; }
    void setVideoSink(QVideoSink *s);
    bool running() const { return is_running; }
    QString error() const { return last_error; }
    QString state() const { return cam_state; }
    QString errorString() const { return error_string; }
    int framesReceived() const { return frames_received; }
    QStringList devices() const { return device_list; }
    QString fakeSource() const { return qEnvironmentVariable("PSWRAP_FAKE_CAM"); }
    QString appDir() const;
    bool forceQtBackend() const { return qEnvironmentVariable("PSWRAP_CAM_BACKEND").compare(QStringLiteral("qt"), Qt::CaseInsensitive) == 0; }

    Q_INVOKABLE void refreshDevices();
    // ชื่อ DirectShow video input ทั้งหมด (รวม software/virtual) — COM enumeration
    static QStringList enumerateDirectShowDevices();

signals:
    void deviceNameChanged();
    void activeChanged();
    void videoSinkChanged();
    void runningChanged();
    void errorChanged();
    void stateChanged();
    void framesReceivedChanged();
    void devicesChanged();
    // PS-WRAP part 2: hook ให้ตัวตัดพื้นหลัง (AI) ดักเฟรม
    void frameReady(const QVideoFrame &frame);

private:
    void restart();
    void stopWorker();
    void setState(const QString &s, const QString &user_error = QString());
    void setRunning(bool r);
    void onWatchdog();

    QString device_name;
    bool is_active = false;
    bool is_running = false;
    QString last_error;
    QString cam_state = QStringLiteral("idle");
    QString error_string;
    int frames_received = 0;
    int frames_notified = 0;
    QStringList device_list;
    QPointer<QVideoSink> sink;
    QPointer<PswrapCameraWorker> worker;   // worker ปัจจุบัน (ตัวที่สั่งหยุดแล้วไม่อยู่ที่นี่ — ลบตัวเองตอน finished)
    quint64 generation = 0;                // สัญญาณจาก worker รุ่นเก่าที่ค้างในคิว → ทิ้ง
    QTimer *watchdog = nullptr;
    QElapsedTimer open_clock;
    QElapsedTimer frame_clock;
};

class PswrapCameraWorker : public QThread
{
    Q_OBJECT
public:
    explicit PswrapCameraWorker(const QString &device);
    // thread-safe · ไม่บล็อก: แค่ตั้ง flag (av_read_frame/open ถูกตัดผ่าน AVIOInterruptCB) + ลงทะเบียน "กำลังปิด" ของ device นี้
    void requestStop();
    bool stopRequested() const { return stop_flag.load() != 0; }
    bool interruptNow();             // AVIOInterruptCB: หยุด หรือ หมดเวลาช่วงเปิด/probe
    QString deviceName() const { return device; }

    // ตอนออกจากแอป: สั่งหยุด worker ทุกตัวแล้วรอให้ปล่อย device (ms) — คืน false ถ้ายังมีตัวค้าง
    static bool stopAllAndWait(int timeout_ms);

signals:
    void opened(int width, int height, const QString &codec);
    void frameCaptured(const QVideoFrame &frame);
    void failed(const QString &user_msg, const QString &detail);
    void closed();

protected:
    void run() override;

private:
    void requestStopLocked();
    int openInput(AVFormatContext **ctx, bool with_hints, QString *detail);

    QString device;
    std::atomic<int> stop_flag { 0 };
    std::atomic<qint64> deadline_ms { -1 };   // เวลาที่ช่วงเปิด/probe ต้องเสร็จ (นับจาก run_clock) · -1 = ไม่มี
    std::atomic<int> throttle_spin { 0 };     // probe แบบ NONBLOCK วนถี่ → interrupt cb หน่วง 1 ms กัน CPU 100%
    QElapsedTimer run_clock;
    // ป้องกันด้วย mutex ของ registry (pswrapcamera.cpp)
    bool closing_registered = false;
    bool done = false;
};
