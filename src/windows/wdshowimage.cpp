#include "wdshowimage.h"
#include "ui_wdshowimage.h"
#include "qcustomplot.h"
#include <QSettings>
#include <QApplication>
#include <QStandardPaths> // Thư viện để đọc đường dẫn AppData
#include <QDir>
#include <QIcon>
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


// =======================================================================
// HÀM TIỆN ÍCH
// =======================================================================

// Hàm lấy đường dẫn an toàn cho file Settings (Theme)
// Đảm bảo ứng dụng có quyền ghi file cài đặt giao diện (sáng/tối) trên mọi hệ điều hành
static QString getThemeSettingsPath() {
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir;
    if (!dir.exists(dataDir)) {
        dir.mkpath(dataDir); // Tạo thư mục nếu chưa tồn tại
    }
    return dataDir + "/settings.ini";
}

// =======================================================================
// CONSTRUCTOR & DESTRUCTOR
// =======================================================================

WdShowImage::WdShowImage(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::WdShowImage)
{
    ui->setupUi(this);

    // Cửa sổ biểu diễn hình học là cửa sổ độc lập để taskbar Windows luôn nhận icon riêng.
    this->setWindowFlag(Qt::Window, true);
    applyPhanMemQHTTWindowIcon(this);

    this->setWindowTitle("Biểu diễn hình học miền nghiệm");
    this->setWindowState(Qt::WindowMaximized); // Mở cửa sổ ở chế độ toàn màn hình
    applyPhanMemQHTTWindowIcon(this);
}

WdShowImage::~WdShowImage()
{
    delete ui;
}

// Nút quay lại (Back): Hiển thị lại cửa sổ cha và đóng cửa sổ hiện tại
void WdShowImage::on_pushButton_clicked()
{
    QWidget *returnWindow = phanMemQHTTReturnWindow(this);
    if (returnWindow) {
        applyPhanMemQHTTWindowIcon(returnWindow);
        returnWindow->show();
        returnWindow->raise();
        returnWindow->activateWindow();
    }
    this->close();
}

// =======================================================================
// HÀM TÍNH TOÁN TỌA ĐỘ
// =======================================================================

// Trích xuất tọa độ (x1, x2) từ ma trận của một bước Đơn hình (SimplexStep)
QPointF WdShowImage::getCoordinateFromStep(const SimplexStep& step) {
    double x1 = 0, x2 = 0;
    int m = step.matrix.size() - 1;       // Số lượng ràng buộc (bỏ hàng Z)
    int n = step.matrix[0].size() - 1;    // Cột chứa hệ số tự do (Right Hand Side)

    int col_x1 = -1, col_x2 = -1;
    int internalIdx = 0;

    // Xác định vị trí cột thực tế của x1 và x2 trong ma trận
    // Xử lý trường hợp biến tự do (isFree) được tách thành 2 biến không âm (x' - x'')
    for (int i = 0; i < (int)originalLp.varBounds.size(); ++i) {
        if (i == 0) col_x1 = internalIdx;
        if (i == 1) col_x2 = internalIdx;
        if (originalLp.varBounds[i].isFree || originalLp.varBounds[i].sign == "free")
            internalIdx += 2; // Biến tự do chiếm 2 cột
        else
            internalIdx += 1; // Biến bình thường chiếm 1 cột
    }

    // Lấy giá trị của x1, x2 từ cột hệ số tự do nếu chúng là biến cơ sở
    for (int i = 0; i < m; ++i) {
        if (col_x1 >= 0 && step.currentBasicVars[i] == col_x1)
            x1 = step.matrix[i][n];
        if (col_x2 >= 0 && step.currentBasicVars[i] == col_x2)
            x2 = step.matrix[i][n];
    }

    // Nếu x2 là biến tự do, giá trị thực x2 = x2' - x2''
    if (col_x2 >= 0 && (originalLp.varBounds.size() > 1) &&
        (originalLp.varBounds[1].isFree || originalLp.varBounds[1].sign == "free")) {
        int col_x2pp = col_x2 + 1; // Cột của x2''
        for (int i = 0; i < m; ++i) {
            if (step.currentBasicVars[i] == col_x2pp) x2 -= step.matrix[i][n];
        }
    }

    // Nếu x1 là biến tự do, giá trị thực x1 = x1' - x1''
    if (col_x1 >= 0 && (originalLp.varBounds.size() > 0) &&
        (originalLp.varBounds[0].isFree || originalLp.varBounds[0].sign == "free")) {
        int col_x1pp = col_x1 + 1; // Cột của x1''
        for (int i = 0; i < m; ++i) {
            if (step.currentBasicVars[i] == col_x1pp) x1 -= step.matrix[i][n];
        }
    }

    // Đảo dấu nếu biến bị giới hạn <= 0 (do lúc chuẩn hóa đã đặt x = -x')
    if (originalLp.varBounds.size() > 0 && originalLp.varBounds[0].sign == "<=") x1 = -x1;
    if (originalLp.varBounds.size() > 1 && originalLp.varBounds[1].sign == "<=") x2 = -x2;

    return QPointF(x1, x2);
}

// -----------------------------------------------------------------------
// HÀM VẼ TỔNG THỂ KHI MỚI MỞ CỬA SỔ
// -----------------------------------------------------------------------
void WdShowImage::drawGraph(const LinearProgram& lp,
                            const LinearProgram& origLp,
                            const std::vector<double>& solution,
                            const std::vector<SimplexStep>& history)
{
    // Lưu trữ dữ liệu bài toán vào class
    currentLp  = lp;
    originalLp = origLp;
    stepHistory = history;
    currentStepIndex = 0; // Luôn bắt đầu từ Bảng 0 (Trạng thái ban đầu)

    // Reset lại biểu đồ trước khi vẽ mới
    ui->plot->clearGraphs();
    ui->plot->clearPlottables();
    ui->plot->clearItems();
    ui->plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom); // Cho phép kéo và cuộn chuột để zoom

    // ĐỌC SETTINGS TỪ APPDATA ĐỂ CẤU HÌNH GIAO DIỆN SÁNG/TỐI
    QSettings settings(getThemeSettingsPath(), QSettings::IniFormat);
    bool isDark = settings.value("dark_mode", false).toBool();

    if (isDark) {
        // Tô màu nền Tối
        ui->plot->setBackground(QBrush(QColor("#181825")));
        ui->plot->axisRect()->setBackground(QBrush(QColor("#11111B")));

        // Màu viền và chữ trục tọa độ Tối
        QPen axisPen(QColor("#A6ADC8"));
        ui->plot->xAxis->setBasePen(axisPen);
        ui->plot->yAxis->setBasePen(axisPen);
        ui->plot->xAxis->setTickPen(axisPen);
        ui->plot->yAxis->setTickPen(axisPen);
        ui->plot->xAxis->setSubTickPen(axisPen);
        ui->plot->yAxis->setSubTickPen(axisPen);
        ui->plot->xAxis->setTickLabelColor(QColor("#CDD6F4"));
        ui->plot->yAxis->setTickLabelColor(QColor("#CDD6F4"));
        ui->plot->xAxis->setLabelColor(QColor("#89B4FA"));
        ui->plot->yAxis->setLabelColor(QColor("#89B4FA"));

        // Lưới tọa độ ở chế độ Tối (Đường mờ)
        QPen gridPen(QColor(69, 71, 90, 100), 1, Qt::DotLine);
        ui->plot->xAxis->grid()->setPen(gridPen);
        ui->plot->yAxis->grid()->setPen(gridPen);
        ui->plot->xAxis->grid()->setZeroLinePen(QPen(QColor("#A6ADC8")));
        ui->plot->yAxis->grid()->setZeroLinePen(QPen(QColor("#A6ADC8")));
    } else {
        // Tô màu nền Sáng (Mặc định)
        ui->plot->setBackground(QBrush(Qt::white));
        ui->plot->axisRect()->setBackground(QBrush(Qt::white));

        // Màu viền và chữ trục tọa độ Sáng
        QPen axisPen(Qt::black);
        ui->plot->xAxis->setBasePen(axisPen);
        ui->plot->yAxis->setBasePen(axisPen);
        ui->plot->xAxis->setTickPen(axisPen);
        ui->plot->yAxis->setTickPen(axisPen);
        ui->plot->xAxis->setSubTickPen(axisPen);
        ui->plot->yAxis->setSubTickPen(axisPen);
        ui->plot->xAxis->setTickLabelColor(Qt::black);
        ui->plot->yAxis->setTickLabelColor(Qt::black);
        ui->plot->xAxis->setLabelColor(Qt::black);
        ui->plot->yAxis->setLabelColor(Qt::black);

        // Lưới tọa độ ở chế độ Sáng
        QPen gridPen(QColor(200, 200, 200, 100), 1, Qt::DotLine);
        ui->plot->xAxis->grid()->setPen(gridPen);
        ui->plot->yAxis->grid()->setPen(gridPen);
        ui->plot->xAxis->grid()->setZeroLinePen(QPen(Qt::black));
        ui->plot->yAxis->grid()->setZeroLinePen(QPen(Qt::black));
    }

    // -------------------------------------------------------------------
    // 1. VẼ CÁC ĐƯỜNG RÀNG BUỘC (Đường thẳng)
    // -------------------------------------------------------------------
    QList<QColor> colors;
    if (isDark) {
        colors = {QColor("#89B4FA"), QColor("#94E2D5"), QColor("#CBA6F7"), QColor("#F9E2AF"), QColor("#F38BA8")};
    } else {
        colors = {Qt::blue, Qt::darkCyan, Qt::darkMagenta, Qt::darkYellow, Qt::darkRed};
    }

    // Duyệt qua từng phương trình ràng buộc trong bài toán gốc
    for (size_t i = 0; i < origLp.A.size(); ++i) {
        if (origLp.A[i].size() < 2) continue; // Chỉ vẽ được nếu có đủ 2 biến x1, x2
        double a1 = origLp.A[i][0], a2 = origLp.A[i][1], b = origLp.b[i];

        QVector<double> xVec, yVec, tVec = {0, 1}; // tVec dùng để truyền vào QCPCurve

        // Tính toán 2 điểm để vẽ đường thẳng
        if (std::abs(a2) > 1e-9) {
            // Nếu hệ số a2 != 0, tính y theo x
            xVec = {-20, 50};
            yVec = {(b - a1 * xVec[0]) / a2, (b - a1 * xVec[1]) / a2};
        } else {
            // Nếu hệ số a2 == 0 (Đường thẳng đứng), tính x theo b
            double xVal = b / a1;
            xVec = {xVal, xVal};
            yVec = {-20, 50};
        }

        QCPCurve *constraint = new QCPCurve(ui->plot->xAxis, ui->plot->yAxis);
        constraint->setData(tVec, xVec, yVec);
        constraint->setPen(QPen(colors[i % colors.size()], 2, Qt::SolidLine));

        // Format Text để hiển thị phương trình (vd: 2x1 + 3x2 = 6)
        QString eqText;
        if (std::abs(a1) > 1e-9) {
            if (std::abs(a1 - 1.0) < 1e-9)       eqText += "x1";
            else if (std::abs(a1 + 1.0) < 1e-9)  eqText += "-x1";
            else eqText += QString::number(a1, 'g', 3) + "x1";
        }
        if (std::abs(a2) > 1e-9) {
            if (!eqText.isEmpty()) {
                eqText += (a2 > 0) ? " + " : " - ";
                if (std::abs(std::abs(a2) - 1.0) > 1e-9)
                    eqText += QString::number(std::abs(a2), 'g', 3);
                eqText += "x2";
            } else {
                if (std::abs(a2 - 1.0) < 1e-9)       eqText += "x2";
                else if (std::abs(a2 + 1.0) < 1e-9)  eqText += "-x2";
                else eqText += QString::number(a2, 'g', 3) + "x2";
            }
        }
        eqText += " = " + QString::number(b, 'g', 3);

        // Hiển thị Label nhãn của phương trình lên đồ thị
        QCPItemText *label = new QCPItemText(ui->plot);
        if (std::abs(a2) > 1e-9) {
            label->position->setCoords(5, (b - a1 * 5) / a2 + 1.5);
        } else {
            label->position->setCoords(b / a1 + 1.5, 5);
        }
        label->setText(eqText);
        label->setColor(colors[i % colors.size()]);
        label->setFont(QFont("Arial", 10, QFont::Bold));
    }

    // -------------------------------------------------------------------
    // 2. TÔ MÀU MIỀN NGHIỆM TỐI ƯU
    // -------------------------------------------------------------------
    std::vector<QPointF> vertices;
    struct Ineq { double a, b, c; }; // Đại diện cho bất phương trình a*x1 + b*x2 <= c
    std::vector<Ineq> ineqs;

    // Chuyển đổi mọi ràng buộc về dạng <=
    for (size_t i = 0; i < origLp.A.size(); ++i) {
        if (origLp.A[i].size() < 2) continue;

        double a = origLp.A[i][0], b = origLp.A[i][1], c = origLp.b[i];
        QString sign = (i < origLp.signs.size()) ? origLp.signs[i].trimmed() : "";

        if (sign == "<=") {
            ineqs.push_back({a,  b,  c});
        }
        else if (sign == ">=") {
            ineqs.push_back({-a, -b, -c});
        }
        else if (sign == "=" || sign == "==") {
            // [FIX EQUALITY GEOMETRY]
            // Ràng buộc "=" phải được giữ đúng là đường thẳng bắt buộc.
            // Khi tính miền nghiệm, nó tương đương đồng thời:
            //      a*x1 + b*x2 <= c
            // và  a*x1 + b*x2 >= c  <=>  -a*x1 - b*x2 <= -c
            //
            // Bản cũ chỉ nhận "==" nên khi UI truyền dấu "=",
            // ràng buộc bằng bị bỏ qua và đồ thị tô sai thành một vùng.
            ineqs.push_back({ a,  b,  c});
            ineqs.push_back({-a, -b, -c});
        }
    }

    // Bổ sung các ràng buộc về dấu của biến (x1 >= 0, x2 >= 0, v.v...)
    if (origLp.varBounds.size() >= 1 && !origLp.varBounds[0].isFree) {
        double val = origLp.varBounds[0].value;
        if (origLp.varBounds[0].sign == ">=")        ineqs.push_back({-1, 0, -val}); // -x1 <= -val
        else if (origLp.varBounds[0].sign == "<=")  ineqs.push_back({ 1, 0,  val});  // x1 <= val
    }
    if (origLp.varBounds.size() >= 2 && !origLp.varBounds[1].isFree) {
        double val = origLp.varBounds[1].value;
        if (origLp.varBounds[1].sign == ">=")        ineqs.push_back({0, -1, -val});
        else if (origLp.varBounds[1].sign == "<=")  ineqs.push_back({0,  1,  val});
    }

    // Đặt biên giả để tạo vùng kín đối với trường hợp miền nghiệm không giới nội
    double BND = 1000.0;
    ineqs.push_back({ 1, 0, BND}); // x1 <= 1000
    ineqs.push_back({ 0, 1, BND}); // x2 <= 1000
    ineqs.push_back({-1, 0, BND}); // -x1 <= 1000
    ineqs.push_back({ 0,-1, BND}); // -x2 <= 1000

    // Tìm tất cả các giao điểm của các cặp đường thẳng
    for (size_t i = 0; i < ineqs.size(); ++i) {
        for (size_t j = i + 1; j < ineqs.size(); ++j) {
            double det = ineqs[i].a * ineqs[j].b - ineqs[i].b * ineqs[j].a;
            if (std::abs(det) < 1e-9) continue; // Song song

            // Hệ Cramer giải hệ phương trình bậc 1
            double x1 = (ineqs[i].c * ineqs[j].b - ineqs[j].c * ineqs[i].b) / det;
            double x2 = (ineqs[i].a * ineqs[j].c - ineqs[j].a * ineqs[i].c) / det;

            // Kiểm tra xem giao điểm có thỏa mãn TOÀN BỘ các bất phương trình không
            bool isValid = true;
            for (size_t k = 0; k < ineqs.size(); ++k) {
                if (ineqs[k].a * x1 + ineqs[k].b * x2 > ineqs[k].c + 1e-6) {
                    isValid = false; break; // Vi phạm miền nghiệm
                }
            }
            if (!isValid) continue;

            // Lọc các điểm trùng lặp
            bool exists = false;
            for (auto& p : vertices)
                if (std::abs(p.x() - x1) < 1e-5 && std::abs(p.y() - x2) < 1e-5)
                { exists = true; break; }
            if (!exists) vertices.push_back(QPointF(x1, x2)); // Lưu đỉnh của miền đa giác
        }
    }

    // Nối các đỉnh lại để tạo thành đa giác lồi (Convex Hull) bằng thuật toán Monotone Chain
    if (vertices.size() >= 3) {
        std::vector<QPointF> pts = vertices;
        // Sắp xếp các điểm theo x, sau đó theo y
        std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) {
            if (std::abs(a.x() - b.x()) > 1e-9) return a.x() < b.x();
            return a.y() < b.y();
        });

        // Hàm tính tích có hướng để kiểm tra chiều quay
        auto cross = [](QPointF O, QPointF A, QPointF B) {
            return (A.x()-O.x())*(B.y()-O.y()) - (A.y()-O.y())*(B.x()-O.x());
        };

        std::vector<QPointF> hull;

        // Tìm bao lồi dưới (Lower Hull)
        for (auto& pt : pts) {
            while (hull.size() >= 2 && cross(hull[hull.size()-2], hull[hull.size()-1], pt) <= 0)
                hull.pop_back();
            hull.push_back(pt);
        }

        // Tìm bao lồi trên (Upper Hull)
        size_t lower_size = hull.size() + 1;
        for (int i = (int)pts.size() - 2; i >= 0; --i) {
            while (hull.size() >= lower_size && cross(hull[hull.size()-2], hull[hull.size()-1], pts[i]) <= 0)
                hull.pop_back();
            hull.push_back(pts[i]);
        }
        if (!hull.empty()) hull.pop_back(); // Điểm đầu và cuối bị trùng do khép vòng

        // Đổ dữ liệu đa giác lồi vào QCPCurve để hiển thị vùng tô màu
        QCPCurve *region = new QCPCurve(ui->plot->xAxis, ui->plot->yAxis);
        QVector<double> pT, pX, pY;
        for (int i = 0; i < (int)hull.size(); ++i) {
            pT.push_back(i); pX.push_back(hull[i].x()); pY.push_back(hull[i].y());
        }
        // Đóng kín vòng đa giác
        pT.push_back(hull.size()); pX.push_back(hull[0].x()); pY.push_back(hull[0].y());

        region->setData(pT, pX, pY);
        region->setBrush(QBrush(isDark ? QColor(243, 139, 168, 60) : QColor(255, 0, 0, 80))); // Tô màu vùng
        region->setPen(Qt::NoPen);
    }
    else if (vertices.size() == 2) {
        // [FIX EQUALITY GEOMETRY]
        // Miền nghiệm có thể là một đoạn thẳng nếu có ràng buộc "=".
        // Trường hợp này không được tô thành vùng có diện tích.
        QCPCurve *segment = new QCPCurve(ui->plot->xAxis, ui->plot->yAxis);
        QVector<double> pT, pX, pY;
        pT << 0 << 1;
        pX << vertices[0].x() << vertices[1].x();
        pY << vertices[0].y() << vertices[1].y();

        segment->setData(pT, pX, pY);
        segment->setPen(QPen(isDark ? QColor("#F38BA8") : Qt::red, 4, Qt::SolidLine));
        segment->setBrush(Qt::NoBrush);
    }
    else if (vertices.size() == 1) {
        // [FIX EQUALITY GEOMETRY]
        // Miền nghiệm chỉ là một điểm, ví dụ:
        //   x1 + x2 = 2, x1 - x2 = 0, 2x1 = 2
        // thì miền nghiệm đúng là duy nhất (1, 1), không phải một vùng tô đỏ.
        QCPGraph *pointGraph = ui->plot->addGraph();
        pointGraph->setLineStyle(QCPGraph::lsNone);
        pointGraph->setScatterStyle(
            QCPScatterStyle(QCPScatterStyle::ssDisc,
                            isDark ? QColor("#F38BA8") : Qt::red,
                            isDark ? QColor("#F38BA8") : Qt::red,
                            10)
            );

        QVector<double> pX, pY;
        pX << vertices[0].x();
        pY << vertices[0].y();
        pointGraph->setData(pX, pY);

        QCPItemText *pointLabel = new QCPItemText(ui->plot);
        pointLabel->position->setCoords(vertices[0].x() + 0.25, vertices[0].y() + 0.25);
        pointLabel->setText(QString("Miền nghiệm: (%1, %2)")
                                .arg(vertices[0].x(), 0, 'f', 2)
                                .arg(vertices[0].y(), 0, 'f', 2));
        pointLabel->setColor(isDark ? QColor("#F38BA8") : Qt::red);
        pointLabel->setFont(QFont("Arial", 10, QFont::Bold));
    }

    // -------------------------------------------------------------------
    // 3. KHỞI TẠO CÁC ĐỐI TƯỢNG VẼ (Path, Z-line, Text Label)
    // -------------------------------------------------------------------

    // Đường đi của thuật toán đơn hình (các đỉnh đã đi qua)
    simplexPath = new QCPCurve(ui->plot->xAxis, ui->plot->yAxis);
    simplexPath->setPen(QPen(isDark ? QColor("#CDD6F4") : Qt::black, 2, Qt::DotLine));
    simplexPath->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, isDark ? QColor("#F38BA8") : Qt::red, 8)); // Dấu chấm tại mỗi đỉnh

    // Đường biểu diễn hàm mục tiêu Z
    zLineGraph = new QCPCurve(ui->plot->xAxis, ui->plot->yAxis);
    zLineGraph->setPen(QPen(isDark ? QColor("#F38BA8") : Qt::red, 2, Qt::DashLine));

    // Nhãn hiển thị trạng thái của bước lặp góc trên bên trái
    stepLabel = new QCPItemText(ui->plot);
    stepLabel->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
    stepLabel->position->setType(QCPItemPosition::ptAxisRectRatio);
    stepLabel->position->setCoords(0.05, 0.05); // Tọa độ tương đối trên màn hình
    stepLabel->setFont(QFont("Arial", 12, QFont::Bold));
    stepLabel->setColor(isDark ? QColor("#F38BA8") : Qt::darkRed);

    ui->plot->xAxis->setLabel("x1");
    ui->plot->yAxis->setLabel("x2");

    // Tính toán khung nhìn (Camera Viewport) sao cho ôm trọn miền nghiệm và đường đi
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool first = true;
    for (auto& v : vertices) {
        if (std::abs(v.x()) < BND - 1 && std::abs(v.y()) < BND - 1) { // Bỏ qua các đỉnh biên giả giới hạn
            if (first) { minX = maxX = v.x(); minY = maxY = v.y(); first = false; }
            else {
                minX = std::min(minX, v.x()); maxX = std::max(maxX, v.x());
                minY = std::min(minY, v.y()); maxY = std::max(maxY, v.y());
            }
        }
    }

    // Đưa cả các tọa độ từng bước lặp vào để tính Viewport
    for (const auto& step : stepHistory) {
        QPointF p = getCoordinateFromStep(step);
        if (first) { minX = maxX = p.x(); minY = maxY = p.y(); first = false; }
        else {
            minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
        }
    }

    // Nếu không có tọa độ nào hợp lệ, fallback về viewport mặc định [-10, 10]
    if (first) { minX = -10; maxX = 10; minY = -10; maxY = 10; }
    // Mở rộng lề thêm 3 đơn vị cho dễ nhìn
    ui->plot->xAxis->setRange(minX - 3, maxX + 3);
    ui->plot->yAxis->setRange(minY - 3, maxY + 3);

    // Bắt đầu vẽ bước đầu tiên (Index 0)
    renderStep(currentStepIndex);
}

// -----------------------------------------------------------------------
// HÀM VẼ LẠI ĐỒ THỊ TẠI MỘT BƯỚC CỤ THỂ (KHI NHẤN NEXT/PREV)
// -----------------------------------------------------------------------
void WdShowImage::renderStep(int stepIndex)
{
    // Cấm vẽ nếu index vượt quá giới hạn mảng lịch sử
    if (stepIndex < 0 || stepIndex >= (int)stepHistory.size()) return;

    // [FIX PATH LINUX] Đọc Theme từ AppData
    QSettings settings(getThemeSettingsPath(), QSettings::IniFormat);
    bool isDark = settings.value("dark_mode", false).toBool();

    // 1. Cập nhật đường đi Simplex từ Bước 0 đến Bước hiện tại
    QVector<double> pathT, pathX, pathY;
    for (int i = 0; i <= stepIndex; ++i) {
        QPointF p = getCoordinateFromStep(stepHistory[i]);
        pathT.push_back(i);
        pathX.push_back(p.x());
        pathY.push_back(p.y());
    }
    simplexPath->setData(pathT, pathX, pathY);

    // Lấy thông tin bước hiện tại
    const SimplexStep& currentStepInfo = stepHistory[stepIndex];
    QPointF currentPoint = getCoordinateFromStep(currentStepInfo);

    // 2. Cập nhật đường Z (hàm mục tiêu) tại vị trí hiện tại
    if (originalLp.c.size() >= 2) {
        double c1 = originalLp.c[0];
        double c2 = originalLp.c[1];
        // Tính giá trị Z hiện tại: Z = c1*x1 + c2*x2
        double currentZ = c1 * currentPoint.x() + c2 * currentPoint.y();

        QVector<double> zX, zY, zT = {0, 1};
        // Vẽ đường Z đi qua điểm hiện tại (c1*x + c2*y = currentZ)
        if (std::abs(c2) > 1e-9) {
            zX = {-20, 50};
            zY = {(currentZ - c1 * zX[0]) / c2,
                  (currentZ - c1 * zX[1]) / c2};
        } else if (std::abs(c1) > 1e-9) {
            double xVal = currentZ / c1;
            zX = {xVal, xVal};
            zY = {-20, 50};
        }
        zLineGraph->setData(zT, zX, zY);
    }

    // 3. CẬP NHẬT NHÃN TRẠNG THÁI [FIX LOGIC THOÁI HÓA]
    // Kiểm tra xem bài toán có Vô số nghiệm (Infinite Problem) hay không
    bool isInfiniteProblem = false;
    if (stepHistory.size() >= 2) {
        int m = (int)stepHistory.back().matrix.size() - 1;
        int n = (int)stepHistory.back().matrix[0].size() - 1;
        // Giá trị Z của bảng cuối và áp chót
        double lastZ = stepHistory.back().matrix[m][n];
        double prevZ = stepHistory[stepHistory.size() - 2].matrix[m][n];

        QPointF lastPt = getCoordinateFromStep(stepHistory.back());
        QPointF prevPt = getCoordinateFromStep(stepHistory[stepHistory.size() - 2]);

        // Khoảng cách Euclid giữa điểm cuối và điểm áp chót
        double dist = std::sqrt(std::pow(lastPt.x() - prevPt.x(), 2) + std::pow(lastPt.y() - prevPt.y(), 2));

        // Nếu Z không đổi, nhưng khoảng cách giữa 2 tọa độ lớn hơn 0,
        // VÀ bài toán không bị vô nghiệm/không giới nội => VÔ SỐ NGHIỆM (nhiều điểm cực biên tối ưu)
        if (std::abs(lastZ - prevZ) < 1e-9 &&
            dist > 1e-4 &&
            !stepHistory.back().isInfeasible &&
            !stepHistory.back().isUnbounded)
        {
            isInfiniteProblem = true;
        }
    }

    // Hiển thị nội dung Nhãn dựa trên index và trạng thái bài toán
    if (stepIndex == (int)stepHistory.size() - 1) { // Đang ở bước cuối cùng
        if (currentStepInfo.isInfeasible) {
            stepLabel->setText(QString("%1\nBài toán VÔ NGHIỆM!.").arg(currentStepInfo.stepName));
            stepLabel->setColor(isDark ? QColor("#F38BA8") : Qt::darkRed);
        }
        else if (currentStepInfo.isUnbounded) {
            stepLabel->setText(QString("%1\nBài toán KHÔNG GIỚI NỘI!.").arg(currentStepInfo.stepName));
            stepLabel->setColor(isDark ? QColor("#F38BA8") : Qt::darkRed);
        }
        else {
            // Đã tối ưu (Cực biên duy nhất hoặc cực biên thứ hai của vô số nghiệm)
            stepLabel->setText(
                QString("%1\n%2: (%3, %4)")
                    .arg(currentStepInfo.stepName)
                    .arg(isInfiniteProblem ? "Tọa độ tối ưu thứ hai" : "Tọa độ tối ưu")
                    .arg(currentPoint.x(), 0, 'f', 2)
                    .arg(currentPoint.y(), 0, 'f', 2)
                );
            stepLabel->setColor(isDark ? QColor("#89B4FA") : Qt::blue);
        }
    }
    else if (stepIndex == (int)stepHistory.size() - 2 && isInfiniteProblem) {
        // Đang ở bước áp chót, nhưng lại là Vô số nghiệm => Đây là Cực biên tối ưu thứ nhất
        stepLabel->setText(
            QString("%1\nTọa độ tối ưu thứ nhất: (%2, %3)")
                .arg(currentStepInfo.stepName)
                .arg(currentPoint.x(), 0, 'f', 2)
                .arg(currentPoint.y(), 0, 'f', 2)
            );
        stepLabel->setColor(isDark ? QColor("#89B4FA") : Qt::blue);
    }
    else {
        // Đang ở các bước trung gian bình thường
        stepLabel->setText(
            QString("%1\nTọa độ: (%2, %3)")
                .arg(currentStepInfo.stepName)
                .arg(currentPoint.x(), 0, 'f', 2)
                .arg(currentPoint.y(), 0, 'f', 2)
            );
        stepLabel->setColor(isDark ? QColor("#CBA6F7") : Qt::darkMagenta);
    }

    // 4. Update hiển thị bật/tắt các nút bấm (Prev/Next)
    ui->btnPrev->setEnabled(stepIndex > 0); // Vô hiệu hóa 'Prev' nếu ở bước 0
    ui->btnNext->setEnabled(stepIndex < (int)stepHistory.size() - 1); // Vô hiệu hóa 'Next' nếu ở bước cuối

    // Refresh lại đồ thị để các thay đổi hiển thị lên màn hình
    ui->plot->replot();
}

// Xử lý Sự kiện khi nhấn Nút "Previous" (Lùi lại 1 bước)
void WdShowImage::on_btnPrev_clicked()
{
    if (currentStepIndex > 0) {
        currentStepIndex--;
        renderStep(currentStepIndex);
    }
}

// Xử lý Sự kiện khi nhấn Nút "Next" (Tiến tới 1 bước)
void WdShowImage::on_btnNext_clicked()
{
    if (currentStepIndex < (int)stepHistory.size() - 1) {
        currentStepIndex++;
        renderStep(currentStepIndex);
    }
}
