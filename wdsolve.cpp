#include "wdsolve.h"
#include "ui_wdsolve.h"
#include "wdchatbot.h"
#include "simplexsolver.h"
#include <QString>
#include <QMessageBox>
#include <QTextEdit>
#include <QSettings>
#include <QDir>
#include <QPushButton>
#include <QBoxLayout>
#include <QLayoutItem>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QFileDialog>
#include <QPdfWriter>
#include <QTextDocument>
#include <QPageSize>
#include <QTextBrowser>
#include <QRegularExpression>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QTextStream>
#include <QProcessEnvironment>
#include <QPointer>
#include <QTimer>
#include <QProgressDialog>
#include <cmath>
#include <algorithm>


// =======================================================================
// [FIX THEME ĐỒNG BỘ] Đọc trạng thái Dark Mode từ AppData trước,
// sau đó mới fallback về applicationDirPath. Cách này giúp bản Linux/AppImage
// không bị lệch theme giữa MainWindow, màn hình kết quả và cửa sổ xem PDF.
// =======================================================================
static bool readDarkModeSetting()
{
    QString appDataSettingsPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.ini";
    if (QFile::exists(appDataSettingsPath)) {
        QSettings appDataSettings(appDataSettingsPath, QSettings::IniFormat);
        if (appDataSettings.contains("dark_mode")) {
            return appDataSettings.value("dark_mode", false).toBool();
        }
    }

    QSettings appDirSettings(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
    return appDirSettings.value("dark_mode", false).toBool();
}


// =======================================================================
// [FIX PDF/LATEX CROSS-PLATFORM]
// Các hàm dưới đây chỉ phục vụ việc tìm Tectonic đã đóng gói trong YAML
// và biên dịch PDF không làm treo UI trên Windows / macOS / Linux.
// =======================================================================
static QString findBundledLatexCompiler(bool* useTectonic)
{
    if (useTectonic) *useTectonic = false;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString appImageDir = qEnvironmentVariable("APPDIR");

#ifdef Q_OS_WIN
    const QString tectonicName = "tectonic.exe";
#else
    const QString tectonicName = "tectonic";
#endif

    QStringList candidates;

    // Windows theo YAML: build_result/tools/tectonic.exe
    candidates << QDir(appDir).filePath("tools/" + tectonicName);
    candidates << QDir(appDir).filePath(tectonicName);

    // Linux/AppImage theo YAML: AppDir/usr/bin/tools/tectonic
    if (!appImageDir.isEmpty()) {
        candidates << QDir(appImageDir).filePath("usr/bin/tools/" + tectonicName);
        candidates << QDir(appImageDir).filePath("usr/tools/" + tectonicName);
        candidates << QDir(appImageDir).filePath("tools/" + tectonicName);
    }
    candidates << QDir(appDir).filePath("../tools/" + tectonicName);
    candidates << QDir(appDir).filePath("../../tools/" + tectonicName);

#ifdef Q_OS_MAC
    // macOS theo YAML: .app/Contents/Resources/tools/tectonic
    candidates << QDir(appDir).filePath("../Resources/tools/" + tectonicName);
    candidates << QDir(appDir).filePath("../../Resources/tools/" + tectonicName);
#endif

    for (const QString& candidate : candidates) {
        QFileInfo fi(QDir::cleanPath(candidate));
        if (fi.exists() && fi.isFile()) {
#ifndef Q_OS_WIN
            QFile::setPermissions(fi.absoluteFilePath(),
                                  QFile::permissions(fi.absoluteFilePath()) |
                                      QFileDevice::ExeOwner |
                                      QFileDevice::ExeUser |
                                      QFileDevice::ExeGroup |
                                      QFileDevice::ExeOther);
#endif
            if (useTectonic) *useTectonic = true;
            return fi.absoluteFilePath();
        }
    }

    QString pathTectonic = QStandardPaths::findExecutable("tectonic");
    if (!pathTectonic.isEmpty()) {
        if (useTectonic) *useTectonic = true;
        return pathTectonic;
    }

    QString pathXeLaTeX = QStandardPaths::findExecutable("xelatex");
    if (!pathXeLaTeX.isEmpty()) {
        if (useTectonic) *useTectonic = false;
        return pathXeLaTeX;
    }

    return "";
}

// =======================================================================
// [FIX FONT PDF/LATEX]
// Tìm thư mục font đóng gói kèm app. YAML mới sẽ đặt Noto Serif ở:
// - Windows: tools/../fonts hoặc fonts cạnh file exe
// - Linux/AppImage: $APPDIR/usr/share/fonts/phanmemqhtt
// - macOS: .app/Contents/Resources/fonts
// Nếu không tìm thấy font đóng gói, LaTeX sẽ fallback sang font hệ thống.
// =======================================================================
static QString findBundledReportFontDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString appImageDir = qEnvironmentVariable("APPDIR");

    QStringList candidates;
    candidates << QDir(appDir).filePath("fonts");
    candidates << QDir(appDir).filePath("../fonts");
    candidates << QDir(appDir).filePath("../../fonts");

    if (!appImageDir.isEmpty()) {
        candidates << QDir(appImageDir).filePath("usr/share/fonts/phanmemqhtt");
        candidates << QDir(appImageDir).filePath("usr/fonts");
    }

#ifdef Q_OS_MAC
    candidates << QDir(appDir).filePath("../Resources/fonts");
    candidates << QDir(appDir).filePath("../../Resources/fonts");
#endif

    for (const QString& candidate : candidates) {
        QDir dir(QDir::cleanPath(candidate));
        if (dir.exists("NotoSerif-Regular.ttf") &&
            dir.exists("NotoSerif-Bold.ttf") &&
            dir.exists("NotoSerif-Italic.ttf") &&
            dir.exists("NotoSerif-BoldItalic.ttf")) {
            QString path = dir.absolutePath();
            path.replace("\\", "/");
            if (!path.endsWith('/')) path += "/";
            return path;
        }
    }

    return "";
}

static QString escapeLatexText(const QString& input)
{
    QString s = input;
    s.replace("\\", "\\textbackslash{}");
    s.replace("&", "\\&");
    s.replace("%", "\\%");
    s.replace("$", "\\$");
    s.replace("#", "\\#");
    s.replace("_", "\\_");
    s.replace("{", "\\{");
    s.replace("}", "\\}");
    s.replace("~", "\\textasciitilde{}");
    s.replace("^", "\\textasciicircum{}");
    return s;
}

static bool exportHtmlPdfFallback(const QString& fileName, const QString& html)
{
    if (fileName.isEmpty()) return false;

    if (QFileInfo::exists(fileName)) {
        QFile::remove(fileName);
    }

    QPdfWriter pdfWriter(fileName);
    pdfWriter.setPageSize(QPageSize(QPageSize::A4));
    pdfWriter.setResolution(300);
    pdfWriter.setPageMargins(QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter);

    QTextDocument reportDocument;
    reportDocument.setHtml(html);
    reportDocument.print(&pdfWriter);

    return QFileInfo::exists(fileName) && QFileInfo(fileName).size() > 0;
}

static QString compactLatexLog(QString log)
{
    // Tectonic lần đầu có thể in rất nhiều dòng note/warning.
    // Chỉ giữ lại lỗi thật để QMessageBox không dài và không gây cảm giác phần mềm bị lỗi nặng.
    QStringList lines = log.split('\n');
    QStringList important;
    int downloadCount = 0;
    int fontWarningCount = 0;

    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("note: downloading ")) {
            ++downloadCount;
            continue;
        }

        if (line.startsWith("warning: accessing absolute path", Qt::CaseInsensitive) ||
            line.contains("build may not be reproducible", Qt::CaseInsensitive)) {
            ++fontWarningCount;
            continue;
        }

        if (line.startsWith("note: ") &&
            (line.contains("generating format", Qt::CaseInsensitive) ||
             line.contains("writing `", Qt::CaseInsensitive))) {
            continue;
        }

        important << line;
    }

    if (downloadCount > 0) {
        important.prepend(QString("Tectonic đã tải %1 file hỗ trợ LaTeX trong lần chạy đầu tiên.").arg(downloadCount));
    }
    if (fontWarningCount > 0) {
        important.prepend(QString("Đã ẩn %1 cảnh báo font không nghiêm trọng của Tectonic.").arg(fontWarningCount));
    }

    QString result = important.join('\n');
    if (result.trimmed().isEmpty()) {
        result = "Không có lỗi LaTeX nghiêm trọng trong log. Nếu PDF LaTeX chưa được tạo, phần mềm đã dùng chế độ PDF tương thích Qt.";
    }
    if (result.length() > 3000) {
        result = result.right(3000);
    }
    return result;
}

static QString findLatexPdfOutput(const QString& dirPath)
{
    QDir dir(dirPath);
    QString expected = dir.filePath("BaoCao_QHTT.pdf");
    if (QFileInfo::exists(expected) && QFileInfo(expected).size() > 0) {
        return expected;
    }

    QFileInfoList pdfs = dir.entryInfoList(QStringList() << "*.pdf", QDir::Files, QDir::Time);
    for (const QFileInfo& pdf : pdfs) {
        if (pdf.exists() && pdf.size() > 0) {
            return pdf.absoluteFilePath();
        }
    }

    return "";
}

WdSolve::WdSolve(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::WdSolve)
{
    ui->setupUi(this);

    // =======================================================================
    // [FIX GIAO DIỆN]
    // =======================================================================
    this->setStyleSheet("");

    this->setWindowTitle("Kết quả tính toán");
    this->setWindowState(Qt::WindowMaximized);
    this->setWindowIcon(QIcon(":/logo.png"));

    this->wd_show    = nullptr;
    this->wd_ChatBot = nullptr;

    QPushButton* btnBack = this->findChild<QPushButton*>("pushButton");
    QPushButton* btnDraw = this->findChild<QPushButton*>("pushButton_2");
    QPushButton* btnChat = this->findChild<QPushButton*>("pushButton_3");
    QPushButton* btnExport = new QPushButton("📄 Xem lời giải ở dạng PDF", this);

    QString btnStyle = "QPushButton { font-weight: bold; padding: 8px; border-radius: 4px; }";

    QList<QPushButton*> bottomButtons = { btnDraw, btnChat, btnExport, btnBack };
    for (QPushButton* btn : bottomButtons) {
        if (!btn) continue;
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setMinimumWidth(0);
        btn->setMaximumWidth(QWIDGETSIZE_MAX);
        btn->setMinimumHeight(30);
        btn->setStyleSheet(btnStyle);
    }

    auto directContainsWidget = [](QLayout* layout, QWidget* widget) -> bool {
        if (!layout || !widget) return false;
        for (int i = 0; i < layout->count(); ++i) {
            QLayoutItem* item = layout->itemAt(i);
            if (item && item->widget() == widget) return true;
        }
        return false;
    };

    QBoxLayout* buttonLayout = nullptr;
    for (QLayout* layout : this->findChildren<QLayout*>()) {
        if (directContainsWidget(layout, btnDraw) &&
            directContainsWidget(layout, btnChat) &&
            directContainsWidget(layout, btnBack)) {
            buttonLayout = qobject_cast<QBoxLayout*>(layout);
            break;
        }
    }

    if (buttonLayout && btnDraw && btnChat && btnExport && btnBack) {
        while (buttonLayout->count() > 0) {
            QLayoutItem* item = buttonLayout->takeAt(0);
            delete item;
        }

        buttonLayout->setContentsMargins(0, 0, 0, 0);
        buttonLayout->setSpacing(8);

        buttonLayout->addWidget(btnDraw, 4);
        buttonLayout->addWidget(btnChat, 2);
        buttonLayout->addWidget(btnExport, 2);
        buttonLayout->addWidget(btnBack, 2);
    }

    // =======================================================================
    // HÀM FORMAT SỐ CHUNG: Luôn luôn lấy 2 chữ số thập phân (kể cả số nguyên ra .00)
    // =======================================================================
    auto formatVal = [](double val) -> QString {
        if (std::abs(val) < 1e-9) return "0.00";
        return QString::number(val, 'f', 2);
    };

    auto formatCoeff = [](double val) -> QString {
        return QString::number(std::abs(val), 'f', 2);
    };

    // =======================================================================
    // LOGIC CHO NÚT XUẤT BÁO CÁO (PDF / LATEX)
    // =======================================================================
    connect(btnExport, &QPushButton::clicked, this, [this, formatVal, formatCoeff]() {
        if (this->currentHistory.empty()) {
            QMessageBox::warning(this, "Trống", "Không có dữ liệu bài toán để xuất báo cáo.");
            return;
        }

        std::vector<QString> varNames;
        for (size_t i = 0; i < currentOriginalLp.varBounds.size(); ++i) {
            if (currentOriginalLp.varBounds[i].isFree || currentOriginalLp.varBounds[i].sign == "free") {
                varNames.push_back(QString("x_{%1}^+").arg(i + 1));
                varNames.push_back(QString("x_{%1}^-").arg(i + 1));
            } else {
                varNames.push_back(QString("x_{%1}").arg(i + 1));
            }
        }
        int origN_internal = varNames.size();
        int num_w = 0;
        bool globalIsPhase1 = false;

        for(const auto& step : currentHistory) {
            if (step.stepName.contains("Pha 1", Qt::CaseInsensitive) || step.stepName.contains("Pha 2", Qt::CaseInsensitive)) {
                globalIsPhase1 = true; break;
            }
        }

        int n_total_vars = currentHistory[0].matrix[0].size() - 1;
        num_w = n_total_vars - origN_internal;
        if (globalIsPhase1) num_w -= 1;
        if (num_w < 0) num_w = 0;

        for (int i = 0; i < num_w; ++i) varNames.push_back(QString("w_{%1}").arg(i + 1));
        if (globalIsPhase1) varNames.push_back("x_0");

        int lastPhase1StepIdx = -1;
        if (globalIsPhase1) {
            lastPhase1StepIdx = currentHistory.size() - 1;
            for (size_t k = 0; k < currentHistory.size(); ++k) {
                if (currentHistory[k].stepName.contains("Pha 2", Qt::CaseInsensitive)) {
                    lastPhase1StepIdx = k - 1;
                    break;
                }
            }
        }

        auto getVarNameHtml = [&](int idx) -> QString {
            if (idx < 0 || idx >= (int)varNames.size()) return "";
            QString v = varNames[idx];

            // [FIX PDF NO-WRAP] x_0 từng bị tách thành "x_" và "0" xuống dòng.
            // Chuyển riêng x_0 sang HTML subscript trước khi bọc nobr.
            if (v == "x_0" || v == "x0") {
                v = "x<sub>0</sub>";
            } else {
                v.replace("_{", "<sub>").replace("}", "</sub>").replace("^+", "<sup>+</sup>").replace("^-", "<sup>-</sup>");
            }

            return "<nobr>" + v + "</nobr>";
        };

        const SimplexStep& lastStep = currentHistory.back();
        int m_last = lastStep.matrix.size() - 1;
        int n_last = lastStep.matrix[0].size() - 1;
        double z_opt = currentOriginalLp.isMaximize ? lastStep.matrix[m_last][n_last] : -lastStep.matrix[m_last][n_last];

        std::vector<double> opt_x(currentOriginalLp.varBounds.size(), 0.0);
        int col_idx = 0;
        for (size_t i = 0; i < currentOriginalLp.varBounds.size(); ++i) {
            if (currentOriginalLp.varBounds[i].isFree || currentOriginalLp.varBounds[i].sign == "free") {
                int plus_idx = col_idx; int minus_idx = col_idx + 1; col_idx += 2;
                double val_plus = 0, val_minus = 0;
                for (int k = 0; k < m_last; ++k) {
                    if (lastStep.currentBasicVars[k] == plus_idx) val_plus = lastStep.matrix[k][n_last];
                    if (lastStep.currentBasicVars[k] == minus_idx) val_minus = lastStep.matrix[k][n_last];
                }
                opt_x[i] = val_plus - val_minus;
            } else {
                int var_idx = col_idx; col_idx += 1;
                double val = 0;
                for (int k = 0; k < m_last; ++k) {
                    if (lastStep.currentBasicVars[k] == var_idx) val = lastStep.matrix[k][n_last];
                }
                opt_x[i] = val;
            }
        }

        QString optSolHtml = "(", optSolTex = "(", varListHtml = "(", varListTex = "(";
        for (size_t i = 0; i < opt_x.size(); ++i) {
            optSolHtml += formatVal(opt_x[i]);
            optSolTex += formatVal(opt_x[i]);
            varListHtml += "x<sub>" + QString::number(i + 1) + "</sub>";
            varListTex += "x_{" + QString::number(i + 1) + "}";
            if (i < opt_x.size() - 1) {
                optSolHtml += ", "; optSolTex += ", ";
                varListHtml += ", "; varListTex += ", ";
            }
        }
        optSolHtml += ")"; optSolTex += ")";
        varListHtml += ")"; varListTex += ")";

        // [FIX BÁO CÁO VÔ SỐ NGHIỆM]
        // Tạo chuỗi điểm nghiệm để in tập lồi chứa các điểm tối ưu.
        auto makePointHtml = [&](const std::vector<double>& values) -> QString {
            if (values.empty()) return "";
            QString s = "(";
            size_t dim = currentOriginalLp.varBounds.size();
            for (size_t i = 0; i < dim; ++i) {
                double val = (i < values.size()) ? values[i] : 0.0;
                s += formatVal(val);
                if (i + 1 < dim) s += ", ";
            }
            s += ")";
            return s;
        };

        auto makePointTex = [&](const std::vector<double>& values) -> QString {
            if (values.empty()) return "";
            QString s = "\\left(";
            size_t dim = currentOriginalLp.varBounds.size();
            for (size_t i = 0; i < dim; ++i) {
                double val = (i < values.size()) ? values[i] : 0.0;
                s += formatVal(val);
                if (i + 1 < dim) s += ", ";
            }
            s += "\\right)";
            return s;
        };

        QString firstPointHtml = makePointHtml(this->currentSolution);
        QString secondPointHtml = makePointHtml(this->currentAltSolution);
        QString firstPointTex = makePointTex(this->currentSolution);
        QString secondPointTex = makePointTex(this->currentAltSolution);

        // ==========================================
        // 1. TẠO CHUỖI HTML ĐỂ RENDER PDF BÁO CÁO
        // ==========================================
        QString html = "<html><head><style>"
                       "body { font-family: 'Times New Roman', serif; font-size: 12pt; color: #000000; background: #ffffff; line-height: 1.35; margin: 0; padding: 0; text-align: justify; }"
                       "h2 { text-align: center; color: #000000; text-transform: uppercase; font-size: 16pt; font-weight: bold; margin: 0 0 18px 0; line-height: 1.25; }"
                       "h3 { text-align: left; color: #000000; font-size: 13.5pt; font-weight: bold; margin: 18px 0 8px 0; line-height: 1.25; page-break-after: avoid; }"
                       "p { font-size: 12pt; line-height: 1.35; margin: 6px 0; text-align: justify; }"
                       "table { page-break-inside: avoid; }"
                       "td { padding: 2px 1px; vertical-align: middle; }"
                       ".math-block { text-align: center; width: 100%; margin: 6px 0 12px 0; font-size: 12pt; }"
                       "</style></head><body>";

        html += "<h2>GIẢI BÀI TOÁN QUY HOẠCH TUYẾN TÍNH</h2>";
        html += "<h3>1. BÀI TOÁN GỐC</h3>";

        html += "<p style='text-align: left; margin-bottom: 5px;'><b>Hàm mục tiêu:</b></p>";
        html += "<div style='text-align: center; width: 100%; font-size: 15pt; margin-bottom: 15px;'>" + QString(currentOriginalLp.isMaximize ? "Max Z = " : "Min Z = ");
        bool isFirstObj = true;
        if (std::abs(currentOriginalLp.c_0) > 1e-9) { html += formatVal(currentOriginalLp.c_0); isFirstObj = false; }
        for (size_t j = 0; j < currentOriginalLp.c.size(); ++j) {
            double val = currentOriginalLp.c[j];
            if (std::abs(val) > 1e-9) {
                QString sign = (val > 0) ? (isFirstObj ? "" : "+ ") : "- ";
                bool isOne = (std::abs(std::abs(val) - 1.0) < 1e-9);
                QString coeffStr = isOne ? "" : formatCoeff(val);
                html += sign + coeffStr + "x<sub>" + QString::number(j + 1) + "</sub> ";
                isFirstObj = false;
            }
        }
        if (isFirstObj) html += "0.00";
        html += "</div>";

        html += "<p style='text-align: left; margin-bottom: 5px;'><b>Hệ ràng buộc:</b></p>";
        html += "<div style='text-align: center; width: 100%; margin-bottom: 15px;'>";
        html += "<table cellspacing='0' cellpadding='0' style='margin: 0 auto; border: none; font-size: 12pt;'>";

        for (size_t i = 0; i < currentOriginalLp.A.size(); ++i) {
            html += "<tr>";
            bool isFirstTerm = true;
            for (size_t j = 0; j < currentOriginalLp.A[i].size(); ++j) {
                double val = currentOriginalLp.A[i][j];
                if (std::abs(val) < 1e-9) {
                    html += "<td width='12'></td><td width='36'></td><td width='24'></td>";
                } else {
                    QString sign = (val > 0) ? (isFirstTerm ? "" : "+") : "-";
                    bool isOne = (std::abs(std::abs(val) - 1.0) < 1e-9);
                    QString coeffStr = isOne ? "" : formatCoeff(val);
                    html += "<td nowrap='nowrap' width='12' align='center' valign='middle' style='text-align: center; padding-left: 0; padding-right: 0; white-space: nowrap;'><div align='center' style='width:12px; text-align:center; margin-left:auto; margin-right:auto;'>" + sign + "</div></td>";
                    html += "<td nowrap='nowrap' width='36' align='right' style='white-space: nowrap; padding-left: 0; padding-right: 1px;'><nobr>" + coeffStr + "</nobr></td>";
                    html += "<td nowrap='nowrap' width='24' align='left' style='white-space: nowrap; padding-left: 1px; padding-right: 0;'><nobr>x<sub>" + QString::number(j + 1) + "</sub></nobr></td>";
                    isFirstTerm = false;
                }
            }
            if (isFirstTerm) html += "<td width='12'></td><td nowrap='nowrap' width='36' align='right' style='white-space: nowrap; padding-left: 0; padding-right: 1px;'><nobr>0.00</nobr></td><td width='24'></td>";

            QString signHtml = currentOriginalLp.signs[i];
            if (signHtml == "<=") signHtml = "&le;"; else if (signHtml == ">=") signHtml = "&ge;";
            html += "<td nowrap='nowrap' width='22' align='center' style='white-space: nowrap; text-align: center; padding-left: 0; padding-right: 0;'><nobr>" + signHtml + "</nobr></td>";
            html += "<td nowrap='nowrap' width='60' align='left' style='white-space: nowrap; padding-left: 2px; padding-right: 0;'><nobr>" + formatVal(currentOriginalLp.b[i]) + "</nobr></td></tr>";
        }

        for (size_t i = 0; i < currentOriginalLp.varBounds.size(); ++i) {
            html += "<tr>";
            for (size_t j = 0; j < currentOriginalLp.A[0].size(); ++j) {
                if (i == j) {
                    html += "<td width='12'></td><td width='36'></td><td nowrap='nowrap' width='24' align='left' style='white-space: nowrap; padding-left: 1px; padding-right: 0;'><nobr>x<sub>" + QString::number(j + 1) + "</sub></nobr></td>";
                } else {
                    html += "<td width='12'></td><td width='36'></td><td width='24'></td>";
                }
            }

            if (currentOriginalLp.varBounds[i].isFree || currentOriginalLp.varBounds[i].sign == "free") {
                html += "<td nowrap='nowrap' colspan='2' align='left' style='white-space: nowrap; padding-left: 2px;'><nobr>&isin; &real;</nobr></td></tr>";
            } else {
                QString s = currentOriginalLp.varBounds[i].sign;
                if (s == "<=") s = "&le;"; else if (s == ">=") s = "&ge;";
                html += "<td nowrap='nowrap' width='22' align='center' style='white-space: nowrap; text-align: center; padding-left: 0; padding-right: 0;'><nobr>" + s + "</nobr></td>";
                html += "<td nowrap='nowrap' width='60' align='left' style='white-space: nowrap; padding-left: 2px; padding-right: 0;'><nobr>" + formatVal(currentOriginalLp.varBounds[i].value) + "</nobr></td></tr>";
            }
        }
        html += "</table></div>";

        // --- CÁC BƯỚC GIẢI DẠNG TỪ VỰNG HTML TRONG PDF ---
        html += "<h3>2. CÁC BƯỚC GIẢI (DẠNG TỪ VỰNG)</h3>";
        for (size_t stepIdx = 0; stepIdx < currentHistory.size(); ++stepIdx) {
            const SimplexStep& step = currentHistory[stepIdx];
            int m = step.matrix.size() - 1;
            int n = step.matrix[0].size() - 1;
            bool isPhase1Loc = globalIsPhase1 && ((int)stepIdx <= lastPhase1StepIdx);

            html += "<p style='text-align: left; margin-top: 25px;'><b>" + step.stepName + "</b></p>";

            // ===================================================================
            // [FIX PDF] Thêm các dòng giải thích giống phần hiển thị dạng từ vựng
            // trong tab "Các bước thực thi". Chỉ áp dụng cho báo cáo PDF.
            // ===================================================================
            QString pdfIntroHtml = "";
            QString zTextForStep = ui->lineEdit_Z->text();
            bool isLastStepPdf = (stepIdx == currentHistory.size() - 1);

            QString pdfOptZHtml;
            if (currentOriginalLp.isMaximize) {
                pdfOptZHtml = "Đối với hàm mục tiêu, vì bài toán gốc là <b>Max Z</b> nên thuật toán đã giải thông qua việc tìm <b>Min(-Z)</b>. Do đó, giá trị lớn nhất của Z sẽ bằng đảo dấu của hằng số tự do trong phương trình -Z hiện tại.";
            } else {
                pdfOptZHtml = "Đối với hàm mục tiêu, giá trị nhỏ nhất của <b>Min Z</b> chính là hằng số tự do trong phương trình Z hiện tại.";
            }

            QString pdfUnboundedHtml;
            if (currentOriginalLp.isMaximize) {
                pdfUnboundedHtml = "<b>* Giải thích không giới nội:</b> tồn tại một biến không cơ sở làm cải thiện hàm mục tiêu, đồng thời các hệ số của biến đó trong các phương trình ràng buộc không làm phá vỡ tính khả thi.<br>"
                                   "<b>* Giải thích:</b> Hàm mục tiêu có thể tăng lên vô hạn mà không vi phạm các ràng buộc. Do đó, giá trị tối ưu của bài toán Max Z là <b>+&infin;</b>.";
            } else {
                pdfUnboundedHtml = "<b>* Giải thích không giới nội:</b> tồn tại một biến không cơ sở làm cải thiện hàm mục tiêu, đồng thời các hệ số của biến đó trong các phương trình ràng buộc không làm phá vỡ tính khả thi.<br>"
                                   "<b>* Giải thích:</b> Hàm mục tiêu có thể giảm xuống vô hạn mà không vi phạm các ràng buộc. Do đó, giá trị tối ưu của bài toán Min Z là <b>-&infin;</b>.";
            }

            if (step.pivotCol >= 0 && step.pivotRow >= 0) {
                QString enterVar = getVarNameHtml(step.pivotCol);
                QString leaveVar = "?";
                if (step.currentBasicVars[step.pivotRow] >= 0 &&
                    step.currentBasicVars[step.pivotRow] < (int)varNames.size()) {
                    leaveVar = getVarNameHtml(step.currentBasicVars[step.pivotRow]);
                }

                if (stepIdx == 0 && isPhase1Loc) {
                    pdfIntroHtml += "<p style='text-align: left; margin: 4px 0 10px 0; font-style: italic; font-size: 12pt;'>"
                                    "&rarr; <b>Phép xoay đặc biệt:</b> Đưa biến phụ <b><font color='green'>" + enterVar + "</font></b> "
                                                 "vào cơ sở để thay thế <b><font color='red'>" + leaveVar + "</font></b> nhằm làm vế phải dương."
                                                 "</p>";
                } else {
                    pdfIntroHtml += "<p style='text-align: left; margin: 4px 0 10px 0; font-style: italic; font-size: 12pt;'>"
                                    "&rarr; Chọn <b><font color='green'>" + enterVar + "</font></b> làm biến vào, đẩy "
                                                 "<b><font color='red'>" + leaveVar + "</font></b> ra khỏi cơ sở."
                                                 "</p>";

                    if (zTextForStep.contains("Vô số nghiệm", Qt::CaseInsensitive) && stepIdx + 2 == currentHistory.size()) {
                        pdfIntroHtml += "<p style='text-align: left; margin: 0 0 8px 0; color: #d9534f; font-size: 12pt;'>"
                                        "&rarr; <b>Kết luận:</b> Đã đạt từ vựng tối ưu và bài toán có vô số nghiệm."
                                        "</p>";
                        pdfIntroHtml += "<p style='text-align: left; margin: 0 0 12px 0; color: #0056b3; font-style: italic; font-size: 11pt;'>"
                                        "<b>* Giải thích vô số nghiệm:</b> vì tồn tại hệ số của biến không cơ sở ở hàm mục tiêu bằng 0 nên bài toán có vô số nghiệm tối ưu.<br>"
                                        "<b>* Giải thích cách đọc nghiệm:</b> cho tất cả các biến không cơ sở bằng 0, khi đó các biến cơ sở nhận giá trị bằng hằng số tự do của phương trình tương ứng.<br>"
                                        "<b>* Giải thích giá trị tối ưu:</b> " + pdfOptZHtml + "<br>"
                                                        "<b>* Lưu ý:</b> Bước xoay tiếp theo chỉ để tìm tọa độ tối ưu thứ 2."
                                                        "</p>";
                    }
                }
            } else {
                if (!isLastStepPdf) {
                    if (isPhase1Loc) {
                        pdfIntroHtml += "<p style='text-align: left; margin: 4px 0 8px 0; color: #d9534f; font-size: 12pt;'>"
                                        "&rarr; <b>Kết luận:</b> Đã đạt từ vựng tối ưu của Pha 1."
                                        "</p>";
                    } else {
                        pdfIntroHtml += "<p style='text-align: left; margin: 4px 0 10px 0; font-style: italic; font-size: 12pt;'>"
                                        "&rarr; Hệ phương trình đã sẵn sàng. Tiếp tục thuật toán Đơn hình."
                                        "</p>";
                    }
                } else {
                    if (zTextForStep.contains("Không giới nội", Qt::CaseInsensitive)) {
                        pdfIntroHtml += "<p style='text-align: left; margin: 4px 0 8px 0; color: #d9534f; font-size: 12pt;'>"
                                        "&rarr; <b>Kết luận:</b> Bài toán không giới nội. Dừng thuật toán."
                                        "</p>";
                        pdfIntroHtml += "<p style='text-align: left; margin: 0 0 12px 0; color: #0056b3; font-style: italic; font-size: 11pt;'>"
                                        + pdfUnboundedHtml +
                                        "</p>";
                    } else if (zTextForStep.contains("Vô nghiệm", Qt::CaseInsensitive)) {
                        pdfIntroHtml += "<p style='text-align: left; margin: 4px 0 8px 0; color: #d9534f; font-size: 12pt;'>"
                                        "&rarr; <b>Kết luận:</b> Bài toán vô nghiệm. Dừng thuật toán."
                                        "</p>";
                        pdfIntroHtml += "<p style='text-align: left; margin: 0 0 12px 0; color: #0056b3; font-style: italic; font-size: 11pt;'>"
                                        "<b>* Giải thích:</b> Hệ ràng buộc mâu thuẫn nhau nên không tồn tại phương án thỏa mãn tất cả các ràng buộc."
                                        "</p>";
                    } else if (zTextForStep.contains("Vô số nghiệm", Qt::CaseInsensitive)) {
                        pdfIntroHtml += "<p style='text-align: left; margin: 4px 0 8px 0; color: #d9534f; font-size: 12pt;'>"
                                        "&rarr; <b>Kết luận:</b> Đã tìm được tọa độ tối ưu thứ 2. Dừng thuật toán."
                                        "</p>";
                        pdfIntroHtml += "<p style='text-align: left; margin: 0 0 12px 0; color: #0056b3; font-style: italic; font-size: 11pt;'>"
                                        "<b>* Giải thích cách lấy nghiệm tối ưu:</b> cho tất cả các biến không cơ sở bằng 0, khi đó các biến cơ sở nhận giá trị bằng hằng số tự do của phương trình tương ứng.<br>"
                                        "<b>* Giải thích giá trị tối ưu:</b> " + pdfOptZHtml +
                                        "</p>";
                    } else {
                        pdfIntroHtml += "<p style='text-align: left; margin: 4px 0 8px 0; color: #d9534f; font-size: 12pt;'>"
                                        "&rarr; <b>Kết luận:</b> Đã đạt từ vựng tối ưu. Dừng thuật toán."
                                        "</p>";
                        pdfIntroHtml += "<p style='text-align: left; margin: 0 0 12px 0; color: #0056b3; font-style: italic; font-size: 11pt;'>"
                                        "<b>* Giải thích cách đọc nghiệm:</b> cho tất cả các biến không cơ sở bằng 0, khi đó các biến cơ sở nhận giá trị bằng hằng số tự do của phương trình tương ứng.<br>"
                                        "<b>* Giải thích giá trị tối ưu:</b> " + pdfOptZHtml +
                                        "</p>";
                    }
                }
            }

            html += pdfIntroHtml;
            html += "<div style='text-align: center; width: 100%;'>";
            html += "<table cellspacing='0' cellpadding='0' style='margin: 0 auto; border: none; font-size: 12pt;'>";

            std::vector<int> rowOrder;
            rowOrder.push_back(m);
            for (int i = 0; i < m; ++i) rowOrder.push_back(i);

            std::vector<int> nonBasicVars;
            for (int j = 0; j < n; ++j) {
                if (!isPhase1Loc && (varNames[j] == "x_0" || varNames[j] == "x0" || varNames[j] == "")) continue;
                bool isBasic = false;
                for (int k = 0; k < m; ++k) { if (step.currentBasicVars[k] == j) { isBasic = true; break; } }
                if (!isBasic) nonBasicVars.push_back(j);
            }

            bool isFirstConstraintRow = true;
            for (int i : rowOrder) {
                bool isZRow = (i == m);
                if (!isZRow && isFirstConstraintRow) {
                    html += "<tr><td colspan='100' style='border: none; padding: 0;'><hr style='border-top: 1px solid black; margin: 6px 0;'></td></tr>";
                    isFirstConstraintRow = false;
                }
                html += "<tr>";

                QString lhsVar = isZRow ? (isPhase1Loc ? "&xi;" : (currentOriginalLp.isMaximize ? "-Z" : "Z")) : getVarNameHtml(step.currentBasicVars[i]);
                // TÔ MÀU ĐỎ BIẾN RA Ở BẢNG PDF (Theo yêu cầu)
                if (!isZRow && i == step.pivotRow) {
                    lhsVar = QString("<font color='red'>%1</font>").arg(lhsVar);
                }

                html += "<td nowrap='nowrap' width='40' align='right' style='border: none; white-space: nowrap;'><b>" + lhsVar + "</b></td>";
                html += "<td nowrap='nowrap' width='18' align='center' style='border: none; white-space: nowrap;'> = </td>";

                double rhsVal = isZRow ? -step.matrix[m][n] : step.matrix[i][n];
                if (std::abs(rhsVal) < 1e-9) rhsVal = 0.0;

                bool hasVarTerms = false;
                for (int j : nonBasicVars) {
                    // [FIX PDF/TEX MAX -> MIN(-Z)]
                    // Dòng hàm mục tiêu phải lấy dấu giống bảng "Các bước thực thi".
                    // Với bài toán Max, thuật toán hiển thị dưới dạng Min(-Z), nên hệ số ở dòng -Z
                    // phải dùng trực tiếp step.matrix[m][j], không đảo dấu như các dòng ràng buộc.
                    double coeff = isZRow ? step.matrix[m][j] : -step.matrix[i][j];
                    if (isZRow && stepIdx == 0 && isPhase1Loc && (varNames[j] == "x_0" || varNames[j] == "x0")) coeff = 1.0;
                    if (std::abs(coeff) >= 1e-9) hasVarTerms = true;
                }

                bool hasConst = std::abs(rhsVal) >= 1e-9 || !hasVarTerms;
                QString constStr = hasConst ? formatVal(rhsVal) : "";

                html += "<td nowrap='nowrap' width='60' align='right' style='border: none; white-space: nowrap; padding-right: 2px;'><nobr>" + constStr + "</nobr></td>";

                bool isFirstRhsTerm = !hasConst;

                for (int j : nonBasicVars) {
                    // [FIX PDF/TEX MAX -> MIN(-Z)]
                    // Dòng hàm mục tiêu phải lấy dấu giống bảng "Các bước thực thi".
                    // Với bài toán Max, thuật toán hiển thị dưới dạng Min(-Z), nên hệ số ở dòng -Z
                    // phải dùng trực tiếp step.matrix[m][j], không đảo dấu như các dòng ràng buộc.
                    double coeff = isZRow ? step.matrix[m][j] : -step.matrix[i][j];
                    if (isZRow && stepIdx == 0 && isPhase1Loc && (varNames[j] == "x_0" || varNames[j] == "x0")) coeff = 1.0;

                    if (std::abs(coeff) < 1e-9) {
                        html += "<td width='14' style='border:none;'></td><td width='46' style='border:none;'></td><td width='28' style='border:none;'></td>";
                    } else {
                        QString sign = "";
                        if (coeff > 0) sign = isFirstRhsTerm ? "" : "+";
                        else sign = "-";
                        isFirstRhsTerm = false;

                        bool isOne = (std::abs(std::abs(coeff) - 1.0) < 1e-9);
                        QString coeffStr = isOne ? "" : formatCoeff(coeff);
                        QString varName = getVarNameHtml(j);
                        // TÔ MÀU XANH BIẾN VÀO Ở BẢNG PDF (Theo yêu cầu)
                        if (j == step.pivotCol) {
                            varName = QString("<b><font color='green'>%1</font></b>").arg(varName);
                        }

                        html += "<td nowrap='nowrap' width='14' align='center' valign='middle' style='border: none; white-space: nowrap; text-align: center; padding-left: 0; padding-right: 0;'><div align='center' style='width:14px; text-align:center; margin-left:auto; margin-right:auto;'><nobr>" + sign + "</nobr></div></td>";
                        html += "<td nowrap='nowrap' width='46' align='right' style='border: none; white-space: nowrap; padding-right: 2px;'><nobr>" + coeffStr + "</nobr></td>";
                        html += "<td nowrap='nowrap' width='28' align='left' style='border: none; white-space: nowrap; padding-left: 1px;'>" + varName + "</td>";
                    }
                }
                html += "</tr>";
            }
            html += "</table></div>";

            if (globalIsPhase1 && (int)stepIdx == lastPhase1StepIdx) {
                double xi_val = -step.matrix[m][n];
                if (std::abs(xi_val) < 1e-9) {
                    html += "<p style='text-align: left; font-style: italic; color: #0056b3; font-size: 12pt;'>* <b>Giải thích:</b> Để suy ra nghiệm, ta cho tất cả các biến không cơ sở (các biến nằm ở vế phải) bằng 0, khi đó các biến cơ sở (vế trái) sẽ nhận giá trị bằng đúng hằng số tự do của phương trình tương ứng. Vì giá trị tối ưu &xi; = 0, bài toán gốc có nghiệm khả thi. Thuật toán sẽ tiếp tục <b>chuyển sang Pha 2</b>.</p>";
                } else {
                    html += "<p style='text-align: left; font-style: italic; color: #d9534f; font-size: 12pt;'>* <b>Giải thích:</b> Để suy ra nghiệm, ta cho tất cả các biến không cơ sở (các biến nằm ở vế phải) bằng 0, khi đó các biến cơ sở (vế trái) sẽ nhận giá trị bằng đúng hằng số tự do của phương trình tương ứng. Vì giá trị tối ưu &xi; &ne; 0, hệ ràng buộc của bài toán gốc mâu thuẫn nhau nên <b>bài toán vô nghiệm</b>.</p>";
                }
            }
        }

        QString zText = ui->lineEdit_Z->text();
        QString reportStatus = "Có nghiệm tối ưu duy nhất";
        if (zText.contains("Vô nghiệm", Qt::CaseInsensitive)) {
            reportStatus = "Không có nghiệm tối ưu (Bài toán vô nghiệm)";
        } else if (zText.contains("Vô số nghiệm", Qt::CaseInsensitive)) {
            reportStatus = "Có vô số nghiệm tối ưu";
        } else if (zText.contains("Không giới nội", Qt::CaseInsensitive)) {
            reportStatus = "Không có nghiệm tối ưu (Bài toán không giới nội)";
        } else if (zText.contains("Lỗi", Qt::CaseInsensitive)) {
            reportStatus = "Không giải được";
        }

        html += "<h3>3. KẾT LUẬN</h3>";
        html += "<p style='text-align: left; margin-bottom: 5px;'><b>Trạng thái:</b> " + reportStatus + "</p>";

        // [FIX KHÔNG GIỚI NỘI] Báo cáo vẫn phải nêu giá trị tối ưu theo hướng vô hạn.
        // Max: +∞, Min: -∞. Không in nghiệm tối ưu vì bài toán không có nghiệm tối ưu hữu hạn.
        if (zText.contains("Không giới nội", Qt::CaseInsensitive)) {
            QString unboundedValueHtml = currentOriginalLp.isMaximize ? "+&infin;" : "-&infin;";
            html += "<p style='text-align: left; margin-bottom: 5px;'><b>Giá trị tối ưu:</b> Z<sup>*</sup> = " + unboundedValueHtml + "</p>";
        }

        if (!zText.contains("Vô nghiệm", Qt::CaseInsensitive) && !zText.contains("Không giới nội", Qt::CaseInsensitive) && !zText.contains("Lỗi", Qt::CaseInsensitive)) {
            html += "<p style='text-align: left; margin-bottom: 5px;'><b>Giá trị tối ưu:</b> Z<sup>*</sup> = " + formatVal(z_opt) + "</p>";

            if (zText.contains("Vô số nghiệm", Qt::CaseInsensitive) &&
                !firstPointHtml.isEmpty() && !secondPointHtml.isEmpty()) {
                html += "<p style='text-align: left; margin-bottom: 5px;'><b>Nghiệm tối ưu thứ nhất:</b> " + varListHtml + " = " + firstPointHtml + "</p>";
                html += "<p style='text-align: left; margin-bottom: 5px;'><b>Nghiệm tối ưu thứ hai:</b> " + varListHtml + " = " + secondPointHtml + "</p>";
                html += "<p style='text-align: left; margin-bottom: 5px;'><b>Tập nghiệm tối ưu:</b> "
                        "{ X(&lambda;) = &lambda;" + firstPointHtml +
                        " + (1 - &lambda;)" + secondPointHtml +
                        " | 0 &le; &lambda; &le; 1 }</p>";
                html += "<p style='text-align: left; margin-bottom: 5px;'><i>Các điểm thuộc tập trên đều là nghiệm tối ưu.</i></p>";
                html += "<p style='text-align: left; margin-bottom: 5px;'><i>Nếu bài toán có nhiều hơn hai biến, tập nghiệm tối ưu tổng quát có thể là một mặt lồi của miền nghiệm khả thi, không nhất thiết chỉ là đoạn thẳng nối hai điểm trên.</i></p>";
            } else {
                html += "<p style='text-align: left; margin-bottom: 5px;'><b>Nghiệm tối ưu:</b> " + varListHtml + " = " + optSolHtml + "</p>";
            }
        }
        html += "</body></html>";

        // ==========================================
        // 2. TẠO CHUỖI LATEX CODE ĐỂ XUẤT FILE .TEX
        // ==========================================
        auto getVarNameTex = [&](int idx) -> QString {
            if (idx < 0 || idx >= (int)varNames.size()) return "?";
            QString v = varNames[idx];
            if (v == "x_0" || v == "x0") return "x_{0}";
            return v;
        };

        auto colorTexVar = [&](const QString& var, const QString& color) -> QString {
            if (var == "?") return var;
            return "\\textcolor{" + color + "}{" + var + "}";
        };

        QString tex = "\\documentclass[12pt,a4paper]{article}\n";
        // [FIX FONT PDF/LATEX]
        // Dùng fontspec + font Unicode để tiếng Việt không bị lỗi dấu trong PDF.
        // Nếu YAML đã đóng gói Noto Serif, LaTeX dùng trực tiếp font trong app.
        // Nếu thiếu font đóng gói, fallback sang font hệ thống phổ biến.
        QString bundledFontDir = findBundledReportFontDir();
        tex += "\\usepackage{fontspec}\n";
        tex += "\\usepackage[vietnamese]{babel}\n";
        tex += "\\usepackage{amsmath, geometry, array, amssymb, xcolor}\n";
        tex += "\\usepackage{graphicx}\n";
        tex += "\\usepackage{adjustbox}\n";
        tex += "\\usepackage{fancyhdr}\n";
        tex += "\\usepackage{needspace}\n";

#if defined(Q_OS_WIN) || defined(Q_OS_WINDOWS) || defined(_WIN32)
        // [FIX WINDOWS FONT]
        // Trên Windows, Tectonic/XeTeX đôi khi lỗi khi fontspec mở font bằng đường dẫn tuyệt đối
        // kiểu C:/.../fonts/NotoSerif-Regular.ttf. Vì vậy Windows dùng font hệ thống trước.
        // Times New Roman hỗ trợ tiếng Việt tốt và có sẵn trên Windows.
        // Nếu máy không có Times New Roman thì fallback sang Arial.
        Q_UNUSED(bundledFontDir);
        tex += "\\IfFontExistsTF{Times New Roman}{\\setmainfont{Times New Roman}}{\n";
        tex += "\\IfFontExistsTF{Arial}{\\setmainfont{Arial}}{\\setmainfont{Latin Modern Roman}}}\n";
#else
        // Linux/macOS vẫn ưu tiên Noto Serif đóng gói kèm app để PDF tiếng Việt ổn định.
        if (!bundledFontDir.isEmpty()) {
            tex += "\\setmainfont{NotoSerif}[\n";
            tex += "  Path={" + bundledFontDir + "},\n";
            tex += "  UprightFont=*-Regular.ttf,\n";
            tex += "  BoldFont=*-Bold.ttf,\n";
            tex += "  ItalicFont=*-Italic.ttf,\n";
            tex += "  BoldItalicFont=*-BoldItalic.ttf\n";
            tex += "]\n";
        } else {
            tex += "\\IfFontExistsTF{Noto Serif}{\\setmainfont{Noto Serif}}{\n";
            tex += "\\IfFontExistsTF{DejaVu Serif}{\\setmainfont{DejaVu Serif}}{\n";
            tex += "\\IfFontExistsTF{Times New Roman}{\\setmainfont{Times New Roman}}{\\setmainfont{Latin Modern Roman}}}}\n";
        }
#endif
        // [FIX REPORT PDF - TRÌNH BÀY CHUYÊN NGHIỆP] \
        // Cấu hình trang theo kiểu báo cáo khoa học: lề rõ, header/footer, \
        // tiêu đề mục có đường kẻ, tiêu đề từng từ vựng có khung nhẹ.
        tex += "\\geometry{left=22mm,right=22mm,top=20mm,bottom=22mm,headheight=15pt,headsep=7mm,footskip=10mm}\n";
        tex += "\\definecolor{ReportBlue}{HTML}{1F4E79}\n";
        tex += "\\definecolor{ReportGray}{HTML}{F4F7FB}\n";
        tex += "\\definecolor{ReportRule}{HTML}{AAB7C4}\n";
        tex += "\\definecolor{ReportRed}{HTML}{C0392B}\n";
        tex += "\\definecolor{ReportGreen}{HTML}{1E8449}\n";
        tex += "\\setlength{\\parindent}{0pt}\n";
        tex += "\\setlength{\\parskip}{4pt}\n";
        tex += "\\setlength{\\arraycolsep}{2.2pt}\n";
        tex += "\\renewcommand{\\arraystretch}{1.15}\n";
        tex += "\\pagestyle{fancy}\n";
        tex += "\\fancyhf{}\n";
        tex += "\\lhead{\\small Báo cáo Quy hoạch tuyến tính}\n";
        tex += "\\rhead{\\small Phần mềm QHTT}\n";
        tex += "\\cfoot{\\small Trang \\thepage}\n";
        tex += "\\renewcommand{\\headrulewidth}{0.4pt}\n";
        tex += "\\renewcommand{\\footrulewidth}{0pt}\n";
        tex += "\\newcommand{\\ReportSection}[1]{\\Needspace{8\\baselineskip}\\vspace{0.25cm}\\noindent{\\Large\\bfseries\\textcolor{ReportBlue}{#1}}\\par\\vspace{0.05cm}\\noindent\\textcolor{ReportRule}{\\rule{\\textwidth}{0.7pt}}\\vspace{0.12cm}}\n";
        tex += "\\newcommand{\\ReportStep}[1]{\\Needspace{7\\baselineskip}\\vspace{0.18cm}\\noindent\\fcolorbox{ReportBlue}{ReportGray}{\\parbox{0.965\\textwidth}{\\textbf{#1}}}\\vspace{0.12cm}}\n";
        tex += "\\newcommand{\\ReportLabel}[1]{\\vspace{0.08cm}\\noindent\\textbf{#1}\\par\\vspace{0.04cm}}\n";
        tex += "\\sloppy\n";
        tex += "\\emergencystretch=3em\n";
        tex += "\\begin{document}\n\n";
        tex += "\\begin{center}\n";
        tex += "{\\Huge\\bfseries\\textcolor{ReportBlue}{GIẢI BÀI TOÁN QUY HOẠCH TUYẾN TÍNH}}\\par\n";
        tex += "\\vspace{0.15cm}\n";
        tex += "\\textcolor{ReportRule}{\\rule{0.78\\textwidth}{0.8pt}}\\par\n";
        tex += "\\vspace{0.12cm}\n";
        tex += "{\\large Báo cáo kết quả tính toán}\\par\n";
        tex += "{\\small Ngày xuất báo cáo: \\today}\\par\n";
        tex += "\\end{center}\n";
        tex += "\\vspace{0.35cm}\n\n";

        tex += "\\ReportSection{1. Phát biểu bài toán}\n";
        tex += "\\ReportLabel{Hàm mục tiêu:}\n";
        tex += "\\begin{center}\n$ " + QString(currentOriginalLp.isMaximize ? "\\text{Max } Z = " : "\\text{Min } Z = ");
        bool isFirstTexObj = true;
        if (std::abs(currentOriginalLp.c_0) > 1e-9) { tex += formatVal(currentOriginalLp.c_0); isFirstTexObj = false; }
        for (size_t j = 0; j < currentOriginalLp.c.size(); ++j) {
            double val = currentOriginalLp.c[j];
            if (std::abs(val) > 1e-9) {
                QString sign = (val > 0) ? (isFirstTexObj ? "" : "+ ") : "- ";
                bool isOne = (std::abs(std::abs(val) - 1.0) < 1e-9);
                QString coeffStr = isOne ? "" : formatCoeff(val);
                tex += sign + coeffStr + "x_{" + QString::number(j + 1) + "} ";
                isFirstTexObj = false;
            }
        }
        if (isFirstTexObj) tex += "0.00";
        tex += " $\n\\end{center}\n\n";

        tex += "\\noindent\\textbf{Hệ ràng buộc (Bao gồm ràng buộc dấu):}\n";

        // [FIX LATEX PDF - KHÔNG MẤT DỮ LIỆU]
        // Không dùng một bảng array quá rộng cho toàn bộ hệ ràng buộc nữa.
        // Mỗi ràng buộc được tự ngắt thành nhiều dòng, nên dù bài toán có nhiều biến
        // thì nội dung vẫn nằm trong trang PDF, không bị cắt mất bên phải.
        struct ReportTermTex {
            QString sign;
            QString coeff;
            QString var;
        };

        auto buildReportTermsTex = [&](const std::vector<double>& coeffs) -> std::vector<ReportTermTex> {
            std::vector<ReportTermTex> terms;
            for (size_t j = 0; j < coeffs.size(); ++j) {
                double val = coeffs[j];
                if (std::abs(val) < 1e-9) continue;

                ReportTermTex term;
                term.sign = (val >= 0) ? "+" : "-";
                bool isOne = (std::abs(std::abs(val) - 1.0) < 1e-9);
                term.coeff = isOne ? "" : formatCoeff(val);
                term.var = "x_{" + QString::number(j + 1) + "}";
                terms.push_back(term);
            }
            return terms;
        };

        auto appendWrappedConstraintTex = [&](const std::vector<double>& coeffs, const QString& sign, double rhs) {
            std::vector<ReportTermTex> terms = buildReportTermsTex(coeffs);
            const int maxTermsPerLine = 4;
            QString rel = sign;
            if (rel == "<=") rel = "\\le";
            else if (rel == ">=") rel = "\\ge";

            tex += "\\[\\begin{aligned}\n";

            if (terms.empty()) {
                tex += "& 0.00 " + rel + " " + formatVal(rhs) + " \\\\\n";
            } else {
                for (int start = 0; start < (int)terms.size(); start += maxTermsPerLine) {
                    int take = std::min(maxTermsPerLine, (int)terms.size() - start);
                    tex += "& ";
                    if (start > 0) tex += "\\quad ";

                    for (int k = 0; k < take; ++k) {
                        const ReportTermTex& term = terms[start + k];
                        bool isFirstPrintedTerm = (start == 0 && k == 0);

                        if (isFirstPrintedTerm) {
                            if (term.sign == "-") tex += "- ";
                        } else {
                            tex += term.sign + " ";
                        }

                        if (!term.coeff.isEmpty()) tex += term.coeff + " ";
                        tex += term.var + " ";
                    }

                    if (start + take >= (int)terms.size()) {
                        tex += rel + " " + formatVal(rhs);
                    }
                    tex += " \\\\\n";
                }
            }

            tex += "\\end{aligned}\\]\n";
        };

        for (size_t i = 0; i < currentOriginalLp.A.size(); ++i) {
            QString s = (i < currentOriginalLp.signs.size()) ? currentOriginalLp.signs[i] : "";
            double rhs = (i < currentOriginalLp.b.size()) ? currentOriginalLp.b[i] : 0.0;
            appendWrappedConstraintTex(currentOriginalLp.A[i], s, rhs);
        }

        tex += "\\ReportLabel{Ràng buộc dấu của biến:}\n";
        tex += "\\[\\begin{aligned}\n";
        for (size_t i = 0; i < currentOriginalLp.varBounds.size(); ++i) {
            if (currentOriginalLp.varBounds[i].isFree || currentOriginalLp.varBounds[i].sign == "free") {
                tex += "x_{" + QString::number(i + 1) + "} &\\in \\mathbb{R}";
            } else {
                QString s = currentOriginalLp.varBounds[i].sign;
                if (s == "<=") s = "\\le";
                else if (s == ">=") s = "\\ge";
                tex += "x_{" + QString::number(i + 1) + "} &" + s + " " + formatVal(currentOriginalLp.varBounds[i].value);
            }

            if (i + 1 < currentOriginalLp.varBounds.size()) {
                tex += " \\\\\n";
            } else {
                tex += "\n";
            }
        }
        tex += "\\end{aligned}\\]\n\n";



        // ===================================================================
        // [FIX PDF/TEX - CĂN CỘT TỪ VỰNG & KHÔNG MẤT THÔNG TIN]
        // Thay vì in từng phương trình riêng lẻ, helper dưới đây in cả một
        // bảng từ vựng theo từng block biến không cơ sở. Trong mỗi block:
        // - Cột biến cơ sở, dấu '=', hằng số tự do được căn dọc.
        // - Dấu +/- nằm trong cột riêng.
        // - Hệ số nằm trong cột riêng.
        // - Biến nằm trong cột riêng.
        // - Số biến mỗi block được tính theo chiều rộng ước lượng để không tràn trang.
        // ===================================================================
        auto estimateTermWidthTex = [&](int varIdx) -> int {
            QString rawVar = getVarNameTex(varIdx);
            // Ước lượng tương đối độ rộng khi render LaTeX.
            // Biến có chỉ số + / - thường dài hơn nên cần ít cột hơn mỗi dòng.
            int width = 9 + rawVar.length();
            if (rawVar.contains("^+")) width += 2;
            if (rawVar.contains("^-")) width += 2;
            if (rawVar.contains("w_{")) width += 1;
            return width;
        };

        auto buildVariableChunksTex = [&](const std::vector<int>& vars) -> std::vector<std::vector<int>> {
            std::vector<std::vector<int>> chunks;
            std::vector<int> currentChunk;

            const int maxEstimatedWidth = 60; // an toàn với A4, font 12pt, lề 22mm
            const int hardMaxTerms = ((int)vars.size() >= 9) ? 3 : 4;

            int currentWidth = 0;
            for (int varIdx : vars) {
                int termWidth = estimateTermWidthTex(varIdx);

                if (!currentChunk.empty() &&
                    (currentWidth + termWidth > maxEstimatedWidth ||
                     (int)currentChunk.size() >= hardMaxTerms)) {
                    chunks.push_back(currentChunk);
                    currentChunk.clear();
                    currentWidth = 0;
                }

                currentChunk.push_back(varIdx);
                currentWidth += termWidth;
            }

            if (!currentChunk.empty()) {
                chunks.push_back(currentChunk);
            }

            // Trường hợp từ vựng chỉ có hằng số tự do, vẫn cần một block để in lhs = rhs.
            if (chunks.empty()) {
                chunks.push_back(std::vector<int>());
            }

            return chunks;
        };

        auto coeffInDictionaryTex = [&](const SimplexStep& step,
                                        int rowIndex,
                                        int varIndex,
                                        int m,
                                        bool isPhase1Loc,
                                        size_t stepIdx) -> double {
            bool isZRow = (rowIndex == m);
            double coeff = isZRow ? step.matrix[m][varIndex] : -step.matrix[rowIndex][varIndex];

            // Pha 1 bước khởi tạo: biến phụ x0 xuất hiện với hệ số 1 ở dòng mục tiêu phụ.
            if (isZRow && stepIdx == 0 && isPhase1Loc &&
                (varNames[varIndex] == "x_0" || varNames[varIndex] == "x0")) {
                coeff = 1.0;
            }

            if (std::abs(coeff) < 1e-9) coeff = 0.0;
            return coeff;
        };

        auto appendDictionaryBlockTex = [&](const SimplexStep& step,
                                            const std::vector<int>& rowOrder,
                                            const std::vector<int>& nonBasicVars,
                                            int m,
                                            int n,
                                            bool isPhase1Loc,
                                            size_t stepIdx) {
            // [FIX PDF/TEX - 10 CỘT CHO MỖI TỪ VỰNG]
            // Mỗi dòng từ vựng chỉ in tối đa 10 biến không cơ sở theo chiều ngang.
            // Nếu số biến vượt quá 10 thì tự xuống dòng.
            // Dòng xuống tiếp theo chỉ in phần còn lại của vế phải, KHÔNG lặp lại
            // biến cơ sở, dấu "=" và hằng số tự do để bảng gọn và dễ đọc.
            const int maxTermColumnsPerLine = 10;

            QString colSpec = "r@{\\;}c@{\\;}r";
            for (int s = 0; s < maxTermColumnsPerLine; ++s) {
                colSpec += "@{\\quad}c@{\\;}r@{\\;}l";
            }

            tex += "\\begin{center}\n";
            tex += "\\begin{adjustbox}{max width=\\textwidth}\n";
            tex += "$\\begin{array}{" + colSpec + "}\n";

            bool separatorPrinted = false;

            auto appendEmptyTermSlots = [&](int usedSlots) {
                for (int pad = usedSlots; pad < maxTermColumnsPerLine; ++pad) {
                    tex += " &  &  & ";
                }
            };

            auto appendTermCell = [&](int varIdx, double coeff) {
                if (std::abs(coeff) < 1e-9) {
                    tex += " &  &  & ";
                    return;
                }

                QString sign = (coeff >= 0) ? "+" : "-";
                bool isOne = (std::abs(std::abs(coeff) - 1.0) < 1e-9);
                QString coeffStr = isOne ? "" : formatCoeff(coeff);

                QString rhsVar = getVarNameTex(varIdx);
                if (varIdx == step.pivotCol) {
                    rhsVar = colorTexVar(rhsVar, "ReportGreen");
                }

                tex += " & " + sign + " & " + coeffStr + " & " + rhsVar;
            };

            for (size_t rr = 0; rr < rowOrder.size(); ++rr) {
                int rowIndex = rowOrder[rr];
                bool isZRow = (rowIndex == m);

                QString lhsVar = isZRow
                                     ? (isPhase1Loc ? "\\xi" : (currentOriginalLp.isMaximize ? "-Z" : "Z"))
                                     : getVarNameTex(step.currentBasicVars[rowIndex]);

                if (!isZRow && rowIndex == step.pivotRow) {
                    lhsVar = colorTexVar(lhsVar, "ReportRed");
                }

                double rhsVal = isZRow ? -step.matrix[m][n] : step.matrix[rowIndex][n];
                if (std::abs(rhsVal) < 1e-9) rhsVal = 0.0;

                // Nếu không có biến không cơ sở, vẫn in dòng lhs = rhs.
                if (nonBasicVars.empty()) {
                    tex += lhsVar + " & = & " + formatVal(rhsVal);
                    appendEmptyTermSlots(0);
                    tex += " \\\\\n";
                } else {
                    for (int start = 0; start < (int)nonBasicVars.size(); start += maxTermColumnsPerLine) {
                        int take = std::min(maxTermColumnsPerLine, (int)nonBasicVars.size() - start);
                        bool isContinuationLine = (start > 0);

                        if (isContinuationLine) {
                            // Dòng xuống tiếp theo: không ghi lại biến cơ sở, dấu "=" và hằng số tự do.
                            tex += " & & ";
                        } else {
                            tex += lhsVar + " & = & " + formatVal(rhsVal);
                        }

                        for (int offset = 0; offset < take; ++offset) {
                            int varIdx = nonBasicVars[start + offset];
                            double coeff = coeffInDictionaryTex(step, rowIndex, varIdx, m, isPhase1Loc, stepIdx);
                            appendTermCell(varIdx, coeff);
                        }

                        appendEmptyTermSlots(take);
                        tex += " \\\\\n";
                    }
                }

                // Kẻ dòng sau toàn bộ dòng mục tiêu, bao gồm cả các dòng xuống tiếp theo.
                if (isZRow && !separatorPrinted) {
                    tex += "\\noalign{\\vskip 2pt}\\hline\\noalign{\\vskip 2pt}\n";
                    separatorPrinted = true;
                }
            }

            tex += "\\end{array}$\n";
            tex += "\\end{adjustbox}\n";
            tex += "\\end{center}\n";
        };

        tex += "\\ReportSection{2. Các bước giải (dạng từ vựng)}\n";
        for (size_t stepIdx = 0; stepIdx < currentHistory.size(); ++stepIdx) {
            const SimplexStep& step = currentHistory[stepIdx];
            int m = step.matrix.size() - 1;
            int n = step.matrix[0].size() - 1;
            bool isPhase1Loc = globalIsPhase1 && ((int)stepIdx <= lastPhase1StepIdx);

            std::vector<int> nonBasicVars;
            for (int j = 0; j < n; ++j) {
                if (!isPhase1Loc && (varNames[j] == "x_0" || varNames[j] == "x0" || varNames[j] == "")) continue;
                bool isBasic = false;
                for (int k = 0; k < m; ++k) { if (step.currentBasicVars[k] == j) { isBasic = true; break; } }
                if (!isBasic) nonBasicVars.push_back(j);
            }

            tex += "\\ReportStep{" + escapeLatexText(step.stepName) + "}\n";

            QString texOptZ;
            if (currentOriginalLp.isMaximize) {
                texOptZ = "Đối với hàm mục tiêu, vì bài toán gốc là \\textbf{Max Z} nên thuật toán đã giải thông qua việc tìm \\textbf{Min($-Z$)}. Do đó, giá trị lớn nhất của $Z$ sẽ bằng đảo dấu của hằng số tự do trong phương trình $-Z$ hiện tại.";
            } else {
                texOptZ = "Đối với hàm mục tiêu, giá trị nhỏ nhất của \\textbf{Min Z} chính là hằng số tự do trong phương trình $Z$ hiện tại.";
            }

            QString texUnbounded;
            if (currentOriginalLp.isMaximize) {
                texUnbounded = "\\noindent\\textit{\\textbf{* Giải thích không giới nội:} tồn tại một biến không cơ sở làm cải thiện hàm mục tiêu, đồng thời các hệ số của biến đó trong các phương trình ràng buộc không làm phá vỡ tính khả thi.}\\\\\n"
                               "\\noindent\\textit{\\textbf{* Giải thích:} Hàm mục tiêu có thể tăng lên vô hạn mà không vi phạm các ràng buộc. Do đó, giá trị tối ưu của bài toán Max Z là $+\\infty$.}\n\n";
            } else {
                texUnbounded = "\\noindent\\textit{\\textbf{* Giải thích không giới nội:} tồn tại một biến không cơ sở làm cải thiện hàm mục tiêu, đồng thời các hệ số của biến đó trong các phương trình ràng buộc không làm phá vỡ tính khả thi.}\\\\\n"
                               "\\noindent\\textit{\\textbf{* Giải thích:} Hàm mục tiêu có thể giảm xuống vô hạn mà không vi phạm các ràng buộc. Do đó, giá trị tối ưu của bài toán Min Z là $-\\infty$.}\n\n";
            }

            bool isLastStepTex = (stepIdx == currentHistory.size() - 1);
            if (step.pivotCol >= 0 && step.pivotRow >= 0) {
                QString enterVar = getVarNameTex(step.pivotCol);
                QString leaveVar = "?";
                if (step.currentBasicVars[step.pivotRow] >= 0 &&
                    step.currentBasicVars[step.pivotRow] < (int)varNames.size()) {
                    leaveVar = getVarNameTex(step.currentBasicVars[step.pivotRow]);
                }

                if (stepIdx == 0 && isPhase1Loc) {
                    tex += "\\noindent\\textit{$\\rightarrow$ \\textbf{Phép xoay đặc biệt:} Đưa biến phụ $" + colorTexVar(enterVar, "ReportGreen") + "$ vào cơ sở để thay thế $" + colorTexVar(leaveVar, "ReportRed") + "$ nhằm làm vế phải dương.}\\\\[0.15cm]\n";
                } else {
                    tex += "\\noindent\\textit{$\\rightarrow$ Chọn $" + colorTexVar(enterVar, "ReportGreen") + "$ làm biến vào, đẩy $" + colorTexVar(leaveVar, "ReportRed") + "$ ra khỏi cơ sở.}\\\\[0.15cm]\n";

                    if (zText.contains("Vô số nghiệm", Qt::CaseInsensitive) && stepIdx + 2 == currentHistory.size()) {
                        tex += "\\noindent\\textcolor{ReportRed}{$\\rightarrow$ \\textbf{Kết luận:} Đã đạt từ vựng tối ưu và bài toán có vô số nghiệm.}\\\\[0.1cm]\n";
                        tex += "\\noindent\\textcolor{ReportBlue}{\\textit{\\textbf{* Giải thích vô số nghiệm:} vì tồn tại hệ số của biến không cơ sở ở hàm mục tiêu bằng 0 nên bài toán có vô số nghiệm tối ưu.}}\\\\\n";
                        tex += "\\noindent\\textcolor{ReportBlue}{\\textit{\\textbf{* Giải thích cách đọc nghiệm:} cho tất cả các biến không cơ sở bằng 0, khi đó các biến cơ sở nhận giá trị bằng hằng số tự do của phương trình tương ứng.}}\\\\\n";
                        tex += "\\noindent\\textcolor{ReportBlue}{\\textit{\\textbf{* Giải thích giá trị tối ưu:} " + texOptZ + "}}\\\\\n";
                        tex += "\\noindent\\textcolor{ReportBlue}{\\textit{\\textbf{* Lưu ý:} Bước xoay tiếp theo chỉ để tìm tọa độ tối ưu thứ 2.}}\\\\[0.15cm]\n";
                    }
                }
            } else {
                if (!isLastStepTex) {
                    if (isPhase1Loc) {
                        tex += "\\noindent\\textcolor{ReportRed}{$\\rightarrow$ \\textbf{Kết luận:} Đã đạt từ vựng tối ưu của Pha 1.}\\\\[0.1cm]\n";
                    } else {
                        tex += "\\noindent\\textit{$\\rightarrow$ Hệ phương trình đã sẵn sàng. Tiếp tục thuật toán Đơn hình.}\\\\[0.15cm]\n";
                    }
                } else {
                    if (zText.contains("Không giới nội", Qt::CaseInsensitive)) {
                        tex += "\\noindent\\textcolor{ReportRed}{$\\rightarrow$ \\textbf{Kết luận:} Bài toán không giới nội. Dừng thuật toán.}\\\\[0.1cm]\n";
                        tex += texUnbounded;
                    } else if (zText.contains("Vô nghiệm", Qt::CaseInsensitive)) {
                        tex += "\\noindent\\textcolor{ReportRed}{$\\rightarrow$ \\textbf{Kết luận:} Bài toán vô nghiệm. Dừng thuật toán.}\\\\[0.1cm]\n";
                        tex += "\\noindent\\textcolor{ReportBlue}{\\textit{\\textbf{* Giải thích:} Hệ ràng buộc mâu thuẫn nhau nên không tồn tại phương án thỏa mãn tất cả các ràng buộc.}}\\\\[0.15cm]\n";
                    } else if (zText.contains("Vô số nghiệm", Qt::CaseInsensitive)) {
                        tex += "\\noindent\\textcolor{ReportRed}{$\\rightarrow$ \\textbf{Kết luận:} Đã tìm được tọa độ tối ưu thứ 2. Dừng thuật toán.}\\\\[0.1cm]\n";
                        tex += "\\noindent\\textcolor{ReportBlue}{\\textit{\\textbf{* Giải thích cách lấy nghiệm tối ưu:} cho tất cả các biến không cơ sở bằng 0, khi đó các biến cơ sở nhận giá trị bằng hằng số tự do của phương trình tương ứng.}}\\\\\n";
                        tex += "\\noindent\\textcolor{ReportBlue}{\\textit{\\textbf{* Giải thích giá trị tối ưu:} " + texOptZ + "}}\\\\[0.15cm]\n";
                    } else {
                        tex += "\\noindent\\textcolor{ReportRed}{$\\rightarrow$ \\textbf{Kết luận:} Đã đạt từ vựng tối ưu. Dừng thuật toán.}\\\\[0.1cm]\n";
                        tex += "\\noindent\\textcolor{ReportBlue}{\\textit{\\textbf{* Giải thích cách đọc nghiệm:} cho tất cả các biến không cơ sở bằng 0, khi đó các biến cơ sở nhận giá trị bằng hằng số tự do của phương trình tương ứng.}}\\\\\n";
                        tex += "\\noindent\\textcolor{ReportBlue}{\\textit{\\textbf{* Giải thích giá trị tối ưu:} " + texOptZ + "}}\\\\[0.15cm]\n";
                    }
                }
            }

            // [FIX PDF/TEX - BẢNG TỪ VỰNG CĂN CỘT]
            // In cả từ vựng thành các block có cùng cột, giúp hệ số và biến
            // thẳng hàng theo chiều dọc, dễ đọc hơn và không tràn ngang.
            std::vector<int> rowOrder;
            rowOrder.push_back(m);
            for (int i = 0; i < m; ++i) {
                rowOrder.push_back(i);
            }

            appendDictionaryBlockTex(step, rowOrder, nonBasicVars, m, n, isPhase1Loc, stepIdx);


            if (globalIsPhase1 && (int)stepIdx == lastPhase1StepIdx) {
                double xi_val = -step.matrix[m][n];
                if (std::abs(xi_val) < 1e-9) {
                    tex += "\\vspace{0.2cm}\n\\noindent\\textit{* \\textbf{Giải thích:} Để suy ra nghiệm, ta cho tất cả các biến không cơ sở (các biến nằm ở vế phải) bằng 0, khi đó các biến cơ sở (vế trái) sẽ nhận giá trị bằng đúng hằng số tự do của phương trình tương ứng. Vì giá trị tối ưu $\\xi = 0$, bài toán gốc có nghiệm khả thi. Thuật toán sẽ tiếp tục \\textbf{chuyển sang Pha 2}.}\n\n";
                } else {
                    tex += "\\vspace{0.2cm}\n\\noindent\\textit{* \\textbf{Giải thích:} Để suy ra nghiệm, ta cho tất cả các biến không cơ sở (các biến nằm ở vế phải) bằng 0, khi đó các biến cơ sở (vế trái) sẽ nhận giá trị bằng đúng hằng số tự do của phương trình tương ứng. Vì giá trị tối ưu $\\xi \\neq 0$, hệ ràng buộc của bài toán gốc mâu thuẫn nhau nên \\textbf{bài toán vô nghiệm}.}\n\n";
                }
            }
        }

        tex += "\\ReportSection{3. Kết luận}\n";
        tex += "\\begin{center}\n";
        tex += "\\fcolorbox{ReportBlue}{ReportGray}{\\begin{minipage}{0.94\\textwidth}\n";
        tex += "\\textbf{Trạng thái:} " + reportStatus + "\\\\[0.18cm]\n";

        // [FIX KHÔNG GIỚI NỘI] Ghi rõ giá trị tối ưu vô hạn trong file .tex/PDF LaTeX.
        if (zText.contains("Không giới nội", Qt::CaseInsensitive)) {
            QString unboundedValueTex = currentOriginalLp.isMaximize ? "+\\infty" : "-\\infty";
            tex += "\\noindent\\textbf{Giá trị tối ưu:} $Z^* = " + unboundedValueTex + "$\\\\[0.2cm]\n";
        }

        if (!zText.contains("Vô nghiệm", Qt::CaseInsensitive) && !zText.contains("Không giới nội", Qt::CaseInsensitive) && !zText.contains("Lỗi", Qt::CaseInsensitive)) {
            tex += "\\noindent\\textbf{Giá trị tối ưu:} $Z^* = " + formatVal(z_opt) + "$\\\\[0.2cm]\n";

            if (zText.contains("Vô số nghiệm", Qt::CaseInsensitive) &&
                !firstPointTex.isEmpty() && !secondPointTex.isEmpty()) {
                tex += "\\noindent\\textbf{Nghiệm tối ưu thứ nhất:} $" + varListTex + " = " + firstPointTex + "$\\\\[0.15cm]\n";
                tex += "\\noindent\\textbf{Nghiệm tối ưu thứ hai:} $" + varListTex + " = " + secondPointTex + "$\\\\[0.15cm]\n";
                tex += "\\noindent\\textbf{Tập nghiệm tối ưu:} "
                       "$\\left\\{ X(\\lambda) = \\lambda " + firstPointTex +
                       " + (1-\\lambda) " + secondPointTex +
                       " \\mid 0 \\le \\lambda \\le 1 \\right\\}$\\\\[0.15cm]\n";
                tex += "\\noindent\\textit{Các điểm thuộc tập trên đều là nghiệm tối ưu.}\\\\[0.15cm]\n";
                tex += "\\noindent\\textit{Nếu bài toán có nhiều hơn hai biến, tập nghiệm tối ưu tổng quát có thể là một mặt lồi của miền nghiệm khả thi, không nhất thiết chỉ là đoạn thẳng nối hai điểm trên.}\n\n";
            } else {
                tex += "\\noindent\\textbf{Nghiệm tối ưu:} $" + varListTex + " = " + optSolTex + "$\n\n";
            }
        }
        tex += "\\end{minipage}}\n";
        tex += "\\end{center}\n";
        tex += "\\end{document}";


        QDialog *previewDialog = new QDialog(this);
        previewDialog->setWindowTitle("Xem lời giải với PDF");
        previewDialog->resize(1250, 880);
        QVBoxLayout *dlgLayout = new QVBoxLayout(previewDialog);

        bool isDark = readDarkModeSetting();
        if (isDark) {
            previewDialog->setStyleSheet("QDialog { background-color: #1E1E2E; } QPushButton { background-color: #313244; color: #CDD6F4; border: 1px solid #45475A; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #45475A; }");
        } else {
            previewDialog->setStyleSheet("QDialog { background-color: #F5F7FA; } QPushButton { background-color: #FFFFFF; color: #333333; border: 1px solid #CCCCCC; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #E8E8E8; }");
        }

        QTextBrowser *previewBrowser = new QTextBrowser(previewDialog);
        previewBrowser->setStyleSheet("QTextBrowser { background-color: #FFFFFF; color: #000000; padding: 28px 36px; border-radius: 4px; border: 1px solid #BDBDBD;}");
        previewBrowser->setHtml(html);
        dlgLayout->addWidget(previewBrowser);

        QHBoxLayout *btnPreviewLayout = new QHBoxLayout();
        QPushButton *btnDownloadPdf = new QPushButton("📥 Tải xuống PDF", previewDialog);
        QPushButton *btnDownloadTex = new QPushButton("📥 Tải xuống .tex", previewDialog);

        btnPreviewLayout->addStretch();
        btnPreviewLayout->addWidget(btnDownloadPdf);
        btnPreviewLayout->addWidget(btnDownloadTex);
        dlgLayout->addLayout(btnPreviewLayout);

        connect(btnDownloadTex, &QPushButton::clicked, previewDialog, [previewDialog, tex]() {
            QString fileName = QFileDialog::getSaveFileName(previewDialog, "Lưu file LaTeX", "BaoCao_QHTT.tex", "LaTeX Files (*.tex)");
            if (!fileName.isEmpty()) {
                QFile file(fileName);
                if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out(&file);
                    out.setEncoding(QStringConverter::Utf8);
                    out << tex;
                    file.close();
                    QMessageBox::information(previewDialog, "Thành công", "Đã lưu thành công file LaTeX (.tex)!");
                }
            }
        });

        connect(btnDownloadPdf, &QPushButton::clicked, previewDialog, [previewDialog, tex, html]() {
            QString fileName = QFileDialog::getSaveFileName(
                previewDialog,
                "Lưu file PDF",
                "BaoCao_QHTT.pdf",
                "PDF Files (*.pdf)",
                nullptr,
                QFileDialog::DontUseNativeDialog
                );
            if (fileName.isEmpty()) return;

            bool useTectonic = false;
            QString compilerPath = findBundledLatexCompiler(&useTectonic);

            if (compilerPath.isEmpty()) {
                // Không có LaTeX compiler thì vẫn xuất PDF được bằng chế độ Qt fallback.
                if (exportHtmlPdfFallback(fileName, html)) {
                    QMessageBox::warning(
                        previewDialog,
                        "Không tìm thấy Tectonic/XeLaTeX",
                        "Không tìm thấy Tectonic hoặc XeLaTeX trên máy.\n\n"
                        "Phần mềm đã tự động xuất PDF bằng chế độ tương thích Qt. "
                        "Bản PDF này có thể không đẹp bằng bản biên dịch LaTeX, "
                        "nhưng sẽ không làm ứng dụng bị treo."
                        );
                } else {
                    QMessageBox::critical(previewDialog, "Lỗi", "Không thể xuất PDF bằng chế độ tương thích Qt.");
                }
                return;
            }

            QTemporaryDir* tempDir = new QTemporaryDir();
            if (!tempDir->isValid()) {
                delete tempDir;
                QMessageBox::critical(previewDialog, "Lỗi", "Không tạo được thư mục tạm để biên dịch PDF.");
                return;
            }

            QString texPath = tempDir->filePath("BaoCao_QHTT.tex");
            QFile texFile(texPath);
            if (!texFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                delete tempDir;
                QMessageBox::critical(previewDialog, "Lỗi", "Không ghi được file LaTeX tạm.");
                return;
            }

            QTextStream out(&texFile);
            out.setEncoding(QStringConverter::Utf8);
            out << tex;
            texFile.close();

            QProcess* process = new QProcess(previewDialog);
            process->setWorkingDirectory(tempDir->path());

            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            QFileInfo compilerInfo(compilerPath);
            QString oldPath = env.value("PATH");
#ifdef Q_OS_WIN
            env.insert("PATH", compilerInfo.absolutePath() + ";" + oldPath);
#else
            env.insert("PATH", compilerInfo.absolutePath() + ":" + oldPath);
#endif

            // Dùng cache ghi được để Tectonic không ghi vào AppImage/.app/thư mục cài đặt.
            QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
            if (cacheRoot.isEmpty()) {
                cacheRoot = QDir::tempPath() + "/SolveLinearProgrammingCache";
            }
            QDir().mkpath(cacheRoot);
            env.insert("TECTONIC_CACHE_DIR", QDir(cacheRoot).filePath("tectonic"));
#ifndef Q_OS_WIN
            env.insert("XDG_CACHE_HOME", cacheRoot);
#endif
            process->setProcessEnvironment(env);

            QStringList args;
            if (useTectonic) {
                // [FIX TECTONIC]
                // --chatter minimal giảm lượng log tải package/font, tránh đầy pipe và tránh QMessageBox quá dài.
                // Dùng tên file tương đối trong workingDirectory thay vì đường dẫn tuyệt đối để giảm warning cross-platform.
                args << "--chatter" << "minimal"
                     << "--keep-logs"
                     << "--outdir" << tempDir->path()
                     << "BaoCao_QHTT.tex";
            } else {
                args << "-interaction=nonstopmode" << "-halt-on-error" << "BaoCao_QHTT.tex";
            }

            QProgressDialog* progress = new QProgressDialog(
                "Đang biên dịch PDF từ LaTeX...\n"
                "Lần đầu chạy Tectonic có thể cần tải gói hỗ trợ, vui lòng chờ.",
                "Hủy",
                0,
                0,
                previewDialog
                );
            progress->setWindowTitle("Đang xuất PDF");
            progress->setWindowModality(Qt::WindowModal);
            progress->setMinimumDuration(0);
            progress->setAutoClose(false);
            progress->setAutoReset(false);
            progress->show();

            QString* stdOutLog = new QString();
            QString* stdErrLog = new QString();

            // [FIX KHÔNG BÁO HỦY GIẢ]
            // QProgressDialog có thể phát tín hiệu canceled() khi ta gọi close()/reset()
            // bằng code sau khi process đã kết thúc. Vì vậy phải tách rõ:
            // - userCanceled: người dùng thật sự bấm Hủy
            // - processFinished: process đã kết thúc, không được coi close() là hủy
            // - timedOut: process bị timeout, hiển thị đúng là quá thời gian chứ không báo hủy.
            bool* userCanceled = new bool(false);
            bool* processFinished = new bool(false);
            bool* timedOut = new bool(false);
            QMetaObject::Connection* cancelConnection = new QMetaObject::Connection();

            // RẤT QUAN TRỌNG: phải đọc stdout/stderr liên tục.
            // Nếu không, Tectonic in quá nhiều dòng "note: downloading ..." sẽ làm đầy pipe buffer
            // và tiến trình bị kẹt, khiến cả Windows/Linux/macOS báo Not Responding.
            QObject::connect(process, &QProcess::readyReadStandardOutput, previewDialog, [process, stdOutLog]() {
                *stdOutLog += QString::fromUtf8(process->readAllStandardOutput());
                if (stdOutLog->length() > 20000) *stdOutLog = stdOutLog->right(12000);
            });

            QObject::connect(process, &QProcess::readyReadStandardError, previewDialog, [process, stdErrLog]() {
                *stdErrLog += QString::fromUtf8(process->readAllStandardError());
                if (stdErrLog->length() > 20000) *stdErrLog = stdErrLog->right(12000);
            });

            *cancelConnection = QObject::connect(progress, &QProgressDialog::canceled, previewDialog,
                                                 [process, userCanceled, processFinished]() {
                                                     if (*processFinished) return;
                                                     *userCanceled = true;
                                                     if (process->state() != QProcess::NotRunning) {
                                                         process->kill();
                                                     }
                                                 });

            QTimer* timeoutTimer = new QTimer(process);
            timeoutTimer->setSingleShot(true);
            QObject::connect(timeoutTimer, &QTimer::timeout, previewDialog, [process, timedOut, processFinished]() {
                if (*processFinished) return;
                *timedOut = true;
                if (process->state() != QProcess::NotRunning) {
                    process->kill();
                }
            });

            QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                             previewDialog,
                             [=](int exitCode, QProcess::ExitStatus exitStatus) {
                                 *processFinished = true;
                                 if (timeoutTimer) timeoutTimer->stop();

                                 // Ngắt tín hiệu canceled() trước khi đóng progress để tránh QProgressDialog
                                 // tự phát canceled() và làm app báo nhầm "Đã hủy" trên Windows/Linux/macOS.
                                 if (cancelConnection) QObject::disconnect(*cancelConnection);
                                 if (progress) {
                                     progress->hide();
                                     progress->deleteLater();
                                 }

                                 // Đọc nốt phần log còn lại để tránh mất lỗi thật.
                                 *stdOutLog += QString::fromUtf8(process->readAllStandardOutput());
                                 *stdErrLog += QString::fromUtf8(process->readAllStandardError());

                                 QString pdfTempPath = findLatexPdfOutput(tempDir->path());
                                 bool success = (exitStatus == QProcess::NormalExit &&
                                                 QFileInfo::exists(pdfTempPath) &&
                                                 QFileInfo(pdfTempPath).size() > 0);

                                 if (*userCanceled) {
                                     QMessageBox::information(previewDialog, "Đã hủy", "Quá trình biên dịch PDF đã bị hủy bởi người dùng.");
                                 } else if (*timedOut) {
                                     bool fallbackOk = exportHtmlPdfFallback(fileName, html);
                                     QString log = compactLatexLog(*stdErrLog + "\n" + *stdOutLog);

                                     if (fallbackOk) {
                                         QMessageBox::warning(
                                             previewDialog,
                                             "Biên dịch LaTeX quá lâu",
                                             "Tectonic/XeLaTeX chạy quá thời gian cho phép nên đã được dừng lại.\n\n"
                                             "Phần mềm đã tự động lưu PDF bằng chế độ tương thích Qt để tránh treo ứng dụng.\n\n"
                                             "Log rút gọn:\n" + log
                                             );
                                     } else {
                                         QMessageBox::critical(
                                             previewDialog,
                                             "Lỗi biên dịch LaTeX",
                                             "Tectonic/XeLaTeX chạy quá thời gian cho phép và cũng không thể xuất bằng chế độ Qt.\n\n"
                                             "Log rút gọn:\n" + log
                                             );
                                     }
                                 } else if (success) {
                                     if (QFileInfo::exists(fileName)) QFile::remove(fileName);

                                     if (QFile::copy(pdfTempPath, fileName)) {
                                         QMessageBox::information(previewDialog, "Thành công", "Đã biên dịch và lưu thành công file PDF từ LaTeX!");
                                     } else {
                                         QMessageBox::critical(previewDialog, "Lỗi", "Không thể lưu file PDF vào vị trí đã chọn.");
                                     }
                                 } else {
                                     // Nếu LaTeX/Tectonic lỗi do tải gói, thiếu mạng, thiếu font..., vẫn không để người dùng thất bại hoàn toàn.
                                     bool fallbackOk = exportHtmlPdfFallback(fileName, html);
                                     QString log = compactLatexLog(*stdErrLog + "\n" + *stdOutLog);

                                     if (fallbackOk) {
                                         QMessageBox::warning(
                                             previewDialog,
                                             "Biên dịch LaTeX không thành công",
                                             "Tectonic/XeLaTeX chưa tạo được PDF LaTeX.\n"
                                             "Nguyên nhân thường gặp trên Windows/Linux là lần đầu Tectonic cần tải bundle hoặc thiếu mạng.\n\n"
                                             "Phần mềm đã tự động lưu PDF bằng chế độ tương thích Qt để tránh lỗi.\n\n"
                                             "Log rút gọn:\n" + log
                                             );
                                     } else {
                                         QMessageBox::critical(
                                             previewDialog,
                                             "Lỗi biên dịch LaTeX",
                                             "Không thể biên dịch file .tex thành PDF và cũng không thể xuất bằng chế độ Qt.\n\n"
                                             "Log rút gọn:\n" + log
                                             );
                                     }
                                 }

                                 delete stdOutLog;
                                 delete stdErrLog;
                                 delete userCanceled;
                                 delete processFinished;
                                 delete timedOut;
                                 delete cancelConnection;
                                 delete tempDir;
                                 process->deleteLater();
                             });

            process->start(compilerPath, args);
            if (!process->waitForStarted(3000)) {
                if (cancelConnection) QObject::disconnect(*cancelConnection);
                if (progress) {
                    progress->hide();
                    progress->deleteLater();
                }
                bool fallbackOk = exportHtmlPdfFallback(fileName, html);
                if (fallbackOk) {
                    QMessageBox::warning(previewDialog, "Không khởi động được LaTeX", "Phần mềm đã tự động xuất PDF bằng chế độ tương thích Qt.");
                } else {
                    QMessageBox::critical(previewDialog, "Lỗi", "Không khởi động được trình biên dịch LaTeX.");
                }
                delete stdOutLog;
                delete stdErrLog;
                delete userCanceled;
                delete processFinished;
                delete timedOut;
                delete cancelConnection;
                delete tempDir;
                process->deleteLater();
                return;
            }

            // Không khóa UI. Nếu quá lâu thì tự hủy và fallback, nhưng người dùng có thể hủy sớm.
            timeoutTimer->start(600000); // 10 phút cho lần đầu Tectonic tải cache.
        });

        previewDialog->exec();
        delete previewDialog;
    });
}

WdSolve::~WdSolve()
{
    delete ui;
}

void WdSolve::displayResults(const LinearProgram& lp,
                             const LinearProgram& originalLp,
                             const QString& status,
                             double optimalZ,
                             const std::vector<double>& solution,
                             const std::vector<double>& altSolution,
                             const std::vector<SimplexStep>& history)
{
    this->currentAltSolution = altSolution;

    // ĐỌC TRẠNG THÁI GIAO DIỆN SÁNG / TỐI TỪ MAINWINDOW
    bool isDark = readDarkModeSetting();

    // ===============================================================
    // TIỀN XỬ LÝ LỊCH SỬ BƯỚC GIẢI
    // ===============================================================
    std::vector<SimplexStep> modHistory = history;
    int vocabCount = 1;
    for (size_t i = 0; i < modHistory.size(); ++i) {
        if (i == 0) {
            vocabCount = 1;
        } else if (modHistory[i].stepName.contains("Khởi tạo Pha 2", Qt::CaseInsensitive)) {
            vocabCount = 1;
        } else {
            vocabCount++;
        }

        if (modHistory[i].stepName.contains("Điểm tối ưu thứ 2", Qt::CaseInsensitive)) {
            modHistory[i].stepName = QString("Từ vựng %1").arg(vocabCount);
        } else {
            modHistory[i].stepName.replace("Vòng lặp", "Từ vựng");
        }
    }

    std::vector<QString> varNames;
    for (size_t i = 0; i < originalLp.varBounds.size(); ++i) {
        if (originalLp.varBounds[i].isFree || originalLp.varBounds[i].sign == "free") {
            varNames.push_back(QString("x%1+").arg(i + 1));
            varNames.push_back(QString("x%1-").arg(i + 1));
        } else {
            varNames.push_back(QString("x%1").arg(i + 1));
        }
    }

    int origN_internal = varNames.size();
    int num_w = 0;
    bool globalIsPhase1 = false;

    if (!modHistory.empty()) {
        for(const auto& step : modHistory) {
            if (step.stepName.contains("Pha 1", Qt::CaseInsensitive) ||
                step.stepName.contains("Pha 2", Qt::CaseInsensitive)) {
                globalIsPhase1 = true;
                break;
            }
        }

        int n_total_vars = modHistory[0].matrix[0].size() - 1;
        num_w = n_total_vars - origN_internal;
        if (globalIsPhase1) num_w -= 1;
        if (num_w < 0) num_w = 0;

        for (int i = 0; i < num_w; ++i) {
            varNames.push_back(QString("w%1").arg(i + 1));
        }
        if (globalIsPhase1) {
            varNames.push_back("x0");
        }
    }

    // ---------------------------------------------------------------
    // 1. KHU VỰC KẾT QUẢ VÀ BẢNG NGHIỆM
    // ---------------------------------------------------------------
    QFont fontZ = ui->lineEdit_Z->font();
    fontZ.setBold(true);
    fontZ.setPointSize(12);
    ui->lineEdit_Z->setFont(fontZ);

    // Tẩy CSS header cứng để ăn theo Theme tổng
    ui->table_solution->horizontalHeader()->setStyleSheet("");

    if (status == "Tối ưu" || status == "Vô số nghiệm") {
        double finalZ = optimalZ;
        if (status == "Vô số nghiệm")
            ui->lineEdit_Z->setText(QString::number(finalZ, 'f', 4) + " (Vô số nghiệm)");
        else
            ui->lineEdit_Z->setText(QString::number(finalZ, 'f', 4));

        int origN = (int)originalLp.varBounds.size();
        bool isInfinite = (status == "Vô số nghiệm");
        int colCount = isInfinite ? 4 : 2;

        ui->table_solution->setRowCount(isInfinite ? origN + 2 : origN);
        ui->table_solution->setColumnCount(colCount);

        QStringList headers;
        if (isInfinite) {
            headers << "Biến" << "Đỉnh 1" << "Đỉnh 2" << "Vector hướng (V)";
        } else {
            headers << "Biến" << "Giá trị";
        }

        ui->table_solution->setHorizontalHeaderLabels(headers);
        ui->table_solution->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        ui->table_solution->verticalHeader()->setVisible(false);

        for (int i = 0; i < origN; ++i) {
            double val1 = (i < (int)solution.size()) ? solution[i] : 0.0;
            QTableWidgetItem *itemVar = new QTableWidgetItem(QString("x%1").arg(i + 1));
            itemVar->setTextAlignment(Qt::AlignCenter);
            ui->table_solution->setItem(i, 0, itemVar);

            QTableWidgetItem *itemVal1 = new QTableWidgetItem(QString::number(val1, 'f', 4));
            itemVal1->setTextAlignment(Qt::AlignCenter);
            ui->table_solution->setItem(i, 1, itemVal1);

            if (isInfinite) {
                double val2 = (i < (int)altSolution.size()) ? altSolution[i] : 0.0;
                QTableWidgetItem *itemVal2 = new QTableWidgetItem(QString::number(val2, 'f', 4));
                itemVal2->setTextAlignment(Qt::AlignCenter);
                ui->table_solution->setItem(i, 2, itemVal2);

                double delta = val2 - val1;
                if (std::abs(delta) < 1e-9) delta = 0.0;
                QTableWidgetItem *itemDelta = new QTableWidgetItem(QString::number(delta, 'f', 4));
                itemDelta->setTextAlignment(Qt::AlignCenter);

                // MÀU NỀN CỘT VECTOR CHUYỂN ĐỔI THEO THEME
                itemDelta->setBackground(QColor(isDark ? "#313244" : "#F0F8FF"));
                ui->table_solution->setItem(i, 3, itemDelta);
            }
        }

        // ===================================================================
        // [FIX VÔ SỐ NGHIỆM] Bổ sung tập nghiệm tối ưu ngay trong bảng nghiệm.
        // Nếu phần mềm tìm được 2 đỉnh tối ưu, mọi tổ hợp lồi của 2 đỉnh này
        // đều là nghiệm tối ưu. Với bài toán nhiều hơn 2 biến, tập nghiệm tối ưu
        // tổng quát có thể là một mặt lồi của miền khả thi, không nhất thiết chỉ
        // là đoạn thẳng nối 2 điểm đã tìm được.
        // ===================================================================
        if (isInfinite) {
            auto makePointDisplay = [&](const std::vector<double>& point) -> QString {
                QStringList values;
                for (int i = 0; i < origN; ++i) {
                    double v = (i < (int)point.size()) ? point[i] : 0.0;
                    if (std::abs(v) < 1e-9) v = 0.0;
                    values << QString::number(v, 'f', 4);
                }
                return "(" + values.join(", ") + ")";
            };

            QString firstPoint = makePointDisplay(solution);
            QString secondPoint = makePointDisplay(altSolution);

            int setRow = origN;
            int noteRow = origN + 1;

            // ===================================================================
            // [FIX UI VÔ SỐ NGHIỆM - THIẾT KẾ CHUYÊN NGHIỆP HƠN]
            // Không span toàn bộ 4 cột nữa vì nhìn giống một khối ghi chú thô.
            // Chia thành 2 dòng dạng "nhãn + nội dung" để đồng bộ với bảng nghiệm.
            // ===================================================================
            const QColor labelBg(isDark ? "#1E3A5F" : "#E8F2FF");
            const QColor contentBg(isDark ? "#263247" : "#F8FBFF");
            const QColor noteBg(isDark ? "#2A2E3F" : "#F6F7F9");
            const QColor noteLabelBg(isDark ? "#3A3148" : "#F1ECFF");

            QTableWidgetItem *itemSetLabel = new QTableWidgetItem("Tập nghiệm");
            itemSetLabel->setTextAlignment(Qt::AlignCenter);
            itemSetLabel->setBackground(labelBg);
            ui->table_solution->setItem(setRow, 0, itemSetLabel);

            QTableWidgetItem *itemSet = new QTableWidgetItem(
                "X(λ) = λ" + firstPoint +
                " + (1 - λ)" + secondPoint +
                ", 0 ≤ λ ≤ 1. Tất cả các điểm thuộc tập này đều là nghiệm tối ưu."
                );
            itemSet->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            itemSet->setBackground(contentBg);
            itemSet->setToolTip(itemSet->text());
            ui->table_solution->setItem(setRow, 1, itemSet);
            ui->table_solution->setSpan(setRow, 1, 1, colCount - 1);

            QTableWidgetItem *itemNoteLabel = new QTableWidgetItem("Ghi chú");
            itemNoteLabel->setTextAlignment(Qt::AlignCenter);
            itemNoteLabel->setBackground(noteLabelBg);
            ui->table_solution->setItem(noteRow, 0, itemNoteLabel);

            QTableWidgetItem *itemNote = new QTableWidgetItem(
                "Nếu bài toán có nhiều hơn hai biến, tập nghiệm tối ưu tổng quát có thể là một mặt lồi của miền nghiệm khả thi; "
                "không nhất thiết chỉ là đoạn thẳng nối hai điểm trên."
                );
            itemNote->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            itemNote->setBackground(noteBg);
            itemNote->setToolTip(itemNote->text());
            ui->table_solution->setItem(noteRow, 1, itemNote);
            ui->table_solution->setSpan(noteRow, 1, 1, colCount - 1);

            ui->table_solution->setWordWrap(true);
            ui->table_solution->setShowGrid(true);
            ui->table_solution->setRowHeight(setRow, 44);
            ui->table_solution->setRowHeight(noteRow, 52);
        }
    } else if (status.contains("giới nội", Qt::CaseInsensitive)) {
        if (originalLp.isMaximize) {
            ui->lineEdit_Z->setText("+∞ (Không giới nội)");
        } else {
            ui->lineEdit_Z->setText("-∞ (Không giới nội)");
        }

        if (!modHistory.empty()) {
            const SimplexStep& lastStep = modHistory.back();
            int m = (int)lastStep.matrix.size() - 1;
            int n = (int)lastStep.matrix[0].size() - 1;
            int origN = (int)originalLp.varBounds.size();

            std::vector<int> nonBasicVars;
            for (int j = 0; j < n; ++j) {
                bool isBasic = false;
                for (int r = 0; r < m; ++r) {
                    if (lastStep.currentBasicVars[r] == j) { isBasic = true; break; }
                }
                if (!isBasic) nonBasicVars.push_back(j);
            }

            ui->table_solution->setRowCount(origN + num_w);
            ui->table_solution->setColumnCount(2);
            ui->table_solution->setHorizontalHeaderLabels({"Biến", "Phương trình (Tập nghiệm vô cực)"});
            ui->table_solution->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
            ui->table_solution->verticalHeader()->setVisible(false);
            ui->table_solution->setShowGrid(true);

            int varIndexInMatrix = 0;
            for (int i = 0; i < origN; ++i) {
                QString varName = QString("x%1").arg(i + 1);
                QTableWidgetItem *itemVar = new QTableWidgetItem(varName);
                itemVar->setTextAlignment(Qt::AlignCenter);
                ui->table_solution->setItem(i, 0, itemVar);

                int colIdx = varIndexInMatrix;
                if (originalLp.varBounds[i].isFree || originalLp.varBounds[i].sign == "free") {
                    varIndexInMatrix += 2;
                } else {
                    varIndexInMatrix += 1;
                }

                int rowIdx = -1;
                for (int r = 0; r < m; ++r) {
                    if (lastStep.currentBasicVars[r] == colIdx) { rowIdx = r; break; }
                }

                QString eqStr = "";
                if (rowIdx != -1) {
                    double rhsVal = lastStep.matrix[rowIdx][n];
                    if (std::abs(rhsVal) < 1e-9) rhsVal = 0.0;

                    bool hasTerms = false;
                    if (std::abs(rhsVal) >= 1e-9) {
                        eqStr = QString::number(rhsVal, 'f', 2);
                        hasTerms = true;
                    }

                    for (int j : nonBasicVars) {
                        if (j < (int)varNames.size() && varNames[j] == "x0") continue;
                        double coeff = -lastStep.matrix[rowIdx][j];
                        if (std::abs(coeff) >= 1e-9) {
                            if (hasTerms) {
                                if (coeff > 0) eqStr += QString(" + %1 %2").arg(QString::number(coeff, 'f', 2), varNames[j]);
                                else eqStr += QString(" - %1 %2").arg(QString::number(std::abs(coeff), 'f', 2), varNames[j]);
                            } else {
                                if (coeff > 0) eqStr = QString("%1 %2").arg(QString::number(coeff, 'f', 2), varNames[j]);
                                else eqStr = QString("-%1 %2").arg(QString::number(std::abs(coeff), 'f', 2), varNames[j]);
                                hasTerms = true;
                            }
                        }
                    }

                    if (!hasTerms) {
                        eqStr = QString::number(rhsVal, 'f', 2);
                    }
                    eqStr += " \u2265 0";
                } else {
                    eqStr = "Biến tham số tùy ý \u2265 0";
                }

                QTableWidgetItem *itemEq = new QTableWidgetItem(eqStr);
                itemEq->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                ui->table_solution->setItem(i, 1, itemEq);
            }

            for (int i = 0; i < num_w; ++i) {
                QString varName = QString("w%1").arg(i + 1);
                QTableWidgetItem *itemVar = new QTableWidgetItem(varName);
                itemVar->setTextAlignment(Qt::AlignCenter);
                ui->table_solution->setItem(origN + i, 0, itemVar);

                int colIdx = varIndexInMatrix + i;
                int rowIdx = -1;
                for (int r = 0; r < m; ++r) {
                    if (lastStep.currentBasicVars[r] == colIdx) { rowIdx = r; break; }
                }

                QString eqStr = "";
                if (rowIdx != -1) {
                    double rhsVal = lastStep.matrix[rowIdx][n];
                    if (std::abs(rhsVal) < 1e-9) rhsVal = 0.0;

                    bool hasTerms = false;
                    if (std::abs(rhsVal) >= 1e-9) {
                        eqStr = QString::number(rhsVal, 'f', 2);
                        hasTerms = true;
                    }

                    for (int j : nonBasicVars) {
                        if (j < (int)varNames.size() && varNames[j] == "x0") continue;
                        double coeff = -lastStep.matrix[rowIdx][j];
                        if (std::abs(coeff) >= 1e-9) {
                            if (hasTerms) {
                                if (coeff > 0) eqStr += QString(" + %1 %2").arg(QString::number(coeff, 'f', 2), varNames[j]);
                                else eqStr += QString(" - %1 %2").arg(QString::number(std::abs(coeff), 'f', 2), varNames[j]);
                            } else {
                                if (coeff > 0) eqStr = QString("%1 %2").arg(QString::number(coeff, 'f', 2), varNames[j]);
                                else eqStr = QString("-%1 %2").arg(QString::number(std::abs(coeff), 'f', 2), varNames[j]);
                                hasTerms = true;
                            }
                        }
                    }
                    if (!hasTerms) eqStr = QString::number(rhsVal, 'f', 2);
                    eqStr += " \u2265 0";
                } else {
                    eqStr = "Biến tham số tùy ý \u2265 0";
                }

                QTableWidgetItem *itemEq = new QTableWidgetItem(eqStr);
                itemEq->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                ui->table_solution->setItem(origN + i, 1, itemEq);
            }
        } else {
            ui->table_solution->clearContents();
            ui->table_solution->setRowCount(0);
            ui->table_solution->setColumnCount(0);
        }
    } else if (status.contains("Vô nghiệm", Qt::CaseInsensitive)) {
        if (originalLp.isMaximize) {
            ui->lineEdit_Z->setText("-∞ (Vô nghiệm)");
        } else {
            ui->lineEdit_Z->setText("+∞ (Vô nghiệm)");
        }
        ui->table_solution->clearContents();
        ui->table_solution->setRowCount(0);
        ui->table_solution->setColumnCount(0);
    } else {
        ui->lineEdit_Z->setText("Lỗi / Không giải được");
        ui->table_solution->clearContents();
        ui->table_solution->setRowCount(0);
        ui->table_solution->setColumnCount(0);
        QMessageBox::critical(this, "Thông báo Thuật toán", status);
    }

    // ---------------------------------------------------------------
    // 2. KHU VỰC VẼ TỪ VỰNG VÀ BẢNG HTML TỰ ĐỘNG CHUYỂN MÀU THEO THEME
    // ---------------------------------------------------------------
    ui->tabWidget_steps->clear();

    QTabWidget *vocabTabWidget = new QTabWidget();
    QTabWidget *tableTabWidget = new QTabWidget();

    // MÀU TAB CSS CHUYỂN ĐỔI SÁNG TỐI
    QString bgTab = isDark ? "#181825" : "#FAFAFA";
    QString borderColor = isDark ? "#45475A" : "#a0a0a0";
    QString tabUnselectedBg = isDark ? "#313244" : "#e6e6e6";
    QString tabSelectedColor = isDark ? "#89B4FA" : "#0056b3";
    QString textColor = isDark ? "#CDD6F4" : "#333333";
    QString hrColor = isDark ? "#CDD6F4" : "#999999";

    QString tabStyle = QString(
                           "QTabWidget::pane { border: 1px solid %1; background-color: %2; top: -1px; } "
                           "QTabBar::tab { color: %3; padding: 8px 15px; font-weight: bold; border: 1px solid %1; border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 2px; background-color: %4; } "
                           "QTabBar::tab:selected { color: %5; background-color: %2; border-top: 2px solid %5; border-bottom-color: %2; } "
                           "QTabBar::tab:!selected { margin-top: 2px; }"
                           ).arg(borderColor, bgTab, textColor, tabUnselectedBg, tabSelectedColor);

    ui->tabWidget_steps->setStyleSheet(tabStyle);
    vocabTabWidget->setStyleSheet(tabStyle);
    tableTabWidget->setStyleSheet(tabStyle);

    bool isPhase1 = globalIsPhase1;

    for (size_t stepIdx = 0; stepIdx < modHistory.size(); ++stepIdx) {
        const SimplexStep& step = modHistory[stepIdx];
        int m = (int)step.matrix.size() - 1;
        int n = (int)step.matrix[0].size() - 1;

        if (step.stepName.contains("Pha 2")) isPhase1 = false;

        QString vocabReadSol = "Để suy ra nghiệm, ta cho tất cả các biến không cơ sở (các biến nằm ở vế phải) bằng 0, khi đó các biến cơ sở (vế trái) sẽ nhận giá trị bằng đúng hằng số tự do của phương trình tương ứng.";
        QString tableReadSol = "Để suy ra nghiệm, ta cho tất cả các biến không cơ sở bằng 0, khi đó các biến cơ sở (ở cột Cơ sở) sẽ nhận giá trị bằng đúng giá trị tại cột RHS tương ứng.";

        QString vocabReadSol2 = "Để suy ra nghiệm tối ưu thứ 2, ta cho tất cả các biến không cơ sở (các biến nằm ở vế phải) bằng 0, khi đó các biến cơ sở (vế trái) sẽ nhận giá trị bằng đúng hằng số tự do của phương trình tương ứng.";
        QString tableReadSol2 = "Để suy ra nghiệm tối ưu thứ 2, ta cho tất cả các biến không cơ sở bằng 0, khi đó các biến cơ sở (ở cột Cơ sở) sẽ nhận giá trị bằng đúng giá trị tại cột RHS tương ứng.";

        QString vocabOptZ, tableOptZ;
        if (originalLp.isMaximize) {
            vocabOptZ = "Đối với hàm mục tiêu, vì bài toán gốc là <b>Max Z</b> nên thuật toán đã giải thông qua việc tìm <b>Min(-Z)</b>. Do đó, giá trị lớn nhất của Z sẽ bằng đảo dấu của hằng số tự do trong phương trình -Z hiện tại.";
            tableOptZ = "Đối với hàm mục tiêu, vì bài toán gốc là <b>Max Z</b> nên thuật toán đã giải thông qua việc tìm <b>Min(-Z)</b>. Do đó, giá trị lớn nhất của Z sẽ bằng đảo dấu của giá trị tại cột RHS của dòng -Z trong bảng hiện tại.";
        } else {
            vocabOptZ = "Đối với hàm mục tiêu, giá trị nhỏ nhất của <b>Min Z</b> chính là hằng số tự do trong phương trình Z hiện tại.";
            tableOptZ = "Đối với hàm mục tiêu, giá trị nhỏ nhất của <b>Min Z</b> chính là giá trị tại cột RHS của dòng Z trong bảng hiện tại.";
        }

        QString commonIntroHtml = "";

        if (stepIdx == 0 && isPhase1) {
            commonIntroHtml += "<div style='background-color: #f0fdf4; padding: 10px 15px; border-left: 4px solid #28a745; margin-bottom: 15px; font-size: 12pt; color: #333333;'>";
            commonIntroHtml += "<b style='color: #28a745;'>Thiết lập bài toán phụ (Pha 1):</b><br/>";
            commonIntroHtml += "Mục tiêu: Tìm Min &xi; = x0.<br/>";
            commonIntroHtml += "<i>(Lưu ý: Bạn đang ở bước Khởi tạo. Biến x0 vừa được thêm vào hệ thống và chuẩn bị được thế chỗ vào cơ sở để làm hệ phương trình khả thi)</i>";
            commonIntroHtml += "</div>";
        }
        else if (step.stepName.contains("Khởi tạo Pha 2")) {
            commonIntroHtml += "<div style='background-color: #e6f2ff; padding: 10px 15px; border-left: 4px solid #0056b3; margin-bottom: 15px; font-size: 12pt; color: #333333;'>";
            commonIntroHtml += "<b style='color: #0056b3;'>Thay vào hàm mục tiêu gốc (Chuyển sang Pha 2):</b><br/>";

            QString origZStr = (originalLp.isMaximize ? "Max Z = " : "Min Z = ");
            bool isFirstOrig = true;
            for (size_t i = 0; i < originalLp.c.size(); ++i) {
                if (std::abs(originalLp.c[i]) > 1e-9) {
                    double val = originalLp.c[i];
                    QString sign = (val > 0 && !isFirstOrig) ? " + " : (val < 0 ? " - " : "");
                    origZStr += sign + QString::number(std::abs(val), 'f', 2) + " x" + QString::number(i+1);
                    isFirstOrig = false;
                }
            }
            if (isFirstOrig) origZStr += "0";

            commonIntroHtml += "Hàm mục tiêu ban đầu: <b>" + origZStr + "</b><br/>";

            if (originalLp.isMaximize) {
                commonIntroHtml += "<i>(Thế các biến cơ sở ở bảng cuối Pha 1 vào hàm Z và lật ngược dấu toàn bộ phương trình thành -Z, ta thu được kết quả bên dưới)</i>";
            } else {
                commonIntroHtml += "<i>(Thế các biến cơ sở ở bảng cuối Pha 1 vào hàm Z và rút gọn, ta thu được phương trình Z mới bên dưới)</i>";
            }
            commonIntroHtml += "</div>";
        }

        std::vector<int> nonBasicVars;
        for (int j = 0; j < n; ++j) {
            bool isBasic = false;
            for (int r = 0; r < m; ++r) {
                if (step.currentBasicVars[r] == j) {
                    isBasic = true; break;
                }
            }
            if (!isBasic) nonBasicVars.push_back(j);
        }

        if (step.pivotCol >= 0 && step.pivotRow >= 0) {
            QString enterVar = (step.pivotCol < (int)varNames.size()) ? varNames[step.pivotCol] : "?";
            QString leaveVar = (step.currentBasicVars[step.pivotRow] != -1 && step.currentBasicVars[step.pivotRow] < (int)varNames.size()) ? varNames[step.currentBasicVars[step.pivotRow]] : "?";

            if (stepIdx == 0 && isPhase1) {
                commonIntroHtml += QString("<p style='color: #333333; font-size: 12pt; margin-bottom: 20px; font-style: italic;'>&rarr; <b>Phép xoay đặc biệt:</b> Đưa biến phụ <b><font color='green'>%1</font></b> vào cơ sở để thay thế <b><font color='red'>%2</font></b> nhằm làm vế phải dương.</p>")
                                       .arg(enterVar).arg(leaveVar);
            } else {
                if (status.contains("Vô số nghiệm", Qt::CaseInsensitive) && stepIdx == modHistory.size() - 2) {
                    commonIntroHtml += QString("<p style='color: #333333; font-size: 12pt; margin-bottom: 4px; margin-top: 8px; font-style: italic;'>&rarr; Chọn <b><font color='green'>%1</font></b> làm biến vào, đẩy <b><font color='red'>%2</font></b> ra khỏi cơ sở.</p>")
                                           .arg(enterVar).arg(leaveVar);
                    commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 4px; margin-top: 0px;'>&rarr; <b>Kết luận:</b> Đã đạt [TU_VUNG_BANG] tối ưu (có vô số nghiệm).</p>";
                    commonIntroHtml += "<p style='color: #0056b3; font-size: 11.5pt; margin-bottom: 15px; margin-top: 0px; font-style: italic;'>";

                    commonIntroHtml += "<b>* Giải thích vô số nghiệm:</b> vì tồn tại hệ số của biến không cơ sở ở hàm mục tiêu bằng 0 nên có vô số nghiệm tối ưu.<br>";
                    commonIntroHtml += "<b>* Giải thích cách đọc nghiệm:</b> [READ_SOLUTION]<br>";
                    commonIntroHtml += "<b>* Giải thích giá trị tối ưu:</b> [OPT_Z]<br>";
                    commonIntroHtml += "<b>* Lưu ý:</b> Bước xoay tiếp theo chỉ để tìm tọa độ tối ưu thứ 2.";
                    commonIntroHtml += "</p>";
                } else {
                    commonIntroHtml += QString("<p style='color: #333333; font-size: 12pt; margin-bottom: 20px; font-style: italic;'>&rarr; Chọn <b><font color='green'>%1</font></b> làm biến vào, đẩy <b><font color='red'>%2</font></b> ra khỏi cơ sở.</p>")
                                           .arg(enterVar).arg(leaveVar);
                }
            }
        } else {
            bool isLastStep = (stepIdx == modHistory.size() - 1);

            if (!isLastStep) {
                if (isPhase1) {
                    commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 5px;'>&rarr; <b>Kết luận:</b> Đã đạt [TU_VUNG_BANG] tối ưu của Pha 1.</p>";
                    commonIntroHtml += "<p style='color: #0056b3; font-size: 11.5pt; margin-bottom: 20px; font-style: italic;'>";
                    commonIntroHtml += "<b>* Giải thích:</b> [READ_SOLUTION]<br>";
                    commonIntroHtml += "Vì giá trị tối ưu <b>&xi; = 0</b>, bài toán gốc có nghiệm khả thi. Thuật toán sẽ tiếp tục <b>chuyển sang Pha 2</b>.";
                    commonIntroHtml += "</p>";
                } else {
                    commonIntroHtml += "<p style='color: #333333; font-size: 12pt; margin-bottom: 20px; font-style: italic;'>&rarr; Hệ phương trình đã sẵn sàng. Tiếp tục thuật toán Đơn hình.</p>";
                }
            } else {
                if (status.contains("xoay vòng", Qt::CaseInsensitive) || status.contains("Cycling", Qt::CaseInsensitive)) {
                    commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 20px;'>&rarr; <b>Kết luận:</b> Phát hiện hiện tượng xoay vòng (Cycling). Dừng thuật toán.</p>";
                } else if (status.contains("giới nội", Qt::CaseInsensitive)) {
                    commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 5px;'>&rarr; <b>Kết luận:</b> Bài toán không giới nội. Dừng thuật toán.</p>";
                    commonIntroHtml += "<p style='color: #0056b3; font-size: 11.5pt; margin-bottom: 20px; font-style: italic;'>";

                    commonIntroHtml += "<b>* Giải thích không giới nội:</b> tồn tại một biến không cơ sở làm cải thiện hàm mục tiêu, đồng thời các hệ số của biến đó trong các phương trình ràng buộc không làm phá vỡ tính khả thi.<br>";

                    if (originalLp.isMaximize) {
                        commonIntroHtml += "<b>* Giải thích:</b> Hàm mục tiêu có thể tăng lên vô hạn mà không vi phạm các ràng buộc. Do đó, giá trị tối ưu của bài toán Max Z là <b>+&infin;</b>.";
                    } else {
                        commonIntroHtml += "<b>* Giải thích:</b> Hàm mục tiêu có thể giảm xuống vô hạn mà không vi phạm các ràng buộc. Do đó, giá trị tối ưu của bài toán Min Z là <b>-&infin;</b>.";
                    }
                    commonIntroHtml += "</p>";
                } else if (status.contains("Vô nghiệm", Qt::CaseInsensitive)) {
                    if (isPhase1) {
                        commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 5px;'>&rarr; <b>Kết luận:</b> Đã đạt [TU_VUNG_BANG] tối ưu của Pha 1.</p>";
                        commonIntroHtml += "<p style='color: #0056b3; font-size: 11.5pt; margin-bottom: 20px; font-style: italic;'>";
                        commonIntroHtml += "<b>* Giải thích:</b> [READ_SOLUTION]<br>";
                        commonIntroHtml += "Vì giá trị tối ưu <b>&xi; &ne; 0</b> (khác 0), hệ ràng buộc của bài toán gốc mâu thuẫn nhau nên <b>không có nghiệm tối ưu cho bài toán</b>.";
                        commonIntroHtml += "</p>";
                    } else {
                        commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 20px;'>&rarr; <b>Kết luận:</b> Bài toán vô nghiệm. Dừng thuật toán.</p>";
                    }
                } else {
                    if (status.contains("Vô số nghiệm", Qt::CaseInsensitive)) {
                        commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 5px;'>&rarr; <b>Kết luận:</b> Đã tìm được tọa độ tối ưu thứ 2. Dừng thuật toán.</p>";
                    } else {
                        commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 5px;'>&rarr; <b>Kết luận:</b> Đã đạt [TU_VUNG_BANG] tối ưu. Dừng thuật toán.</p>";
                    }

                    QString solStr = "";
                    bool firstVar = true;
                    for (int i = 0; i < m; ++i) {
                        int basicVarIndex = step.currentBasicVars[i];
                        QString lhsVarName = (basicVarIndex != -1 && basicVarIndex < (int)varNames.size()) ? varNames[basicVarIndex] : "?";
                        double rhsVal = step.matrix[i][n];
                        if (std::abs(rhsVal) < 1e-9) rhsVal = 0.0;
                        if (!firstVar) solStr += ", ";
                        solStr += QString("%1 = %2").arg(lhsVarName).arg(QString::number(rhsVal, 'f', 2));
                        firstVar = false;
                    }

                    commonIntroHtml += "<p style='color: #0056b3; font-size: 11.5pt; margin-bottom: 20px; font-style: italic;'>";

                    if (status.contains("Vô số nghiệm", Qt::CaseInsensitive)) {
                        commonIntroHtml += "<b>* Giải thích cách lấy nghiệm tối ưu:</b> [READ_SOLUTION_2]";
                    } else {
                        commonIntroHtml += "<b>* Giải thích cách đọc nghiệm và giá trị tối ưu:</b> [READ_SOLUTION]<br>[OPT_Z]";
                    }
                    commonIntroHtml += "</p>";
                }
            }
        }

        // --- 2.2 RÁP HTML CHO TAB "DẠNG TỪ VỰNG" ---
        QString htmlVocab = "<html><body style='font-family: \"Times New Roman\", serif; margin: 0; padding: 0; color: #333333;'>";
        QString titleStrVocab;
        if (stepIdx == 0) {
            if (isPhase1) titleStrVocab = "Từ vựng 1 (Khởi tạo Pha 1)";
            else titleStrVocab = "Từ vựng 1";
        } else if (step.stepName.contains("Khởi tạo Pha 2")) {
            titleStrVocab = "Từ vựng 1 (Khởi tạo Pha 2)";
        } else {
            titleStrVocab = step.stepName.split(" (").first();
        }

        htmlVocab += QString("<p style='color: #0056b3; font-size: 14pt; font-weight: bold; margin-bottom: 8px; margin-top: 0; border-bottom: 2px solid #0056b3; padding-bottom: 4px; display: inline-block;'>%1:</p>").arg(titleStrVocab);

        QString vocabIntroHtml = commonIntroHtml;
        vocabIntroHtml.replace("[TU_VUNG_BANG]", "từ vựng");
        vocabIntroHtml.replace("[READ_SOLUTION]", vocabReadSol);
        vocabIntroHtml.replace("[READ_SOLUTION_2]", vocabReadSol2);
        vocabIntroHtml.replace("[OPT_Z]", vocabOptZ);
        htmlVocab += vocabIntroHtml;

        htmlVocab += "<table align='center' cellpadding='8' cellspacing='0' style='margin-top: 10px;'>";

        htmlVocab += "<tr style='font-size: 16pt; font-weight: bold;'>";
        QString zLabel;
        double zRhsVal = -step.matrix[m][n];
        double coeffMultiplier = 1.0;

        if (isPhase1) zLabel = "&xi;";
        else zLabel = originalLp.isMaximize ? "-Z" : "Z";

        htmlVocab += QString("<td align='right'>%1</td><td align='center'> = </td>").arg(zLabel);
        if (std::abs(zRhsVal) < 1e-9) zRhsVal = 0.0;

        if (stepIdx == 0 && isPhase1 && std::abs(zRhsVal) < 1e-9) {
            htmlVocab += "<td></td>";
        } else {
            htmlVocab += QString("<td align='right'>%1</td>").arg(QString::number(zRhsVal, 'f', 2));
        }

        for (int j : nonBasicVars) {
            if (!isPhase1 && j < (int)varNames.size() && varNames[j] == "x0") continue;
            double val = step.matrix[m][j];
            if (std::abs(val) >= 1e-9) {
                double coeff = val * coeffMultiplier;
                QString sign = (coeff > 0) ? "+" : "-";
                if (stepIdx == 0 && isPhase1 && std::abs(zRhsVal) < 1e-9 && coeff > 0) sign = "";

                QString num = QString::number(std::abs(coeff), 'f', 2);
                QString varName = (j < (int)varNames.size()) ? varNames[j] : "?";
                if (j == step.pivotCol) varName = QString("<b><font color='green'>%1</font></b>").arg(varName);

                htmlVocab += QString("<td align='center'>%1</td><td align='right'>%2</td><td align='left'>%3</td>")
                                 .arg(sign, num, varName);
            } else {
                htmlVocab += "<td></td><td></td><td></td>";
            }
        }
        htmlVocab += "</tr>";

        int totalCols = 3 + 3 * nonBasicVars.size();
        htmlVocab += QString("<tr><td colspan='%1' style='padding: 2px 0;'><hr size='1' width='100%' color='%2' noshade='noshade' style='height: 1px; border: 0; margin: 0;'></td></tr>").arg(totalCols).arg(isDark ? "#FFFFFF" : "#999999");

        for (int i = 0; i < m; ++i) {
            htmlVocab += "<tr style='font-size: 15pt;'>";
            int basicVarIndex = step.currentBasicVars[i];
            QString lhsVarName = (basicVarIndex != -1 && basicVarIndex < (int)varNames.size()) ? varNames[basicVarIndex] : "?";
            if (i == step.pivotRow) lhsVarName = QString("<b><font color='red'>%1</font></b>").arg(lhsVarName);

            htmlVocab += QString("<td align='right'>%1</td><td align='center'> = </td>").arg(lhsVarName);
            double rhsVal = step.matrix[i][n];
            if (std::abs(rhsVal) < 1e-9) rhsVal = 0.0;
            htmlVocab += QString("<td align='right'>%1</td>").arg(QString::number(rhsVal, 'f', 2));

            for (int j : nonBasicVars) {
                if (!isPhase1 && j < (int)varNames.size() && varNames[j] == "x0") continue;
                double val = step.matrix[i][j];
                if (std::abs(val) >= 1e-9) {
                    double coeff = -val;
                    QString sign = (coeff > 0) ? "+" : "-";
                    QString num = QString::number(std::abs(coeff), 'f', 2);
                    QString varName = (j < (int)varNames.size()) ? varNames[j] : "?";
                    if (j == step.pivotCol) varName = QString("<b><font color='green'>%1</font></b>").arg(varName);

                    htmlVocab += QString("<td align='center'>%1</td><td align='right'>%2</td><td align='left'>%3</td>")
                                     .arg(sign, num, varName);
                } else {
                    htmlVocab += "<td></td><td></td><td></td>";
                }
            }
            htmlVocab += "</tr>";
        }
        htmlVocab += "</table></body></html>";


        // --- 2.3 RÁP HTML CHO TAB "DẠNG BẢNG" ---
        QString htmlTable = "<html><body style='font-family: \"Times New Roman\", serif; margin: 0; padding: 0; color: #333333;'>";
        QString titleStrTable = titleStrVocab;
        titleStrTable.replace("Từ vựng", "Bảng");

        htmlTable += QString("<p style='color: #0056b3; font-size: 14pt; font-weight: bold; margin-bottom: 8px; margin-top: 0; border-bottom: 2px solid #0056b3; padding-bottom: 4px; display: inline-block;'>%1:</p>").arg(titleStrTable);

        QString tableIntroHtml = commonIntroHtml;
        tableIntroHtml.replace("[TU_VUNG_BANG]", "bảng");
        tableIntroHtml.replace("[READ_SOLUTION]", tableReadSol);
        tableIntroHtml.replace("[READ_SOLUTION_2]", tableReadSol2);
        tableIntroHtml.replace("[OPT_Z]", tableOptZ);
        htmlTable += tableIntroHtml;

        htmlTable += "<table width='100%' border='1' cellspacing='0' cellpadding='10' style='border-collapse: collapse; text-align: center; border: 1px solid #aaa; margin-top: 15px;'>";

        htmlTable += "<tr style='background-color: #e6f2ff; font-weight: bold; font-size: 15pt;'>";
        htmlTable += "<td>Cơ sở</td>";
        for (int j = 0; j < n; ++j) {
            QString vName = (j < (int)varNames.size()) ? varNames[j] : QString("x%1").arg(j);
            if (!isPhase1 && vName == "x0") continue;
            htmlTable += QString("<td>%1</td>").arg(vName);
        }
        htmlTable += "<td>RHS</td>";
        htmlTable += "</tr>";

        for (int i = 0; i < m; ++i) {
            htmlTable += "<tr style='font-size: 15pt;'>";
            int basicVarIndex = step.currentBasicVars[i];
            QString lhsVarName = (basicVarIndex != -1 && basicVarIndex < (int)varNames.size()) ? varNames[basicVarIndex] : "?";
            if (i == step.pivotRow) lhsVarName = QString("<b><font color='red'>%1</font></b>").arg(lhsVarName);
            else lhsVarName = QString("<b>%1</b>").arg(lhsVarName);

            htmlTable += QString("<td>%1</td>").arg(lhsVarName);

            for (int j = 0; j < n; ++j) {
                QString vName = (j < (int)varNames.size()) ? varNames[j] : "";
                if (!isPhase1 && vName == "x0") continue;

                double val = step.matrix[i][j];
                if (std::abs(val) < 1e-9) val = 0.0;

                if (i == step.pivotRow && j == step.pivotCol) {
                    htmlTable += QString("<td style='background-color: #d4edda; font-weight: bold; color: #155724;'>%1</td>").arg(QString::number(val, 'f', 2));
                } else {
                    htmlTable += QString("<td>%1</td>").arg(QString::number(val, 'f', 2));
                }
            }
            double valRhs = step.matrix[i][n];
            if (std::abs(valRhs) < 1e-9) valRhs = 0.0;
            htmlTable += QString("<td><b>%1</b></td>").arg(QString::number(valRhs, 'f', 2));
            htmlTable += "</tr>";
        }

        htmlTable += "<tr style='background-color: #fff3cd; font-weight: bold; font-size: 15pt;'>";
        QString zLabelTable;
        if (isPhase1) zLabelTable = "&xi;";
        else zLabelTable = originalLp.isMaximize ? "-Z" : "Z";
        htmlTable += QString("<td>%1</td>").arg(zLabelTable);

        for (int j = 0; j < n; ++j) {
            QString vName = (j < (int)varNames.size()) ? varNames[j] : "";
            if (!isPhase1 && vName == "x0") continue;

            double val = -step.matrix[m][j];
            if (std::abs(val) < 1e-9) val = 0.0;

            if (j == step.pivotCol) {
                htmlTable += QString("<td><b><font color='green'>%1</font></b></td>").arg(QString::number(val, 'f', 2));
            } else {
                htmlTable += QString("<td>%1</td>").arg(QString::number(val, 'f', 2));
            }
        }

        double valZRhs = -step.matrix[m][n];
        if (std::abs(valZRhs) < 1e-9) valZRhs = 0.0;
        htmlTable += QString("<td><b>%1</b></td>").arg(QString::number(valZRhs, 'f', 2));
        htmlTable += "</tr>";
        htmlTable += "</table></body></html>";

        // =======================================================================
        // ÁP DỤNG DARK MODE LÊN MÃ NGUỒN HTML
        // Thay thế toàn bộ mã màu cứng thành mã màu tối tương ứng
        // =======================================================================
        if (isDark) {
            htmlVocab.replace("color: #333333", "color: #CDD6F4");
            htmlVocab.replace("color: #d9534f", "color: #F38BA8");
            htmlVocab.replace("color: #0056b3", "color: #89B4FA");
            htmlVocab.replace("border-bottom: 2px solid #0056b3", "border-bottom: 2px solid #89B4FA");
            htmlVocab.replace("color='green'", "color='#A6E3A1'");
            htmlVocab.replace("color='red'", "color='#F38BA8'");
            htmlVocab.replace("background-color: #f0fdf4", "background-color: #1e3a29");
            htmlVocab.replace("border-left: 4px solid #28a745", "border-left: 4px solid #A6E3A1");
            htmlVocab.replace("color: #28a745", "color: #A6E3A1");
            htmlVocab.replace("background-color: #e6f2ff", "background-color: #1e293b");
            htmlVocab.replace("border-left: 4px solid #0056b3", "border-left: 4px solid #89B4FA");
            htmlVocab.replace("border-top: 1px dashed #999", "border-top: 1px dashed #45475A");

            htmlTable.replace("color: #333333", "color: #CDD6F4");
            htmlTable.replace("color: #d9534f", "color: #F38BA8");
            htmlTable.replace("color: #0056b3", "color: #89B4FA");
            htmlTable.replace("border-bottom: 2px solid #0056b3", "border-bottom: 2px solid #89B4FA");
            htmlTable.replace("color='green'", "color='#A6E3A1'");
            htmlTable.replace("color='red'", "color='#F38BA8'");
            htmlTable.replace("background-color: #f0fdf4", "background-color: #1e3a29");
            htmlTable.replace("border-left: 4px solid #28a745", "border-left: 4px solid #A6E3A1");
            htmlTable.replace("color: #28a745", "color: #A6E3A1");
            htmlTable.replace("background-color: #e6f2ff", "background-color: #1e293b");
            htmlTable.replace("border-left: 4px solid #0056b3", "border-left: 4px solid #89B4FA");
            htmlTable.replace("border: 1px solid #aaa", "border: 1px solid #45475A");
            htmlTable.replace("background-color: #d4edda", "background-color: #2d4a22");
            htmlTable.replace("color: #155724", "color: #A6E3A1");
            htmlTable.replace("background-color: #fff3cd", "background-color: #454224");
        }

        QTextEdit *textEditVocab = new QTextEdit();
        textEditVocab->setReadOnly(true);
        textEditVocab->setStyleSheet(QString("QTextEdit { font-family: 'Times New Roman', serif; padding: 20px; border: none; background-color: %1; }").arg(isDark ? "#181825" : "#FAFAFA"));
        textEditVocab->setHtml(htmlVocab);
        vocabTabWidget->addTab(textEditVocab, step.stepName);


        QTextEdit *textEditTable = new QTextEdit();
        textEditTable->setReadOnly(true);
        textEditTable->setStyleSheet(QString("QTextEdit { font-family: 'Times New Roman', serif; padding: 20px; border: none; background-color: %1; }").arg(isDark ? "#181825" : "#FAFAFA"));
        textEditTable->setHtml(htmlTable);

        QString tabNameTable = step.stepName;
        tabNameTable.replace("Từ vựng", "Bảng");
        tableTabWidget->addTab(textEditTable, tabNameTable);
    }

    ui->tabWidget_steps->addTab(vocabTabWidget, "[w, x, z] HIỂN THỊ DẠNG TỪ VỰNG");
    ui->tabWidget_steps->addTab(tableTabWidget, "📊 HIỂN THỊ DẠNG BẢNG");

    this->currentLp           = lp;
    this->currentOriginalLp   = originalLp;
    this->currentSolution     = solution;
    this->currentHistory      = modHistory;
}

void WdSolve::on_pushButton_clicked()
{
    if (this->parentWidget()) {
        this->parentWidget()->show();
    }
    this->hide();
}

void WdSolve::on_pushButton_2_clicked()
{
    if (this->currentOriginalLp.c.size() == 2) {
        if (!this->wd_show) {
            this->wd_show = new WdShowImage(this);
        }

        this->wd_show->drawGraph(currentLp, currentOriginalLp,
                                 currentSolution, this->currentHistory);
        this->wd_show->show();
    } else {
        QMessageBox::warning(this, "Warning",
                             "Chỉ hỗ trợ vẽ với bài toán 2 biến số");
    }
}

void WdSolve::on_pushButton_3_clicked()
{
    const LinearProgram& lp = currentOriginalLp;

    auto normalizeSign = [](const QString& sign) -> QString {
        QString s = sign.trimmed();
        if (s == "<=") return "≤";
        if (s == ">=") return "≥";
        if (s == "=")  return "=";
        if (s.compare("free", Qt::CaseInsensitive) == 0) return "free";
        return s.isEmpty() ? "?" : s;
    };

    auto formatNumber = [](double value) -> QString {
        if (std::abs(value) < 1e-9) value = 0.00;
        return QString::number(value, 'f', 2);
    };

    auto buildLinearExpression = [&](const std::vector<double>& coeffs, bool keepZeroTerms = false) -> QString {
        QString expr;
        bool hasTerm = false;

        for (size_t i = 0; i < coeffs.size(); ++i) {
            double val = coeffs[i];
            if (std::abs(val) < 1e-9 && !keepZeroTerms) continue;

            QString varName = "x" + QString::number((int)i + 1);
            double absVal = std::abs(val);
            if (std::abs(absVal) < 1e-9) absVal = 0.0;

            QString term = formatNumber(absVal) + "*" + varName;

            if (!hasTerm) {
                if (val < 0) expr += "-";
                expr += term;
                hasTerm = true;
            } else {
                expr += (val >= 0) ? " + " : " - ";
                expr += term;
            }
        }

        if (!hasTerm) expr = "0.00";
        return expr;
    };

    // ===================================================================
    // [FIX CHATBOT CONTEXT]
    // Truyền thêm toàn bộ dữ liệu đang hiển thị ở cửa sổ WdSolve cho Chatbot:
    // - Bảng nghiệm / giá trị tối ưu
    // - Các từ vựng theo từng bước
    // - Bảng đơn hình theo từng bước
    // Chỉ phục vụ context cho Chatbot, không thay đổi thuật toán / UI khác.
    // ===================================================================
    auto buildInternalVariableNames = [&]() -> std::vector<QString> {
        std::vector<QString> names;

        for (size_t i = 0; i < currentOriginalLp.varBounds.size(); ++i) {
            if (currentOriginalLp.varBounds[i].isFree || currentOriginalLp.varBounds[i].sign == "free") {
                names.push_back(QString("x%1+").arg(i + 1));
                names.push_back(QString("x%1-").arg(i + 1));
            } else {
                names.push_back(QString("x%1").arg(i + 1));
            }
        }

        if (!currentHistory.empty() &&
            !currentHistory[0].matrix.empty() &&
            !currentHistory[0].matrix[0].empty()) {
            int totalColsWithoutRhs = (int)currentHistory[0].matrix[0].size() - 1;
            int remain = totalColsWithoutRhs - (int)names.size();

            bool hasPhase = false;
            for (const SimplexStep& step : currentHistory) {
                if (step.stepName.contains("Pha 1", Qt::CaseInsensitive) ||
                    step.stepName.contains("Pha 2", Qt::CaseInsensitive)) {
                    hasPhase = true;
                    break;
                }
            }

            if (hasPhase) remain -= 1;
            if (remain < 0) remain = 0;

            for (int i = 0; i < remain; ++i) {
                names.push_back(QString("w%1").arg(i + 1));
            }

            if (hasPhase) {
                names.push_back("x0");
            }
        }

        return names;
    };

    std::vector<QString> contextVarNames = buildInternalVariableNames();

    auto getStepVarName = [&](int index) -> QString {
        if (index >= 0 && index < (int)contextVarNames.size()) {
            return contextVarNames[index];
        }
        return QString("?");
    };

    auto buildVocabularyEquation = [&](const SimplexStep& step, int rowIndex, bool isPhase1Step) -> QString {
        if (step.matrix.empty() || step.matrix[0].empty()) return "";

        int m = (int)step.matrix.size() - 1;
        int n = (int)step.matrix[0].size() - 1;
        bool isZRow = (rowIndex == m);

        QString lhs;
        if (isZRow) {
            lhs = isPhase1Step ? "ξ" : (currentOriginalLp.isMaximize ? "-Z" : "Z");
        } else {
            lhs = getStepVarName(step.currentBasicVars[rowIndex]);
        }

        double rhsVal = isZRow ? -step.matrix[m][n] : step.matrix[rowIndex][n];
        if (std::abs(rhsVal) < 1e-9) rhsVal = 0.0;

        QString eq = lhs + " = " + formatNumber(rhsVal);

        for (int col = 0; col < n; ++col) {
            bool isBasic = false;
            for (int r = 0; r < m; ++r) {
                if (step.currentBasicVars[r] == col) {
                    isBasic = true;
                    break;
                }
            }
            if (isBasic) continue;

            if (!isPhase1Step && (getStepVarName(col) == "x0")) {
                continue;
            }

            double coeff = isZRow ? step.matrix[m][col] : -step.matrix[rowIndex][col];
            if (isZRow && isPhase1Step && getStepVarName(col) == "x0") {
                coeff = 1.0;
            }

            if (std::abs(coeff) < 1e-9) continue;

            eq += (coeff >= 0) ? " + " : " - ";
            double absCoeff = std::abs(coeff);

            if (std::abs(absCoeff - 1.0) >= 1e-9) {
                eq += formatNumber(absCoeff) + "*";
            }
            eq += getStepVarName(col);
        }

        return eq;
    };

    auto buildSimplexTableText = [&](const SimplexStep& step, bool isPhase1Step) -> QString {
        if (step.matrix.empty() || step.matrix[0].empty()) return "";

        int m = (int)step.matrix.size() - 1;
        int n = (int)step.matrix[0].size() - 1;

        QString tableText;
        tableText += "Bảng đơn hình của " + step.stepName + ":\n";
        tableText += "Dòng | Cơ sở | RHS";

        for (int col = 0; col < n; ++col) {
            if (!isPhase1Step && getStepVarName(col) == "x0") continue;
            tableText += " | " + getStepVarName(col);
        }
        tableText += "\n";

        QString objectiveLabel = isPhase1Step ? "ξ" : (currentOriginalLp.isMaximize ? "-Z" : "Z");
        tableText += "0 | " + objectiveLabel + " | " + formatNumber(step.matrix[m][n]);
        for (int col = 0; col < n; ++col) {
            if (!isPhase1Step && getStepVarName(col) == "x0") continue;
            tableText += " | " + formatNumber(step.matrix[m][col]);
        }
        tableText += "\n";

        for (int row = 0; row < m; ++row) {
            QString basis = getStepVarName(step.currentBasicVars[row]);
            tableText += QString("%1 | %2 | %3")
                             .arg(row + 1)
                             .arg(basis)
                             .arg(formatNumber(step.matrix[row][n]));

            for (int col = 0; col < n; ++col) {
                if (!isPhase1Step && getStepVarName(col) == "x0") continue;
                tableText += " | " + formatNumber(step.matrix[row][col]);
            }
            tableText += "\n";
        }

        if (step.pivotCol >= 0 && step.pivotRow >= 0) {
            tableText += "Biến vào: " + getStepVarName(step.pivotCol) + "\n";
            tableText += "Biến ra: " + getStepVarName(step.currentBasicVars[step.pivotRow]) + "\n";
            tableText += "Dòng trụ: " + QString::number(step.pivotRow + 1) + "\n";
            tableText += "Cột trụ: " + getStepVarName(step.pivotCol) + "\n";
        } else {
            tableText += "Không có phép xoay ở bước này.\n";
        }

        return tableText;
    };

    auto buildAllExecutionStepsForChatbot = [&]() -> QString {
        QString result;

        if (currentHistory.empty()) {
            return "Không có dữ liệu các bước thực thi.\n";
        }

        bool isPhase1Step = false;
        for (const SimplexStep& step : currentHistory) {
            if (step.stepName.contains("Pha 1", Qt::CaseInsensitive) ||
                step.stepName.contains("Pha 2", Qt::CaseInsensitive)) {
                isPhase1Step = true;
                break;
            }
        }

        result += "\n5) CÁC BƯỚC THỰC THI - DẠNG TỪ VỰNG\n";
        result += "Lưu ý: Đây là dữ liệu từ tab [w, x, z] HIỂN THỊ DẠNG TỪ VỰNG trong cửa sổ kết quả.\n";

        for (size_t stepIdx = 0; stepIdx < currentHistory.size(); ++stepIdx) {
            const SimplexStep& step = currentHistory[stepIdx];

            if (step.stepName.contains("Pha 2", Qt::CaseInsensitive)) {
                isPhase1Step = false;
            }

            if (step.matrix.empty() || step.matrix[0].empty()) continue;

            int m = (int)step.matrix.size() - 1;

            result += "\n--- " + step.stepName + " ---\n";

            if (step.pivotCol >= 0 && step.pivotRow >= 0) {
                QString enterVar = getStepVarName(step.pivotCol);
                QString leaveVar = getStepVarName(step.currentBasicVars[step.pivotRow]);

                result += "Mô tả phép xoay: Chọn " + enterVar + " làm biến vào, đẩy " + leaveVar + " ra khỏi cơ sở.\n";
                result += "Biến vào: " + enterVar + "\n";
                result += "Biến ra: " + leaveVar + "\n";
                result += "Dòng trụ: " + QString::number(step.pivotRow + 1) + "\n";
                result += "Cột trụ: " + enterVar + "\n";
            } else {
                result += "Mô tả phép xoay: Không có phép xoay ở bước này.\n";
            }

            result += "Từ vựng / hệ phương trình tại bước này:\n";
            result += buildVocabularyEquation(step, m, isPhase1Step) + "\n";
            for (int row = 0; row < m; ++row) {
                result += buildVocabularyEquation(step, row, isPhase1Step) + "\n";
            }
        }

        isPhase1Step = false;
        for (const SimplexStep& step : currentHistory) {
            if (step.stepName.contains("Pha 1", Qt::CaseInsensitive) ||
                step.stepName.contains("Pha 2", Qt::CaseInsensitive)) {
                isPhase1Step = true;
                break;
            }
        }

        result += "\n6) CÁC BƯỚC THỰC THI - DẠNG BẢNG ĐƠN HÌNH\n";
        result += "Lưu ý: Đây là dữ liệu từ tab HIỂN THỊ DẠNG BẢNG trong cửa sổ kết quả.\n";

        for (size_t stepIdx = 0; stepIdx < currentHistory.size(); ++stepIdx) {
            const SimplexStep& step = currentHistory[stepIdx];

            if (step.stepName.contains("Pha 2", Qt::CaseInsensitive)) {
                isPhase1Step = false;
            }

            result += "\n--- " + step.stepName + " ---\n";
            result += buildSimplexTableText(step, isPhase1Step);
        }

        return result;
    };

    QString optType = lp.isMaximize ? "Max" : "Min";
    QString contextString;

    contextString += "THÔNG TIN BÀI TOÁN QUY HOẠCH TUYẾN TÍNH\n";
    contextString += "Loại bài toán: Tìm " + optType + " Z\n";
    contextString += "Số biến: " + QString::number(lp.c.size()) + "\n";
    contextString += "Số ràng buộc chính: " + QString::number(lp.A.size()) + "\n\n";

    contextString += "1) HÀM MỤC TIÊU\n";
    contextString += optType + " Z = ";
    if (std::abs(lp.c_0) > 1e-9) {
        contextString += formatNumber(lp.c_0);
        if (!lp.c.empty()) contextString += " + ";
    }
    contextString += buildLinearExpression(lp.c, true) + "\n\n";

    contextString += "2) HỆ RÀNG BUỘC CHÍNH\n";
    contextString += "Lưu ý: Mỗi ràng buộc dưới đây có đầy đủ vế trái, dấu ràng buộc và vế phải. Không được bỏ qua dấu ≤, ≥ hoặc = khi giải thích.\n";

    for (size_t i = 0; i < lp.A.size(); ++i) {
        QString sign = (i < lp.signs.size()) ? normalizeSign(lp.signs[i]) : "?";
        QString rhs  = (i < lp.b.size()) ? formatNumber(lp.b[i]) : "?";

        contextString += QString("R%1: %2 %3 %4\n")
                             .arg((int)i + 1)
                             .arg(buildLinearExpression(lp.A[i], true))
                             .arg(sign)
                             .arg(rhs);
    }

    contextString += "\nDanh sách dấu ràng buộc theo từng dòng:\n";
    for (size_t i = 0; i < lp.A.size(); ++i) {
        QString sign = (i < lp.signs.size()) ? normalizeSign(lp.signs[i]) : "?";
        contextString += QString("R%1 có dấu %2\n").arg((int)i + 1).arg(sign);
    }

    contextString += "\n3) RÀNG BUỘC DẤU CỦA BIẾN\n";
    for (size_t i = 0; i < lp.varBounds.size(); ++i) {
        QString varName = "x" + QString::number((int)i + 1);

        if (lp.varBounds[i].isFree || lp.varBounds[i].sign == "free") {
            contextString += varName + " là biến tự do, không bị ràng buộc dấu\n";
        } else {
            contextString += varName + " " + normalizeSign(lp.varBounds[i].sign) + " " + formatNumber(lp.varBounds[i].value) + "\n";
        }
    }

    contextString += "\n4) KẾT QUẢ PHẦN MỀM ĐÃ GIẢI RA\n";
    contextString += "Trạng thái / giá trị hiển thị tại ô kết quả Z: " + ui->lineEdit_Z->text() + "\n";

    if (!currentSolution.empty()) {
        contextString += "Nghiệm tối ưu đang hiển thị:\n";
        for (size_t i = 0; i < currentSolution.size(); ++i) {
            contextString += QString("x%1 = %2\n").arg((int)i + 1).arg(formatNumber(currentSolution[i]));
        }
    }

    if (!currentAltSolution.empty()) {
        contextString += "Nghiệm tối ưu khác / nghiệm phụ nếu có:\n";
        for (size_t i = 0; i < currentAltSolution.size(); ++i) {
            contextString += QString("x%1 = %2\n").arg((int)i + 1).arg(formatNumber(currentAltSolution[i]));
        }
    }

    if (ui->table_solution->rowCount() > 0 && ui->table_solution->columnCount() > 0) {
        contextString += "\nBảng nghiệm đang hiển thị trong cửa sổ kết quả:\n";

        QStringList tableHeaders;
        for (int col = 0; col < ui->table_solution->columnCount(); ++col) {
            QTableWidgetItem *header = ui->table_solution->horizontalHeaderItem(col);
            tableHeaders << (header ? header->text() : QString("Cột %1").arg(col + 1));
        }
        contextString += tableHeaders.join(" | ") + "\n";

        for (int row = 0; row < ui->table_solution->rowCount(); ++row) {
            QStringList rowValues;
            for (int col = 0; col < ui->table_solution->columnCount(); ++col) {
                QTableWidgetItem *item = ui->table_solution->item(row, col);
                rowValues << (item ? item->text() : "");
            }
            contextString += rowValues.join(" | ") + "\n";
        }
    }

    contextString += buildAllExecutionStepsForChatbot();

    contextString += "\n--- HƯỚNG DẪN DÀNH CHO BẠN (TRỢ LÝ AI) ---\n";
    contextString += "Bạn là một AI hỗ trợ giải đáp toán học môn Quy hoạch tuyến tính. Bạn CHỈ được phép giải thích hoặc trả lời các câu hỏi liên quan đến nội dung bài toán quy hoạch tuyến tính ở trên, phương pháp giải, bảng đơn hình, từ vựng, hoặc các khái niệm toán học liên quan.\n";
    contextString += "Khi người dùng hỏi về 'từ vựng', 'bảng', 'bảng đơn hình', 'các bước thực thi', 'biến vào', 'biến ra', 'dòng trụ', 'cột trụ', hãy ưu tiên dùng dữ liệu trong mục 5 và mục 6 của context.\n";
    contextString += "Khi giải thích hệ ràng buộc, bắt buộc phải dùng đúng dấu ràng buộc đã cung cấp trong từng dòng R1, R2, ... Không được tự đổi chiều dấu và không được bỏ mất vế phải.\n";
    contextString += "Nếu người dùng hỏi về nghiệm tối ưu hoặc giá trị tối ưu, hãy đối chiếu cả mục 4, bảng nghiệm đang hiển thị, và các bước thực thi trước khi trả lời.\n";
    contextString += "QUAN TRỌNG: Nếu người dùng hỏi bất kỳ câu hỏi ngoài lề (không thuộc phạm vi của bài toán hoặc quy hoạch tuyến tính), bạn KHÔNG ĐƯỢC trả lời nội dung đó. Bạn BẮT BUỘC phải trả lời chính xác bằng câu sau và không giải thích gì thêm:\n\"Xin lỗi câu hỏi của bạn không thuộc phạm vi của bài toán\"";

    if (!this->wd_ChatBot) this->wd_ChatBot = new WdChatBot(this);

    this->wd_ChatBot->setProblemContext(contextString);
    this->wd_ChatBot->show();
    this->wd_ChatBot->raise();
    this->wd_ChatBot->activateWindow();
}
