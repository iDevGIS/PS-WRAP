
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
	include/pswrapvertical.h
	src/pswrapvertical.cpp
	src/qmlmainwindow_pswrapvertical.cpp
	src/qmlmainwindow_pswrapmic.cpp
	include/pswrapgameprofiles.h
	src/pswrapgameprofiles.cpp
	src/qmlmainwindow_pswrapprofiles.cpp
	include/pswrapsecrets.h
	src/pswrapsecrets.cpp
	include/pswraplive.h
	src/pswraplive.cpp
	src/qmlmainwindow_pswraplive.cpp
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
endif()   # PS-WRAP: Network สำหรับ single instance (QLocalServer)
if(WIN32)
	target_link_libraries(chiaki advapi32)   # PS-WRAP: Credential Manager (CredWriteW/CredReadW) ของ pswrapsecrets
endif()
if(TARGET FFMPEG::swscale)
	target_link_libraries(chiaki FFMPEG::swscale)   # PS-WRAP: Go Live ย่อภาพเป็น 16:9 (pswraplive.cpp) — ทุกแพลตฟอร์ม ไม่ใช่แค่ DirectShow
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
	target_link_libraries(chiaki "-framework AVFoundation" "-framework IOKit")
endif()

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fanalyzer")
set_target_properties(chiaki PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/MacOSXBundleInfo.plist.in"
		MACOSX_BUNDLE_BUNDLE_NAME chiaki-ng
		MACOSX_BUNDLE_BUNDLE_VERSION ${CHIAKI_VERSION}
		MACOSX_BUNDLE_COPYRIGHT "thestr4ng3r (AGPLv3)"
		MACOSX_BUNDLE_GUI_IDENTIFIER "org.streetpea.chiaking"
		MACOSX_BUNDLE_ICON_FILE chiaking.icns
		RESOURCE "${RESOURCE_FILES}")

if(WIN32)
	# PS-WRAP: ไฟล์ exe ชื่อ PS-WRAP.exe (target ยังชื่อ chiaki ลด diff กับ upstream)
	set_target_properties(chiaki PROPERTIES OUTPUT_NAME "PS-WRAP")
endif()

install(TARGETS chiaki
		RUNTIME DESTINATION bin
		BUNDLE DESTINATION bin)
install(FILES chiaking.desktop DESTINATION share/applications)
install(FILES chiaking.png DESTINATION share/icons/hicolor/512x512/apps)
install(FILES io.github.streetpea.Chiaki4deck.appdata.xml DESTINATION share/metainfo)
