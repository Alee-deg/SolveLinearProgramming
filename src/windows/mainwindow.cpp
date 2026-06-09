#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QApplication>
#include <QSettings>
#include <QMenuBar>
#include <QPushButton>
#include <QFile>
#include <QStringList>
#include <QPointer>
#include <QWindow>
#include <QVariant>
#include <QTimer>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// =======================================================================
// [FIX ICON TASKBAR - WINDOWS / MACOS / LINUX]
// Lý do lỗi cũ:
// - MainWindow có icon, nhưng Dashboard / WdSolve / WdChatBot / WdShowImage
//   được mở như cửa sổ top-level khác.
// - Trên Windows, nếu cửa sổ mới không có icon native riêng hoặc là owned window
//   của MainWindow đang bị hide, taskbar có thể mất icon hoặc hiện icon mặc định.
//
// Cách xử lý:
// 1. Tải icon từ Qt Resource với nhiều đường dẫn fallback.
// 2. Set icon cho QApplication và từng top-level window.
// 3. Trên Windows, ép thêm icon native bằng WM_SETICON từ resource icon của .exe.
// 4. Các cửa sổ chuyển màn hình sẽ được tạo với parent = nullptr để không trở
//    thành owned window bị phụ thuộc taskbar vào MainWindow.
// =======================================================================
static QIcon phanMemQHTTAppIcon()
{
    const QStringList resourceCandidates = {
        ":/logo.png",
        ":/new/prefix1/logo.png",
        ":/logo.ico",
        ":/new/prefix1/logo.ico",
        ":/Logo TA Ngang.png",
        ":/new/prefix1/Logo TA Ngang.png"
    };

    for (const QString& path : resourceCandidates) {
        if (QFile::exists(path)) {
            QIcon icon(path);
            if (!icon.isNull()) {
                if (qApp) {
                    qApp->setWindowIcon(icon);
                }
                return icon;
            }
        }
    }

    if (qApp && !qApp->windowIcon().isNull()) {
        return qApp->windowIcon();
    }

    return QIcon();
}

#ifdef Q_OS_WIN
static HICON phanMemQHTTLoadWindowsResourceIcon(int width, int height)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);

    HICON icon = reinterpret_cast<HICON>(
        LoadImageW(instance, L"IDI_ICON1", IMAGE_ICON, width, height, LR_DEFAULTCOLOR)
        );

    if (!icon) {
        icon = reinterpret_cast<HICON>(
            LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, width, height, LR_DEFAULTCOLOR)
            );
    }

    if (!icon) {
        icon = reinterpret_cast<HICON>(
            LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, width, height, LR_DEFAULTCOLOR)
            );
    }

    return icon;
}

static void applyPhanMemQHTTNativeWindowsIcon(QWidget *window)
{
    if (!window) return;

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return;

    HICON bigIcon = phanMemQHTTLoadWindowsResourceIcon(
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON)
        );

    HICON smallIcon = phanMemQHTTLoadWindowsResourceIcon(
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON)
        );

    if (bigIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
        SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(bigIcon));
    }

    if (smallIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(smallIcon));
    }
}
#endif

static void applyPhanMemQHTTWindowIcon(QWidget *window)
{
    if (!window) return;

    const QIcon icon = phanMemQHTTAppIcon();
    if (!icon.isNull()) {
        window->setWindowIcon(icon);

        if (window->windowHandle()) {
            window->windowHandle()->setIcon(icon);
        }
    }

#ifdef Q_OS_WIN
    // Ép tạo native handle và set icon lại sau khi Qt đã tạo cửa sổ thật.
    applyPhanMemQHTTNativeWindowsIcon(window);

    QPointer<QWidget> safeWindow(window);
    QTimer::singleShot(0, window, [safeWindow, icon]() {
        if (!safeWindow) return;

        if (!icon.isNull()) {
            safeWindow->setWindowIcon(icon);
            if (safeWindow->windowHandle()) {
                safeWindow->windowHandle()->setIcon(icon);
            }
        }

        applyPhanMemQHTTNativeWindowsIcon(safeWindow.data());
    });
#endif
}

static void setPhanMemQHTTReturnWindow(QWidget *childWindow, QWidget *returnWindow)
{
    if (!childWindow || !returnWindow) return;
    childWindow->setProperty("phanMemQHTT_returnWindow",
                             QVariant::fromValue<QObject*>(returnWindow));
}

static QWidget* phanMemQHTTReturnWindow(QWidget *currentWindow)
{
    if (!currentWindow) return nullptr;

    QObject *returnObject =
        currentWindow->property("phanMemQHTT_returnWindow").value<QObject*>();

    QWidget *returnWidget = qobject_cast<QWidget*>(returnObject);
    if (returnWidget) return returnWidget;

    return currentWindow->parentWidget();
}


// THÊM THƯ VIỆN ĐỂ LƯU FILE XUYÊN HỆ ĐIỀU HÀNH
#include <QStandardPaths>
#include <QDir>

// =======================================================================
// [FIX LINUX] HÀM TẠO ĐƯỜNG DẪN AN TOÀN CHO SETTINGS
// =======================================================================
static QString getSettingsPath() {
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir;
    if (!dir.exists(dataDir)) {
        dir.mkpath(dataDir);
    }
    return dataDir + "/settings.ini";
}

// =======================================================================
// HÀM QUẢN LÝ THEME GLOBAL (Ép buộc toàn bộ ứng dụng đổi màu bằng !important)
// =======================================================================
void applyGlobalTheme(bool isDark) {
    QString css;
    if (isDark) {
        css = "* { font-family: 'Times New Roman'; font-size: 13pt; }"
              "QMainWindow, QDialog, QWidget#centralwidget { background-color: #1E1E2E !important; color: #CDD6F4 !important; }"

              /* ÉP CHỮ MÀU TRẮNG TUYỆT ĐỐI */
              "QLabel, QRadioButton, QCheckBox, QGroupBox { background-color: transparent !important; color: #CDD6F4 !important; border: none !important; }"

              /* ÉP MÀU BẢNG BIỂU VÀ THANH DỌC (FIX LỖI VỆT ĐEN) */
              "QTableWidget, QTableView { background-color: #181825 !important; color: #CDD6F4 !important; gridline-color: #45475A !important; border: 1px solid #45475A !important; }"
              "QTableView::viewport { background-color: #181825 !important; }"
              "QHeaderView { background-color: #181825 !important; border: none !important; }"
              "QTableWidget::item { background-color: transparent !important; color: #CDD6F4 !important; }"
              "QHeaderView::section { background-color: #313244 !important; color: #CDD6F4 !important; font-weight: bold !important; border: 1px solid #45475A !important; padding: 4px !important; }"
              "QTableCornerButton::section { background-color: #313244 !important; border: 1px solid #45475A !important; }"

              /* ÉP MÀU Ô NHẬP LIỆU */
              "QLineEdit { background-color: transparent !important; color: #CDD6F4 !important; border: none !important; }"
              "QLineEdit:focus { background-color: #313244 !important; border: 2px solid #89B4FA !important; }"
              "QLineEdit:disabled { background-color: #313244 !important; color: #6C7086 !important; }"
              "QComboBox, QSpinBox { background-color: #313244 !important; color: #CDD6F4 !important; border: 1px solid #45475A !important; border-radius: 4px !important; padding: 2px !important; }"
              "QComboBox:focus, QSpinBox:focus { background-color: #45475A !important; border: 2px solid #89B4FA !important; }"
              "QComboBox QAbstractItemView { background-color: #313244 !important; color: #CDD6F4 !important; selection-background-color: #45475A !important; }"

              /* TRẢ LẠI KHUNG CHO Ô GIÁ TRỊ TỐI ƯU */
              "QLineEdit#lineEdit_Z { background-color: #313244 !important; border: 1px solid #45475A !important; border-radius: 4px !important; padding: 4px !important; color: #A6E3A1 !important; font-weight: bold !important; }"

              /* NÚT BẤM THƯỜNG */
              "QPushButton { background-color: #313244 !important; color: #CDD6F4 !important; border: 1px solid #45475A !important; border-radius: 4px !important; padding: 6px 15px !important; font-weight: bold !important; }"
              "QPushButton:hover { background-color: #45475A !important; }"

              /* NÚT GIẢI BÀI TOÁN (NỔI BẬT CHUNG) */
              "QPushButton#pushButton_3 { background-color: #89B4FA !important; color: #1E1E2E !important; border: none !important; }"
              "QPushButton#pushButton_3:hover { background-color: #B4BEFE !important; }"

              /* FIX LỖI: ÉP RIÊNG NÚT HỎI ĐÁP Ở WDSOLVE VỀ LẠI MÀU BÌNH THƯỜNG */
              "WdSolve QPushButton#pushButton_3 { background-color: #313244 !important; color: #CDD6F4 !important; border: 1px solid #45475A !important; }"
              "WdSolve QPushButton#pushButton_3:hover { background-color: #45475A !important; }"

              /* NÚT GẠT ĐỔI MÀU GÓC TRÁI */
              "QPushButton#btnThemeToggle { background-color: #313244 !important; color: #F9E2AF !important; border: 1px solid #45475A !important; border-radius: 20px !important; font-weight: bold !important; font-size: 12pt !important; }"
              "QPushButton#btnThemeToggle:hover { background-color: #45475A !important; }"

              "QMessageBox { background-color: #1E1E2E !important; }"
              "QMessageBox QLabel { background-color: transparent !important; color: #CDD6F4 !important; font-weight: bold !important; }"
              "QTextBrowser { background-color: #181825 !important; color: #CDD6F4 !important; border: 1px solid #45475A !important; border-radius: 8px !important; padding: 15px !important; }";
    } else {
        css = "* { font-family: 'Times New Roman'; font-size: 13pt; }"
              "QMainWindow, QDialog, QWidget#centralwidget { background-color: #F5F7FA !important; color: #333333 !important; }"

              /* ÉP CHỮ MÀU ĐEN TUYỆT ĐỐI */
              "QLabel, QRadioButton, QCheckBox, QGroupBox { background-color: transparent !important; color: #333333 !important; border: none !important; }"

              /* ÉP MÀU BẢNG BIỂU VÀ THANH DỌC (FIX LỖI VỆT ĐEN) */
              "QTableWidget, QTableView { background-color: #FFFFFF !important; color: #333333 !important; gridline-color: #CCCCCC !important; border: 1px solid #CCCCCC !important; }"
              "QTableView::viewport { background-color: #FFFFFF !important; }"
              "QHeaderView { background-color: #FFFFFF !important; border: none !important; }"
              "QTableWidget::item { background-color: transparent !important; color: #333333 !important; }"
              "QHeaderView::section { background-color: #E8E8E8 !important; color: #333333 !important; font-weight: bold !important; border: 1px solid #C0C0C0 !important; padding: 4px !important; }"
              "QTableCornerButton::section { background-color: #E8E8E8 !important; border: 1px solid #C0C0C0 !important; }"

              /* ÉP MÀU Ô NHẬP LIỆU */
              "QLineEdit { background-color: transparent !important; color: #333333 !important; border: none !important; }"
              "QLineEdit:focus { background-color: #FFFFFF !important; border: 2px solid #0078D7 !important; }"
              "QLineEdit:disabled { background-color: #E8E8E8 !important; color: #999999 !important; }"
              "QComboBox, QSpinBox { background-color: #FFFFFF !important; color: #333333 !important; border: 1px solid #CCCCCC !important; border-radius: 4px !important; padding: 2px !important; }"
              "QComboBox:focus, QSpinBox:focus { background-color: #F5F7FA !important; border: 2px solid #0078D7 !important; }"
              "QComboBox QAbstractItemView { background-color: #FFFFFF !important; color: #333333 !important; selection-background-color: #E6F0FA !important; }"

              /* TRẢ LẠI KHUNG CHO Ô GIÁ TRỊ TỐI ƯU */
              "QLineEdit#lineEdit_Z { background-color: #FFFFFF !important; border: 1px solid #CCCCCC !important; border-radius: 4px !important; padding: 4px !important; color: #0078D7 !important; font-weight: bold !important; }"

              /* NÚT BẤM THƯỜNG */
              "QPushButton { background-color: #FFFFFF !important; color: #333333 !important; border: 1px solid #CCCCCC !important; border-radius: 4px !important; padding: 6px 15px !important; font-weight: bold !important; }"
              "QPushButton:hover { background-color: #E8E8E8 !important; }"

              /* NÚT GIẢI BÀI TOÁN (NỔI BẬT CHUNG) */
              "QPushButton#pushButton_3 { background-color: #0078D7 !important; color: #FFFFFF !important; border: none !important; }"
              "QPushButton#pushButton_3:hover { background-color: #005A9E !important; }"

              /* FIX LỖI: ÉP RIÊNG NÚT HỎI ĐÁP Ở WDSOLVE VỀ LẠI MÀU BÌNH THƯỜNG */
              "WdSolve QPushButton#pushButton_3 { background-color: #FFFFFF !important; color: #333333 !important; border: 1px solid #CCCCCC !important; }"
              "WdSolve QPushButton#pushButton_3:hover { background-color: #E8E8E8 !important; }"

              /* NÚT GẠT ĐỔI MÀU GÓC TRÁI */
              "QPushButton#btnThemeToggle { background-color: #FFFFFF !important; color: #333333 !important; border: 1px solid #CCCCCC !important; border-radius: 20px !important; font-weight: bold !important; font-size: 12pt !important; }"
              "QPushButton#btnThemeToggle:hover { background-color: #E8E8E8 !important; }"

              "QMessageBox { background-color: #F5F7FA !important; }"
              "QMessageBox QLabel { background-color: transparent !important; color: #222222 !important; font-weight: bold !important; }"
              "QTextBrowser { background-color: #FFFFFF !important; color: #212529 !important; border: 1px solid #DEE2E6 !important; border-radius: 8px !important; padding: 15px !important; }";
    }

    // Ép màu sắc lên toàn bộ ứng dụng (MainWindow, Dashboard, WdSolve, v.v.)
    qApp->setStyleSheet(css);
    for (QWidget *w : qApp->allWidgets()) {
        // Chỉ reset những cửa sổ top-level không phải MainWindow
        if (w->isWindow() && !w->styleSheet().isEmpty()) {
            w->setStyleSheet("");
        }
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Tẩy rửa tàn dư CSS để cửa sổ nghe lời Theme
    this->setStyleSheet("");
    ui->centralwidget->setStyleSheet("");

    // Ẩn hoàn toàn thanh MenuBar thừa thãi ở trên cùng
    menuBar()->hide();

    // =======================================================================
    // TẠO NÚT GẠT ĐỔI MÀU GẮN TRỰC TIẾP LÊN GÓC TRÁI TRÊN CÙNG
    // =======================================================================
    QPushButton *btnThemeToggle = new QPushButton(ui->centralwidget); // Gắn thẳng vào màn hình chính
    btnThemeToggle->setObjectName("btnThemeToggle"); // Nhận CSS bo tròn
    btnThemeToggle->setCursor(Qt::PointingHandCursor);

    // Đặt vị trí Tuyệt đối (Absolute Positioning): x = 20, y = 20, chiều rộng = 110, chiều cao = 40
    btnThemeToggle->setGeometry(20, 20, 110, 40);

    // [FIX LINUX] Đọc trạng thái khởi đầu từ AppData
    QString iniPath = getSettingsPath();
    QSettings settings(iniPath, QSettings::IniFormat);
    bool isDarkMode = settings.value("dark_mode", false).toBool();

    btnThemeToggle->setText(isDarkMode ? "☀️ Sáng" : "🌙 Tối");
    applyGlobalTheme(isDarkMode);

    // Sự kiện khi bấm nút gạt
    connect(btnThemeToggle, &QPushButton::clicked, this, [btnThemeToggle, iniPath]() {
        QSettings s(iniPath, QSettings::IniFormat);
        bool currentDark = s.value("dark_mode", false).toBool();
        bool newDark = !currentDark;

        s.setValue("dark_mode", newDark);
        btnThemeToggle->setText(newDark ? "☀️ Sáng" : "🌙 Tối");
        applyGlobalTheme(newDark); // Lập tức đổi màu toàn bộ cửa sổ
    });

    this->dashboard = nullptr;
    this->setWindowTitle("Phần mềm giải Quy Hoạch Tuyến Tính - Ver 1.0");
    this->setWindowState(Qt::WindowMaximized);
    applyPhanMemQHTTWindowIcon(this);
}

MainWindow::~MainWindow()
{
    delete this->dashboard;
    delete ui;
}

void MainWindow::on_pushButton_3_clicked()
{
    // 1. Tạo hiệu ứng mờ dần cho MainWindow
    QGraphicsOpacityEffect *fadeEffect = new QGraphicsOpacityEffect(this);
    this->centralWidget()->setGraphicsEffect(fadeEffect);

    QPropertyAnimation *animOut = new QPropertyAnimation(fadeEffect, "opacity");
    animOut->setDuration(150);
    animOut->setStartValue(1.0);
    animOut->setEndValue(0.0);

    connect(animOut, &QPropertyAnimation::finished, this, [=]() {
        this->hide();

        // 2. CHỈ TẠO MỚI NẾU CHƯA TỒN TẠI (Giúp giữ nguyên giao diện cũ)
        if (!this->dashboard) {
            // Không truyền this làm parent.
            // Nếu Dashboard là owned window của MainWindow đang bị hide,
            // Windows taskbar có thể không lấy icon đúng.
            this->dashboard = new Dashboard(nullptr);
            setPhanMemQHTTReturnWindow(this->dashboard, this);
            applyPhanMemQHTTWindowIcon(this->dashboard);

            connect(this->dashboard, &Dashboard::destroyed, this, [this](){
                this->dashboard = nullptr;
            });
        }

        setPhanMemQHTTReturnWindow(this->dashboard, this);
        applyPhanMemQHTTWindowIcon(this->dashboard);
        this->dashboard->show();

        // 3. Làm sáng dần Dashboard (Fade-in)
        QGraphicsOpacityEffect *fadeInEffect = new QGraphicsOpacityEffect(this->dashboard);
        this->dashboard->centralWidget()->setGraphicsEffect(fadeInEffect);

        QPropertyAnimation *animIn = new QPropertyAnimation(fadeInEffect, "opacity");
        animIn->setDuration(150);
        animIn->setStartValue(0.0);
        animIn->setEndValue(1.0);
        animIn->start(QAbstractAnimation::DeleteWhenStopped);
    });

    animOut->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::on_btnGioiThieu_clicked()
{
    QDialog *policyDialog = new QDialog(this);
    applyPhanMemQHTTWindowIcon(policyDialog);
    policyDialog->setWindowTitle("Giới thiệu");
    policyDialog->resize(1150, 1050);

    QVBoxLayout *layout = new QVBoxLayout(policyDialog);
    layout->setContentsMargins(20, 20, 20, 20);

    QTextBrowser *textBrowser = new QTextBrowser(policyDialog);
    textBrowser->setOpenExternalLinks(true);

    QString htmlContent = R"(
        <div style="font-family: 'Times New Roman', serif; font-size: 18pt; line-height: 1.6;">
            <h2 style="color: #0056B3; text-align: center; margin-bottom: 5px; font-size: 24pt;">GIỚI THIỆU</h2>
            <hr style="background-color: #CCCCCC; height: 1px; border: none; margin-bottom: 20px;">

            <h3 style="color: #D9534F; margin-bottom: 5px; font-size: 22pt;">1. GIỚI THIỆU CHUNG</h3>

            <p style="color: #0056B3; margin-top: 15px; margin-bottom: 5px; font-size: 15pt;">1.1 Mục đích và tính cấp thiết của đề tài</p>
            <p style="margin-top: 0px; text-align: justify;">Trong bối cảnh chuyển đổi số mạnh mẽ, các mô hình toán học tối ưu hóa ngày càng khẳng định được vai trò quan trọng. Quy hoạch tuyến tính (QHTT) là một phân nhánh cốt lõi của Tối ưu hóa. Tuy nhiên, việc giải thủ công bằng thuật toán Đơn hình đối mặt với nhiều rào cản về độ phức tạp tính toán và thời gian.</p>
            <p style="text-align: justify;">Do đó, phần mềm này được phát triển nhằm mục đích cung cấp một công cụ hỗ trợ tính toán chính xác, tự động hóa quy trình giải bài toán, từ đó tối ưu hóa hiệu suất nghiên cứu và học tập.</p>

            <p style="color: #0056B3; margin-top: 20px; margin-bottom: 5px; font-size: 15pt;">1.2 Đối tượng mục tiêu và phạm vi ứng dụng</p>
            <ul style="margin-top: 0px;">
                <li>Sinh viên:</b> Công cụ hỗ trợ tự học, kiểm tra lời giải, thực hành kỹ năng lập mô hình.</li>
                <li>Giảng viên:</b> Phương tiện trực quan để minh họa các bước thực hiện thuật toán.</li>
                <li>Nhà nghiên cứu:</b> Hỗ trợ tính toán sơ bộ và kiểm chứng các mô hình tối ưu hóa.</li>
            </ul>

            <p style="color: #0056B3; margin-top: 20px; margin-bottom: 5px; font-size: 15pt;">1.3 Tính chất nổi bật</p>
            <ul style="margin-top: 0px;">
                <li>Giải toán đa năng:</b> Xử lý bài toán Min/Max với số lượng biến và ràng buộc không giới hạn.</li>
                <li>Trực quan hóa hình học:</b> Tự động vẽ miền nghiệm cho bài toán 2 biến.</li>
                <li>Phân tích thuật toán chi tiết:</b> Hiển thị các bảng đơn hình qua từng bước lặp.</li>
                <li>Trợ lý ảo thông minh:</b> Tích hợp Chatbot AI để giải thích thêm về các bước giải.</li>
            </ul>
            <br>
            <h3 style="color: #D9534F; margin-bottom: 5px; font-size: 22pt;">2. THÔNG TIN PHÁT TRIỂN & HỖ TRỢ</h3>

            <p style="color: #0056B3; margin-top: 15px; margin-bottom: 5px; font-size: 15pt;">2.1 Thông tin tác giả</p>
            <p style="margin-top: 0px;">Dự án được nghiên cứu và phát triển bởi nhóm sinh viên Trường Đại học Khoa học Tự nhiên, ĐHQG-HCM (HCMUS)</b>. Chúng tôi luôn hoan nghênh các ý kiến đóng góp để hoàn thiện thuật toán và giao diện.</p>

            <p style="color: #0056B3; margin-top: 20px; margin-bottom: 5px; font-size: 15pt;">2.2 Nền tảng & Công nghệ sử dụng (Credits)</p>
            <ul style="margin-top: 0px;">
                <li>Nền tảng hỗ trợ:</b> Tương thích đa nền tảng (Cross-platform) bao gồm Windows, macOS và Linux.</li>
                <li>Ngôn ngữ & Framework:</b> Lõi thuật toán được viết hoàn toàn bằng C++ tiêu chuẩn. Giao diện đồ họa (GUI) được xây dựng dựa trên nền tảng mã nguồn mở Qt Framework (Qt6)</b>.</li>
                <li>Tích hợp AI:</b> Chatbot hướng dẫn sử dụng được trợ lực bởi Mô hình ngôn ngữ lớn (LLM).</li>
            </ul>

            <p style="color: #0056B3; margin-top: 20px; margin-bottom: 5px; font-size: 15pt;">2.3 Phản hồi & Báo lỗi (Feedback / Issues)</p>
            <p style="margin-top: 0px; margin-bottom: 20px;">Trong quá trình sử dụng, nếu phát hiện lỗi tính toán (Bug), xin vui lòng gửi báo cáo lỗi (kèm theo bài toán mẫu) thông qua kho lưu trữ dự án trên GitHub</b> hoặc liên hệ trực tiếp qua email của nhóm phát triển.</p>
        </div>
    )";

    // [FIX LINUX] Chuyển đổi mã màu HTML cho dễ nhìn trong Dark Mode từ đường dẫn chuẩn
    QSettings settings(getSettingsPath(), QSettings::IniFormat);
    if (settings.value("dark_mode", false).toBool()) {
        htmlContent.replace("#0056B3", "#89B4FA");
        htmlContent.replace("#D9534F", "#F38BA8");
        htmlContent.replace("#CCCCCC", "#45475A");
    }

    textBrowser->setHtml(htmlContent);
    layout->addWidget(textBrowser);

    QPushButton *btnClose = new QPushButton("Đóng", policyDialog);
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, policyDialog, &QDialog::accept);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);
    btnLayout->addStretch();

    layout->addLayout(btnLayout);

    policyDialog->exec();
    delete policyDialog;
}

void MainWindow::on_btnChinhSach_clicked()
{
    QDialog *policyDialog = new QDialog(this);
    applyPhanMemQHTTWindowIcon(policyDialog);
    policyDialog->setWindowTitle("Chính sách sử dụng");
    policyDialog->resize(1150, 1050);

    QVBoxLayout *layout = new QVBoxLayout(policyDialog);
    layout->setContentsMargins(20, 20, 20, 20);

    QTextBrowser *textBrowser = new QTextBrowser(policyDialog);
    textBrowser->setOpenExternalLinks(true);

    QString htmlContent = R"(
        <div style="font-family: 'Times New Roman', serif; font-size: 18pt; line-height: 1.6;">
            <br>
            <h3 style="color: #D9534F; margin-bottom: 5px; text-align: center; font-size: 24pt;">CHÍNH SÁCH VÀ ĐIỀU KHOẢN SỬ DỤNG</h3>

            <p style="color: #0056B3; margin-top: 25px; margin-bottom: 10px; font-size: 15pt;">- Quy định về bản quyền và Mục đích sử dụng</p>
            <ul style="margin-top: 0px;">
                <li>Tính chất dự án:</b> Đây là một sản phẩm trí tuệ phục vụ mục tiêu giáo dục và cộng đồng phi lợi nhuận. Mọi hành vi thương mại hóa phần mềm mà không có sự đồng ý của tác giả đều vi phạm điều khoản.</li>
                <li>Quyền tác giả:</b> Khuyến khích chia sẻ và học hỏi từ mã nguồn. Bất kỳ bài báo, nghiên cứu nào sử dụng kết quả từ phần mềm đều phải trích dẫn nguồn đầy đủ.</li>
            </ul>

            <p style="color: #0056B3; margin-top: 25px; margin-bottom: 10px; font-size: 15pt;">- Cam kết bảo mật và Giới hạn trách nhiệm</p>
            <ul style="margin-top: 0px; margin-bottom: 20px;">
                <li style="margin-bottom: 5px;">Độ tin cậy của kết quả:</b> Kết quả trả về chỉ mang tính chất tham khảo học thuật. Nhóm phát triển không chịu trách nhiệm pháp lý cho các quyết định thực tế phát sinh từ sai số tính toán.</li>
                <li>Bảo mật thông tin:</b>Cam kết tuyệt đối về tính riêng tư. Mọi dữ liệu bài toán đều được xử lý offline cục bộ (local memory)</b>. Phần mềm KHÔNG thu thập thông tin cá nhân hay đẩy dữ liệu lên máy chủ.</li>
            </ul>
        </div>
    )";

    // [FIX LINUX] Chuyển đổi mã màu HTML cho dễ nhìn trong Dark Mode từ đường dẫn chuẩn
    QSettings settings(getSettingsPath(), QSettings::IniFormat);
    if (settings.value("dark_mode", false).toBool()) {
        htmlContent.replace("#0056B3", "#89B4FA");
        htmlContent.replace("#D9534F", "#F38BA8");
    }

    textBrowser->setHtml(htmlContent);
    layout->addWidget(textBrowser);

    QPushButton *btnClose = new QPushButton("Đóng", policyDialog);
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, policyDialog, &QDialog::accept);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);
    btnLayout->addStretch();

    layout->addLayout(btnLayout);

    policyDialog->exec();
    delete policyDialog;
}
