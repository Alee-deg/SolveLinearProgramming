cmake_minimum_required(VERSION 3.5)

project(SolveLinearProgramming VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Thêm thư mục hiện tại vào danh sách tìm kiếm file Header (.h)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# 1. BỔ SUNG COMPONENTS 'Network' ĐỂ GỌI API VÀ 'Svg' ĐỂ HIỂN THỊ ICON
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets PrintSupport Network Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets PrintSupport Network Svg)

# 2. GOM TẤT CẢ FILE NGUỒN VÀO ĐÂY
set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        dashboard.h
        dashboard.cpp
        dashboard.ui
        wdsolve.h
        wdsolve.cpp
        wdsolve.ui
        Struct.h
        simplexsolver.h
        simplexsolver.cpp
        qcustomplot.h        # <-- Thêm file header QCustomPlot
        qcustomplot.cpp      # <-- Thêm file source QCustomPlot
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(SolveLinearProgramming
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        resource.qrc
        wdshowimage.h wdshowimage.cpp wdshowimage.ui
        wdchatbot.h wdchatbot.cpp wdchatbot.ui app_icon.rc
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

# 3. LIÊN KẾT THƯ VIỆN NETWORK, PRINTSUPPORT VÀ SVG VÀO TARGET CHÍNH
target_link_libraries(SolveLinearProgramming PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::PrintSupport
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Svg           # <-- Đã thêm thư viện Svg ở đây để load icon
)

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

include(GNUInstallDirs)
install(TARGETS SolveLinearProgramming
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(SolveLinearProgramming)
endif()
