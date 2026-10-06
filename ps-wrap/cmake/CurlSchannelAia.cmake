# Apply the tracked extension to a build-local copy, leaving the submodule clean.
find_package(Git REQUIRED)
set(_aia_patch "${CMAKE_CURRENT_LIST_DIR}/curl-schannel-aia.patch")
set(CHIAKI_CURL_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/curl-patched-source")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/curl/"
    DESTINATION "${CHIAKI_CURL_SOURCE_DIR}" PATTERN ".git" EXCLUDE)
# Refresh patch inputs even if the previous patched copy has newer timestamps.
# PS-WRAP: แปลงไฟล์ที่จะแพตช์และตัวแพตช์เป็น LF ก่อนเสมอ — ถ้าเครื่องตั้ง core.autocrlf=true submodule curl จะถูก
#   checkout เป็น CRLF ส่วนแพตช์เป็น LF (.gitattributes ของเรา) → "patch does not apply" ใน clone ใหม่
foreach(_input include/curl/curl.h lib/vtls/schannel_verify.c)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/curl/${_input}" _aia_text)
    string(REPLACE "\r\n" "\n" _aia_text "${_aia_text}")
    file(WRITE "${CHIAKI_CURL_SOURCE_DIR}/${_input}" "${_aia_text}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/curl/${_input}"
        "${CMAKE_CURRENT_BINARY_DIR}/curl-aia-inputs/${_input}" COPYONLY)  # ให้ CMake รู้ว่าต้อง configure ใหม่เมื่อ input เปลี่ยน
endforeach()
file(READ "${_aia_patch}" _aia_text)
string(REPLACE "\r\n" "\n" _aia_text "${_aia_text}")
set(_aia_patch "${CMAKE_CURRENT_BINARY_DIR}/curl-schannel-aia.lf.patch")
file(WRITE "${_aia_patch}" "${_aia_text}")
# PS-WRAP: ถ้าโฟลเดอร์ build อยู่ใต้ git repo อื่น (repo ของเราเอง) `git apply --directory` จะข้าม path นอก subdir แบบเงียบๆ
#   (exit 0 แต่ไม่แพตช์) → ทำ copy ของ curl ให้เป็น repo ของตัวเอง (git init) แล้ว apply ที่ราก
#   (GIT_CEILING_DIRECTORIES ใช้ไม่ได้กับ git ของ MSYS2 เพราะแยก list ด้วย ':' ชน "C:/")
if(NOT EXISTS "${CHIAKI_CURL_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" init -q WORKING_DIRECTORY "${CHIAKI_CURL_SOURCE_DIR}")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check "${_aia_patch}"
    WORKING_DIRECTORY "${CHIAKI_CURL_SOURCE_DIR}"
    RESULT_VARIABLE _can_apply)
if(NOT _can_apply EQUAL 0)
    message(FATAL_ERROR "Bundled curl does not match the Schannel AIA patch")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${_aia_patch}"
    WORKING_DIRECTORY "${CHIAKI_CURL_SOURCE_DIR}"
    RESULT_VARIABLE _apply_result)
if(NOT _apply_result EQUAL 0)
    message(FATAL_ERROR "Failed to apply the Schannel AIA patch")
endif()
