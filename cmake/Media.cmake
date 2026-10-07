# The codecs of the webcam recording (re/webcam.md): AV1 (libaom, realtime only) and Opus. Built from their release
# archives as static libraries; TS_DEPS_DIR names a folder with the archives already downloaded.
include(ExternalProject)
include(FetchContent)

set(TS_DEPS_DIR "" CACHE PATH "Folder with libaom-*.tar.gz and opus-*.tar.gz (downloaded when not there)")
option(TS_SYSTEM_CODECS "Link the system's libaom and opus (pkg-config) instead of building them" OFF)

set(TS_AOM_VERSION 3.15.1)
set(TS_AOM_SHA256 8ca0c52746174603500f0adb6f2a215d69c9ca2aab2acb3caa06fb791d8d01bf)
set(TS_OPUS_VERSION 1.5.2)
set(TS_OPUS_SHA256 65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1)

function(ts_archive_url out name url)
    if(TS_DEPS_DIR AND EXISTS "${TS_DEPS_DIR}/${name}")
        set(${out} "${TS_DEPS_DIR}/${name}" PARENT_SCOPE)
    else()
        set(${out} "${url}/${name}" PARENT_SCOPE)
    endif()
endfunction()

if(TS_SYSTEM_CODECS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AOM REQUIRED IMPORTED_TARGET aom>=3.6)
    pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
    add_library(ts_aom INTERFACE)
    target_link_libraries(ts_aom INTERFACE PkgConfig::AOM)
    add_library(ts_opus INTERFACE)
    target_link_libraries(ts_opus INTERFACE PkgConfig::OPUS)
    return()
endif()

# MinGW: GCC does not align the stack of Win64 functions to 32 bytes, and spilled AVX2 registers (__m256i) crash on
# aligned moves; the assembler turns them into unaligned ones (as FFmpeg and x264 build there).
set(ts_codec_flags "")
if(MINGW AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(ts_codec_flags "-Wa,-muse-unaligned-vector-move")
endif()

# libaom: its own CMake run (it writes its options and warnings into the global compiler flags), optimized for size
# (the realtime encoder at speed 10 is far ahead of the camera all the same; the single exe is 2 MB smaller), without
# AVX-512. The realtime encoder and the decoder, 8 bits, nothing else.
ts_archive_url(aom_url libaom-${TS_AOM_VERSION}.tar.gz https://storage.googleapis.com/aom-releases)
set(aom_prefix ${CMAKE_BINARY_DIR}/_deps/aom)
set(aom_lib ${aom_prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}aom${CMAKE_STATIC_LIBRARY_SUFFIX})
set(aom_args
    -DCMAKE_BUILD_TYPE=MinSizeRel
    -DENABLE_AVX512=0
    -DCMAKE_INSTALL_PREFIX=${aom_prefix}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS} ${ts_codec_flags}"
    "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} ${ts_codec_flags}"
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF
    -DENABLE_DOCS=0 -DENABLE_EXAMPLES=0 -DENABLE_TESTS=0 -DENABLE_TESTDATA=0 -DENABLE_TOOLS=0 -DENABLE_APPS=0
    -DCONFIG_REALTIME_ONLY=1 -DCONFIG_AV1_HIGHBITDEPTH=0 -DCONFIG_WEBM_IO=0 -DCONFIG_LIBYUV=0)
if(APPLE)
    list(APPEND aom_args -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
    if(CMAKE_OSX_ARCHITECTURES)
        list(APPEND aom_args -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES})
        if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64" AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
            list(APPEND aom_args -DAOM_TARGET_CPU=x86_64)
        endif()
    endif()
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    list(APPEND aom_args -DENABLE_NASM=ON)
endif()
ExternalProject_Add(aom_ext
    URL ${aom_url}
    URL_HASH SHA256=${TS_AOM_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PREFIX ${aom_prefix}
    CMAKE_ARGS ${aom_args}
    BUILD_BYPRODUCTS ${aom_lib}
    EXCLUDE_FROM_ALL TRUE)
file(MAKE_DIRECTORY ${aom_prefix}/include)
add_library(ts_aom STATIC IMPORTED GLOBAL)
set_target_properties(ts_aom PROPERTIES IMPORTED_LOCATION ${aom_lib} INTERFACE_INCLUDE_DIRECTORIES ${aom_prefix}/include)
add_dependencies(ts_aom aom_ext)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(ts_aom INTERFACE Threads::Threads m)
endif()

# opus: a well-behaved subproject. The library only.
ts_archive_url(opus_url opus-${TS_OPUS_VERSION}.tar.gz https://downloads.xiph.org/releases/opus)
FetchContent_Declare(opus URL ${opus_url} URL_HASH SHA256=${TS_OPUS_SHA256} DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(opus)
if(ts_codec_flags)
    target_compile_options(opus PRIVATE ${ts_codec_flags})
endif()
add_library(ts_opus INTERFACE)
target_link_libraries(ts_opus INTERFACE Opus::opus)
