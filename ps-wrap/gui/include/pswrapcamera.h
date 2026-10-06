// PS-WRAP: กล้อง DirectShow ผ่าน libavdevice — ให้ facecam ใช้ virtual camera ได้ (NVIDIA Broadcast, OBS Virtual Camera, Streamlabs)
// Qt Multimedia backend WMF ไม่ enumerate DirectShow software device พวกนี้ จึงดึงเองด้วย ffmpeg ที่ chiaki-ng ใช้อยู่แล้ว
#pragma once

#include <QObject>
#include <QtGlobal>
#include <QStringList>
#include <QThread>
#include <QVideoFrame>
#include <QPointer>
#include <QVideoSink>
#include <atomic>

class PswrapCameraWorker;

class PswrapCamera : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QStringList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString fakeSource READ fakeSource CONSTANT)
    Q_PROPERTY(QString appDir READ appDir CONSTANT)   // PS-WRAP: โฟลเดอร์ exe (โหลด fx/*.png ภาพจริง)   // PS-WRAP test: env PSWRAP_FAKE_CAM = ไฟล์วิดีโอแทนกล้อง

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
    QStringList devices() const { return device_list; }
    QString fakeSource() const { return qEnvironmentVariable("PSWRAP_FAKE_CAM"); }
    QString appDir() const;

    Q_INVOKABLE void refreshDevices();
    // ชื่อ DirectShow video input ทั้งหมด (รวม software/virtual) — COM enumeration
    static QStringList enumerateDirectShowDevices();

    // เรียกจาก worker thread
    void deliverFrame(const QVideoFrame &frame);
    void reportError(const QString &msg);
    void reportRunning(bool r);

signals:
    void deviceNameChanged();
    void activeChanged();
    void videoSinkChanged();
    void runningChanged();
    void errorChanged();
    void devicesChanged();
    // PS-WRAP part 2: hook ให้ตัวตัดพื้นหลัง (AI) ดักเฟรม
    void frameReady(const QVideoFrame &frame);

private:
    void restart();
    void stopWorker();

    QString device_name;
    bool is_active = false;
    bool is_running = false;
    QString last_error;
    QStringList device_list;
    QPointer<QVideoSink> sink;
    PswrapCameraWorker *worker = nullptr;
};

class PswrapCameraWorker : public QThread
{
    Q_OBJECT
public:
    PswrapCameraWorker(PswrapCamera *owner, const QString &device);
    void requestStop() { stop_flag.store(1); }
    bool stopRequested() const { return stop_flag.load() != 0; }

protected:
    void run() override;

private:
    PswrapCamera *owner;
    QString device;
    std::atomic<int> stop_flag { 0 };
};
