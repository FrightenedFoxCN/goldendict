# Qt Single Application - Vendored library
# Build as static library to avoid namespace issues

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

set(QSA_SOURCES
    src/qtsingleapplication.cpp
    src/qtlocalpeer.cpp
    src/qtlockedfile.cpp
)

if(WIN32)
    list(APPEND QSA_SOURCES src/qtlockedfile_win.cpp)
else()
    list(APPEND QSA_SOURCES src/qtlockedfile_unix.cpp)
endif()

set(QSA_HEADERS
    src/qtsingleapplication.h
    src/qtlocalpeer.h
    src/qtlockedfile.h
)

# Create static library
add_library(QtSingleApplication STATIC ${QSA_SOURCES} ${QSA_HEADERS})

# Configure library settings
target_include_directories(QtSingleApplication PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(QtSingleApplication PUBLIC
    Qt6::Core Qt6::Network Qt6::Widgets
)

# Handle namespace properly
target_compile_definitions(QtSingleApplication PUBLIC
    QT_QTLOCKEDFILE_EXPORT=
)


# This is a header-only-ish library - sources are compiled into the main executable
# via src/CMakeLists.txt, so we don't create a separate library target here.
# This file is kept for reference and future modularization.
