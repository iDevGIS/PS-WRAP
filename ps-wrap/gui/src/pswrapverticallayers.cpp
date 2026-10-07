// PS-WRAP: รูป / GIF ในภาพแนวตั้ง 9:16 (ดู pswrapverticallayers.h)
#include <pswrapverticallayers.h>

#include "settings.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMovie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <map>

Q_DECLARE_LOGGING_CATEGORY(pswrapRec)

namespace {

constexpr int kMaxSide = 1080;   // ย่อด้านยาวไม่เกินนี้ (canvas กว้าง 1080) — กัน VRAM/RAM บวมจาก GIF ใหญ่

// ภาพที่ render thread ใช้ (QImage แชร์ข้อมูล — copy ใต้ mutex ถูก)
struct Snap
{
	int id = 0;
	quint64 version = 0;
	QImage img;
	float cx = 0.5f, cy = 0.5f, w = 0.5f;
};

struct Shared
{
	QMutex mutex;
	std::vector<Snap> snaps;   // ลำดับวาด ล่าง → บน
};
Shared &shared()
{
	static Shared s;
	return s;
}

// render thread เท่านั้น
struct GpuEntry
{
	pl_tex tex = nullptr;
	quint64 version = 0;
};
std::map<int, GpuEntry> &gpuCache()
{
	static std::map<int, GpuEntry> c;
	return c;
}
pl_fmt &gpuFmt()   // rgba8 ของ gpu ปัจจุบัน — ล้างใน ReleaseGpu
{
	static pl_fmt f = nullptr;
	return f;
}

QSize fitSize(QSize s)
{
	if (s.isEmpty())
		return s;
	if (s.width() > kMaxSide || s.height() > kMaxSide)
		s.scale(kMaxSide, kMaxSide, Qt::KeepAspectRatio);
	return s;
}

} // namespace

PsWrapVerticalLayers::PsWrapVerticalLayers(QObject *parent, std::function<Settings *()> getter)
	: QObject(parent), settings_getter(std::move(getter))
{
	if (Settings *s = settings_getter())
		setMode(s->GetVerticalLayout());
}

PsWrapVerticalLayers::~PsWrapVerticalLayers()
{
	clearAll();
	QMutexLocker locker(&shared().mutex);
	shared().snaps.clear();
}

QString PsWrapVerticalLayers::overlaysFolder() const
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("overlays"));
}

QVariantList PsWrapVerticalLayers::layers() const
{
	QVariantList out;
	for (const Layer &l : list) {
		QVariantMap m;
		m[QStringLiteral("id")] = l.id;
		m[QStringLiteral("name")] = l.name;
		m[QStringLiteral("cx")] = l.cx;
		m[QStringLiteral("cy")] = l.cy;
		m[QStringLiteral("w")] = l.w;
		m[QStringLiteral("aspect")] = l.size.height() > 0 ? qreal(l.size.width()) / l.size.height() : 1.0;
		m[QStringLiteral("opacity")] = l.opacity;
		m[QStringLiteral("animated")] = l.movie != nullptr;
		out.append(m);
	}
	return out;
}

PsWrapVerticalLayers::Layer *PsWrapVerticalLayers::find(int id)
{
	for (Layer &l : list)
		if (l.id == id)
			return &l;
	return nullptr;
}

void PsWrapVerticalLayers::clearAll()
{
	for (Layer &l : list)
		delete l.movie;
	list.clear();
}

bool PsWrapVerticalLayers::loadLayer(Layer &l, QString *err)
{
	QImageReader probe(l.file);
	if (!probe.canRead()) {
		if (err)
			*err = tr("This file is not a supported image (PNG, JPG, GIF or SVG).");
		return false;
	}
	const bool animated = probe.supportsAnimation() && probe.imageCount() > 1;
	QSize native = probe.size();
	if (animated) {
		l.movie = new QMovie(l.file, QByteArray(), this);
		if (!l.movie->isValid()) {
			delete l.movie;
			l.movie = nullptr;
			if (err)
				*err = tr("Could not read the animation in this file.");
			return false;
		}
		l.movie->setCacheMode(QMovie::CacheNone);
		if (native.isEmpty()) {
			l.movie->jumpToFrame(0);
			native = l.movie->currentImage().size();
		}
		l.size = fitSize(native);
		if (l.size != native)
			l.movie->setScaledSize(l.size);
		const int id = l.id;
		connect(l.movie, &QMovie::frameChanged, this, [this, id](int) {
			if (Layer *x = find(id)) {
				refreshFrame(*x);
				publish();
			}
		});
		l.movie->jumpToFrame(0);
		if (animating)
			l.movie->start();
	} else {
		QImageReader reader(l.file);
		reader.setAutoTransform(true);
		const QSize fit = fitSize(native.isEmpty() ? QSize(kMaxSide, kMaxSide) : native);
		if (!native.isEmpty() && fit != native)
			reader.setScaledSize(fit);   // SVG วาดที่ขนาดนี้ (คมกว่าย่อทีหลัง)
		QImage img = reader.read();
		if (img.isNull()) {
			if (err)
				*err = tr("Could not read this image: %1").arg(reader.errorString());
			return false;
		}
		if (img.width() > kMaxSide || img.height() > kMaxSide)
			img = img.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		l.still = img.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
		l.size = l.still.size();
	}
	refreshFrame(l);
	return !l.size.isEmpty();
}

// ภาพที่ส่งให้ render = เฟรมปัจจุบัน (premultiplied) × opacity
void PsWrapVerticalLayers::refreshFrame(Layer &l)
{
	QImage src = l.movie ? l.movie->currentImage() : l.still;
	if (src.isNull())
		return;
	if (src.format() != QImage::Format_RGBA8888_Premultiplied)
		src = src.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
	if (l.opacity < 0.999f) {
		QImage out(src.size(), QImage::Format_RGBA8888_Premultiplied);
		out.fill(Qt::transparent);
		QPainter p(&out);
		p.setOpacity(l.opacity);
		p.drawImage(0, 0, src);
		p.end();
		src = out;
	}
	l.frame = src;
	l.version++;
}

void PsWrapVerticalLayers::publish()
{
	std::vector<Snap> snaps;
	snaps.reserve(list.size());
	for (const Layer &l : list) {
		if (l.frame.isNull())
			continue;
		Snap s;
		s.id = l.id;
		s.version = l.version;
		s.img = l.frame;
		s.cx = l.cx;
		s.cy = l.cy;
		s.w = l.w;
		snaps.push_back(std::move(s));
	}
	QMutexLocker locker(&shared().mutex);
	shared().snaps = std::move(snaps);
}

void PsWrapVerticalLayers::save()
{
	Settings *s = settings_getter();
	if (!s)
		return;
	QJsonArray arr;
	for (const Layer &l : list) {
		QJsonObject o;
		o[QStringLiteral("file")] = QFileInfo(l.file).fileName();   // อยู่ในโฟลเดอร์ overlays เสมอ
		o[QStringLiteral("name")] = l.name;
		o[QStringLiteral("cx")] = l.cx;
		o[QStringLiteral("cy")] = l.cy;
		o[QStringLiteral("w")] = l.w;
		o[QStringLiteral("opacity")] = l.opacity;
		arr.append(o);
	}
	s->SetVerticalLayers(mode, arr.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void PsWrapVerticalLayers::setMode(int m)
{
	mode = m;
	clearAll();
	if (Settings *s = settings_getter()) {
		const QJsonArray arr = QJsonDocument::fromJson(s->GetVerticalLayers(mode).toUtf8()).array();
		const QDir dir(overlaysFolder());
		for (const QJsonValue &v : arr) {
			const QJsonObject o = v.toObject();
			const QString name = QFileInfo(o.value(QStringLiteral("file")).toString()).fileName();   // กัน path แปลกใน settings
			if (name.isEmpty() || int(list.size()) >= maxLayers())
				continue;
			Layer l;
			l.id = next_id++;
			l.file = dir.filePath(name);
			l.name = o.value(QStringLiteral("name")).toString(name);
			l.cx = float(std::clamp(o.value(QStringLiteral("cx")).toDouble(0.5), 0.0, 1.0));
			l.cy = float(std::clamp(o.value(QStringLiteral("cy")).toDouble(0.15), 0.0, 1.0));
			l.w = float(std::clamp(o.value(QStringLiteral("w")).toDouble(0.8), 0.05, 1.5));
			l.opacity = float(std::clamp(o.value(QStringLiteral("opacity")).toDouble(1.0), 0.1, 1.0));
			QString err;
			if (!QFileInfo::exists(l.file) || !loadLayer(l, &err)) {
				qCWarning(pswrapRec) << "vertical layer skipped:" << name << err;
				delete l.movie;
				continue;
			}
			list.push_back(std::move(l));
		}
	}
	publish();
	emit layersChanged();
}

void PsWrapVerticalLayers::setAnimating(bool on)
{
	if (animating == on)
		return;
	animating = on;
	for (Layer &l : list) {
		if (!l.movie)
			continue;
		if (on)
			l.movie->start();
		else
			l.movie->stop();
	}
}

QString PsWrapVerticalLayers::addImage(const QUrl &url)
{
	const QString src = url.isLocalFile() ? url.toLocalFile() : url.toString();
	return addFile(src, QFileInfo(src).fileName());
}

void PsWrapVerticalLayers::addImageUrl(const QString &text)
{
	const QUrl url = QUrl::fromUserInput(text.trimmed());
	if (url.isLocalFile()) {
		emit addFinished(addImage(url));
		return;
	}
	if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
		emit addFinished(tr("Paste a link that starts with https://"));
		return;
	}
	if (int(list.size()) >= maxLayers()) {
		emit addFinished(tr("You can add up to %1 images per layout.").arg(maxLayers()));
		return;
	}
	constexpr qint64 kMaxBytes = 50ll * 1024 * 1024;   // GIF ใหญ่กว่านี้กิน RAM มากเกิน
	QNetworkRequest req(url);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PS-WRAP"));
	req.setTransferTimeout(30000);
	QNetworkReply *reply = nam.get(req);
	pending_downloads++;
	emit downloadingChanged();
	connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64 total) {
		if (got > kMaxBytes || total > kMaxBytes)
			reply->abort();
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
		reply->deleteLater();
		pending_downloads--;
		emit downloadingChanged();
		if (reply->error() != QNetworkReply::NoError) {
			emit addFinished(reply->error() == QNetworkReply::OperationCanceledError
				? tr("This file is larger than 50 MB.")
				: tr("Could not download the image: %1").arg(reply->errorString()));
			return;
		}
		// ลิงก์อาจไม่มีนามสกุล — addFile ดูชนิดจากเนื้อไฟล์
		const QByteArray data = reply->readAll();
		QTemporaryFile tmp(QDir(QDir::tempPath()).filePath(QStringLiteral("pswrap-overlay-XXXXXX")));
		if (!tmp.open() || tmp.write(data) != data.size()) {
			emit addFinished(tr("Could not save the downloaded image."));
			return;
		}
		tmp.close();
		QString name = QFileInfo(url.path()).fileName();
		if (name.isEmpty())
			name = url.host();
		emit addFinished(addFile(tmp.fileName(), name));
	});
}

QString PsWrapVerticalLayers::addFile(const QString &src, const QString &display_name)
{
	if (int(list.size()) >= maxLayers())
		return tr("You can add up to %1 images per layout.").arg(maxLayers());
	const QFileInfo fi(src);
	if (!fi.isFile())
		return tr("File not found: %1").arg(QDir::toNativeSeparators(src));
	// คัดลอกเข้าโฟลเดอร์ของแอป — ย้าย/ลบไฟล์ต้นฉบับแล้ว layer ยังอยู่
	const QDir dir(overlaysFolder());
	if (!dir.mkpath(QStringLiteral(".")))
		return tr("Could not create the folder %1.").arg(QDir::toNativeSeparators(dir.path()));
	QString suffix = fi.suffix().toLower();
	if (suffix.isEmpty() || suffix.size() > 4) {   // ไฟล์จากลิงก์ (ไฟล์ชั่วคราวไม่มีนามสกุล): ใช้ชนิดจากเนื้อไฟล์
		QImageReader probe(src);
		suffix = QString::fromLatin1(probe.format()).toLower();
	}
	const QString dst = dir.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix));
	if (!QFile::copy(fi.absoluteFilePath(), dst))
		return tr("Could not copy the file to %1.").arg(QDir::toNativeSeparators(dir.path()));

	Layer l;
	l.id = next_id++;
	l.file = dst;
	l.name = display_name.isEmpty() ? fi.fileName() : display_name;
	QString err;
	if (!loadLayer(l, &err)) {
		delete l.movie;
		QFile::remove(dst);
		return err;
	}
	// วางกลางด้านบน · ภาพแนวตั้ง/สี่เหลี่ยม → แคบลง ไม่ให้สูงเกินพื้นที่ว่าง
	const float aspect = l.size.height() > 0 ? float(l.size.width()) / l.size.height() : 1.0f;
	l.w = std::min(0.8f, 0.28f * aspect * 16.0f / 9.0f);
	l.cx = 0.5f;
	l.cy = 0.16f;
	list.push_back(std::move(l));
	save();
	publish();
	emit layersChanged();
	return QString();
}

void PsWrapVerticalLayers::move(int id, qreal cx, qreal cy)
{
	Layer *l = find(id);
	if (!l)
		return;
	l->cx = float(std::clamp(cx, 0.0, 1.0));
	l->cy = float(std::clamp(cy, 0.0, 1.0));
	save();
	publish();
	emit layersChanged();
}

void PsWrapVerticalLayers::resize(int id, qreal w)
{
	Layer *l = find(id);
	if (!l)
		return;
	l->w = float(std::clamp(w, 0.05, 1.5));
	save();
	publish();
	emit layersChanged();
}

void PsWrapVerticalLayers::setOpacity(int id, qreal opacity)
{
	Layer *l = find(id);
	if (!l)
		return;
	l->opacity = float(std::clamp(opacity, 0.1, 1.0));
	refreshFrame(*l);
	save();
	publish();
	emit layersChanged();
}

void PsWrapVerticalLayers::raise(int id)
{
	auto it = std::find_if(list.begin(), list.end(), [id](const Layer &l) { return l.id == id; });
	if (it == list.end() || it + 1 == list.end())
		return;
	std::rotate(it, it + 1, list.end());
	save();
	publish();
	emit layersChanged();
}

void PsWrapVerticalLayers::lower(int id)
{
	auto it = std::find_if(list.begin(), list.end(), [id](const Layer &l) { return l.id == id; });
	if (it == list.end() || it == list.begin())
		return;
	std::rotate(list.begin(), it, it + 1);
	save();
	publish();
	emit layersChanged();
}

void PsWrapVerticalLayers::remove(int id)
{
	auto it = std::find_if(list.begin(), list.end(), [id](const Layer &l) { return l.id == id; });
	if (it == list.end())
		return;
	const QString file = it->file;
	delete it->movie;
	list.erase(it);
	save();
	publish();
	emit layersChanged();
	// ลบไฟล์ที่คัดลอกไว้ ถ้าเลย์เอาต์อื่นไม่ได้ใช้ไฟล์เดียวกัน
	if (Settings *s = settings_getter()) {
		const QString name = QFileInfo(file).fileName();
		for (int m = 0; m < 3; m++)
			if (s->GetVerticalLayers(m).contains(name))
				return;
	}
	QFile::remove(file);
}

// ---------------------------------------------------------------- render thread

void pswrapVerticalLayersAppend(pl_gpu gpu, float W, float H, std::vector<pl_overlay> &overlays, std::vector<pl_overlay_part> &parts)
{
	if (!gpu || W <= 0 || H <= 0)
		return;
	std::vector<Snap> snaps;
	{
		QMutexLocker locker(&shared().mutex);
		snaps = shared().snaps;
	}
	auto &cache = gpuCache();
	// layer ที่ถูกลบ → คืน texture
	for (auto it = cache.begin(); it != cache.end();) {
		const bool alive = std::any_of(snaps.begin(), snaps.end(), [&](const Snap &s) { return s.id == it->first; });
		if (alive) {
			++it;
			continue;
		}
		pl_tex_destroy(gpu, &it->second.tex);
		it = cache.erase(it);
	}
	if (snaps.empty())
		return;

	pl_fmt &fmt = gpuFmt();
	if (!fmt)
		fmt = pl_find_named_fmt(gpu, "rgba8");
	if (!fmt || !(fmt->caps & PL_FMT_CAP_SAMPLEABLE))
		return;

	for (const Snap &s : snaps) {
		if (parts.size() >= parts.capacity())
			break;   // ผู้เรียก reserve ไม่พอ — ไม่ให้ vector ย้ายที่ (overlay เดิมชี้ part อยู่)
		const QImage &img = s.img;
		if (img.isNull())
			continue;
		GpuEntry &e = cache[s.id];
		if (!e.tex || e.version != s.version) {
			pl_tex_params tp = {};
			tp.w = img.width();
			tp.h = img.height();
			tp.format = fmt;
			tp.sampleable = true;
			tp.host_writable = true;
			tp.debug_tag = PL_DEBUG_TAG;
			if (!pl_tex_recreate(gpu, &e.tex, &tp))
				continue;
			pl_tex_transfer_params xfer = {};
			xfer.tex = e.tex;
			xfer.row_pitch = size_t(img.bytesPerLine());
			xfer.ptr = const_cast<uchar *>(img.constBits());
			if (!pl_tex_upload(gpu, &xfer))
				continue;
			e.version = s.version;
		}
		const float aspect = float(img.width()) / float(img.height());
		const float w = s.w * W, h = w / aspect;
		const float cx = s.cx * W, cy = s.cy * H;
		pl_overlay_part part = {};
		part.src = {0, 0, float(img.width()), float(img.height())};
		part.dst = {cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f};
		parts.push_back(part);

		pl_overlay ov = {};
		ov.tex = e.tex;
		ov.mode = PL_OVERLAY_NORMAL;
		ov.repr = pl_color_repr_rgb;
		ov.repr.alpha = PL_ALPHA_PREMULTIPLIED;
		ov.color = pl_color_space_srgb;
		ov.parts = &parts.back();
		ov.num_parts = 1;
		overlays.push_back(ov);
	}
}

void pswrapVerticalLayersReleaseGpu(pl_gpu gpu)
{
	if (!gpu)
		return;
	for (auto &kv : gpuCache())
		if (kv.second.tex)
			pl_tex_destroy(gpu, &kv.second.tex);
	gpuCache().clear();
	gpuFmt() = nullptr;
}
