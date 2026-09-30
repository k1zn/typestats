cmake_minimum_required(VERSION 3.21)
project(TypingStatistics VERSION 1.44 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets LinguistTools Test)

# --- third party ---
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(third_party/libuiohook EXCLUDE_FROM_ALL)
add_subdirectory(third_party/QXlsx EXCLUDE_FROM_ALL)

# --- core (Qt Core only, unit-testable) ---
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
add_library(tscore STATIC ${CORE_SOURCES})
target_include_directories(tscore PUBLIC src)
target_link_libraries(tscore PUBLIC Qt6::Core)

# --- command-line statistics (differential test bench) ---
add_executable(tsstat src/cli/tsstat.cpp)
target_link_libraries(tsstat PRIVATE tscore)

# --- application ---
file(GLOB APP_SOURCES CONFIGURE_DEPENDS
    src/platform/*.cpp src/platform/*.h
    src/ui/*.cpp src/ui/*.h
    src/export/*.cpp src/export/*.h)
qt_add_executable(TypingStatistics WIN32 MACOSX_BUNDLE src/main.cpp ${APP_SOURCES} resources/resources.qrc)
target_include_directories(TypingStatistics PRIVATE src)
target_compile_definitions(TypingStatistics PRIVATE TS_VERSION="${PROJECT_VERSION}")
target_link_libraries(TypingStatistics PRIVATE tscore Qt6::Widgets uiohook QXlsx::QXlsx)

file(GLOB TS_FILES CONFIGURE_DEPENDS i18n/*.ts)
if(TS_FILES)
    qt_add_translations(TypingStatistics TS_FILES ${TS_FILES} RESOURCE_PREFIX /i18n)
endif()

# --- tests ---
enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/tst_*.cpp)
foreach(src ${TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE tscore Qt6::Test)
    target_compile_definitions(${name} PRIVATE TS_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
    add_test(NAME ${name} COMMAND ${name})
endforeach()
