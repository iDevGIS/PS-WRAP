
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(APPLE)
	enable_language(OBJCXX)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Concurrent Svg Qml Quick Widgets Network Multimedia)
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.10.0")
	find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()

find_package(Qt6 COMPONENTS WebEngineQuick)
find_package(Qt6 COMPONENTS DBus)

if(WIN32)
	add_definitions(-DWIN32_LEAN_AND_MEAN)
endif()

set(SOURCE_FILES
	include/exception.h
	src/main.cpp
	include/discoverymanager.h
	src/discoverymanager.cpp
	include/streamsession.h
	src/streamsession.cpp
	include/sessionlog.h
	src/sessionlog.cpp
	include/settings.h
	src/settings.cpp
	include/host.h
	src/host.cpp
	res/resources.qrc
	include/controllermanager.h
	src/controllermanager.cpp
	include/psnaccountid.h
	src/psnaccountid.cpp
	include/psntoken.h
	src/psntoken.cpp
	include/jsonrequester.h
	src/jsonrequester.cpp
	src/qml/qml.qrc
	res/pad3d/pad3d.qrc
	include/qmlmainwindow.h
	src/qmlmainwindow.cpp
	include/qmlbackend.h
	src/qmlbackend.cpp
	include/qmlcontroller.h
	src/qmlcontroller.cpp
	include/qmlsettings.h
	src/qmlsettings.cpp
	include/qmlsvgprovider.h
	src/qmlsvgprovider.cpp
	include/systemdinhibit.h
	src/systemdinhibit.cpp
	include/pswrapcamera.h
	src/pswrapcamera.cpp
	include/pswrapsegmenter.h
	src/pswrapsegmenter.cpp
	include/pswrapfacetracker.h
	src/pswrapfacetracker.cpp
	include/pswraprecorder.h
	src/pswraprecorder.cpp
	include/pswrapreccapture.h
	src/pswrapreccapture.cpp
	include/pswrapmicmeter.h
	src/pswrapmicmeter.cpp
	include/pswrapvoiceproc.h
	src/pswrapvoiceproc.cpp
	src/qmlmainwindow_pswraprec.cpp
	src/qmlmainwindow_pswrapshot.cpp
	src/qmlmainwindow_pswrapclock.cpp
	src/qmlmainwindow_pswrapsize.cpp
	src/qmlmainwindow_pswrapabout.cpp
	include/pswrapvertical.h
	src/pswrapvertical.cpp
	src/qmlmainwindow_pswrapvertical.cpp
	include/pswrapverticallayers.h
	src/pswrapverticallayers.cpp
	include/pswrapchat.h
	src/pswrapchat.cpp
	src/qmlmainwindow_pswrapchat.cpp
	src/qmlmainwindow_pswrapmic.cpp
	include/pswrapgameprofiles.h
	src/pswrapgameprofiles.cpp
	src/qmlmainwindow_pswrapprofiles.cpp
	include/pswrapsecrets.h
	src/pswrapsecrets.cpp
	include/pswraplive.h
	src/pswraplive.cpp
	src/qmlmainwindow_pswraplive.cpp
	include/pswrapframegen.h
	src/pswrapframegen.cpp
	src/qmlmainwindow_pswrapvisual.cpp
	include/pswrapdiscord.h
	src/pswrapdiscord.cpp
	src/qmlmainwindow_pswrapdiscord.cpp
	include/pswrappsnweblogin.h
	src/pswrappsnweblogin.cpp
	src/qmlbackend_pswrappsn.cpp
	include/pswrapupdater.h
	src/pswrapupdater.cpp
	include/pswrapnetcheck.h
	src/pswrapnetcheck.cpp
	src/qmlbackend_pswraptools.cpp
	src/streamsession_pswraplink.cpp
	src/streamsession_pswrapawake.cpp
	src/qmlmainwindow_pswraplink.cpp
	)
set(RESOURCE_FILES "")

if(APPLE)
	list(APPEND RESOURCE_FILES "chiaking.icns")
elseif(WIN32)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/chiaking.rc.in
		${CMAKE_CURRENT_SOURCE_DIR}/chiaking.rc
		@ONLY)
	list(APPEND RESOURCE_FILES "chiaking.rc")
endif()

if(CHIAKI_ENABLE_STEAMDECK_NATIVE AND LINUX)
	list(APPEND SOURCE_FILES
		include/sdinputcontext.h
		src/sdinputcontext.cpp
	)
endif()

if(APPLE)
	list(APPEND SOURCE_FILES
		include/macMicPermission.h
		src/macMicPermission.m
		include/macWakeSleep.h
		src/macWakeSleep.cpp
		include/macMetalLayer.h
		src/macMetalLayer.mm
		include/macNetInfo.h
		src/macNetInfo.mm
	)
elseif(WIN32)
	list(APPEND SOURCE_FILES
		include/windowsWakeSleep.h
		src/windowsWakeSleep.cpp
	)
endif()

add_executable(chiaki WIN32
		${RESOURCE_FILES}
		${SOURCE_FILES}
)
target_include_directories(chiaki PRIVATE include)

add_library(placebo-libav-impl src/libav_impl.c)
target_link_libraries(placebo-libav-impl PkgConfig::LIBPLACEBO FFMPEG::avcodec FFMPEG::avutil FFMPEG::avformat)
target_link_libraries(chiaki PkgConfig::LIBPLACEBO placebo-libav-impl)

target_link_libraries(chiaki chiaki-lib)
if(CHIAKI_ENABLE_CLI)
	add_definitions(-DCHIAKI_ENABLE_CLI)
	target_link_libraries(chiaki chiaki-cli-lib)
endif()

target_link_libraries(chiaki Qt::Core Qt::Gui Qt::GuiPrivate Qt::Concurrent Qt::Svg Qt::Qml Qt::Quick Qt::Widgets Qt::Network)
target_link_libraries(chiaki Qt::Multimedia)   # PS-WRAP: QVideoSink/QVideoFrame สำหรับ DirectShow camera
target_include_directories(chiaki PRIVATE ${CMAKE_SOURCE_DIR}/third-party/onnxruntime/include)   # PS-WRAP: ORT C API (โหลด DLL ตอนรัน)
# PS-WRAP: DirectShow camera (avdevice + swscale + COM)
find_package(FFMPEG COMPONENTS avdevice swscale)
if(WIN32 AND TARGET FFMPEG::avdevice AND TARGET FFMPEG::swscale)
	target_link_libraries(chiaki FFMPEG::avdevice FFMPEG::swscale ole32 oleaut32 strmiids uuid)
	target_compile_definitions(chiaki PRIVATE PSWRAP_DSHOW_CAMERA=1)
elseif(TARGET FFMPEG::avdevice)
	target_link_libraries(chiaki FFMPEG::avdevice)   # PS-WRAP mac/linux: pswrapcamera.cpp เรียก avdevice_register_all ทุกแพลตฟอร์ม
endif()   # PS-WRAP: Network สำหรับ single instance (QLocalServer)
if(WIN32)
	target_link_libraries(chiaki advapi32)   # PS-WRAP: Credential Manager (CredWriteW/CredReadW) ของ pswrapsecrets
	# PS-WRAP: ล็อกอิน PSN ในหน้าต่าง WebView2 (pswrappsnweblogin.cpp) — WebView2Loader.dll โหลดตอนรัน วางข้าง exe
	target_include_directories(chiaki PRIVATE ${CMAKE_SOURCE_DIR}/third-party/webview2/include)
	target_link_libraries(chiaki ole32 uuid dwmapi)
	target_link_libraries(chiaki iphlpapi wlanapi ws2_32)   # PS-WRAP: ตรวจการเชื่อมต่อ (pswrapnetcheck.cpp)
	add_custom_command(TARGET chiaki POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/third-party/webview2/x64/WebView2Loader.dll $<TARGET_FILE_DIR:chiaki>)
endif()
if(TARGET FFMPEG::swscale)
	target_link_libraries(chiaki FFMPEG::swscale)   # PS-WRAP: Go Live ย่อภาพเป็น 16:9 (pswraplive.cpp) — ทุกแพลตฟอร์ม ไม่ใช่แค่ DirectShow
endif()
# PS-WRAP Link: ส่งภาพ/รับจอยเพื่อนผ่าน WebRTC · เปิดด้วย -DPSWRAP_LINK=ON -DPSWRAP_LIBDATACHANNEL_DIR=<source> (scripts/build.ps1 ทำให้)
# เปิด/ปิดจากเมนูสตรีม (Chiaki.link) · ต้องมี host key ใน Credential Manager — ดู pswraplink.h
option(PSWRAP_LINK "PS-WRAP Link (WebRTC)" OFF)
if(PSWRAP_LINK)
	set(PSWRAP_LIBDATACHANNEL_DIR "" CACHE PATH "libdatachannel source (PS-WRAP-LINK/spike/deps/libdatachannel)")
	set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
	set(NO_TESTS ON CACHE BOOL "" FORCE)
	set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
	set(USE_NICE OFF CACHE BOOL "" FORCE)
	set(ENABLE_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)   # libsrtp: -Werror กับ %x/u_long บน mingw
	add_subdirectory(${PSWRAP_LIBDATACHANNEL_DIR} libdatachannel EXCLUDE_FROM_ALL)
	target_sources(chiaki PRIVATE include/pswraplink.h src/pswraplink.cpp include/pswraplinkvoice.h src/pswraplinkvoice.cpp ${CMAKE_SOURCE_DIR}/third-party/qrcodegen/qrcodegen.cpp)
	target_include_directories(chiaki PRIVATE ${CMAKE_SOURCE_DIR}/third-party/qrcodegen)   # QR ลิงก์เชิญ (nayuki, MIT)
	target_link_libraries(chiaki LibDataChannel::LibDataChannelStatic)   # static: ไม่ต้องวาง DLL เพิ่ม
	# signaling ผ่าน wss ใช้ QWebSocket — rtc::WebSocket บน Windows ไม่ตรวจใบรับรอง TLS (Qt6WebSockets.dll ต้องไปกับ release)
	find_package(Qt6 REQUIRED COMPONENTS WebSockets)
	target_link_libraries(chiaki Qt::WebSockets)
	target_compile_definitions(chiaki PRIVATE PSWRAP_LINK=1)
	# beta key ของ Link (ให้ผู้ใช้ทั่วไปเปิดห้องได้) — อ่านจากไฟล์นอก repo ตอน configure · ไม่มีไฟล์ = build ปกติที่ต้องใช้ host key เอง
	# เก็บแบบสลับบิต (ไม่ใช่การเข้ารหัส — กันแค่ strings ตรงๆ; เซิร์ฟเวอร์จำกัดห้อง/IP ของ key นี้แยก และถอนได้)
	if(APPLE)
		set(_beta_default "$ENV{HOME}/Library/Application Support/PS-WRAP-build/link-beta-key")   # PS-WRAP-MAC: ไม่มี %LOCALAPPDATA%
	else()
		set(_beta_default "$ENV{LOCALAPPDATA}/PS-WRAP-build/link-beta-key")
	endif()
	set(PSWRAP_LINK_BETA_KEY_FILE "${_beta_default}" CACHE FILEPATH "Link beta key file (hex, outside the repo)")
	if(EXISTS "${PSWRAP_LINK_BETA_KEY_FILE}")
		file(READ "${PSWRAP_LINK_BETA_KEY_FILE}" _beta)
		string(STRIP "${_beta}" _beta)
		if(NOT _beta MATCHES "^[0-9a-f]+$")
			message(FATAL_ERROR "PS-WRAP Link: beta key file must be lowercase hex")
		endif()
		string(LENGTH "${_beta}" _n)
		math(EXPR _last "${_n} - 1")
		set(_arr "")
		foreach(_i RANGE ${_last})
			string(SUBSTRING "${_beta}" ${_i} 1 _ch)
			string(FIND "0123456789abcdef" "${_ch}" _v)
			math(EXPR _m "(${_i} * 7 + 3) % 16")
			math(EXPR _x "${_v} ^ ${_m}")
			string(APPEND _arr "${_x},")
		endforeach()
		file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/pswraplink_betakey.h"
			CONTENT "// generated — ไม่ commit
static const unsigned char kBetaKey[] = {${_arr}};
")
		set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PSWRAP_LINK_BETA_KEY_FILE}")
		target_include_directories(chiaki PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
		target_compile_definitions(chiaki PRIVATE PSWRAP_LINK_BETA=1)
		message(STATUS "PS-WRAP Link: beta key embedded")
		unset(_beta)
	endif()
	if(WIN32)
		target_link_libraries(chiaki winmm)   # timeBeginPeriod ของ pacer
	endif()
endif()
if (${Qt6DBus_FOUND} AND NOT WIN32)
	target_link_libraries(chiaki Qt::DBus)
if (NOT APPLE)
	target_compile_definitions(chiaki PRIVATE CHIAKI_HAVE_DBUS)
endif()
endif()
if (${Qt6WebEngineQuick_FOUND})
	target_link_libraries(chiaki Qt::WebEngineQuick)
	target_compile_definitions(chiaki PRIVATE CHIAKI_HAVE_WEBENGINE)
endif()
target_link_libraries(chiaki SDL2::SDL2)
if(CHIAKI_ENABLE_SPEEX)
  target_link_libraries(chiaki PkgConfig::SpeexDSP)
  target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_SPEEX)
  # PS-WRAP: RNNoise (ถ้ามี) ลดเสียงรบกวนไมค์แทน speex denoise — ไม่มีก็ build ได้ (ใช้ speex แทน)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(RNNOISE IMPORTED_TARGET rnnoise)
    if(RNNOISE_FOUND)
      target_link_libraries(chiaki PkgConfig::RNNOISE)
      target_compile_definitions(chiaki PRIVATE PSWRAP_HAVE_RNNOISE)
    endif()
  endif()
endif()
if(CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER)
endif()
if(CHIAKI_ENABLE_SETSU)
	target_link_libraries(chiaki setsu)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_SETSU)
endif()
if(CHIAKI_ENABLE_STEAMDECK_NATIVE)
	target_link_libraries(chiaki sdeck)
	target_compile_definitions(chiaki PRIVATE QT_STATICPLUGIN CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE)
endif()
if(CHIAKI_ENABLE_STEAM_SHORTCUT)
	target_link_libraries(chiaki cpp-steam-tools)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_STEAM_SHORTCUT)
endif()
if(NOT CHIAKI_ENABLE_FFMPEG_DECODER)
	message(FATAL_ERROR "Chiaki GUI requires CHIAKI_ENABLE_FFMPEG_DECODER=ON")
endif()
if(APPLE)
	target_link_libraries(chiaki "-framework AVFoundation" "-framework IOKit" "-framework QuartzCore" "-framework Security" "-framework CoreFoundation" "-framework CoreWLAN")   # PS-WRAP-MAC: CAMetalLayer (macMetalLayer.mm) · Keychain (pswrapsecrets.cpp) · Wi-Fi info (macNetInfo.mm)
	# PS-WRAP-MAC: Qt permission plugin ของ macOS เป็น static — qt_add_executable จะ link ให้เองจาก Info.plist แต่ upstream ใช้ add_executable
	# ไม่ link = "Could not find permission plugin for QCameraPermission" → Qt ถือว่าไม่ได้รับอนุญาต กล้อง/ไมค์เปิดไม่ได้
	# PS-WRAP-MAC: โมเดล AI facecam (res/models) + ภาพ fx (res/fx-photo) ต้องอยู่ข้าง exe (โค้ดหาใน applicationDirPath เหมือน Windows)
	add_custom_command(TARGET chiaki POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/res/models $<TARGET_FILE_DIR:chiaki>/models
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/res/fx-photo $<TARGET_FILE_DIR:chiaki>/fx)
	foreach(_perm Camera Microphone)
		if(TARGET Qt6::QDarwin${_perm}PermissionPlugin)
			target_link_libraries(chiaki Qt6::QDarwin${_perm}PermissionPlugin Qt6::QDarwin${_perm}PermissionPlugin_init)
			set_property(TARGET chiaki PROPERTY _qt_has_QDarwin${_perm}PermissionPlugin_usage_description TRUE)
		endif()
	endforeach()
endif()

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fanalyzer")
set_target_properties(chiaki PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/MacOSXBundleInfo.plist.in"
		MACOSX_BUNDLE_BUNDLE_NAME PS-WRAP-MAC
		MACOSX_BUNDLE_BUNDLE_VERSION ${PSWRAP_VERSION}
		MACOSX_BUNDLE_COPYRIGHT "iDevGIS / chiaki-ng contributors (AGPLv3)"
		MACOSX_BUNDLE_GUI_IDENTIFIER "com.idevgis.pswrap"
		MACOSX_BUNDLE_ICON_FILE chiaking.icns
		RESOURCE "${RESOURCE_FILES}")

if(WIN32)
	# PS-WRAP: ไฟล์ exe ชื่อ PS-WRAP.exe (target ยังชื่อ chiaki ลด diff กับ upstream)
	set_target_properties(chiaki PROPERTIES OUTPUT_NAME "PS-WRAP")
elseif(APPLE)
	# PS-WRAP-MAC: bundle ชื่อ PS-WRAP-MAC.app (ชื่อบน menu bar มาจาก CFBundleName ด้านบน)
	set_target_properties(chiaki PROPERTIES OUTPUT_NAME "PS-WRAP-MAC")
endif()

install(TARGETS chiaki
		RUNTIME DESTINATION bin
		BUNDLE DESTINATION bin)
install(FILES chiaking.desktop DESTINATION share/applications)
install(FILES chiaking.png DESTINATION share/icons/hicolor/512x512/apps)
install(FILES io.github.streetpea.Chiaki4deck.appdata.xml DESTINATION share/metainfo)
