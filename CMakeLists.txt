cmake_minimum_required(VERSION 3.20)
project(GoldenDict VERSION 1.5.1 LANGUAGES CXX C OBJCXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Use ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# Qt configuration
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Xml
    Network
    Svg
    Widgets
    PrintSupport
    Help
    WebEngineWidgets
    WebEngineCore
    WebChannel
    Core5Compat
    LinguistTools
)

# Optional Qt modules
option(MAKE_QTMULTIMEDIA_PLAYER "Build with Qt Multimedia player support" ON)
if(MAKE_QTMULTIMEDIA_PLAYER)
    find_package(Qt6 REQUIRED COMPONENTS Multimedia)
    add_compile_definitions(MAKE_QTMULTIMEDIA_PLAYER)
endif()

# Optional features
option(MAKE_FFMPEG_PLAYER "Build with FFmpeg player support" ON)
if(MAKE_FFMPEG_PLAYER)
    add_compile_definitions(MAKE_FFMPEG_PLAYER)
endif()

option(MAKE_ZIM_SUPPORT "Build with ZIM format support" ON)
if(MAKE_ZIM_SUPPORT)
    add_compile_definitions(MAKE_ZIM_SUPPORT)
endif()

option(MAKE_EXTRA_TIFF_HANDLER "Build with extra TIFF handler" ON)
if(MAKE_EXTRA_TIFF_HANDLER)
    add_compile_definitions(MAKE_EXTRA_TIFF_HANDLER)
endif()

option(MAKE_CHINESE_CONVERSION_SUPPORT "Build with Chinese conversion support" ON)
if(MAKE_CHINESE_CONVERSION_SUPPORT)
    add_compile_definitions(MAKE_CHINESE_CONVERSION_SUPPORT)
endif()

# Build-time version information
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(GIT_VERSION)
        set(PROGRAM_VERSION "${GIT_VERSION}")
    else()
        set(PROGRAM_VERSION "1.5.1+git")
    endif()
else()
    set(PROGRAM_VERSION "1.5.1")
endif()

add_compile_definitions(PROGRAM_VERSION="${PROGRAM_VERSION}")

# Release build optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(NDEBUG NO_GD_DPRINTF)
endif()

# Platform-specific settings
if(WIN32)
    set(CMAKE_WIN32_EXECUTABLE ON)
    add_compile_definitions(__WIN32 _CRT_SECURE_NO_WARNINGS)
    
    if(MSVC)
        add_compile_options(/wd4290)  # C++ exception specification ignored
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /OPT:REF /OPT:ICF")
        add_compile_definitions(GD_NO_MANIFEST NOMINMAX)
        
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            add_compile_definitions(__WIN64)
        endif()
    endif()
endif()

if(UNIX AND NOT APPLE)
    add_compile_definitions(HAVE_X11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -rdynamic")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
endif()

if(APPLE)
    set_property(GLOBAL PROPERTY XCODE_GENERATE_TOP_LEVEL_PROJECT_ONLY ON)
endif()

# Find required libraries
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)

# Find zstd library directly
find_library(ZSTD_LIBRARY zstd REQUIRED)
message(STATUS "Found zstd: ${ZSTD_LIBRARY}")

find_library(OPENCC_LIBRARY opencc REQUIRED)
message(STATUS "Found opencc: ${OPENCC_LIBRARY}")

if(MAKE_ZIM_SUPPORT)
    find_package(LibLZMA REQUIRED)
endif()

if(MAKE_EXTRA_TIFF_HANDLER)
    find_package(TIFF REQUIRED)
endif()

find_package(opencc REQUIRED)

if(MAKE_CHINESE_CONVERSION_SUPPORT)
endif()

# Platform-specific dependencies
if(WIN32)
    find_package(Hunspell REQUIRED)
    find_library(LZO2_LIBRARY NAMES lzo2)
    if(MAKE_FFMPEG_PLAYER)
        find_library(AO_LIBRARY NAMES ao)
        find_library(SWRESAMPLE_LIBRARY NAMES swresample-gd)
        find_library(AVUTIL_LIBRARY NAMES avutil-gd)
        find_library(AVFORMAT_LIBRARY NAMES avformat-gd)
        find_library(AVCODEC_LIBRARY NAMES avcodec-gd)
    endif()
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HUNSPELL REQUIRED hunspell)
    pkg_check_modules(VORBIS REQUIRED vorbisfile vorbis ogg)
    
    if(MAKE_FFMPEG_PLAYER)
        pkg_check_modules(FFMPEG REQUIRED libavutil libavformat libavcodec libswresample ao)
    endif()
    
    if(NOT ARM)
        find_package(X11 REQUIRED)
    endif()
endif()

if(APPLE)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        # Homebrew packages
        pkg_check_modules(VORBIS REQUIRED vorbisfile vorbis ogg)
        pkg_check_modules(HUNSPELL REQUIRED hunspell)
        pkg_check_modules(LZO2 REQUIRED lzo2)
        
        if(MAKE_FFMPEG_PLAYER)
            pkg_check_modules(FFMPEG REQUIRED libavutil libavformat libavcodec libswresample ao)
        endif()
        
        if(MAKE_EXTRA_TIFF_HANDLER)
            pkg_check_modules(TIFF REQUIRED libtiff-4)
        endif()
    endif()
    
    find_library(APPKIT AppKit REQUIRED)
    find_library(CARBON Carbon REQUIRED)
    find_library(ICONV iconv REQUIRED)
endif()

# Add subdirectories
add_subdirectory(qtsingleapplication)
add_subdirectory(src)

# Installation
if(UNIX AND NOT APPLE)
    set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation prefix" FORCE)
    
    install(TARGETS GoldenDict
        RUNTIME DESTINATION bin
    )
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/locale/"
        DESTINATION share/goldendict/locale/
        FILES_MATCHING PATTERN "*.qm"
    )
    install(DIRECTORY help/
        DESTINATION share/goldendict/help/
        FILES_MATCHING PATTERN "*.qch"
    )
    install(FILES redist/icons/*
        DESTINATION share/pixmaps/
    )
    install(FILES redist/*.desktop
        DESTINATION share/applications/
    )
    install(FILES redist/*.metainfo.xml
        DESTINATION share/metainfo/
    )
endif()

if(APPLE)
    install(TARGETS GoldenDict
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
    )
endif()

if(WIN32)
    install(TARGETS GoldenDict
        RUNTIME DESTINATION bin
    )
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "GoldenDict ${PROJECT_VERSION} - Build Configuration")
message(STATUS "==========================================")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  Qt Multimedia Player: ${MAKE_QTMULTIMEDIA_PLAYER}")
message(STATUS "  FFmpeg Player: ${MAKE_FFMPEG_PLAYER}")
message(STATUS "  ZIM Support: ${MAKE_ZIM_SUPPORT}")
message(STATUS "  Extra TIFF Handler: ${MAKE_EXTRA_TIFF_HANDLER}")
message(STATUS "  Chinese Conversion: ${MAKE_CHINESE_CONVERSION_SUPPORT}")
message(STATUS "")
