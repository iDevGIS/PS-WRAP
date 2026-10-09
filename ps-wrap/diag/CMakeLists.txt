# PS-WRAP-Diagnostics.exe — เครื่องมือวินิจฉัยปัญหา (Win32 ล้วน + static เพื่อให้เปิดได้แม้ Qt/DLL ของแอปพัง)
set(PSWRAP_DIAG_ICON "${CMAKE_SOURCE_DIR}/gui/pswrap.ico")
string(REPLACE "." "," PSWRAP_DIAG_VERSION_COMMA "${PSWRAP_VERSION}")
configure_file(pswrapdiag.rc.in "${CMAKE_CURRENT_BINARY_DIR}/pswrapdiag.rc" @ONLY)
configure_file(pswrapdiag.manifest "${CMAKE_CURRENT_BINARY_DIR}/pswrapdiag.manifest" COPYONLY)

add_executable(pswrap-diagnostics WIN32 pswrapdiag.cpp "${CMAKE_CURRENT_BINARY_DIR}/pswrapdiag.rc")
set_target_properties(pswrap-diagnostics PROPERTIES OUTPUT_NAME "PS-WRAP-Diagnostics")
target_compile_definitions(pswrap-diagnostics PRIVATE UNICODE _UNICODE)
target_include_directories(pswrap-diagnostics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")   # windres หา pswrapdiag.manifest
target_link_libraries(pswrap-diagnostics PRIVATE dbghelp wevtapi comctl32 shell32 ole32 uuid user32 gdi32 advapi32)
if(MINGW)
	target_link_options(pswrap-diagnostics PRIVATE -municode -static -static-libgcc -static-libstdc++ -s)
endif()
