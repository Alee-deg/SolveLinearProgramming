cmake_minimum_required(VERSION 3.16)

project(SolveLinearProgramming VERSION 0.1 LANGUAGES CXX)

# ======================================================================
# Qt / C++ config
# ======================================================================
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# File .ui đã được gom vào thư mục ui/
set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

# ======================================================================
# Qt components
# ======================================================================
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets PrintSupport Network Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets PrintSupport Network Svg)

# ======================================================================
# Source files sau khi gom thư mục
# ======================================================================
set(PROJECT_SOURCES
    src/main.cpp

    src/core/Struct.h
    src/core/simplexsolver.h
    src/core/simplexsolver.cpp

    src/windows/mainwindow.h
    src/windows/mainwindow.cpp

    src/windows/dashboard.h
    src/windows/dashboard.cpp

    src/windows/wdsolve.h
    src/windows/wdsolve.cpp

    src/windows/wdchatbot.h
    src/windows/wdchatbot.cpp

    src/windows/wdshowimage.h
    src/windows/wdshowimage.cpp

    src/windows/qcustomplot.h
    src/windows/qcustomplot.cpp

    ui/mainwindow.ui
    ui/dashboard.ui
    ui/wdsolve.ui
    ui/wdchatbot.ui
    ui/wdshowimage.ui

    resources/resource.qrc
)

# ======================================================================
# Windows app icon
# ======================================================================
# Sau khi restructure, logo.ico đã nằm ở resources/images/logo.ico.
# Nếu vẫn compile resources/app_icon.rc cũ, windres sẽ báo:
#   can't open icon file `logo.ico`
#
# Cách dưới đây tạo app_icon_generated.rc trong thư mục build bằng đường dẫn
# tuyệt đối tới logo.ico, nên không phụ thuộc working directory của windres.
# Hoạt động ổn với GitHub Actions Windows + MinGW.
if(WIN32)
    set(APP_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/images/logo.ico")

    if(EXISTS "${APP_ICON_FILE}")
        set(GENERATED_APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/app_icon_generated.rc")

        # Dùng path kiểu CMake với dấu "/" để windres đọc được trên Windows.
        file(TO_CMAKE_PATH "${APP_ICON_FILE}" APP_ICON_FILE_FOR_RC)

        file(WRITE "${GENERATED_APP_ICON_RC}"
"#include <windows.h>\nIDI_ICON1 ICON DISCARDABLE \"${APP_ICON_FILE_FOR_RC}\"\n"
        )

        list(APPEND PROJECT_SOURCES
            "${GENERATED_APP_ICON_RC}"
        )
    else()
        message(WARNING "Không tìm thấy icon Windows: ${APP_ICON_FILE}. File .exe vẫn build được nhưng không có icon app.")
    endif()
endif()

# ======================================================================
# Create executable
# ======================================================================
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(SolveLinearProgramming
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(SolveLinearProgramming SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(SolveLinearProgramming
            ${PROJECT_SOURCES}
        )
    endif()
endif()

# ======================================================================
# Include directories
# Giữ nguyên được các dòng include cũ kiểu:
# #include "simplexsolver.h"
# #include "dashboard.h"
# ======================================================================
target_include_directories(SolveLinearProgramming PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

# ======================================================================
# Link Qt libraries
# ======================================================================
target_link_libraries(SolveLinearProgramming PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::PrintSupport
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Svg
)

# ======================================================================
# Bundle properties
# ======================================================================
if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.SolveLinearProgramming)
endif()

set_target_properties(SolveLinearProgramming PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# ======================================================================
# Install
# ======================================================================
include(GNUInstallDirs)

install(TARGETS SolveLinearProgramming
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(SolveLinearProgramming)
endif()
