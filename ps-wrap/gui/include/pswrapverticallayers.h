// PS-WRAP: รูป / GIF ที่วางในภาพแนวตั้ง 9:16 (แบนเนอร์ โลโก้ ฯลฯ) — ใช้ร่วมกันทั้ง preview, Record 9:16 และ Go Live แนวตั้ง
//
// GUI thread (PsWrapVerticalLayers): โหลดไฟล์ (QImageReader / QMovie สำหรับ GIF) → ภาพ RGBA premultiplied (คูณความโปร่งแล้ว)
//   → snapshot ใต้ mutex พร้อมเลข version · เก็บต่อเลย์เอาต์ใน settings (JSON) · ไฟล์ถูกคัดลอกไปโฟลเดอร์ overlays ของแอป
// render thread (pswrapVerticalLayersAppend): อัปโหลดเป็น pl_tex เฉพาะ layer ที่ version เปลี่ยน → pl_overlay ต่อท้ายให้ pswrapRenderVertical
// ตำแหน่ง layer = สัดส่วนของ canvas 9:16 (จุดกึ่งกลาง cx,cy + ความกว้าง w) · ความสูงคิดจากสัดส่วนภาพ
#ifndef PSWRAP_VERTICAL_LAYERS_H
#define PSWRAP_VERTICAL_LAYERS_H

#include <libplacebo/renderer.h>

#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QVariantList>

#include <functional>
#include <vector>

class QMovie;
class Settings;

class PsWrapVerticalLayers : public QObject
{
	Q_OBJECT
	// [{id, name, cx, cy, w, aspect, opacity, animated}] ล่างสุด → บนสุด (ลำดับวาด)
	Q_PROPERTY(QVariantList layers READ layers NOTIFY layersChanged)
	Q_PROPERTY(int maxLayers READ maxLayers CONSTANT)
	Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged)

public:
	PsWrapVerticalLayers(QObject *parent, std::function<Settings *()> settings_getter);
	~PsWrapVerticalLayers() override;

	QVariantList layers() const;
	static int maxLayers() { return 8; }
	bool downloading() const { return pending_downloads > 0; }

	void setMode(int mode);          // เปลี่ยนเลย์เอาต์ → โหลดชุดของเลย์เอาต์นั้น
	void setAnimating(bool on);      // GIF เล่นเฉพาะตอนมีสตรีม (ไม่กิน CPU ตอนอยู่หน้าแรก)

	Q_INVOKABLE QString addImage(const QUrl &file);   // "" = สำเร็จ · ไม่งั้นข้อความ error
	// ลิงก์ http(s) (เช่น Giphy) → ดาวน์โหลดแบบ async แล้วเพิ่ม · ผลแจ้งทาง addFinished(error) ("" = สำเร็จ)
	Q_INVOKABLE void addImageUrl(const QString &url);
	Q_INVOKABLE void move(int id, qreal cx, qreal cy);
	Q_INVOKABLE void resize(int id, qreal w);
	Q_INVOKABLE void setOpacity(int id, qreal opacity);
	Q_INVOKABLE void raise(int id);    // ขึ้นบนสุด
	Q_INVOKABLE void lower(int id);    // ลงล่างสุด
	Q_INVOKABLE void remove(int id);
	Q_INVOKABLE QString overlaysFolder() const;

signals:
	void layersChanged();
	void downloadingChanged();
	void addFinished(const QString &error);

private:
	struct Layer
	{
		int id = 0;
		QString file;          // path ในโฟลเดอร์ overlays
		QString name;          // ชื่อไฟล์ต้นฉบับ (โชว์ในเมนู)
		float cx = 0.5f, cy = 0.15f, w = 0.8f, opacity = 1.0f;
		QSize size;            // ขนาดภาพหลังย่อ
		QMovie *movie = nullptr;
		QImage still;          // ภาพนิ่ง (premultiplied ยังไม่คูณ opacity)
		QImage frame;          // ภาพที่ส่งให้ render (คูณ opacity แล้ว)
		quint64 version = 0;
	};

	std::function<Settings *()> settings_getter;
	std::vector<Layer> list;
	int mode = 0;
	int next_id = 1;
	bool animating = false;
	QNetworkAccessManager nam;
	int pending_downloads = 0;
	QString addFile(const QString &path, const QString &display_name);

	bool loadLayer(Layer &l, QString *err);
	void refreshFrame(Layer &l);
	void publish();     // snapshot → render thread
	void save();
	void clearAll();
	Layer *find(int id);
};

// render thread เท่านั้น: ต่อ pl_overlay ของทุก layer ลง overlays (1 part ต่อ overlay) · W,H = ขนาด canvas
// overlay ชี้ part ใน `parts` → ผู้เรียกต้อง reserve parts ไว้ ≥ maxLayers() + ส่วนที่จะต่อเอง ก่อนเรียก (กัน vector ย้ายที่)
// overlay.parts ยังชี้ index เดิมได้เพราะไม่ย้าย · ห้าม push เกิน capacity จนกว่าจะ render เสร็จ
void pswrapVerticalLayersAppend(pl_gpu gpu, float W, float H, std::vector<pl_overlay> &overlays, std::vector<pl_overlay_part> &parts);
void pswrapVerticalLayersReleaseGpu(pl_gpu gpu);   // ตอนปิด renderer (render thread จบแล้ว)

#endif // PSWRAP_VERTICAL_LAYERS_H
