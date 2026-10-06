// PS-WRAP: face tracker v2 สำหรับ facecam effect — 2 ขั้น
//  1) BlazeFace (128×128, NMS ในตัว) หา bbox เมื่อยังไม่มีหน้า/หลุด
//  2) MediaPipe Face Landmarker (478 จุด, 256×256 crop) ติดตามจากครอปของเฟรมก่อน (เหมือน MediaPipe) → จุดบนผิวหน้าจริง + head pose (yaw/pitch/roll)
//  กรองสั่นด้วย 1-Euro filter · ทุกพิกัดเป็น normalized 0..1 ของเฟรมกล้อง · โครงเดียวกับ PswrapSegmenter (inputSink → forwardSink + worker mailbox)
#pragma once

#include <QObject>
#include <QVideoSink>
#include <QVideoFrame>
#include <QPointer>
#include <QPointF>
#include <QRectF>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <atomic>
#include <vector>
#include <array>
#include <cmath>

class PswrapFaceTrackerWorker;

struct PswrapFaceResult
{
    bool found = false;
    bool mesh = false;            // true = จุดจาก face mesh (478), false = จาก BlazeFace (6 จุดหยาบ)
    QPointF eyeL, eyeR;           // กึ่งกลางตา (ตาที่อยู่ซ้ายในภาพก่อน)
    QPointF eyeOuterL, eyeOuterR; // หางตาด้านนอก
    QPointF noseTip, noseBridge;
    QPointF mouthTop, mouthBottom, mouthL, mouthR;
    QPointF forehead, chin, templeL, templeR;
    QRectF rect;
    qreal yaw = 0, pitch = 0, roll = 0;   // องศา (ภาพกล้องดิบ ก่อน mirror)
    qreal faceWidth = 0;                   // ระยะขมับ-ขมับ จริง (3D, สัดส่วนของความกว้างเฟรม)
    qreal faceHeight = 0;                  // หน้าผาก-คาง จริง (สัดส่วนของความกว้างเฟรม)
    int ms = 0;
};

class PswrapFaceTracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QVideoSink *inputSink READ inputSink CONSTANT)
    Q_PROPERTY(QVideoSink *forwardSink READ forwardSink WRITE setForwardSink NOTIFY forwardSinkChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool meshAvailable READ meshAvailable CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY frameSizeChanged)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY frameSizeChanged)
    Q_PROPERTY(bool faceFound READ faceFound NOTIFY faceChanged)
    Q_PROPERTY(bool meshActive READ meshActive NOTIFY faceChanged)
    Q_PROPERTY(QPointF leftEye READ leftEye NOTIFY faceChanged)
    Q_PROPERTY(QPointF rightEye READ rightEye NOTIFY faceChanged)
    Q_PROPERTY(QPointF eyeOuterL READ eyeOuterL NOTIFY faceChanged)
    Q_PROPERTY(QPointF eyeOuterR READ eyeOuterR NOTIFY faceChanged)
    Q_PROPERTY(QPointF nose READ nose NOTIFY faceChanged)
    Q_PROPERTY(QPointF noseBridge READ noseBridge NOTIFY faceChanged)
    Q_PROPERTY(QPointF mouth READ mouth NOTIFY faceChanged)
    Q_PROPERTY(QPointF mouthBottom READ mouthBottom NOTIFY faceChanged)
    Q_PROPERTY(QPointF mouthL READ mouthL NOTIFY faceChanged)
    Q_PROPERTY(QPointF mouthR READ mouthR NOTIFY faceChanged)
    Q_PROPERTY(QPointF forehead READ forehead NOTIFY faceChanged)
    Q_PROPERTY(QPointF chin READ chin NOTIFY faceChanged)
    Q_PROPERTY(QPointF templeL READ templeL NOTIFY faceChanged)
    Q_PROPERTY(QPointF templeR READ templeR NOTIFY faceChanged)
    Q_PROPERTY(QRectF faceRect READ faceRect NOTIFY faceChanged)
    Q_PROPERTY(qreal yaw READ yaw NOTIFY faceChanged)
    Q_PROPERTY(qreal pitch READ pitch NOTIFY faceChanged)
    Q_PROPERTY(qreal roll READ roll NOTIFY faceChanged)
    Q_PROPERTY(qreal faceWidth READ faceWidth NOTIFY faceChanged)
    Q_PROPERTY(qreal faceHeight READ faceHeight NOTIFY faceChanged)
    Q_PROPERTY(int inferenceMs READ inferenceMs NOTIFY inferenceMsChanged)
    Q_PROPERTY(qreal smoothing READ smoothing WRITE setSmoothing NOTIFY smoothingChanged)

public:
    explicit PswrapFaceTracker(QObject *parent = nullptr);
    ~PswrapFaceTracker() override;

    bool enabled() const { return is_enabled; }
    void setEnabled(bool v);
    QVideoSink *inputSink() const { return input_sink; }
    QVideoSink *forwardSink() const { return forward_sink; }
    void setForwardSink(QVideoSink *s);
    bool ready() const { return is_ready; }
    bool available() const;
    bool meshAvailable() const;
    QString error() const { return last_error; }
    int frameWidth() const { return frame_w; }
    int frameHeight() const { return frame_h; }
    bool faceFound() const { return res.found; }
    bool meshActive() const { return res.found && res.mesh; }
    QPointF leftEye() const { return res.eyeL; }
    QPointF rightEye() const { return res.eyeR; }
    QPointF eyeOuterL() const { return res.eyeOuterL; }
    QPointF eyeOuterR() const { return res.eyeOuterR; }
    QPointF nose() const { return res.noseTip; }
    QPointF noseBridge() const { return res.noseBridge; }
    QPointF mouth() const { return res.mouthTop; }
    QPointF mouthBottom() const { return res.mouthBottom; }
    QPointF mouthL() const { return res.mouthL; }
    QPointF mouthR() const { return res.mouthR; }
    QPointF forehead() const { return res.forehead; }
    QPointF chin() const { return res.chin; }
    QPointF templeL() const { return res.templeL; }
    QPointF templeR() const { return res.templeR; }
    QRectF faceRect() const { return res.rect; }
    qreal yaw() const { return res.yaw; }
    qreal pitch() const { return res.pitch; }
    qreal roll() const { return res.roll; }
    qreal faceWidth() const { return res.faceWidth; }
    qreal faceHeight() const { return res.faceHeight; }
    int inferenceMs() const { return infer_ms; }
    qreal smoothing() const { return smooth; }
    void setSmoothing(qreal v);

    Q_INVOKABLE void feed(const QVideoFrame &frame);
    static QString modelPath();       // BlazeFace
    static QString meshModelPath();   // Face Landmarker

    // จาก worker
    void deliverFace(const PswrapFaceResult &r);
    void reportReady(bool ok, const QString &err);

signals:
    void enabledChanged();
    void forwardSinkChanged();
    void readyChanged();
    void errorChanged();
    void frameSizeChanged();
    void faceChanged();
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
    PswrapFaceResult res;
    int infer_ms = 0;
    qreal smooth = 0.5;
    QVideoSink *input_sink = nullptr;
    QPointer<QVideoSink> forward_sink;
    PswrapFaceTrackerWorker *worker = nullptr;
};

// 1-Euro filter (Casiez 2012) — นุ่มตอนนิ่ง ตามทันตอนขยับไว
struct PswrapOneEuro
{
    double min_cutoff = 1.5, beta = 6.0, d_cutoff = 1.0;
    bool init = false;
    double x_prev = 0, dx_prev = 0;
    static double alpha(double cutoff, double dt) { const double tau = 1.0 / (2.0 * 3.14159265358979 * cutoff); return 1.0 / (1.0 + tau / dt); }
    double filter(double x, double dt)
    {
        if (!init) { init = true; x_prev = x; dx_prev = 0; return x; }
        const double dx = (x - x_prev) / dt;
        const double ad = alpha(d_cutoff, dt);
        const double dxh = ad * dx + (1 - ad) * dx_prev;
        const double cutoff = min_cutoff + beta * std::abs(dxh);
        const double a = alpha(cutoff, dt);
        const double xh = a * x + (1 - a) * x_prev;
        x_prev = xh; dx_prev = dxh;
        return xh;
    }
    void reset() { init = false; }
};

class PswrapFaceTrackerWorker : public QThread
{
    Q_OBJECT
public:
    explicit PswrapFaceTrackerWorker(PswrapFaceTracker *owner);
    ~PswrapFaceTrackerWorker() override;
    void submit(const QVideoFrame &frame);
    void requestStop();
    void setSmoothing(qreal v) { smoothing.store(static_cast<int>(v * 1000)); }

protected:
    void run() override;

private:
    struct Ort;
    bool loadRuntime(QString &err);
    bool detectBlaze(const QImage &img, bool &found, float out[16]);
    bool runMesh(const QImage &img, const QRectF &crop_norm, std::vector<float> &pts, float &presence);
    void applyFilters(PswrapFaceResult &r, double dt);

    PswrapFaceTracker *owner;
    QMutex mutex;
    QWaitCondition cond;
    QVideoFrame pending;
    bool has_pending = false;
    std::atomic<int> stop_flag { 0 };
    std::atomic<int> smoothing { 500 };
    std::vector<float> blaze_buf;
    std::vector<float> mesh_buf;
    Ort *ort = nullptr;
    bool mesh_ok = false;
    // tracking state
    bool have_face = false;
    QRectF track_crop;            // ครอปจัตุรัส (normalized ของเฟรม) สำหรับ mesh เฟรมถัดไป
    int miss_count = 0;
    QElapsedTimer clock;
    qint64 last_ns = 0;
    std::array<PswrapOneEuro, 40> filt;   // 14 จุด ×2 + yaw/pitch/roll + faceW/faceH + rect 4 = 37
};
