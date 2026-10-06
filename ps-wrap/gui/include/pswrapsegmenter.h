// PS-WRAP: AI person segmentation สำหรับ facecam (ตัดพื้นหลังโดยไม่ใช้ฉากเขียว)
// - โมเดล MediaPipe Selfie Segmentation (ONNX, 256×256) รันบน CPU ผ่าน ONNX Runtime
// - ONNX Runtime โหลดตอนรัน (LoadLibrary onnxruntime.dll ข้าง exe) ผ่าน C API → ไม่ต้อง link/ไม่ชน compiler · ไม่มี DLL = ปิดฟีเจอร์เงียบๆ
// - เฟรมเข้า inputSink → ส่งต่อ forwardSink (ภาพปกติ) + ทุกเฟรมที่ worker ว่างรัน inference → mask 256×256 ส่งไป maskSink
#pragma once

#include <QObject>
#include <QVideoSink>
#include <QVideoFrame>
#include <QPointer>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QImage>
#include <atomic>
#include <vector>

class PswrapSegmenterWorker;

class PswrapSegmenter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QVideoSink *inputSink READ inputSink CONSTANT)
    Q_PROPERTY(QVideoSink *forwardSink READ forwardSink WRITE setForwardSink NOTIFY forwardSinkChanged)
    Q_PROPERTY(QVideoSink *maskSink READ maskSink WRITE setMaskSink NOTIFY maskSinkChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY frameSizeChanged)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY frameSizeChanged)
    Q_PROPERTY(int inferenceMs READ inferenceMs NOTIFY inferenceMsChanged)
    Q_PROPERTY(qreal smoothing READ smoothing WRITE setSmoothing NOTIFY smoothingChanged)

public:
    explicit PswrapSegmenter(QObject *parent = nullptr);
    ~PswrapSegmenter() override;

    bool enabled() const { return is_enabled; }
    void setEnabled(bool v);
    QVideoSink *inputSink() const { return input_sink; }
    QVideoSink *forwardSink() const { return forward_sink; }
    void setForwardSink(QVideoSink *s);
    QVideoSink *maskSink() const { return mask_sink; }
    void setMaskSink(QVideoSink *s);
    bool ready() const { return is_ready; }
    bool available() const;   // onnxruntime.dll + โมเดล มีอยู่ข้าง exe
    QString error() const { return last_error; }
    int frameWidth() const { return frame_w; }
    int frameHeight() const { return frame_h; }
    int inferenceMs() const { return infer_ms; }
    qreal smoothing() const { return smooth; }
    void setSmoothing(qreal v);

    Q_INVOKABLE void feed(const QVideoFrame &frame);   // ทางเลือก: ป้อนเฟรมตรงๆ (DshowCamera.frameReady)

    static QString runtimePath();
    static QString modelPath();

    // จาก worker
    void deliverMask(const QVideoFrame &mask, int ms);
    void reportReady(bool ok, const QString &err);

signals:
    void enabledChanged();
    void forwardSinkChanged();
    void maskSinkChanged();
    void readyChanged();
    void errorChanged();
    void frameSizeChanged();
    void inferenceMsChanged();
    void smoothingChanged();

private:
    void onInputFrame(const QVideoFrame &frame);
    void startWorker();
    void stopWorker();

    bool is_enabled = false;
    bool is_ready = false;
    QString last_error;
    int frame_w = 0, frame_h = 0;
    int infer_ms = 0;
    qreal smooth = 0.6;
    QVideoSink *input_sink = nullptr;
    QPointer<QVideoSink> forward_sink;
    QPointer<QVideoSink> mask_sink;
    PswrapSegmenterWorker *worker = nullptr;
};

class PswrapSegmenterWorker : public QThread
{
    Q_OBJECT
public:
    explicit PswrapSegmenterWorker(PswrapSegmenter *owner);
    ~PswrapSegmenterWorker() override;
    void submit(const QVideoFrame &frame);   // mailbox: เก็บเฟรมล่าสุดเท่านั้น
    void requestStop();
    void setSmoothing(qreal v) { smoothing.store(static_cast<int>(v * 1000)); }

protected:
    void run() override;

private:
    bool loadRuntime(QString &err);
    bool runInference(const QVideoFrame &frame, QVideoFrame &mask_out);

    PswrapSegmenter *owner;
    QMutex mutex;
    QWaitCondition cond;
    QVideoFrame pending;
    bool has_pending = false;
    std::atomic<int> stop_flag { 0 };
    std::atomic<int> smoothing { 600 };
    std::vector<float> input_buf;
    std::vector<float> prev_mask;
    struct Ort;
    Ort *ort = nullptr;
};
