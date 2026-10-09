// PS-WRAP: อัปเดตในแอป — ดู include/pswrapupdater.h
#include "pswrapupdater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
const char *kReleasesApi = "https://api.github.com/repos/iDevGIS/PS-WRAP/releases?per_page=15";
const char *kReleasesPage = "https://github.com/iDevGIS/PS-WRAP/releases";
const char *kKeyAuto = "pswrap/updateAutoCheck";
const char *kKeyDismissed = "pswrap/updateDismissed";
constexpr int kPeriodMs = 12 * 60 * 60 * 1000;

// "v0.7.0" → [0,7,0] · ป้ายต่อท้าย (เช่น -test) = ไม่ใช่ release จริง → ว่าง
QList<int> parseVersion(QString v)
{
	if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
		v.remove(0, 1);
	QList<int> out;
	for (const QString &part : v.split(QLatin1Char('.'))) {
		bool ok = false;
		const int n = part.toInt(&ok);
		if (!ok)
			return {};
		out << n;
	}
	return out;
}

// release notes → ข้อความสั้นสำหรับหน้าต่างอัปเดต: ตัดรูปและส่วน Download (ลิงก์/SHA ไม่ต้องโชว์ซ้ำ)
QString trimNotes(const QString &body)
{
	QStringList keep;
	for (const QString &line : body.split(QLatin1Char('\n'))) {
		const QString t = line.trimmed();
		if (t.startsWith(QStringLiteral("### Download")))
			break;
		if (t.startsWith(QStringLiteral("![")))
			continue;
		keep << line;
	}
	return keep.join(QLatin1Char('\n')).trimmed();
}
} // namespace

PsWrapUpdater::PsWrapUpdater(QObject *parent) : QObject(parent), net(new QNetworkAccessManager(this))
{
	periodic.setInterval(kPeriodMs);
	connect(&periodic, &QTimer::timeout, this, [this]() { if (autoCheck()) check(false); });
	periodic.start();
	QTimer::singleShot(8000, this, [this]() { if (autoCheck()) check(false); });
}

QString PsWrapUpdater::currentVersion() const
{
	// ทดสอบ: PSWRAP_UPDATE_TEST_CURRENT=0.6.0 = ทำเป็นเวอร์ชันเก่า (ลองดาวน์โหลด/ติดตั้ง release ล่าสุดจริงทั้งสาย)
	const QString fake = qEnvironmentVariable("PSWRAP_UPDATE_TEST_CURRENT");
	return fake.isEmpty() ? QCoreApplication::applicationVersion() : fake;
}

int PsWrapUpdater::compareVersions(const QString &a, const QString &b)
{
	const QList<int> x = parseVersion(a), y = parseVersion(b);
	for (int i = 0; i < qMax(x.size(), y.size()); ++i) {
		const int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
		if (p != q)
			return p < q ? -1 : 1;
	}
	return 0;
}

bool PsWrapUpdater::updateAvailable() const
{
	return !latest_.isEmpty() && compareVersions(latest_, currentVersion()) > 0;
}

bool PsWrapUpdater::dismissed() const
{
	return !latest_.isEmpty() && QSettings().value(QLatin1String(kKeyDismissed)).toString() == latest_;
}

bool PsWrapUpdater::autoCheck() const
{
	return QSettings().value(QLatin1String(kKeyAuto), true).toBool();
}

void PsWrapUpdater::setAutoCheck(bool on)
{
	if (on == autoCheck())
		return;
	QSettings().setValue(QLatin1String(kKeyAuto), on);
	emit autoCheckChanged();
	if (on)
		check(false);
}

bool PsWrapUpdater::canInstall() const
{
#ifdef Q_OS_WIN
	// ตัว portable (dist / zip) มี qt.conf ข้าง exe · build สำหรับพัฒนาไม่มี → ไม่ทับ
	const QDir dir(QCoreApplication::applicationDirPath());
	if (!QFileInfo::exists(dir.filePath(QStringLiteral("qt.conf"))))
		return false;
	QFile probe(dir.filePath(QStringLiteral(".pswrap-write-test")));
	if (!probe.open(QIODevice::WriteOnly))
		return false;
	probe.close();
	probe.remove();
	return true;
#else
	return false;
#endif
}

void PsWrapUpdater::setState(const QString &s, const QString &err)
{
	state_ = s;
	error_ = err;
	emit changed();
}

void PsWrapUpdater::check(bool manual)
{
	if (state_ == QLatin1String("checking") || state_ == QLatin1String("downloading") || state_ == QLatin1String("ready"))
		return;
	if (manual || state_ != QLatin1String("available"))
		setState(QStringLiteral("checking"));
	QNetworkRequest req{QUrl(QString::fromLatin1(kReleasesApi))};
	req.setRawHeader("Accept", "application/vnd.github+json");
	req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PS-WRAP/%1").arg(currentVersion()));
	req.setTransferTimeout(20000);
	QNetworkReply *reply = net->get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, manual]() { onCheckFinished(reply, manual); });
}

void PsWrapUpdater::onCheckFinished(QNetworkReply *reply, bool manual)
{
	reply->deleteLater();
	if (reply->error() != QNetworkReply::NoError) {
		// เช็คอัตโนมัติพลาด (ออฟไลน์ ฯลฯ) = เงียบ · กดเช็คเอง = บอก
		if (manual)
			setState(QStringLiteral("error"), reply->errorString());
		else
			setState(updateAvailable() ? QStringLiteral("available") : QStringLiteral("idle"));
		return;
	}
	const QJsonArray releases = QJsonDocument::fromJson(reply->readAll()).array();
	QJsonObject best;
	QString bestTag;
	for (const QJsonValue &v : releases) {
		const QJsonObject r = v.toObject();
		const QString tag = r.value(QStringLiteral("tag_name")).toString();
		if (r.value(QStringLiteral("draft")).toBool() || parseVersion(tag).isEmpty())
			continue;
		if (bestTag.isEmpty() || compareVersions(tag, bestTag) > 0) {
			best = r;
			bestTag = tag;
		}
	}
	if (bestTag.isEmpty()) {
		setState(manual ? QStringLiteral("error") : QStringLiteral("idle"), tr("No releases found"));
		return;
	}
	latest_ = bestTag.mid(bestTag.startsWith(QLatin1Char('v')) ? 1 : 0);
	page_url_ = best.value(QStringLiteral("html_url")).toString(QString::fromLatin1(kReleasesPage));
	const QString body = best.value(QStringLiteral("body")).toString();
	notes_ = trimNotes(body);
	const QRegularExpressionMatch sha = QRegularExpression(QStringLiteral("SHA-256:\\s*`?([0-9A-Fa-f]{64})")).match(body);
	asset_sha256_ = sha.hasMatch() ? sha.captured(1).toLower() : QString();
	asset_url_.clear();
	asset_name_.clear();
	asset_size_ = 0;
	for (const QJsonValue &a : best.value(QStringLiteral("assets")).toArray()) {
		const QJsonObject o = a.toObject();
		const QString name = o.value(QStringLiteral("name")).toString();
		if (name.endsWith(QStringLiteral("-win64.zip"))) {
			asset_name_ = name;
			asset_url_ = o.value(QStringLiteral("browser_download_url")).toString();
			asset_size_ = o.value(QStringLiteral("size")).toVariant().toLongLong();
			break;
		}
	}
	setState(updateAvailable() ? QStringLiteral("available") : QStringLiteral("uptodate"));
}

void PsWrapUpdater::download()
{
	if (!updateAvailable() || state_ == QLatin1String("downloading") || state_ == QLatin1String("ready"))
		return;
	if (!canInstall() || asset_url_.isEmpty()) {
		openPage();
		return;
	}
	work_dir_ = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("PS-WRAP-update"));
	QDir(work_dir_).removeRecursively();
	QDir().mkpath(work_dir_);
	download_file = new QFile(QDir(work_dir_).filePath(asset_name_), this);
	if (!download_file->open(QIODevice::WriteOnly)) {
		setState(QStringLiteral("error"), tr("Can't write the download to %1").arg(QDir::toNativeSeparators(work_dir_)));
		return;
	}
	progress_ = 0;
	emit progressChanged();
	setState(QStringLiteral("downloading"));
	QNetworkRequest req{QUrl(asset_url_)};
	req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PS-WRAP/%1").arg(currentVersion()));
	download_reply = net->get(req);
	connect(download_reply, &QNetworkReply::readyRead, this, [this]() { download_file->write(download_reply->readAll()); });
	connect(download_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
		const qint64 t = total > 0 ? total : asset_size_;
		progress_ = t > 0 ? double(got) / double(t) : 0;
		emit progressChanged();
	});
	connect(download_reply, &QNetworkReply::finished, this, &PsWrapUpdater::onDownloadFinished);
}

void PsWrapUpdater::onDownloadFinished()
{
	QNetworkReply *reply = download_reply;
	download_reply = nullptr;
	reply->deleteLater();
	download_file->write(reply->readAll());
	download_file->close();
	const QString path = download_file->fileName();
	download_file->deleteLater();
	download_file = nullptr;
	if (reply->error() != QNetworkReply::NoError) {
		setState(QStringLiteral("error"), reply->errorString());
		return;
	}
	if (!asset_sha256_.isEmpty()) {
		QFile f(path);
		QCryptographicHash hash(QCryptographicHash::Sha256);
		if (!f.open(QIODevice::ReadOnly) || !hash.addData(&f) || QString::fromLatin1(hash.result().toHex()) != asset_sha256_) {
			setState(QStringLiteral("error"), tr("The download is damaged (SHA-256 does not match). Try again."));
			return;
		}
	}
	extract();
}

void PsWrapUpdater::extract()
{
#ifdef Q_OS_WIN
	const QString zip = QDir(work_dir_).filePath(asset_name_);
	const QString out = QDir(work_dir_).filePath(QStringLiteral("new"));
	QDir().mkpath(out);
	// tar.exe มากับ Windows 10 1803+ (bsdtar แตก zip ได้) — ไม่ต้องพก unzip เอง
	auto *tar = new QProcess(this);
	const QString tarExe = QDir(QString::fromLocal8Bit(qgetenv("SystemRoot"))).filePath(QStringLiteral("System32/tar.exe"));
	connect(tar, &QProcess::finished, this, [this, tar, out](int code, QProcess::ExitStatus st) {
		tar->deleteLater();
		if (st != QProcess::NormalExit || code != 0) {
			setState(QStringLiteral("error"), tr("Could not unpack the update (tar exit %1)").arg(code));
			return;
		}
		// zip มีโฟลเดอร์บนสุด PS-WRAP-vX.Y.Z/ — หาโฟลเดอร์ที่มี PS-WRAP.exe
		extracted_dir_.clear();
		QDirIterator it(out, {QStringLiteral("PS-WRAP.exe")}, QDir::Files, QDirIterator::Subdirectories);
		if (it.hasNext())
			extracted_dir_ = QFileInfo(it.next()).absolutePath();
		if (extracted_dir_.isEmpty()) {
			setState(QStringLiteral("error"), tr("The update package has no PS-WRAP.exe"));
			return;
		}
		progress_ = 1;
		emit progressChanged();
		setState(QStringLiteral("ready"));
	});
	tar->start(QDir::toNativeSeparators(tarExe), {QStringLiteral("-xf"), QDir::toNativeSeparators(zip), QStringLiteral("-C"), QDir::toNativeSeparators(out)});
#else
	setState(QStringLiteral("error"), tr("Automatic install is only available on Windows"));
#endif
}

void PsWrapUpdater::install()
{
#ifdef Q_OS_WIN
	if (state_ != QLatin1String("ready") || extracted_dir_.isEmpty())
		return;
	const QString src = QDir::toNativeSeparators(extracted_dir_);
	const QString dst = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
	const QString pid = QString::number(QCoreApplication::applicationPid());
	// รอแอปนี้ปิด → คัดลอกทับ (settings อยู่ใน registry/AppData ไม่โดน) → เปิดตัวใหม่
	const QString script = QStringLiteral(
		"@echo off\r\n"
		"chcp 65001 >nul\r\n"
		":wait\r\n"
		"tasklist /FI \"PID eq %1\" 2>nul | find \"%1\" >nul\r\n"
		"if not errorlevel 1 ( timeout /t 1 /nobreak >nul & goto wait )\r\n"
		"robocopy \"%2\" \"%3\" /E /R:5 /W:1 /NFL /NDL /NJH /NJS /NP >nul\r\n"
		"start \"\" \"%3\\PS-WRAP.exe\"\r\n").arg(pid, src, dst);
	const QString bat = QDir(work_dir_).filePath(QStringLiteral("install.cmd"));
	QFile f(bat);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		setState(QStringLiteral("error"), tr("Can't write the install script"));
		return;
	}
	f.write(script.toUtf8());
	f.close();
	QProcess p;
	p.setProgram(QStringLiteral("cmd.exe"));
	p.setArguments({QStringLiteral("/c"), QDir::toNativeSeparators(bat)});
	p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
	if (!p.startDetached()) {
		setState(QStringLiteral("error"), tr("Can't start the installer"));
		return;
	}
	QCoreApplication::quit();
#endif
}

void PsWrapUpdater::openPage()
{
	QDesktopServices::openUrl(QUrl(page_url_.isEmpty() ? QString::fromLatin1(kReleasesPage) : page_url_));
}

void PsWrapUpdater::dismiss()
{
	if (latest_.isEmpty())
		return;
	QSettings().setValue(QLatin1String(kKeyDismissed), latest_);
	emit changed();
}
