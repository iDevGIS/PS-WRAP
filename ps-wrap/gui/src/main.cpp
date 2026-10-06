
// Use the native entry point instead of SDL's WinMain wrapper.
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#endif
#ifdef main
#undef main
#endif

// ugly workaround because Windows does weird things and ENOTIME
int real_main(int argc, char *argv[]);
int main(int argc, char *argv[]) { return real_main(argc, argv); }

#include <streamsession.h>
#include <settings.h>
#include <host.h>
#include <controllermanager.h>
#include <discoverymanager.h>
#include <qmlmainwindow.h>
#include <QApplication>
#include <QtTypes>

#ifdef CHIAKI_ENABLE_CLI
#include <chiaki-cli.h>
#endif

#include <chiaki/session.h>
#include <chiaki/regist.h>
#include <chiaki/base64.h>

#include <stdio.h>
#include <string.h>

#ifdef CHIAKI_HAVE_WEBENGINE
#include <QtWebEngineQuick>
#endif

#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>
#include <QCryptographicHash>
#include <QMap>
#include <QSettings>
#include <QSurfaceFormat>

Q_DECLARE_METATYPE(ChiakiLogLevel)
Q_DECLARE_METATYPE(ChiakiRegistEventType)

#if defined(CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE) && defined(Q_OS_LINUX)
#include <QtPlugin>
Q_IMPORT_PLUGIN(SDInputContextPlugin)
#endif

#ifdef CHIAKI_ENABLE_CLI
struct CLICommand
{
	int (*cmd)(ChiakiLog *log, int argc, char *argv[]);
};

static const QMap<QString, CLICommand> cli_commands = {
	{ "discover", { chiaki_cli_cmd_discover } },
	{ "wakeup", { chiaki_cli_cmd_wakeup } }
};
#endif

int RunStream(QGuiApplication &app, const StreamSessionConnectInfo &connect_info);
int RunMain(QGuiApplication &app, Settings *settings, bool exit_app_on_stream_exit, const QString &instance_name = QString());

// PS-WRAP: ย้าย settings จาก chiaki-ng (org/app "Chiaki") มาที่ "PS-WRAP" ครั้งแรกที่เปิด — คัดลอก ไม่ลบของเดิม
// (chiaki-ng ที่ติดตั้งไว้ยังใช้ของเดิมต่อได้) · ครอบคลุม settings หลัก, pl_render_params, และทุก profile "Chiaki-<ชื่อ>"
static void pswrapCopySettings(QSettings &from, QSettings &to)
{
	const QStringList keys = from.allKeys();
	for (const QString &key : keys)
		to.setValue(key, from.value(key));
	to.sync();
}

static void pswrapMigrateLegacyChiakiSettings()
{
	QSettings target(QStringLiteral("PS-WRAP"), QStringLiteral("PS-WRAP"));
	if (target.value(QStringLiteral("pswrap/migrated_from_chiaki"), false).toBool() || !target.allKeys().isEmpty())
		return;
	QSettings legacy(QStringLiteral("Chiaki"), QStringLiteral("Chiaki"));
	if (legacy.allKeys().isEmpty())
		return;
	pswrapCopySettings(legacy, target);
	QSettings legacy_placebo(QStringLiteral("Chiaki"), QStringLiteral("pl_render_params"));
	QSettings target_placebo(QStringLiteral("PS-WRAP"), QStringLiteral("pl_render_params"));
	if (target_placebo.allKeys().isEmpty())
		pswrapCopySettings(legacy_placebo, target_placebo);
	int profiles = 0;
	// รายชื่อ profile อยู่ใน array "profiles" (settings/profile_name) ของ settings หลัก — ใช้ได้ทุก OS
	const int profile_count = legacy.beginReadArray(QStringLiteral("profiles"));
	QStringList profile_names;
	for (int i = 0; i < profile_count; i++) {
		legacy.setArrayIndex(i);
		profile_names.append(legacy.value(QStringLiteral("settings/profile_name")).toString());
	}
	legacy.endArray();
	for (const QString &profile : profile_names) {
		if (profile.isEmpty())
			continue;
		QSettings from(QStringLiteral("Chiaki"), QStringLiteral("Chiaki-") + profile);
		QSettings to(QStringLiteral("PS-WRAP"), QStringLiteral("PS-WRAP-") + profile);
		if (to.allKeys().isEmpty() && !from.allKeys().isEmpty()) {
			pswrapCopySettings(from, to);
			profiles++;
		}
	}
	target.setValue(QStringLiteral("pswrap/migrated_from_chiaki"), true);
	target.sync();
	fprintf(stderr, "PS-WRAP: migrated settings from Chiaki (%d keys, %d profiles)\n", static_cast<int>(legacy.allKeys().size()), profiles);
}

int real_main(int argc, char *argv[])
{
	qRegisterMetaType<DiscoveryHost>();
	qRegisterMetaType<RegisteredHost>();
	qRegisterMetaType<HostMAC>();
	qRegisterMetaType<ChiakiQuitReason>();
	qRegisterMetaType<ChiakiRegistEventType>();
	qRegisterMetaType<ChiakiLogLevel>();

	// PS-WRAP: ที่เก็บข้อมูลของเราเอง (registry HKCU\Software\PS-WRAP, %APPDATA%\PS-WRAP\PS-WRAP) ไม่ใช้ร่วมกับ chiaki-ng ที่ติดตั้งไว้
	QGuiApplication::setOrganizationName(QStringLiteral("PS-WRAP"));
	QGuiApplication::setApplicationName(QStringLiteral("PS-WRAP"));
	pswrapMigrateLegacyChiakiSettings();
	QGuiApplication::setApplicationVersion(CHIAKI_VERSION);
	QGuiApplication::setApplicationDisplayName("PS-WRAP");
#if defined(Q_OS_MACOS)
	qputenv("QT_MTL_NO_TRANSACTION", "1");
#endif
#if defined(Q_OS_LINUX)
	if(qEnvironmentVariableIsSet("FLATPAK_ID"))
		QGuiApplication::setDesktopFileName(qEnvironmentVariable("FLATPAK_ID"));
	else
#endif
		QGuiApplication::setDesktopFileName("chiaki-ng");

	qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");
#if defined(Q_OS_WIN)
	const size_t cSize = strlen(argv[0])+1;
	wchar_t wc[cSize];
	mbstowcs (wc, argv[0], cSize);
	QString import_path = QFileInfo(QString::fromWCharArray(wc)).dir().absolutePath() + "/qml";
	qputenv("QML_IMPORT_PATH", import_path.toUtf8());
#endif
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
	qputenv("ANV_VIDEO_DECODE", "1");
	qputenv("RADV_PERFTEST", "video_decode");
#endif
#ifdef CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
	if (qEnvironmentVariableIsSet("SteamDeck"))
		qputenv("QT_IM_MODULE", "sdinput");
#endif

	ChiakiErrorCode err = chiaki_lib_init();
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "Chiaki lib init failed: %s\n", chiaki_error_string(err));
		return 1;
	}

    SDL_SetHint(SDL_HINT_APP_NAME, "PS-WRAP");

	if(SDL_Init(SDL_INIT_AUDIO) < 0)
	{
		fprintf(stderr, "SDL Audio init failed: %s\n", SDL_GetError());
		return 1;
	}

	QGuiApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
#ifdef CHIAKI_HAVE_WEBENGINE
	QtWebEngineQuick::initialize();
#endif
	setvbuf(stderr, nullptr, _IONBF, 0);   // PS-WRAP: stderr ไม่ buffer — log ครบแม้ process ถูก kill (ใช้เก็บ log ทดสอบ)
	QApplication app(argc, argv);

#ifdef Q_OS_MACOS
	QGuiApplication::setWindowIcon(QIcon(":/icons/pswrap.svg"));
#else
	QGuiApplication::setWindowIcon(QIcon(":/icons/pswrap.svg"));
#endif

	QCommandLineParser parser;
	parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);
	parser.addHelpOption();
	
	QStringList cmds;
	cmds.append("stream");
	cmds.append("list");
#ifdef CHIAKI_ENABLE_CLI
	cmds.append(cli_commands.keys());
#endif

	parser.addPositionalArgument("command", cmds.join(", "));
	parser.addPositionalArgument("nickname", "Needed for stream command to get credentials for connecting. "
			"Use 'list' to get the nickname.");
	parser.addPositionalArgument("host", "Address to connect to (when using the stream command).");

	QCommandLineOption profile_option("profile", "", "profile", "Configuration profile");
	parser.addOption(profile_option);

	QCommandLineOption stream_exit_option("exit-app-on-stream-exit", "Exit the GUI application when the stream session ends.");
	parser.addOption(stream_exit_option);

	QCommandLineOption regist_key_option("registkey", "", "registkey");
	parser.addOption(regist_key_option);

	QCommandLineOption morning_option("morning", "", "morning");
	parser.addOption(morning_option);

	QCommandLineOption fullscreen_option("fullscreen", "Start window in fullscreen mode [maintains aspect ratio, adds black bars to fill unsused parts of screen if applicable] (only for use with stream command).");
	parser.addOption(fullscreen_option);

	QCommandLineOption dualsense_option("dualsense", "Enable DualSense haptics and adaptive triggers (PS5 and DualSense connected via USB only).");
	parser.addOption(dualsense_option);

	QCommandLineOption zoom_option("zoom", "Start window in fullscreen zoomed in to fit screen [maintains aspect ratio, cutting off edges of image to fill screen] (only for use with stream command)");
	parser.addOption(zoom_option);

	QCommandLineOption stretch_option("stretch", "Start window in fullscreen stretched to fit screen [distorts aspect ratio to fill screen] (only for use with stream command).");
	parser.addOption(stretch_option);

	QCommandLineOption passcode_option("passcode", "Automatically send your PlayStation login passcode (only affects users with a login passcode set on their PlayStation console).", "passcode");
	parser.addOption(passcode_option);

	parser.process(app);
	QStringList args = parser.positionalArguments();

	Settings settings(parser.isSet(profile_option) ? parser.value(profile_option) : QString());
	bool exit_app_on_stream_exit = parser.isSet(stream_exit_option);
	if(parser.isSet(profile_option))
		settings.SetCurrentProfile(parser.value(profile_option));
	Settings alt_settings(parser.isSet(profile_option) ? "" : settings.GetCurrentProfile());
	if(!settings.GetCurrentProfile().isEmpty())
		QGuiApplication::setApplicationDisplayName(QString("PS-WRAP:%1").arg(settings.GetCurrentProfile()));
	bool use_alt_settings = false;
	if(!parser.isSet(profile_option))
		use_alt_settings = true;

	if(args.length() == 0)
	{
		// PS-WRAP: single instance ต่อโปรไฟล์ — ถ้ามีตัวเดิมรันอยู่ ส่ง "activate" ให้มันดึงหน้าต่างขึ้นแล้วออก
		const QString profile_key = use_alt_settings ? alt_settings.GetCurrentProfile() : settings.GetCurrentProfile();
		const QString instance_name = QStringLiteral("pswrap-") + QString::fromLatin1(QCryptographicHash::hash(profile_key.toUtf8(), QCryptographicHash::Md5).toHex().left(12));
		{
			QLocalSocket probe;
			probe.connectToServer(instance_name);
			if(probe.waitForConnected(300))
			{
				probe.write("activate\n");
				probe.waitForBytesWritten(300);
				probe.disconnectFromServer();
				return 0;
			}
		}
		return RunMain(app, use_alt_settings ? &alt_settings : &settings, exit_app_on_stream_exit, instance_name);
	}

	if(args[0] == "list")
	{
		for(const auto &host : settings.GetRegisteredHosts())
			printf("Host: %s \n", host.GetServerNickname().toLocal8Bit().constData());
		return 0;
	}
	if(args[0] == "stream")
	{
		if(args.length() < 2)
			parser.showHelp(1);

		//QString host = args[sizeof(args) -1]; //the ip is always the last param for stream
		QString host = args[args.size()-1];
		QByteArray morning;
		QByteArray regist_key;
		QString initial_login_passcode;
		ChiakiTarget target = CHIAKI_TARGET_PS4_10;

		if(parser.value(regist_key_option).isEmpty() && parser.value(morning_option).isEmpty())
		{
			if(args.length() < 3)
				parser.showHelp(1);

			bool found = false;
			for(const auto &temphost : settings.GetRegisteredHosts())
			{
				if(temphost.GetServerNickname() == args[1])
				{
					found = true;
					morning = temphost.GetRPKey();
					regist_key = temphost.GetRPRegistKey();
					target = temphost.GetTarget();
					break;
				}
			}
			if(!found)
			{
				printf("No configuration found for '%s'\n", args[1].toLocal8Bit().constData());
				return 1;
			}
		}
		else
		{
			// TODO: explicit option for target
			regist_key = parser.value(regist_key_option).toUtf8();
			if(regist_key.length() > sizeof(ChiakiConnectInfo::regist_key))
			{
				printf("Given regist key is too long (expected size <=%llu, got %" PRIdQSIZETYPE")\n",
					(unsigned long long)sizeof(ChiakiConnectInfo::regist_key),
					regist_key.length());
				return 1;
			}
			regist_key += QByteArray(sizeof(ChiakiConnectInfo::regist_key) - regist_key.length(), 0);
			morning = QByteArray::fromBase64(parser.value(morning_option).toUtf8());
			if(morning.length() != sizeof(ChiakiConnectInfo::morning))
			{
				printf("Given morning has invalid size (expected %llu, got %" PRIdQSIZETYPE")\n",
					(unsigned long long)sizeof(ChiakiConnectInfo::morning),
					morning.length());
				printf("Given morning has invalid size (expected %llu)", (unsigned long long)sizeof(ChiakiConnectInfo::morning));
				return 1;
			}
		}
		if ((parser.isSet(stretch_option) && (parser.isSet(zoom_option) || parser.isSet(fullscreen_option))) || (parser.isSet(zoom_option) && parser.isSet(fullscreen_option)))
		{
			printf("Must choose between fullscreen, zoom or stretch option.");
			return 1;
		}
		if(parser.value(passcode_option).isEmpty())
		{
			//Set to empty if it wasn't given by user.
			initial_login_passcode = QString("");
		}
		else
		{
			initial_login_passcode = parser.value(passcode_option);
			if(initial_login_passcode.length() != 4)
			{
				printf("Login passcode must be 4 digits. You entered %" PRIdQSIZETYPE "digits)\n", initial_login_passcode.length());
				return 1;
			}
		}
		
		StreamSessionConnectInfo connect_info(
				use_alt_settings ? &alt_settings : &settings,
				target,
				std::move(host),
				QString(),
				std::move(regist_key),
				std::move(morning),
				std::move(initial_login_passcode),
				QString(),
				false,
				parser.isSet(fullscreen_option),
				parser.isSet(zoom_option),
				parser.isSet(stretch_option));

		return RunStream(app, connect_info);
	}
#ifdef CHIAKI_ENABLE_CLI
	else if(cli_commands.contains(args[0]))
	{
		ChiakiLog log;
		// TODO: add verbose arg
		chiaki_log_init(&log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, chiaki_log_cb_print, nullptr);

		const auto &cmd = cli_commands[args[0]];
		int sub_argc = args.count();
		QVector<QByteArray> sub_argv_b(sub_argc);
		QVector<char *> sub_argv(sub_argc);
		for(size_t i=0; i<sub_argc; i++)
		{
			sub_argv_b[i] = args[i].toLocal8Bit();
			sub_argv[i] = sub_argv_b[i].data();
		}
		return cmd.cmd(&log, sub_argc, sub_argv.data());
	}
#endif
	else
	{
		parser.showHelp(1);
	}
}

int RunMain(QGuiApplication &app, Settings *settings, bool exit_app_on_stream_exit, const QString &instance_name)
{
	QmlMainWindow main_window(settings, exit_app_on_stream_exit);
	main_window.show();
	// PS-WRAP: server รับคำสั่ง "activate" จาก instance ที่เปิดซ้ำ → ดึงหน้าต่างขึ้น (คืนจาก tray/minimize ด้วย)
	QLocalServer instance_server;
	if(!instance_name.isEmpty())
	{
		QLocalServer::removeServer(instance_name);   // เผื่อ crash รอบก่อนทิ้ง socket ค้าง
		instance_server.setSocketOptions(QLocalServer::UserAccessOption);
		if(instance_server.listen(instance_name))
		{
			QObject::connect(&instance_server, &QLocalServer::newConnection, &main_window, [&instance_server, &main_window]() {
				while(QLocalSocket *sock = instance_server.nextPendingConnection())
				{
					QObject::connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
					QObject::connect(sock, &QLocalSocket::readyRead, &main_window, [sock, &main_window]() {
						if(sock->readAll().contains("activate"))
							main_window.restoreFromTray();
					});
				}
			});
		}
	}
	return app.exec();
}

int RunStream(QGuiApplication &app, const StreamSessionConnectInfo &connect_info)
{
	QmlMainWindow main_window(connect_info);
	main_window.show();
	return app.exec();
}
