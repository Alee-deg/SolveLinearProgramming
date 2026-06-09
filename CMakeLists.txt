cmake_minimum_required(VERSION 3.16)

project(SolveLinearProgramming VERSION 0.1 LANGUAGES CXX)

# ======================================================================
# Cấu hình Qt/C++
# ======================================================================
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cho phép include header cùng thư mục hiện tại và thư mục build sinh ra ui_*.h
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Vì file .ui đã được gom vào thư mục ui/
set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

# ======================================================================
# Thư viện Qt cần dùng
# Widgets      : giao diện chính
# PrintSupport : in / xuất PDF / hỗ trợ QPrinter
# Network      : gọi API chatbot
# Svg          : hiển thị icon SVG nếu có
# ======================================================================
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets PrintSupport Network Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets PrintSupport Network Svg)

# ======================================================================
# File nguồn sau khi gom repository
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

# File .rc chỉ dành cho Windows. Không đưa vào Linux/macOS để tránh lỗi build.
if(WIN32)
    list(APPEND PROJECT_SOURCES
        resources/app_icon.rc
    )
endif()

# ======================================================================
# Tạo executable
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
# Giúp code vẫn dùng được:
#   #include "simplexsolver.h"
#   #include "dashboard.h"
#   #include "ui_dashboard.h"
# mà không cần sửa hàng loạt include.
# ======================================================================

target_include_directories(SolveLinearProgramming PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

# ======================================================================
# Link thư viện Qt
# ======================================================================

target_link_libraries(SolveLinearProgramming PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::PrintSupport
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Svg
)

# ======================================================================
# Thuộc tính app bundle / executable
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
# Cài đặt / đóng gói
# ======================================================================

include(GNUInstallDirs)

install(TARGETS SolveLinearProgramming
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ======================================================================
# Finalize Qt6 executable
# ======================================================================

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(SolveLinearProgramming)
endif()
