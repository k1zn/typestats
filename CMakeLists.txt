cmake_minimum_required(VERSION 3.21)
# macOS: the oldest system Qt 6.8 runs on; CGPreflightListenEventAccess needs 10.15.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
project(TypingStatistics VERSION 1.2.0 LANGUAGES C CXX)
# The version of the remake (the release tags are v<it>); shown everywhere as "re-<it>".
set(TS_REMAKE_VERSION "${PROJECT_VERSION}")
set(TS_VERSION "re-${TS_REMAKE_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# The core computes in the 80-bit x87 format (src/core/Ext80.h): long double where it is that format,
# the software Ext80 elsewhere. TS_SOFT_EXT80=ON uses the software one everywhere (to test it).
option(TS_SOFT_EXT80 "Software 80-bit arithmetic even where long double is x87" OFF)

# The same floating-point results everywhere: no fused multiply-add (GCC contracts by default on
# arm64, Clang within expressions).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-ffp-contract=off>)
endif()

# Network: the time stamps of a recording (re/stamps.md), RFC 3161 over plain HTTP.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Test)
# A static Qt has no Linguist tools: TS_LRELEASE then names lrelease.exe of another Qt.
find_package(Qt6 QUIET COMPONENTS LinguistTools)

# --- third party ---
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(third_party/QXlsx EXCLUDE_FROM_ALL)
# The webcam (re/webcam.md): without it the video buttons stay disabled.
option(TS_WEBCAM "Webcam recording: AV1 and Opus codecs" ON)
if(TS_WEBCAM)
    set(CMAKE_AUTOMOC OFF)
    include(cmake/Media.cmake)
    set(CMAKE_AUTOMOC ON)
endif()

# Warnings for our own code (the targets below), not for the third-party code above.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# --- core (Qt Core only, unit-testable) ---
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
add_library(tscore STATIC ${CORE_SOURCES})
target_include_directories(tscore PUBLIC src)
target_link_libraries(tscore PUBLIC Qt6::Core)
if(TS_SOFT_EXT80)
    target_compile_definitions(tscore PUBLIC TS_SOFT_EXT80)
endif()

# --- command-line statistics (differential test bench) ---
add_executable(tsstat src/cli/tsstat.cpp)
target_link_libraries(tsstat PRIVATE tscore)

# --- the keyboard hook and the desktop (src/platform) ---
set(PLATFORM_SOURCES
    src/platform/KeyboardHook.h src/platform/KeyboardHook.cpp src/platform/HookClock.h
    src/platform/DesktopParsers.h src/platform/DesktopParsers.cpp
    src/platform/FileAssociation.h src/platform/FileAssociation.cpp)
if(WIN32)
    list(APPEND PLATFORM_SOURCES src/platform/KeyboardHookWin.cpp)
elseif(APPLE)
    # macOS: a listen-only event tap (re/crossplatform.md, step 6).
    list(APPEND PLATFORM_SOURCES src/platform/mac/KeyboardHookMac.cpp)
else()
    # Linux: evdev + xkbcommon (re/crossplatform.md, step 5).
    file(GLOB LINUX_SOURCES CONFIGURE_DEPENDS src/platform/linux/*.cpp src/platform/linux/*.h)
    list(APPEND PLATFORM_SOURCES ${LINUX_SOURCES})
endif()
add_library(tsplatform STATIC ${PLATFORM_SOURCES})
target_include_directories(tsplatform PUBLIC src)
target_link_libraries(tsplatform PUBLIC tscore)
if(WIN32)
    target_link_libraries(tsplatform PRIVATE shell32) # SHChangeNotify (FileAssociation)
elseif(APPLE)
    target_link_libraries(tsplatform PRIVATE "-framework ApplicationServices" "-framework Carbon" "-framework CoreFoundation"
                          "-framework CoreServices")
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
    target_link_libraries(tsplatform PUBLIC PkgConfig::XKBCOMMON)
    # Optional: sway's layout names (xkbregistry), X11 sessions (xcb), KDE and systemd-localed (QtDBus).
    pkg_check_modules(XKBREGISTRY IMPORTED_TARGET xkbregistry)
    if(XKBREGISTRY_FOUND)
        target_link_libraries(tsplatform PRIVATE PkgConfig::XKBREGISTRY)
        target_compile_definitions(tsplatform PRIVATE TS_HAVE_XKBREGISTRY)
    endif()
    pkg_check_modules(XKBX11 IMPORTED_TARGET xkbcommon-x11 xcb xcb-xkb)
    if(XKBX11_FOUND)
        target_link_libraries(tsplatform PRIVATE PkgConfig::XKBX11)
        target_compile_definitions(tsplatform PRIVATE TS_HAVE_X11)
    endif()
    find_package(Qt6 QUIET COMPONENTS DBus)
    if(TARGET Qt6::DBus)
        target_link_libraries(tsplatform PRIVATE Qt6::DBus)
        target_compile_definitions(tsplatform PRIVATE TS_HAVE_DBUS)
    endif()
    message(STATUS "Linux keyboard: xkbregistry ${XKBREGISTRY_FOUND}, X11 ${XKBX11_FOUND}, D-Bus ${Qt6DBus_FOUND}")
endif()

# --- the webcam's codecs (re/webcam.md) ---
if(TS_WEBCAM)
    set(MEDIA_SOURCES src/media/Yuv.h src/media/Yuv.cpp src/media/Av1Codec.h src/media/Av1Codec.cpp
        src/media/OpusCodec.h src/media/OpusCodec.cpp src/media/Capture.h)
    list(APPEND MEDIA_SOURCES src/media/AudioOut.h)
    if(WIN32)
        # Media Foundation (DirectShow for virtual cameras), waveOut
        list(APPEND MEDIA_SOURCES src/media/CaptureWin.cpp src/media/CaptureDShow.h src/media/CaptureDShow.cpp
             src/media/AudioOutWin.cpp)
    elseif(APPLE)
        # AVFoundation, AudioQueue.
        enable_language(OBJCXX)
        list(APPEND MEDIA_SOURCES src/media/mac/CaptureMac.mm src/media/mac/AudioOutMac.cpp)
        set_source_files_properties(src/media/mac/CaptureMac.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # V4L2; PulseAudio's simple API, loaded at run time.
        list(APPEND MEDIA_SOURCES src/media/linux/CaptureLinux.cpp src/media/linux/AudioOutLinux.cpp
             src/media/linux/PulseSimple.h src/media/linux/PulseSimple.cpp)
    else()
        list(APPEND MEDIA_SOURCES src/media/CaptureStub.cpp src/media/AudioOutStub.cpp)
    endif()
    add_library(tsmedia STATIC ${MEDIA_SOURCES})
    target_include_directories(tsmedia PUBLIC src)
    target_link_libraries(tsmedia PUBLIC Qt6::Gui PRIVATE ts_aom ts_opus)
    if(WIN32)
        target_link_libraries(tsmedia PRIVATE mfuuid strmiids ole32 oleaut32 winmm)
    elseif(APPLE)
        target_link_libraries(tsmedia PRIVATE "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
                              "-framework AudioToolbox" "-framework Foundation")
    else()
        target_link_libraries(tsmedia PRIVATE ${CMAKE_DL_LIBS})
    endif()
    target_compile_definitions(tsmedia PUBLIC TS_HAVE_WEBCAM)
endif()

# --- application ---
# The windows are a library so that tests can drive them (tests/tst_ui*.cpp, offscreen).
file(GLOB UI_SOURCES CONFIGURE_DEPENDS
    src/ui/*.cpp src/ui/*.h
    src/export/*.cpp src/export/*.h)
if(NOT TS_WEBCAM)
    list(FILTER UI_SOURCES EXCLUDE REGEX "/(WebcamRecorder|VideoWindow|VideoPropertiesDialog)\.")
endif()
add_library(tsui STATIC ${UI_SOURCES})
target_include_directories(tsui PUBLIC src)
target_link_libraries(tsui PUBLIC tscore tsplatform Qt6::Widgets Qt6::Network QXlsx::QXlsx)
if(TS_WEBCAM)
    target_link_libraries(tsui PUBLIC tsmedia)
endif()


qt_add_executable(TypingStatistics WIN32 MACOSX_BUNDLE src/main.cpp resources/resources.qrc)
if(WIN32)
    target_sources(TypingStatistics PRIVATE resources/app.rc) # the icon of the .exe
elseif(APPLE)
    # A fixed bundle id: the Input Monitoring permission is kept for it across builds.
    set(ICNS resources/macos/app.icns)
    set_source_files_properties(${ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(TypingStatistics PRIVATE ${ICNS})
    set_target_properties(TypingStatistics PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/macos/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER org.typingstatistics.TypingStatistics
        MACOSX_BUNDLE_BUNDLE_NAME "Typing statistics"
        MACOSX_BUNDLE_ICON_FILE app.icns
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${TS_REMAKE_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT "Typing statistics: Igor V. Filimonov, 2016. Qt port.")
endif()
target_compile_definitions(TypingStatistics PRIVATE TS_VERSION="${TS_VERSION}")
target_compile_definitions(tsui PRIVATE TS_VERSION="${TS_VERSION}")
target_link_libraries(TypingStatistics PRIVATE tsui)

# --- installation ---
# Linux: the program, its menu entry and icon, and the udev rule that gives the logged-in user the
# keyboards (re/crossplatform.md, step 5). The rule goes to the system's rules directory, wherever the
# prefix is (TS_UDEV_RULES_DIR overrides it, packagers set it to their staging tree).
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    set(TS_UDEV_RULES_DIR "/usr/lib/udev/rules.d" CACHE PATH "Where the udev rule is installed")
    set(TS_APP_ID org.typingstatistics.TypingStatistics)
    install(TARGETS TypingStatistics RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES resources/linux/${TS_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/linux/${TS_APP_ID}.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(FILES resources/icons/app.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps
            RENAME ${TS_APP_ID}.png)
    install(FILES resources/linux/app-256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME ${TS_APP_ID}.png)
    install(FILES resources/linux/70-typingstatistics.rules DESTINATION ${TS_UDEV_RULES_DIR})
elseif(APPLE)
    install(TARGETS TypingStatistics BUNDLE DESTINATION .)
elseif(WIN32)
    install(TARGETS TypingStatistics RUNTIME DESTINATION .)
endif()

file(GLOB TS_FILES CONFIGURE_DEPENDS i18n/*.ts)
if(TS_FILES AND TARGET Qt6::lrelease)
    qt_add_translations(TypingStatistics TS_FILES ${TS_FILES} RESOURCE_PREFIX /i18n)
elseif(TS_FILES)
    # Distributions keep Qt's tools out of PATH (Debian: /usr/lib/qt6/bin).
    find_program(TS_LRELEASE NAMES lrelease lrelease-qt6 REQUIRED
                 HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBEXECS}")
    set(QM_FILES)
    foreach(ts ${TS_FILES})
        get_filename_component(base ${ts} NAME_WE)
        set(qm ${CMAKE_BINARY_DIR}/i18n/${base}.qm)
        add_custom_command(OUTPUT ${qm} COMMAND ${TS_LRELEASE} -silent ${ts} -qm ${qm} DEPENDS ${ts} VERBATIM)
        list(APPEND QM_FILES ${qm})
    endforeach()
    set_source_files_properties(${QM_FILES} PROPERTIES GENERATED TRUE)
    qt_add_resources(TypingStatistics i18n PREFIX /i18n BASE ${CMAKE_BINARY_DIR}/i18n FILES ${QM_FILES})
endif()
# Qt's own texts in Russian (Yes/No/Cancel/Close, the file dialog outside Windows): the program is a single file,
# so the translation is inside it. A static Qt has no translations: take them from a shared one (TS_QT_TRANSLATIONS).
set(ts_qt_translations "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
if(NOT EXISTS "${ts_qt_translations}/qtbase_ru.qm" AND TS_LRELEASE)
    # The shared Qt whose lrelease builds a static one's program has them (<qt>/bin/lrelease -> <qt>/translations).
    get_filename_component(ts_lrelease_qt "${TS_LRELEASE}" DIRECTORY)
    get_filename_component(ts_lrelease_qt "${ts_lrelease_qt}" DIRECTORY)
    set(ts_qt_translations "${ts_lrelease_qt}/translations")
endif()
set(TS_QT_TRANSLATIONS "${ts_qt_translations}" CACHE PATH "Qt's translations (qtbase_ru.qm)")
if(EXISTS "${TS_QT_TRANSLATIONS}/qtbase_ru.qm")
    qt_add_resources(TypingStatistics qt_i18n PREFIX /i18n BASE "${TS_QT_TRANSLATIONS}"
                     FILES "${TS_QT_TRANSLATIONS}/qtbase_ru.qm")
else()
    message(STATUS "qtbase_ru.qm not found in TS_QT_TRANSLATIONS: Qt's buttons stay in English")
endif()

# --- tests ---
enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/tst_*.cpp)
foreach(src ${TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    if(name STREQUAL "tst_evdev" AND (WIN32 OR APPLE)) # the Linux backend
        continue()
    endif()
    if(name STREQUAL "tst_media" AND NOT TS_WEBCAM)
        continue()
    endif()
    add_executable(${name} ${src})
    # One library each (they bring tscore along): the macOS linker warns of a library given twice.
    if(name MATCHES "^tst_(ui|perf)")
        target_link_libraries(${name} PRIVATE tsui)
        target_sources(${name} PRIVATE resources/resources.qrc)
    elseif(name MATCHES "^tst_(platform|evdev)$")
        target_link_libraries(${name} PRIVATE tsplatform)
    elseif(name STREQUAL "tst_media")
        target_link_libraries(${name} PRIVATE tsmedia tscore)
    else()
        target_link_libraries(${name} PRIVATE tscore)
    endif()
    target_link_libraries(${name} PRIVATE Qt6::Test)
    target_compile_definitions(${name} PRIVATE TS_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
    # The performance bench (re/perf.md) needs large recordings and minutes: it is run by hand.
    if(name STREQUAL "tst_perf")
        if(WIN32)
            target_link_libraries(${name} PRIVATE psapi)
        endif()
        # A static Qt has no offscreen plugin: the bench of the single exe runs on "minimal:enable_fonts".
        get_target_property(qt_core_type Qt6::Core TYPE)
        if(qt_core_type STREQUAL "STATIC_LIBRARY" AND TARGET Qt6::QMinimalIntegrationPlugin)
            qt_import_plugins(${name} INCLUDE Qt6::QMinimalIntegrationPlugin)
        endif()
        continue()
    endif()
    add_test(NAME ${name} COMMAND ${name})
    # A hang fails with its output (the last test function run) instead of holding CI for its whole limit.
    set_tests_properties(${name} PROPERTIES TIMEOUT 300)
    if(name MATCHES "^tst_ui")
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endforeach()
