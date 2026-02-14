# Source file organization by module

# Core application files
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/core)
file(GLOB CORE_SOURCES "${CORE_DIR}/*.cc")
file(GLOB CORE_HEADERS "${CORE_DIR}/*.hh")

# Dictionary management
set(DICTIONARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dictionary)
file(GLOB DICTIONARY_SOURCES "${DICTIONARY_DIR}/*.cc")
file(GLOB DICTIONARY_HEADERS "${DICTIONARY_DIR}/*.hh")

# Dictionary format handlers
set(FORMATS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/formats)
file(GLOB FORMATS_SOURCES "${FORMATS_DIR}/*.cc")
file(GLOB FORMATS_HEADERS "${FORMATS_DIR}/*.hh")

# UI and visual components
set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ui)
file(GLOB UI_SOURCES "${UI_DIR}/*.cc")
file(GLOB UI_HEADERS "${UI_DIR}/*.hh")
file(GLOB UI_FORMS "${UI_DIR}/forms/*.ui")
list(APPEND UI_FORMS "${UI_DIR}/forms/labels.ui")
file(GLOB UI_RESOURCES "${UI_DIR}/*.qrc")

# Audio players and sound handlers
set(AUDIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/audio)
file(GLOB AUDIO_SOURCES "${AUDIO_DIR}/*.cc")
# Exclude Windows-specific files
list(FILTER AUDIO_SOURCES EXCLUDE REGEX "speechclient_win\\.cc|speechhlp\\.cc")
file(GLOB AUDIO_HEADERS "${AUDIO_DIR}/*.hh")
list(FILTER AUDIO_HEADERS EXCLUDE REGEX "speechhlp\\.hh|sapi\\.hh|sphelper\\.hh")

# Text processing and transliteration
set(TEXT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/text)
file(GLOB TEXT_SOURCES "${TEXT_DIR}/*.cc")
file(GLOB TEXT_HEADERS "${TEXT_DIR}/*.hh")

# Network and online services
set(NETWORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/network)
file(GLOB NETWORK_SOURCES "${NETWORK_DIR}/*.cc")
file(GLOB NETWORK_HEADERS "${NETWORK_DIR}/*.hh")

# Utility and helper functions
set(UTIL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/util)
file(GLOB UTIL_SOURCES "${UTIL_DIR}/*.cc" "${UTIL_DIR}/*.c")
file(GLOB UTIL_HEADERS "${UTIL_DIR}/*.hh" "${UTIL_DIR}/*.h")

# About and documentation
set(ABOUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/about)
file(GLOB ABOUT_SOURCES "${ABOUT_DIR}/*.cc")
file(GLOB ABOUT_HEADERS "${ABOUT_DIR}/*.hh")

# Platform-specific code
set(PLATFORM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform)
file(GLOB PLATFORM_SOURCES "${PLATFORM_DIR}/*.cc" "${PLATFORM_DIR}/*.cpp" "${PLATFORM_DIR}/*.mm" "${PLATFORM_DIR}/*.c")
file(GLOB PLATFORM_HEADERS "${PLATFORM_DIR}/*.hh" "${PLATFORM_DIR}/*.h")
# Exclude Windows-only files
list(FILTER PLATFORM_SOURCES EXCLUDE REGEX ".*_win(32|64)\\.(cc|cpp|c)|x64\\.cc|wordbyauto\\.cc|hotkeys\\.c|guids\\.c")
list(FILTER PLATFORM_HEADERS EXCLUDE REGEX ".*_win(32|64)\\.h|x64\\.hh|wordbyauto\\.hh|uiauto\\.hh|hotkeys\\.h|fixx11h\\.h")

# Combine all sources and headers
set(ALL_SOURCES
    ${CORE_SOURCES} ${DICTIONARY_SOURCES} ${FORMATS_SOURCES} ${UI_SOURCES}
    ${AUDIO_SOURCES} ${TEXT_SOURCES} ${NETWORK_SOURCES} ${UTIL_SOURCES}
    ${ABOUT_SOURCES} ${PLATFORM_SOURCES}
)

set(ALL_HEADERS
    ${CORE_HEADERS} ${DICTIONARY_HEADERS} ${FORMATS_HEADERS} ${UI_HEADERS}
    ${AUDIO_HEADERS} ${TEXT_HEADERS} ${NETWORK_HEADERS} ${UTIL_HEADERS}
    ${ABOUT_HEADERS} ${PLATFORM_HEADERS}
)

set(ALL_FORMS ${UI_FORMS})
set(ALL_RESOURCES ${UI_RESOURCES})

# Platform-specific Objective-C++ files
if(APPLE)
    file(GLOB MM_SOURCES "${PLATFORM_DIR}/*.mm")
    set_source_files_properties(${MM_SOURCES} PROPERTIES LANGUAGE OBJCXX)
endif()

# Create main executable
add_executable(SilverDict 
    ${ALL_SOURCES} ${ALL_FORMS} ${ALL_RESOURCES}
)

# Add macOS app icon
if(APPLE)
    set(MACOSX_ICON_FILE "${UI_DIR}/icons/macicon.icns")
    set_source_files_properties(${MACOSX_ICON_FILE} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    target_sources(SilverDict PRIVATE ${MACOSX_ICON_FILE})
endif()

# Skip AUTOMOC for files with inline Q_OBJECT classes
# Note: articleview.cc needs MOC because it has #include "articleview.moc"
set_source_files_properties(
    ${UI_DIR}/mainwindow.cc
    PROPERTIES SKIP_AUTOMOC ON
)

# Add source files asproperty for IDE organization
foreach(hdr ${ALL_HEADERS})
    set_source_files_properties(${hdr} PROPERTIES HEADER_FILE_ONLY ON)
    target_sources(SilverDict PRIVATE ${hdr})
endforeach()

# Configure MOC to handle subdirectories properly
set_target_properties(SilverDict PROPERTIES
    AUTOUIC_SEARCH_PATHS "${UI_DIR}/forms"
    AUTOMOC_MACRO_NAMES  "Q_OBJECT"
    AUTOMOC_PATH_PREFIX  ON
)

# Set output properties
if(APPLE)
    set_target_properties(SilverDict PROPERTIES
        OUTPUT_NAME "SilverDict"
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "SilverDict"
        MACOSX_BUNDLE_EXECUTABLE_NAME "SilverDict"
        MACOSX_BUNDLE_INFO_STRING "SilverDict - Dictionary lookup program"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.goldendict.SilverDict"
        MACOSX_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "macicon.icns"
        BUNDLE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
elseif(NOT WIN32)
    set_target_properties(SilverDict PROPERTIES OUTPUT_NAME "silverdict")
endif()

# Link Qt libraries
target_link_libraries(SilverDict PRIVATE
    Qt6::Core Qt6::Gui Qt6::Xml Qt6::Network Qt6::Svg Qt6::Widgets
    Qt6::PrintSupport Qt6::Help Qt6::WebEngineWidgets Qt6::WebEngineCore
    Qt6::WebChannel Qt6::Core5Compat ZLIB::ZLIB BZip2::BZip2
    QtSingleApplication
    ${ZSTD_LIBRARY} ${OPENCC_LIBRARY}
)

if(MAKE_QTMULTIMEDIA_PLAYER)
    target_link_libraries(SilverDict PRIVATE Qt6::Multimedia)
endif()

if(MAKE_ZIM_SUPPORT)
    if(TARGET LibLZMA::LibLZMA)
        target_link_libraries(SilverDict PRIVATE LibLZMA::LibLZMA)
    endif()
endif()

if(MAKE_EXTRA_TIFF_HANDLER)
    target_link_libraries(SilverDict PRIVATE TIFF::TIFF)
endif()

# Platform-specific linking
if(WIN32)
    target_link_libraries(SilverDict PRIVATE ${HUNSPELL_LIBRARIES} iconv
        shell32 user32 sapi ole32 oleaut32 advapi32 comdlg32 wsock32 psapi dwmapi
        vorbisfile vorbis ogg
    )
    if(LZO2_LIBRARY)
        target_link_libraries(SilverDict PRIVATE ${LZO2_LIBRARY})
    endif()
    if(MAKE_FFMPEG_PLAYER)
        target_link_libraries(SilverDict PRIVATE ${AO_LIBRARY} ${SWRESAMPLE_LIBRARY}
            ${AVUTIL_LIBRARY} ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY}
        )
    endif()
    target_include_directories(SilverDict PRIVATE ../third-party/windows/include)
    target_link_directories(SilverDict PRIVATE ../third-party/windows/lib)

elseif(UNIX AND NOT APPLE)
    target_link_directories(SilverDict PRIVATE ${HUNSPELL_LIBRARY_DIRS})
    target_link_libraries(SilverDict PRIVATE ${HUNSPELL_LIBRARIES} ${VORBIS_LIBRARIES})
    if(MAKE_FFMPEG_PLAYER)
        target_link_libraries(SilverDict PRIVATE ${FFMPEG_LIBRARIES})
    endif()
    if(NOT ARM)
        target_link_libraries(SilverDict PRIVATE ${X11_LIBRARIES})
    else()
        target_link_libraries(SilverDict PRIVATE iconv)
    endif()
    target_compile_definitions(SilverDict PRIVATE PROGRAM_DATA_DIR="/usr/local/share/goldendict/")

elseif(APPLE)
    target_link_libraries(SilverDict PRIVATE ${APPKIT} ${CARBON} ${ICONV})
    if(VORBIS_FOUND)
        target_link_directories(SilverDict PRIVATE ${VORBIS_LIBRARY_DIRS})
        target_link_libraries(SilverDict PRIVATE ${VORBIS_LIBRARIES})
    endif()
    if(HUNSPELL_FOUND)
        target_link_directories(SilverDict PRIVATE ${HUNSPELL_LIBRARY_DIRS})
        target_link_libraries(SilverDict PRIVATE ${HUNSPELL_LIBRARIES})
    endif()
    if(LZO2_FOUND)
        target_link_directories(SilverDict PRIVATE ${LZO2_LIBRARY_DIRS})
        target_link_libraries(SilverDict PRIVATE ${LZO2_LIBRARIES})
    endif()
    if(MAKE_FFMPEG_PLAYER AND FFMPEG_FOUND)
        target_link_directories(SilverDict PRIVATE ${FFMPEG_LIBRARY_DIRS})
        target_link_libraries(SilverDict PRIVATE ${FFMPEG_LIBRARIES})
    endif()
endif()

# Include directories - all subdirectories
target_include_directories(SilverDict PRIVATE
    ${CORE_DIR} ${DICTIONARY_DIR} ${FORMATS_DIR} ${UI_DIR} ${AUDIO_DIR}
    ${TEXT_DIR} ${NETWORK_DIR} ${UTIL_DIR} ${ABOUT_DIR} ${PLATFORM_DIR} ${QSA_DIR}
)

# Translations
file(GLOB TRANSLATION_FILES ../resources/locale/*.ts)
qt_add_translations(SilverDict TS_FILES ${TRANSLATION_FILES})
