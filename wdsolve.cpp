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
#include <cmath>

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
    // HÀM FORMAT SỐ CHUNG: Làm tròn 2 chữ số thập phân, nếu là số nguyên thì bỏ .00
    // =======================================================================
    auto formatVal = [](double val) -> QString {
        if (std::abs(val) < 1e-9) return "0";
        if (std::abs(val - std::round(val)) < 1e-9) return QString::number(std::round(val));
        return QString::number(val, 'f', 2);
    };

    auto formatCoeff = [](double val) -> QString {
        double v = std::abs(val);
        if (std::abs(v - std::round(v)) < 1e-9) return QString::number(std::round(v));
        return QString::number(v, 'f', 2);
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
            v.replace("_{", "<sub>").replace("}", "</sub>").replace("^+", "<sup>+</sup>").replace("^-", "<sup>-</sup>");
            return v;
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

        struct TermInfo { bool isPos; double coeff; QString varHtml; QString varTex; };

        // ==========================================
        // 1. TẠO CHUỖI HTML ĐỂ RENDER PDF BÁO CÁO
        // ==========================================
        QString html = "<html><head><style>"
                       "body { font-family: 'Times New Roman', serif; font-size: 14pt; color: black; background: white; line-height: 1.5; padding: 20px; }"
                       "h2 { text-align: center; color: #000000; text-transform: uppercase; margin-bottom: 30px; }"
                       "h3 { text-align: left; color: #000000; margin-top: 30px; margin-bottom: 10px; }"
                       "td { padding: 4px 2px; }" // Giảm padding để bảng gọn gàng hơn
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
        if (isFirstObj) html += "0";
        html += "</div>";

        html += "<p style='text-align: left; margin-bottom: 5px;'><b>Hệ ràng buộc:</b></p>";
        html += "<div style='text-align: center; width: 100%; margin-bottom: 15px;'>";
        html += "<table cellspacing='0' cellpadding='0' style='margin: 0 auto; border: none; font-size: 15pt;'>";

        for (size_t i = 0; i < currentOriginalLp.A.size(); ++i) {
            html += "<tr>";
            bool isFirstTerm = true;
            for (size_t j = 0; j < currentOriginalLp.A[i].size(); ++j) {
                double val = currentOriginalLp.A[i][j];
                if (std::abs(val) < 1e-9) {
                    html += "<td width='20'></td><td width='40'></td><td width='25'></td>";
                } else {
                    QString sign = (val > 0) ? (isFirstTerm ? "" : "+") : "-";
                    bool isOne = (std::abs(std::abs(val) - 1.0) < 1e-9);
                    QString coeffStr = isOne ? "" : formatCoeff(val);
                    html += "<td width='20' align='center'>" + sign + "</td>";
                    html += "<td width='40' align='right'>" + coeffStr + "</td>";
                    html += "<td width='25' align='left'>x<sub>" + QString::number(j + 1) + "</sub></td>";
                    isFirstTerm = false;
                }
            }
            if (isFirstTerm) html += "<td width='20'></td><td width='40' align='right'>0</td><td width='25'></td>";

            QString signHtml = currentOriginalLp.signs[i];
            if (signHtml == "<=") signHtml = "&le;"; else if (signHtml == ">=") signHtml = "&ge;";
            html += "<td width='35' align='center'>" + signHtml + "</td>";
            html += "<td width='45' align='left'>" + formatVal(currentOriginalLp.b[i]) + "</td></tr>";
        }

        for (size_t i = 0; i < currentOriginalLp.varBounds.size(); ++i) {
            html += "<tr>";
            for (size_t j = 0; j < currentOriginalLp.A[0].size(); ++j) {
                if (i == j) {
                    html += "<td width='20'></td><td width='40'></td><td width='25' align='left'>x<sub>" + QString::number(j + 1) + "</sub></td>";
                } else {
                    html += "<td width='20'></td><td width='40'></td><td width='25'></td>";
                }
            }

            if (currentOriginalLp.varBounds[i].isFree || currentOriginalLp.varBounds[i].sign == "free") {
                html += "<td colspan='2' align='left' style='padding-left: 10px;'>&isin; &real;</td></tr>";
            } else {
                QString s = currentOriginalLp.varBounds[i].sign;
                if (s == "<=") s = "&le;"; else if (s == ">=") s = "&ge;";
                html += "<td width='35' align='center'>" + s + "</td>";
                html += "<td width='45' align='left'>" + formatVal(currentOriginalLp.varBounds[i].value) + "</td></tr>";
            }
        }
        html += "</table></div>";

        // --- CÁC BƯỚC GIẢI DẠNG TỪ VỰNG HTML ---
        html += "<h3>2. CÁC BƯỚC GIẢI (DẠNG TỪ VỰNG)</h3>";
        for (size_t stepIdx = 0; stepIdx < currentHistory.size(); ++stepIdx) {
            const SimplexStep& step = currentHistory[stepIdx];
            int m = step.matrix.size() - 1;
            int n = step.matrix[0].size() - 1;
            bool isPhase1Loc = globalIsPhase1 && ((int)stepIdx <= lastPhase1StepIdx);

            html += "<p style='text-align: left; margin-top: 25px;'><b>" + step.stepName + "</b></p>";

            // [FIX MỚI] Dùng Table cứng cột để đảm bảo căn dọc hoàn hảo
            html += "<div style='text-align: center; width: 100%;'>";
            html += "<table cellspacing='0' cellpadding='0' style='margin: 0 auto; border: none; font-size: 15pt;'>";

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
                html += "<td width='40' align='right' style='border: none;'><b>" + lhsVar + "</b></td>";
                html += "<td width='25' align='center' style='border: none;'> = </td>";

                double rhsVal = isZRow ? -step.matrix[m][n] : step.matrix[i][n];
                if (std::abs(rhsVal) < 1e-9) rhsVal = 0.0;

                bool hasVarTerms = false;
                for (int j : nonBasicVars) {
                    double coeff = -step.matrix[isZRow ? m : i][j];
                    if (isZRow && stepIdx == 0 && isPhase1Loc && (varNames[j] == "x_0" || varNames[j] == "x0")) coeff = 1.0;
                    if (std::abs(coeff) >= 1e-9) hasVarTerms = true;
                }

                bool hasConst = std::abs(rhsVal) >= 1e-9 || !hasVarTerms;
                QString constStr = hasConst ? formatVal(rhsVal) : "";
                html += "<td width='55' align='right' style='border: none;'>" + constStr + "</td>";

                bool isFirstRhsTerm = !hasConst;

                for (int j : nonBasicVars) {
                    double coeff = -step.matrix[isZRow ? m : i][j];
                    if (isZRow && stepIdx == 0 && isPhase1Loc && (varNames[j] == "x_0" || varNames[j] == "x0")) coeff = 1.0;

                    if (std::abs(coeff) < 1e-9) {
                        html += "<td width='20' style='border:none;'></td><td width='45' style='border:none;'></td><td width='30' style='border:none;'></td>";
                    } else {
                        QString sign = "";
                        if (coeff > 0) sign = isFirstRhsTerm ? "" : "+";
                        else sign = "-";
                        isFirstRhsTerm = false;

                        bool isOne = (std::abs(std::abs(coeff) - 1.0) < 1e-9);
                        QString coeffStr = isOne ? "" : formatCoeff(coeff);
                        QString varName = getVarNameHtml(j);

                        html += "<td width='20' align='center' style='border: none;'>" + sign + "</td>";
                        html += "<td width='45' align='right' style='border: none;'>" + coeffStr + "</td>";
                        html += "<td width='30' align='left' style='border: none; padding-left: 2px;'>" + varName + "</td>";
                    }
                }
                html += "</tr>";
            }
            html += "</table></div>";

            if (globalIsPhase1 && (int)stepIdx == lastPhase1StepIdx) {
                double xi_val = -step.matrix[m][n];
                if (std::abs(xi_val) < 1e-9) {
                    html += "<p style='text-align: left; font-style: italic; color: #0056b3; font-size: 13pt;'>* <b>Giải thích:</b> Để suy ra nghiệm, ta cho tất cả các biến không cơ sở (các biến nằm ở vế phải) bằng 0, khi đó các biến cơ sở (vế trái) sẽ nhận giá trị bằng đúng hằng số tự do của phương trình tương ứng. Vì giá trị tối ưu &xi; = 0, bài toán gốc có nghiệm khả thi. Thuật toán sẽ tiếp tục <b>chuyển sang Pha 2</b>.</p>";
                } else {
                    html += "<p style='text-align: left; font-style: italic; color: #d9534f; font-size: 13pt;'>* <b>Giải thích:</b> Để suy ra nghiệm, ta cho tất cả các biến không cơ sở (các biến nằm ở vế phải) bằng 0, khi đó các biến cơ sở (vế trái) sẽ nhận giá trị bằng đúng hằng số tự do của phương trình tương ứng. Vì giá trị tối ưu &xi; &ne; 0, hệ ràng buộc của bài toán gốc mâu thuẫn nhau nên <b>bài toán vô nghiệm</b>.</p>";
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
        if (!zText.contains("Vô nghiệm", Qt::CaseInsensitive) && !zText.contains("Không giới nội", Qt::CaseInsensitive) && !zText.contains("Lỗi", Qt::CaseInsensitive)) {
            html += "<p style='text-align: left; margin-bottom: 5px;'><b>Giá trị tối ưu:</b> Z<sup>*</sup> = " + formatVal(z_opt) + "</p>";
            html += "<p style='text-align: left; margin-bottom: 5px;'><b>Nghiệm tối ưu:</b> " + varListHtml + " = " + optSolHtml + "</p>";
        }
        html += "</body></html>";

        // ==========================================
        // 2. TẠO CHUỖI LATEX CODE ĐỂ XUẤT FILE .TEX
        // ==========================================
        QString tex = "\\documentclass[12pt,a4paper]{article}\n";
        tex += "\\usepackage[utf8]{inputenc}\n";
        tex += "\\usepackage[T5]{fontenc}\n";
        tex += "\\usepackage{amsmath, geometry, array, amssymb}\n";
        tex += "\\geometry{margin=1in}\n";
        tex += "\\begin{document}\n\n";
        tex += "\\begin{center}\n\\Large\\textbf{GIẢI BÀI TOÁN QUY HOẠCH TUYẾN TÍNH}\n\\end{center}\n\\vspace{0.5cm}\n\n";

        tex += "\\section*{1. Phát biểu bài toán}\n";
        tex += "\\noindent\\textbf{Hàm mục tiêu:}\n";
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
        if (isFirstTexObj) tex += "0";
        tex += " $\n\\end{center}\n\n";

        tex += "\\noindent\\textbf{Hệ ràng buộc (Bao gồm ràng buộc dấu):}\n";

        QString colsFormat = "";
        for (size_t j = 0; j < currentOriginalLp.c.size(); ++j) colsFormat += "c r l ";
        colsFormat += "c l";

        tex += "\\begin{center}\n\\[ \\begin{array}{" + colsFormat + "}\n";

        for (size_t i = 0; i < currentOriginalLp.A.size(); ++i) {
            bool isFirstTerm = true;
            for (size_t j = 0; j < currentOriginalLp.A[i].size(); ++j) {
                double val = currentOriginalLp.A[i][j];
                if (std::abs(val) < 1e-9) {
                    tex += " & & & ";
                } else {
                    QString sign = (val > 0) ? (isFirstTerm ? "" : "+") : "-";
                    bool isOne = (std::abs(std::abs(val) - 1.0) < 1e-9);
                    QString coeffStr = isOne ? "" : formatCoeff(val);
                    tex += sign + " & " + coeffStr + " & x_{" + QString::number(j + 1) + "} & ";
                    isFirstTerm = false;
                }
            }
            if (isFirstTerm) tex += " & 0 & & ";

            QString s = currentOriginalLp.signs[i];
            if (s == "<=") s = "\\le";
            else if (s == ">=") s = "\\ge";
            tex += s + " & " + formatVal(currentOriginalLp.b[i]) + " \\\\\n";
        }
        for (size_t i = 0; i < currentOriginalLp.varBounds.size(); ++i) {
            for (size_t j = 0; j < currentOriginalLp.c.size(); ++j) {
                if (i == j) {
                    tex += " & & x_{" + QString::number(j + 1) + "} & ";
                } else {
                    tex += " & & & ";
                }
            }
            if (currentOriginalLp.varBounds[i].isFree || currentOriginalLp.varBounds[i].sign == "free") {
                tex += "\\multicolumn{2}{l}{\\in \\mathbb{R}} \\\\\n";
            } else {
                QString s = currentOriginalLp.varBounds[i].sign;
                if (s == "<=") s = "\\le"; else if (s == ">=") s = "\\ge";
                tex += s + " & " + formatVal(currentOriginalLp.varBounds[i].value) + " \\\\\n";
            }
        }
        tex += "\\end{array} \\]\n\\end{center}\n\n";

        // --- CÁC BƯỚC GIẢI DẠNG TỪ VỰNG LATEX ---
        tex += "\\section*{2. Các bước giải (Dạng từ vựng)}\n";
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

            tex += "\\vspace{0.3cm}\n\\noindent \\textbf{" + step.stepName + "}\n\n";
            // Lưới cột LaTeX: Right (LHS) | Center (=) | Right (Const) | [Center (Sign) | Right (Coeff) | Left (Var)]...
            tex += "\\begin{center}\n\\[\n\\begin{array}{r c r " + QString("c r l ").repeated(nonBasicVars.size()) + "}\n";

            std::vector<int> rowOrder;
            rowOrder.push_back(m);
            for (int i = 0; i < m; ++i) rowOrder.push_back(i);

            bool isFirstConstraintRowTex = true;
            for (int i : rowOrder) {
                bool isZRow = (i == m);
                if (!isZRow && isFirstConstraintRowTex) {
                    tex += "\\hline\n";
                    isFirstConstraintRowTex = false;
                }

                QString lhsVar = isZRow ? (isPhase1Loc ? "\\xi" : (currentOriginalLp.isMaximize ? "-Z" : "Z")) : varNames[step.currentBasicVars[i]];
                tex += lhsVar + " & = & ";

                double rhsVal = isZRow ? -step.matrix[m][n] : step.matrix[i][n];
                if (std::abs(rhsVal) < 1e-9) rhsVal = 0.0;

                bool hasVarTerms = false;
                for (int j : nonBasicVars) {
                    double coeff = -step.matrix[isZRow ? m : i][j];
                    if (isZRow && stepIdx == 0 && isPhase1Loc && (varNames[j] == "x_0" || varNames[j] == "x0")) coeff = 1.0;
                    if (std::abs(coeff) >= 1e-9) hasVarTerms = true;
                }

                bool hasConst = std::abs(rhsVal) >= 1e-9 || !hasVarTerms;
                QString constTex = hasConst ? formatVal(rhsVal) : "";
                tex += constTex;

                bool isFirstRhsTerm = !hasConst;

                for (int j : nonBasicVars) {
                    double coeff = -step.matrix[isZRow ? m : i][j];
                    if (isZRow && stepIdx == 0 && isPhase1Loc && (varNames[j] == "x_0" || varNames[j] == "x0")) coeff = 1.0;

                    if (std::abs(coeff) < 1e-9) {
                        tex += " & & & ";
                    } else {
                        QString sign = "";
                        if (coeff > 0) sign = isFirstRhsTerm ? "" : "+";
                        else sign = "-";
                        isFirstRhsTerm = false;

                        bool isOne = (std::abs(std::abs(coeff) - 1.0) < 1e-9);
                        QString cStr = isOne ? "" : formatCoeff(coeff);
                        tex += " & " + sign + " & " + cStr + " & " + varNames[j];
                    }
                }
                tex += " \\\\\n";
            }
            tex += "\\end{array}\n\\]\n\\end{center}\n";

            if (globalIsPhase1 && (int)stepIdx == lastPhase1StepIdx) {
                double xi_val = -step.matrix[m][n];
                if (std::abs(xi_val) < 1e-9) {
                    tex += "\\vspace{0.2cm}\n\\noindent\\textit{* \\textbf{Giải thích:} Để suy ra nghiệm, ta cho tất cả các biến không cơ sở (các biến nằm ở vế phải) bằng 0, khi đó các biến cơ sở (vế trái) sẽ nhận giá trị bằng đúng hằng số tự do của phương trình tương ứng. Vì giá trị tối ưu $\\xi = 0$, bài toán gốc có nghiệm khả thi. Thuật toán sẽ tiếp tục \\textbf{chuyển sang Pha 2}.}\n\n";
                } else {
                    tex += "\\vspace{0.2cm}\n\\noindent\\textit{* \\textbf{Giải thích:} Để suy ra nghiệm, ta cho tất cả các biến không cơ sở (các biến nằm ở vế phải) bằng 0, khi đó các biến cơ sở (vế trái) sẽ nhận giá trị bằng đúng hằng số tự do của phương trình tương ứng. Vì giá trị tối ưu $\\xi \\neq 0$, hệ ràng buộc của bài toán gốc mâu thuẫn nhau nên \\textbf{bài toán vô nghiệm}.}\n\n";
                }
            }
        }

        tex += "\\section*{3. Kết luận}\n";
        tex += "\\noindent\\textbf{Trạng thái:} " + reportStatus + "\\\\[0.2cm]\n";
        if (!zText.contains("Vô nghiệm", Qt::CaseInsensitive) && !zText.contains("Không giới nội", Qt::CaseInsensitive) && !zText.contains("Lỗi", Qt::CaseInsensitive)) {
            tex += "\\noindent\\textbf{Giá trị tối ưu:} $Z^* = " + formatVal(z_opt) + "$\\\\[0.2cm]\n";
            tex += "\\noindent\\textbf{Nghiệm tối ưu:} $" + varListTex + " = " + optSolTex + "$\n\n";
        }
        tex += "\\end{document}";

        QDialog *previewDialog = new QDialog(this);
        previewDialog->setWindowTitle("Xem trước Báo cáo PDF");
        previewDialog->resize(950, 750);
        QVBoxLayout *dlgLayout = new QVBoxLayout(previewDialog);

        QSettings settings(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
        bool isDark = settings.value("dark_mode", false).toBool();
        if (isDark) {
            previewDialog->setStyleSheet("QDialog { background-color: #1E1E2E; } QPushButton { background-color: #313244; color: #CDD6F4; border: 1px solid #45475A; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #45475A; }");
        } else {
            previewDialog->setStyleSheet("QDialog { background-color: #F5F7FA; } QPushButton { background-color: #FFFFFF; color: #333333; border: 1px solid #CCCCCC; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #E8E8E8; }");
        }

        QTextBrowser *previewBrowser = new QTextBrowser(previewDialog);
        previewBrowser->setStyleSheet("QTextBrowser { background-color: #FFFFFF; color: #000000; padding: 20px; border-radius: 4px; border: 1px solid gray;}");
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

        connect(btnDownloadPdf, &QPushButton::clicked, previewDialog, [previewDialog, previewBrowser]() {
            QString fileName = QFileDialog::getSaveFileName(previewDialog, "Lưu file PDF", "BaoCao_QHTT.pdf", "PDF Files (*.pdf)");
            if (!fileName.isEmpty()) {
                QPdfWriter pdfWriter(fileName);
                pdfWriter.setPageSize(QPageSize(QPageSize::A4));
                pdfWriter.setResolution(300);
                QMarginsF margins(15, 15, 15, 15);
                pdfWriter.setPageMargins(margins, QPageLayout::Millimeter);

                previewBrowser->document()->print(&pdfWriter);
                QMessageBox::information(previewDialog, "Thành công", "Đã lưu thành công file PDF!");
            }
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

    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QSettings settings(dataDir + "/settings.ini", QSettings::IniFormat);
    bool isDark = settings.value("dark_mode", false).toBool();

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

    int lastPhase1StepIdx = -1;
    if (globalIsPhase1) {
        lastPhase1StepIdx = modHistory.size() - 1;
        for (size_t k = 0; k < modHistory.size(); ++k) {
            if (modHistory[k].stepName.contains("Pha 2", Qt::CaseInsensitive)) {
                lastPhase1StepIdx = k - 1;
                break;
            }
        }
    }

    auto formatValUI = [](double val) -> QString {
        if (std::abs(val) < 1e-9) return "0";
        if (std::abs(val - std::round(val)) < 1e-9) return QString::number(std::round(val));
        return QString::number(val, 'f', 2);
    };

    auto formatCoeffUI = [](double val) -> QString {
        double v = std::abs(val);
        if (std::abs(v - std::round(v)) < 1e-9) return QString::number(std::round(v));
        return QString::number(v, 'f', 2);
    };

    QFont fontZ = ui->lineEdit_Z->font();
    fontZ.setBold(true);
    fontZ.setPointSize(12);
    ui->lineEdit_Z->setFont(fontZ);

    ui->table_solution->horizontalHeader()->setStyleSheet("");

    if (status == "Tối ưu" || status == "Vô số nghiệm") {
        double finalZ = optimalZ;
        if (status == "Vô số nghiệm")
            ui->lineEdit_Z->setText(formatValUI(finalZ) + " (Vô số nghiệm)");
        else
            ui->lineEdit_Z->setText(formatValUI(finalZ));

        int origN = (int)originalLp.varBounds.size();
        bool isInfinite = (status == "Vô số nghiệm");
        int colCount = isInfinite ? 4 : 2;

        ui->table_solution->setRowCount(origN);
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

            QTableWidgetItem *itemVal1 = new QTableWidgetItem(formatValUI(val1));
            itemVal1->setTextAlignment(Qt::AlignCenter);
            ui->table_solution->setItem(i, 1, itemVal1);

            if (isInfinite) {
                double val2 = (i < (int)altSolution.size()) ? altSolution[i] : 0.0;
                QTableWidgetItem *itemVal2 = new QTableWidgetItem(formatValUI(val2));
                itemVal2->setTextAlignment(Qt::AlignCenter);
                ui->table_solution->setItem(i, 2, itemVal2);

                double delta = val2 - val1;
                if (std::abs(delta) < 1e-9) delta = 0.0;
                QTableWidgetItem *itemDelta = new QTableWidgetItem(formatValUI(delta));
                itemDelta->setTextAlignment(Qt::AlignCenter);

                itemDelta->setBackground(QColor(isDark ? "#313244" : "#F0F8FF"));
                ui->table_solution->setItem(i, 3, itemDelta);
            }
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
                        eqStr = formatValUI(rhsVal);
                        hasTerms = true;
                    }

                    for (int j : nonBasicVars) {
                        if (j < (int)varNames.size() && varNames[j] == "x0") continue;
                        double coeff = -lastStep.matrix[rowIdx][j];
                        if (std::abs(coeff) >= 1e-9) {
                            if (hasTerms) {
                                if (coeff > 0) eqStr += QString(" + %1 %2").arg(formatCoeffUI(coeff), varNames[j]);
                                else eqStr += QString(" - %1 %2").arg(formatCoeffUI(std::abs(coeff)), varNames[j]);
                            } else {
                                if (coeff > 0) eqStr = QString("%1 %2").arg(formatCoeffUI(coeff), varNames[j]);
                                else eqStr = QString("-%1 %2").arg(formatCoeffUI(std::abs(coeff)), varNames[j]);
                                hasTerms = true;
                            }
                        }
                    }

                    if (!hasTerms) {
                        eqStr = formatValUI(rhsVal);
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
                        eqStr = formatValUI(rhsVal);
                        hasTerms = true;
                    }

                    for (int j : nonBasicVars) {
                        if (j < (int)varNames.size() && varNames[j] == "x0") continue;
                        double coeff = -lastStep.matrix[rowIdx][j];
                        if (std::abs(coeff) >= 1e-9) {
                            if (hasTerms) {
                                if (coeff > 0) eqStr += QString(" + %1 %2").arg(formatCoeffUI(coeff), varNames[j]);
                                else eqStr += QString(" - %1 %2").arg(formatCoeffUI(std::abs(coeff)), varNames[j]);
                            } else {
                                if (coeff > 0) eqStr = QString("%1 %2").arg(formatCoeffUI(coeff), varNames[j]);
                                else eqStr = QString("-%1 %2").arg(formatCoeffUI(std::abs(coeff)), varNames[j]);
                                hasTerms = true;
                            }
                        }
                    }
                    if (!hasTerms) eqStr = formatValUI(rhsVal);
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

    ui->tabWidget_steps->clear();

    QTabWidget *vocabTabWidget = new QTabWidget();
    QTabWidget *tableTabWidget = new QTabWidget();

    QString bgTab = isDark ? "#181825" : "#FAFAFA";
    QString borderColor = isDark ? "#45475A" : "#a0a0a0";
    QString tabUnselectedBg = isDark ? "#313244" : "#e6e6e6";
    QString tabSelectedColor = isDark ? "#89B4FA" : "#0056b3";
    QString textColor = isDark ? "#CDD6F4" : "#333333";

    QString tabStyle = QString(
                           "QTabWidget::pane { border: 1px solid %1; background-color: %2; top: -1px; } "
                           "QTabBar::tab { color: %3; padding: 8px 15px; font-weight: bold; border: 1px solid %1; border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 2px; background-color: %4; } "
                           "QTabBar::tab:selected { color: %5; background-color: %2; border-top: 2px solid %5; border-bottom-color: %2; } "
                           "QTabBar::tab:!selected { margin-top: 2px; }"
                           ).arg(borderColor, bgTab, textColor, tabUnselectedBg, tabSelectedColor);

    ui->tabWidget_steps->setStyleSheet(tabStyle);
    vocabTabWidget->setStyleSheet(tabStyle);
    tableTabWidget->setStyleSheet(tabStyle);

    for (size_t stepIdx = 0; stepIdx < modHistory.size(); ++stepIdx) {
        const SimplexStep& step = modHistory[stepIdx];
        int m = (int)step.matrix.size() - 1;
        int n = (int)step.matrix[0].size() - 1;

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

        if (step.pivotCol >= 0 && step.pivotRow >= 0) {
            QString enterVar = (step.pivotCol < (int)varNames.size()) ? varNames[step.pivotCol] : "?";
            QString leaveVar = (step.currentBasicVars[step.pivotRow] != -1 && step.currentBasicVars[step.pivotRow] < (int)varNames.size()) ? varNames[step.currentBasicVars[step.pivotRow]] : "?";

            if (stepIdx == 0 && globalIsPhase1) {
                commonIntroHtml += QString("<p style='color: #333333; font-size: 12pt; margin-bottom: 20px; font-style: italic;'>&rarr; <b>Phép xoay đặc biệt:</b> Đưa biến phụ <b><font color='green'>%1</font></b> vào cơ sở để thay thế <b><font color='red'>%2</font></b> nhằm làm vế phải dương.</p>").arg(enterVar).arg(leaveVar);
            } else {
                if (status.contains("Vô số nghiệm", Qt::CaseInsensitive) && stepIdx == modHistory.size() - 2) {
                    commonIntroHtml += QString("<p style='color: #333333; font-size: 12pt; margin-bottom: 4px; margin-top: 8px; font-style: italic;'>&rarr; Chọn <b><font color='green'>%1</font></b> làm biến vào, đẩy <b><font color='red'>%2</font></b> ra khỏi cơ sở.</p>").arg(enterVar).arg(leaveVar);
                    commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 4px; margin-top: 0px;'>&rarr; <b>Kết luận:</b> Đã đạt [TU_VUNG_BANG] tối ưu (có vô số nghiệm).</p>";
                    commonIntroHtml += "<p style='color: #0056b3; font-size: 11.5pt; margin-bottom: 15px; margin-top: 0px; font-style: italic;'>";
                    commonIntroHtml += "<b>* Giải thích vô số nghiệm:</b> vì tồn tại hệ số của biến không cơ sở ở hàm mục tiêu bằng 0 ở từ vựng tối ưu nên bài toán có vô số nghiệm tối ưu.<br>";
                    commonIntroHtml += "<b>* Giải thích cách đọc nghiệm:</b> [READ_SOLUTION]<br>";
                    commonIntroHtml += "<b>* Giải thích giá trị tối ưu:</b> [OPT_Z]<br>";
                    commonIntroHtml += "<b>* Lưu ý:</b> Bước xoay tiếp theo chỉ để tìm tọa độ tối ưu thứ 2.</p>";
                } else {
                    commonIntroHtml += QString("<p style='color: #333333; font-size: 12pt; margin-bottom: 20px; font-style: italic;'>&rarr; Chọn <b><font color='green'>%1</font></b> làm biến vào, đẩy <b><font color='red'>%2</font></b> ra khỏi cơ sở.</p>").arg(enterVar).arg(leaveVar);
                }
            }
        } else {
            bool isLastStep = (stepIdx == modHistory.size() - 1);

            if (globalIsPhase1 && (int)stepIdx == lastPhase1StepIdx) {
                double xi_val = -step.matrix[m][n];
                if (std::abs(xi_val) < 1e-9) {
                    commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 5px;'>&rarr; <b>Kết luận:</b> Đã đạt [TU_VUNG_BANG] tối ưu của Pha 1.</p>";
                    commonIntroHtml += "<p style='color: #0056b3; font-size: 11.5pt; margin-bottom: 20px; font-style: italic;'>";
                    commonIntroHtml += "<b>* Giải thích:</b> [READ_SOLUTION]<br>";
                    commonIntroHtml += "Vì giá trị tối ưu <b>&xi; = 0</b>, bài toán gốc có nghiệm khả thi. Thuật toán sẽ tiếp tục <b>chuyển sang Pha 2</b>.</p>";
                } else {
                    commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 5px;'>&rarr; <b>Kết luận:</b> Đã đạt [TU_VUNG_BANG] tối ưu của Pha 1.</p>";
                    commonIntroHtml += "<p style='color: #0056b3; font-size: 11.5pt; margin-bottom: 20px; font-style: italic;'>";
                    commonIntroHtml += "<b>* Giải thích:</b> [READ_SOLUTION]<br>";
                    commonIntroHtml += "Vì giá trị tối ưu <b>&xi; &ne; 0</b> (khác 0), hệ ràng buộc của bài toán gốc mâu thuẫn nhau nên <b>không có nghiệm tối ưu cho bài toán</b>.</p>";
                }
            } else if (isLastStep) {
                if (status.contains("xoay vòng", Qt::CaseInsensitive) || status.contains("Cycling", Qt::CaseInsensitive)) {
                    commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 20px;'>&rarr; <b>Kết luận:</b> Phát hiện hiện tượng xoay vòng (Cycling). Dừng thuật toán.</p>";
                } else if (status.contains("giới nội", Qt::CaseInsensitive)) {
                    commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 5px;'>&rarr; <b>Kết luận:</b> Bài toán không giới nội. Dừng thuật toán.</p>";
                    commonIntroHtml += "<p style='color: #0056b3; font-size: 11.5pt; margin-bottom: 20px; font-style: italic;'>";
                    commonIntroHtml += "<b>* Giải thích không giới nội:</b> các hệ số đứng trước biến ứng với hệ số âm nhất ở hàm mục tiêu trong các phương trình ở hệ ràng buộc đều dương.<br>";
                    if (originalLp.isMaximize) commonIntroHtml += "<b>* Giải thích:</b> Hàm mục tiêu có thể tăng lên vô hạn mà không vi phạm các ràng buộc. Do đó, giá trị tối ưu của bài toán Max Z là <b>+&infin;</b>.</p>";
                    else commonIntroHtml += "<b>* Giải thích:</b> Hàm mục tiêu có thể giảm xuống vô hạn mà không vi phạm các ràng buộc. Do đó, giá trị tối ưu của bài toán Min Z là <b>-&infin;</b>.</p>";
                } else if (status.contains("Vô nghiệm", Qt::CaseInsensitive)) {
                    commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 20px;'>&rarr; <b>Kết luận:</b> Không có nghiệm tối ưu (Bài toán vô nghiệm). Dừng thuật toán.</p>";
                } else {
                    if (status.contains("Vô số nghiệm", Qt::CaseInsensitive)) {
                        commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 5px;'>&rarr; <b>Kết luận:</b> Đã tìm được tọa độ tối ưu thứ 2. Dừng thuật toán.</p>";
                    } else {
                        commonIntroHtml += "<p style='color: #d9534f; font-size: 12pt; margin-bottom: 5px;'>&rarr; <b>Kết luận:</b> Đã đạt [TU_VUNG_BANG] tối ưu. Dừng thuật toán.</p>";
                    }
                    QString solStr = ""; bool firstVar = true;
                    for (int i = 0; i < m; ++i) {
                        int basicVarIndex = step.currentBasicVars[i];
                        QString lhsVarName = (basicVarIndex != -1 && basicVarIndex < (int)varNames.size()) ? varNames[basicVarIndex] : "?";
                        double rhsVal = step.matrix[i][n];
                        if (std::abs(rhsVal) < 1e-9) rhsVal = 0.0;
                        if (!firstVar) solStr += ", ";
                        solStr += QString("%1 = %2").arg(lhsVarName).arg(formatValUI(rhsVal));
                        firstVar = false;
                    }
                    commonIntroHtml += "<p style='color: #0056b3; font-size: 11.5pt; margin-bottom: 20px; font-style: italic;'>";
                    if (status.contains("Vô số nghiệm", Qt::CaseInsensitive)) {
                        commonIntroHtml += "<b>* Giải thích cách lấy nghiệm tối ưu:</b> [READ_SOLUTION_2]</p>";
                    } else {
                        commonIntroHtml += "<b>* Giải thích cách đọc nghiệm và giá trị tối ưu:</b> [READ_SOLUTION]<br>[OPT_Z]</p>";
                    }
                }
            } else {
                commonIntroHtml += "<p style='color: #333333; font-size: 12pt; margin-bottom: 20px; font-style: italic;'>&rarr; Hệ phương trình đã sẵn sàng. Tiếp tục thuật toán Đơn hình.</p>";
            }
        }

        // --- RÁP HTML CHO TAB "DẠNG TỪ VỰNG" ---
        QString htmlVocab = "<html><body style='font-family: \"Times New Roman\", serif; margin: 0; padding: 0; color: #333333;'>";
        QString titleStrVocab = step.stepName.split(" (").first();
        if (stepIdx == 0 && globalIsPhase1) titleStrVocab = "Từ vựng 1 (Khởi tạo Pha 1)";
        else if (step.stepName.contains("Khởi tạo Pha 2")) titleStrVocab = "Từ vựng 1 (Khởi tạo Pha 2)";

        htmlVocab += QString("<p style='color: #0056b3; font-size: 14pt; font-weight: bold; margin-bottom: 8px; margin-top: 0; border-bottom: 2px solid #0056b3; padding-bottom: 4px; display: inline-block;'>%1:</p>").arg(titleStrVocab);

        if (step.stepName.contains("Khởi tạo Pha 2")) {
            QString origZStr = (originalLp.isMaximize ? "Max Z = " : "Min Z = ");
            bool isFirstOrig = true;
            for (size_t i = 0; i < originalLp.c.size(); ++i) {
                if (std::abs(originalLp.c[i]) > 1e-9) {
                    double val = originalLp.c[i];
                    QString sign = (val > 0 && !isFirstOrig) ? " + " : (val < 0 ? " - " : "");
                    origZStr += sign + formatCoeffUI(val) + " x" + QString::number(i+1);
                    isFirstOrig = false;
                }
            }
            if (isFirstOrig) origZStr += "0";

            htmlVocab += "<div style='background-color: #e6f2ff; padding: 10px 15px; border-left: 4px solid #0056b3; margin-bottom: 15px; font-size: 12pt; color: #333333;'>";
            htmlVocab += "<b style='color: #0056b3;'>Thay vào hàm mục tiêu gốc (Chuyển sang Pha 2):</b><br/>";
            htmlVocab += "Hàm mục tiêu ban đầu: <b>" + origZStr + "</b><br/>";
            if (originalLp.isMaximize) htmlVocab += "<i>(Thế các biến cơ sở ở bảng cuối Pha 1 vào hàm Z và lật ngược dấu toàn bộ phương trình thành -Z, ta thu được kết quả bên dưới)</i></div>";
            else htmlVocab += "<i>(Thế các biến cơ sở ở bảng cuối Pha 1 vào hàm Z và rút gọn, ta thu được phương trình Z mới bên dưới)</i></div>";
        }

        QString vocabIntroHtml = commonIntroHtml;
        vocabIntroHtml.replace("[TU_VUNG_BANG]", "từ vựng");
        vocabIntroHtml.replace("[READ_SOLUTION]", vocabReadSol);
        vocabIntroHtml.replace("[READ_SOLUTION_2]", vocabReadSol2);
        vocabIntroHtml.replace("[OPT_Z]", vocabOptZ);
        htmlVocab += vocabIntroHtml;

        bool isPhase1Loc = globalIsPhase1 && ((int)stepIdx <= lastPhase1StepIdx);

        std::vector<int> nonBasicVars;
        for (int j = 0; j < n; ++j) {
            if (!isPhase1Loc && (varNames[j] == "x_0" || varNames[j] == "x0" || varNames[j] == "")) continue;
            bool isBasic = false;
            for (int r = 0; r < m; ++r) {
                if (step.currentBasicVars[r] == j) { isBasic = true; break; }
            }
            if (!isBasic) nonBasicVars.push_back(j);
        }

        // [FIX MỚI] Giao diện Từ Vựng: Dùng bảng chia cột cố định như khi xuất PDF để căn dọc tuyệt đối
        htmlVocab += "<table align='center' cellpadding='0' cellspacing='0' style='margin-top: 10px; font-size: 15pt;'>";

        std::vector<int> rowOrder;
        rowOrder.push_back(m);
        for (int i = 0; i < m; ++i) rowOrder.push_back(i);

        bool isFirstConstraintRow = true;
        for (int i : rowOrder) {
            bool isZRow = (i == m);
            if (!isZRow && isFirstConstraintRow) {
                htmlVocab += "<tr><td colspan='100' style='border: none; padding: 0;'><hr style='border-top: 1px solid black; margin: 6px 0;'></td></tr>";
                isFirstConstraintRow = false;
            }
            htmlVocab += "<tr>";

            QString vNameRaw = isZRow ? (isPhase1Loc ? "&xi;" : (currentOriginalLp.isMaximize ? "-Z" : "Z"))
                                      : (step.currentBasicVars[i] != -1 ? varNames[step.currentBasicVars[i]] : "?");
            QString lhsVarName = vNameRaw;
            lhsVarName.replace(QRegularExpression("([a-zA-Z])(\\d+)"), "\\1<sub>\\2</sub>");
            lhsVarName.replace("+", "<sup>+</sup>");
            lhsVarName.replace("-", "<sup>-</sup>");
            lhsVarName.replace("_0", "<sub>0</sub>");

            if (!isZRow && i == step.pivotRow) lhsVarName = QString("<b><font color='red'>%1</font></b>").arg(lhsVarName);

            htmlVocab += QString("<td width='40' align='right' style='padding: 4px 8px;'><b>%1</b></td>").arg(lhsVarName);
            htmlVocab += "<td width='25' align='center' style='padding: 4px 8px;'> = </td>";

            double rhsVal = isZRow ? -step.matrix[m][n] : step.matrix[i][n];
            if (std::abs(rhsVal) < 1e-9) rhsVal = 0.0;

            bool hasVarTerms = false;
            for (int j : nonBasicVars) {
                double coeff = -step.matrix[isZRow ? m : i][j];
                if (isZRow && stepIdx == 0 && isPhase1Loc && (varNames[j] == "x_0" || varNames[j] == "x0")) coeff = 1.0;
                if (std::abs(coeff) >= 1e-9) hasVarTerms = true;
            }

            bool hasConst = std::abs(rhsVal) >= 1e-9 || !hasVarTerms;
            QString constStr = hasConst ? formatValUI(rhsVal) : "";
            htmlVocab += QString("<td width='55' align='right' style='padding: 4px 8px;'>%1</td>").arg(constStr);

            bool isFirstRhsTerm = !hasConst;

            for (int j : nonBasicVars) {
                QString varName = varNames[j];
                varName.replace(QRegularExpression("([a-zA-Z])(\\d+)"), "\\1<sub>\\2</sub>");
                varName.replace("+", "<sup>+</sup>");
                varName.replace("-", "<sup>-</sup>");
                varName.replace("_0", "<sub>0</sub>");
                if (j == step.pivotCol) varName = QString("<b><font color='green'>%1</font></b>").arg(varName);

                double coeff = -step.matrix[isZRow ? m : i][j];
                if (isZRow && stepIdx == 0 && isPhase1Loc && (varNames[j] == "x_0" || varNames[j] == "x0")) coeff = 1.0;

                if (std::abs(coeff) < 1e-9) {
                    htmlVocab += "<td width='20' style='padding: 0 4px;'></td><td width='45' style='padding: 0 2px;'></td><td width='30' style='padding: 0 2px;'></td>";
                } else {
                    QString sign = "";
                    if (coeff > 0) sign = isFirstRhsTerm ? "" : "+";
                    else sign = "-";
                    isFirstRhsTerm = false;

                    bool isOne = (std::abs(std::abs(coeff) - 1.0) < 1e-9);
                    QString coeffStr = isOne ? "" : formatCoeffUI(coeff);

                    htmlVocab += QString("<td width='20' align='center' style='padding: 0 4px;'>%1</td>").arg(sign);
                    htmlVocab += QString("<td width='45' align='right' style='padding: 0 2px;'>%1</td>").arg(coeffStr);
                    htmlVocab += QString("<td width='30' align='left' style='padding: 0 2px;'>%1</td>").arg(varName);
                }
            }
            htmlVocab += "</tr>";
        }
        htmlVocab += "</table></body></html>";

        // --- RÁP HTML CHO TAB "DẠNG BẢNG" ---
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
            if (!isPhase1Loc && (vName == "x0" || vName == "x_0" || vName == "")) continue;
            htmlTable += QString("<td>%1</td>").arg(vName);
        }
        htmlTable += "<td>RHS</td></tr>";

        for (int i = 0; i < m; ++i) {
            htmlTable += "<tr style='font-size: 15pt;'>";
            int basicVarIndex = step.currentBasicVars[i];
            QString lhsVarName = (basicVarIndex != -1 && basicVarIndex < (int)varNames.size()) ? varNames[basicVarIndex] : "?";
            if (i == step.pivotRow) lhsVarName = QString("<b><font color='red'>%1</font></b>").arg(lhsVarName);
            else lhsVarName = QString("<b>%1</b>").arg(lhsVarName);

            htmlTable += QString("<td>%1</td>").arg(lhsVarName);

            for (int j = 0; j < n; ++j) {
                QString vName = (j < (int)varNames.size()) ? varNames[j] : "";
                if (!isPhase1Loc && (vName == "x0" || vName == "x_0" || vName == "")) continue;

                double val = step.matrix[i][j];
                if (std::abs(val) < 1e-9) val = 0.0;

                if (i == step.pivotRow && j == step.pivotCol) {
                    htmlTable += QString("<td style='background-color: #d4edda; font-weight: bold; color: #155724;'>%1</td>").arg(formatValUI(val));
                } else {
                    htmlTable += QString("<td>%1</td>").arg(formatValUI(val));
                }
            }
            double valRhs = step.matrix[i][n];
            if (std::abs(valRhs) < 1e-9) valRhs = 0.0;
            htmlTable += QString("<td><b>%1</b></td>").arg(formatValUI(valRhs));
            htmlTable += "</tr>";
        }

        htmlTable += "<tr style='background-color: #fff3cd; font-weight: bold; font-size: 15pt;'>";
        QString zLabelTable = isPhase1Loc ? "&xi;" : (originalLp.isMaximize ? "-Z" : "Z");
        htmlTable += QString("<td>%1</td>").arg(zLabelTable);

        for (int j = 0; j < n; ++j) {
            QString vName = (j < (int)varNames.size()) ? varNames[j] : "";
            if (!isPhase1Loc && (vName == "x0" || vName == "x_0" || vName == "")) continue;

            double val = -step.matrix[m][j];
            if (std::abs(val) < 1e-9) val = 0.0;

            if (j == step.pivotCol) {
                htmlTable += QString("<td><b><font color='green'>%1</font></b></td>").arg(formatValUI(val));
            } else {
                htmlTable += QString("<td>%1</td>").arg(formatValUI(val));
            }
        }

        double valZRhs = -step.matrix[m][n];
        if (std::abs(valZRhs) < 1e-9) valZRhs = 0.0;
        htmlTable += QString("<td><b>%1</b></td>").arg(formatValUI(valZRhs));
        htmlTable += "</tr></table></body></html>";

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
        if (std::abs(value) < 1e-9) value = 0.0;
        return QString::number(value, 'g', 10);
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

        if (!hasTerm) expr = "0";
        return expr;
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
    contextString += "Giá trị hiển thị tại ô kết quả Z: " + ui->lineEdit_Z->text() + "\n";

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

    contextString += "\n--- HƯỚNG DẪN DÀNH CHO BẠN (TRỢ LÝ AI) ---\n";
    contextString += "Bạn là một AI hỗ trợ giải đáp toán học môn Quy hoạch tuyến tính. Bạn CHỈ được phép giải thích hoặc trả lời các câu hỏi liên quan đến nội dung bài toán quy hoạch tuyến tính ở trên, phương pháp giải, hoặc các khái niệm toán học liên quan.\n";
    contextString += "Khi giải thích hệ ràng buộc, bắt buộc phải dùng đúng dấu ràng buộc đã cung cấp trong từng dòng R1, R2, ... Không được tự đổi chiều dấu và không được bỏ mất vế phải.\n";
    contextString += "QUAN TRỌNG: Nếu người dùng hỏi bất kỳ câu hỏi ngoài lề (không thuộc phạm vi của bài toán hoặc quy hoạch tuyến tính), bạn KHÔNG ĐƯỢC trả lời nội dung đó. Bạn BẮT BUỘC phải trả lời chính xác bằng câu sau và không giải thích gì thêm:\n\"Xin lỗi câu hỏi của bạn không thuộc phạm vi của bài toán\"";

    if (!this->wd_ChatBot) this->wd_ChatBot = new WdChatBot(this);

    this->wd_ChatBot->setProblemContext(contextString);
    this->wd_ChatBot->show();
    this->wd_ChatBot->raise();
    this->wd_ChatBot->activateWindow();
}
