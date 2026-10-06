// PS-WRAP: preset รายเกม — ดูคำอธิบายใน pswrapgameprofiles.h
#include "pswrapgameprofiles.h"

#include "qmlbackend.h"
#include "qmlsettings.h"
#include "settings.h"
#include "streamsession.h"

#include <chiaki/session.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMetaProperty>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui);

namespace {

// ช่องที่ preset ตั้งได้ · ชนิด b = bool, i = int (ช่วงค่าดู clampField)
// video: ใช้กับ PS5 ในบ้านเท่านั้น (local) — ตรงกับหน้า Settings › Stream (Local PS5)
// อื่นๆ: ชื่อ = ชื่อ Q_PROPERTY บน QmlMainWindow (Chiaki.window.*)
struct Field { const char *key; char type; bool window_prop; };
const Field kFields[] = {
	{ "resolution",    'i', false },   // ChiakiVideoResolutionPreset 1..4 (360p..1080p)
	{ "fps",           'i', false },   // 30 / 60
	{ "bitrate",       'i', false },   // kbps · 0 = อัตโนมัติตามความละเอียด
	{ "padOverlay",    'b', true },
	{ "camOverlay",    'b', true },
	{ "statsOverlay",  'b', true },
	{ "micOverlay",    'b', true },
	{ "clockOverlay",  'b', true },    // มีเมื่องาน clock overlay เข้ามาแล้ว (เช็คด้วย windowSupports)
	{ "camFx",         'i', true },    // 0..13 (WebcamOverlay.fx)
	{ "camBackground", 'i', true },    // 0 keep · 1 green · 2 blue · 3 AI
	{ "replayEnabled", 'b', true },    // Instant Replay — มีเมื่องาน replay เข้ามาแล้ว
};

const char *kSettingsKey = "pswrap/gameProfiles";

bool clampField(const Field &f, const QVariant &in, QVariant &out)
{
	if (!in.isValid() || in.isNull())
		return false;
	if (f.type == 'b') {
		out = in.toBool();
		return true;
	}
	bool ok = false;
	int v = in.toInt(&ok);
	if (!ok)
		return false;
	const QByteArray k(f.key);
	if (k == "resolution")
		ok = v >= CHIAKI_VIDEO_RESOLUTION_PRESET_360p && v <= CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
	else if (k == "fps")
		ok = v == CHIAKI_VIDEO_FPS_PRESET_30 || v == CHIAKI_VIDEO_FPS_PRESET_60;
	else if (k == "bitrate")
		ok = v >= 0 && v <= 200000;
	else if (k == "camFx")
		v = qBound(0, v, 13);
	else if (k == "camBackground")
		v = qBound(0, v, 3);
	if (!ok)
		return false;
	out = v;
	return true;
}

unsigned int defaultBitrate(int resolution)
{
	switch (resolution) {
	case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: return 2000;
	case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: return 6000;
	case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: return 10000;
	case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: return 15000;
	default: return 0;
	}
}

} // namespace

PsWrapGameProfiles::PsWrapGameProfiles(QObject *window, QmlBackend *backend, std::function<Settings *()> settings_getter)
	: QObject(window), window(window), backend(backend), settings_getter(std::move(settings_getter))
{
	load();
	if (backend) {
		connect(backend, &QmlBackend::sessionChanged, this, &PsWrapGameProfiles::onSessionChanged);
		connect(backend, &QmlBackend::hostsChanged, this, &PsWrapGameProfiles::refreshRunningGames);
		// เปลี่ยน profile = Settings ตัวใหม่ → QmlSettings::setSettings เรียก refreshAllKeys (ยิง remotePlayAskChanged) → โหลดใหม่
		if (QmlSettings *qs = backend->qmlSettings())
			connect(qs, &QmlSettings::remotePlayAskChanged, this, &PsWrapGameProfiles::reloadIfSettingsChanged);
	}
	refreshRunningGames();
}

// ---------- storage ----------

void PsWrapGameProfiles::load()
{
	Settings *s = settings();
	loaded_from = s;
	data = {};
	if (!s)
		return;
	const QByteArray raw = s->GetPsWrapGameProfiles().toUtf8();
	if (raw.isEmpty())
		return;
	QJsonParseError err{};
	const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) {
		qCWarning(chiakiGui) << "PS-WRAP game profiles: invalid JSON in settings, ignoring:" << err.errorString();
		return;
	}
	data = doc.object();
}

void PsWrapGameProfiles::store()
{
	Settings *s = settings();
	if (!s)
		return;
	s->SetPsWrapGameProfiles(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
}

void PsWrapGameProfiles::reloadIfSettingsChanged()
{
	if (settings() == loaded_from)
		return;
	load();
	emit profilesChanged();
	emit enabledChanged();
}

// ---------- QML API ----------

QVariantList PsWrapGameProfiles::profiles() const
{
	QVariantList out;
	const QJsonObject all = data.value(QStringLiteral("profiles")).toObject();
	for (auto it = all.begin(); it != all.end(); ++it) {
		QVariantMap m = it.value().toObject().toVariantMap();
		m[QStringLiteral("titleId")] = it.key();
		if (m.value(QStringLiteral("name")).toString().isEmpty())
			m[QStringLiteral("name")] = it.key();
		out.append(m);
	}
	std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
		return a.toMap().value(QStringLiteral("name")).toString().compare(
			b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
	});
	return out;
}

bool PsWrapGameProfiles::enabled() const
{
	return data.value(QStringLiteral("enabled")).toBool(true);
}

void PsWrapGameProfiles::setEnabled(bool v)
{
	if (v == enabled())
		return;
	data[QStringLiteral("enabled")] = v;
	store();
	emit enabledChanged();
}

bool PsWrapGameProfiles::hasProfile(const QString &title_id) const
{
	return !title_id.isEmpty() && data.value(QStringLiteral("profiles")).toObject().contains(title_id);
}

QVariantMap PsWrapGameProfiles::profile(const QString &title_id) const
{
	if (!hasProfile(title_id))
		return {};
	QVariantMap m = data.value(QStringLiteral("profiles")).toObject().value(title_id).toObject().toVariantMap();
	m[QStringLiteral("titleId")] = title_id;
	if (m.value(QStringLiteral("name")).toString().isEmpty())
		m[QStringLiteral("name")] = title_id;
	return m;
}

QVariantMap PsWrapGameProfiles::normalize(const QVariantMap &in)
{
	QVariantMap out;
	const QString name = in.value(QStringLiteral("name")).toString().trimmed();
	if (!name.isEmpty())
		out[QStringLiteral("name")] = name;
	for (const Field &f : kFields) {
		QVariant v;
		if (clampField(f, in.value(QString::fromLatin1(f.key)), v))
			out[QString::fromLatin1(f.key)] = v;
	}
	return out;
}

bool PsWrapGameProfiles::saveProfile(const QVariantMap &p)
{
	const QString title_id = p.value(QStringLiteral("titleId")).toString().trimmed();
	if (title_id.isEmpty())
		return false;
	QJsonObject all = data.value(QStringLiteral("profiles")).toObject();
	all[title_id] = QJsonObject::fromVariantMap(normalize(p));
	data[QStringLiteral("profiles")] = all;
	store();
	emit profilesChanged();
	return true;
}

void PsWrapGameProfiles::removeProfile(const QString &title_id)
{
	QJsonObject all = data.value(QStringLiteral("profiles")).toObject();
	if (!all.contains(title_id))
		return;
	all.remove(title_id);
	data[QStringLiteral("profiles")] = all;
	store();
	emit profilesChanged();
}

QVariantMap PsWrapGameProfiles::snapshotCurrent(const QString &title_id, const QString &name) const
{
	QVariantMap m;
	m[QStringLiteral("titleId")] = title_id;
	m[QStringLiteral("name")] = name.isEmpty() ? title_id : name;
	if (Settings *s = settings()) {
		m[QStringLiteral("resolution")] = static_cast<int>(s->GetResolutionLocalPS5());
		m[QStringLiteral("fps")] = static_cast<int>(s->GetFPSLocalPS5());
		m[QStringLiteral("bitrate")] = static_cast<int>(s->GetBitrateLocalPS5());
	}
	for (const Field &f : kFields) {
		if (!f.window_prop || !windowSupports(QString::fromLatin1(f.key)))
			continue;
		m[QString::fromLatin1(f.key)] = window->property(f.key);
	}
	return m;
}

bool PsWrapGameProfiles::windowSupports(const QString &property) const
{
	return window && window->metaObject()->indexOfProperty(property.toLatin1().constData()) >= 0;
}

QStringList PsWrapGameProfiles::fieldKeys() const
{
	QStringList out;
	for (const Field &f : kFields)
		out.append(QString::fromLatin1(f.key));
	return out;
}

void PsWrapGameProfiles::refreshRunningGames()
{
	if (!backend)
		return;
	QVariantList out;
	QStringList seen;
	const QVariantList hosts = backend->hosts();
	for (const QVariant &hv : hosts) {
		const QVariantMap h = hv.toMap();
		if (!h.value(QStringLiteral("discovered")).toBool())
			continue;
		const QString tid = h.value(QStringLiteral("titleId")).toString();
		if (tid.isEmpty() || seen.contains(tid))
			continue;
		seen.append(tid);
		QVariantMap g;
		g[QStringLiteral("titleId")] = tid;
		const QString app = h.value(QStringLiteral("app")).toString();
		g[QStringLiteral("name")] = app.isEmpty() ? tid : app;
		g[QStringLiteral("host")] = h.value(QStringLiteral("address"));
		g[QStringLiteral("console")] = h.value(QStringLiteral("name"));
		out.append(g);
	}
	if (out == running_games)
		return;
	running_games = out;
	emit runningGamesChanged();
}

// ---------- session hook ----------

bool PsWrapGameProfiles::prepareSession(StreamSessionConnectInfo &info, const QVariantList &hosts)
{
	reloadIfSettingsChanged();
	pending.clear();
	if (!enabled())
		return false;

	// หาเกมที่เครื่องนี้กำลังรัน: host ที่ discovery เจอ address ตรงกัน (PSN remote ไม่มีข้อมูล discovery → ไม่ใช้ preset)
	QString title_id, app;
	if (info.duid.isEmpty() && !info.host.isEmpty()) {
		for (const QVariant &hv : hosts) {
			const QVariantMap h = hv.toMap();
			if (!h.value(QStringLiteral("discovered")).toBool() || h.value(QStringLiteral("address")).toString() != info.host)
				continue;
			title_id = h.value(QStringLiteral("titleId")).toString();
			app = h.value(QStringLiteral("app")).toString();
			break;
		}
	}
	if (title_id.isEmpty() || !hasProfile(title_id))
		return false;

	pending = profile(title_id);
	if (!app.isEmpty())
		pending[QStringLiteral("runningName")] = app;
	qCInfo(chiakiGui) << "PS-WRAP game profile: using preset for" << title_id << pending.value(QStringLiteral("name")).toString();

	// วิดีโอ: PS5 ในบ้านเท่านั้น (เหมือนหน้า Settings › Local PS5)
	if (!chiaki_target_is_ps5(info.target))
		return false;
	const bool has_res = pending.contains(QStringLiteral("resolution"));
	const bool has_fps = pending.contains(QStringLiteral("fps"));
	const bool has_rate = pending.contains(QStringLiteral("bitrate"));
	if (!has_res && !has_fps && !has_rate)
		return false;
	Settings *s = settings();
	if (!s)
		return false;

	const int res = has_res ? pending.value(QStringLiteral("resolution")).toInt() : static_cast<int>(s->GetResolutionLocalPS5());
	const int fps = has_fps ? pending.value(QStringLiteral("fps")).toInt() : static_cast<int>(s->GetFPSLocalPS5());
	unsigned int rate;
	if (has_rate)
		rate = pending.value(QStringLiteral("bitrate")).toUInt();
	else if (has_res)
		rate = 0;   // เปลี่ยนความละเอียด = ใช้ bitrate มาตรฐานของความละเอียดนั้น (เหมือนหน้า Settings ที่รีเซ็ต bitrate เมื่อเปลี่ยนความละเอียด)
	else
		rate = s->GetBitrateLocalPS5();

	ChiakiConnectVideoProfile p = {};
	chiaki_connect_video_profile_preset(&p, static_cast<ChiakiVideoResolutionPreset>(res), static_cast<ChiakiVideoFPSPreset>(fps));
	if (rate)
		p.bitrate = rate;
	else if (!p.bitrate)
		p.bitrate = defaultBitrate(res);
	p.codec = info.video_profile.codec;   // codec/HDR ตาม setting เดิม
	info.video_profile = p;
	qCInfo(chiakiGui) << "PS-WRAP game profile: video" << p.width << "x" << p.height << "@" << p.max_fps << "bitrate" << p.bitrate;
	return true;
}

void PsWrapGameProfiles::onSessionChanged(StreamSession *s)
{
	if (s) {
		if (pending.isEmpty())
			return;
		const QVariantMap p = pending;
		pending.clear();
		applyWindowFields(p);
		const QString name = p.value(QStringLiteral("name")).toString();
		const QString tid = p.value(QStringLiteral("titleId")).toString();
		if (name != active_name || tid != active_title_id) {
			active_name = name;
			active_title_id = tid;
			emit activeProfileChanged();
		}
		return;
	}
	pending.clear();
	restoreWindowFields();
	if (!active_name.isEmpty() || !active_title_id.isEmpty()) {
		active_name.clear();
		active_title_id.clear();
		emit activeProfileChanged();
	}
}

void PsWrapGameProfiles::applyWindowFields(const QVariantMap &p)
{
	restoreWindowFields();
	if (!window)
		return;
	for (const Field &f : kFields) {
		if (!f.window_prop || !p.contains(QString::fromLatin1(f.key)))
			continue;
		if (!windowSupports(QString::fromLatin1(f.key))) {
			qCInfo(chiakiGui) << "PS-WRAP game profile: window has no property" << f.key << "- skipped";
			continue;
		}
		const QVariant before = window->property(f.key);
		QVariant after = p.value(QString::fromLatin1(f.key));
		if (f.type == 'b')
			after = after.toBool();
		else
			after = after.toInt();
		if (before == after)
			continue;
		window->setProperty(f.key, after);
		applied.append({ QByteArray(f.key), before, window->property(f.key) });
	}
}

void PsWrapGameProfiles::restoreWindowFields()
{
	if (window) {
		// คืนค่าเดิมเฉพาะช่องที่ยังเป็นค่าของ preset (ถ้าผู้ใช้สลับเองระหว่างสตรีม = เคารพค่าที่ผู้ใช้เลือก)
		for (auto it = applied.crbegin(); it != applied.crend(); ++it) {
			if (window->property(it->prop.constData()) == it->after)
				window->setProperty(it->prop.constData(), it->before);
		}
	}
	applied.clear();
}
