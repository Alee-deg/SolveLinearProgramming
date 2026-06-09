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
# App icon paths
# ======================================================================
set(APP_ICON_WIN "${CMAKE_CURRENT_SOURCE_DIR}/resources/images/logo.ico")
set(APP_ICON_MAC "${CMAKE_CURRENT_SOURCE_DIR}/resources/images/logo.icns")
set(APP_ICON_LINUX "${CMAKE_CURRENT_SOURCE_DIR}/resources/images/logo.png")

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
# Windows icon
# ======================================================================
# Windows cần file .rc để gắn icon vào file .exe.
# Không dùng resources/app_icon.rc cố định nữa vì sau khi restructure path dễ sai.
# CMake tự tạo app_icon_generated.rc bằng đường dẫn tuyệt đối tới logo.ico.
if(WIN32)
    if(EXISTS "${APP_ICON_WIN}")
        set(GENERATED_APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/app_icon_generated.rc")
        file(TO_CMAKE_PATH "${APP_ICON_WIN}" APP_ICON_WIN_FOR_RC)

        file(WRITE "${GENERATED_APP_ICON_RC}"
"#include <windows.h>\nIDI_ICON1 ICON DISCARDABLE \"${APP_ICON_WIN_FOR_RC}\"\n"
        )

        list(APPEND PROJECT_SOURCES
            "${GENERATED_APP_ICON_RC}"
        )
    else()
        message(WARNING "Không tìm thấy Windows icon: ${APP_ICON_WIN}. File .exe vẫn build được nhưng không có icon.")
    endif()
endif()

# ======================================================================
# macOS icon
# ======================================================================
# macOS KHÔNG dùng .ico hoặc .rc. macOS bundle cần file .icns trong:
#   SolveLinearProgramming.app/Contents/Resources/logo.icns
#
# Cần đặt file:
#   resources/images/logo.icns
#
# Nếu chưa có logo.icns, tạo từ logo.png bằng iconutil trên macOS hoặc trong
# GitHub Actions macOS job.
if(APPLE)
    if(EXISTS "${APP_ICON_MAC}")
        set(MACOSX_BUNDLE_ICON_FILE "logo.icns")
        set_source_files_properties("${APP_ICON_MAC}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
        list(APPEND PROJECT_SOURCES
            "${APP_ICON_MAC}"
        )
    else()
        message(WARNING "Không tìm thấy macOS icon: ${APP_ICON_MAC}. App .app/.dmg vẫn build được nhưng có thể không có icon.")
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
# Linux desktop entry + icon install
# ======================================================================
# Linux AppImage/desktop environment KHÔNG lấy icon từ .rc hay .ico.
# Cần có .desktop file và icon .png theo tên app.
#
# Với AppImage bằng linuxdeploy, workflow vẫn nên copy file desktop/icon vào AppDir.
# Block install dưới đây giúp khi dùng cmake --install hoặc linuxdeploy plugin,
# icon/desktop metadata có sẵn đúng chuẩn.
if(UNIX AND NOT APPLE)
    set(LINUX_DESKTOP_FILE "${CMAKE_CURRENT_BINARY_DIR}/SolveLinearProgramming.desktop")

    file(WRITE "${LINUX_DESKTOP_FILE}"
"[Desktop Entry]\n"
"Type=Application\n"
"Name=SolveLinearProgramming\n"
"Comment=Linear Programming Solver\n"
"Exec=SolveLinearProgramming\n"
"Icon=SolveLinearProgramming\n"
"Terminal=false\n"
"Categories=Education;Science;Math;\n"
    )

    if(EXISTS "${APP_ICON_LINUX}")
        install(FILES "${APP_ICON_LINUX}"
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME SolveLinearProgramming.png
        )
    else()
        message(WARNING "Không tìm thấy Linux icon: ${APP_ICON_LINUX}. AppImage vẫn build được nhưng có thể không có icon.")
    endif()

    install(FILES "${LINUX_DESKTOP_FILE}"
        DESTINATION share/applications
    )
endif()

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
