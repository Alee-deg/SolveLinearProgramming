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

WdSolve::WdSolve(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::WdSolve)
{
    ui->setupUi(this);

    // =======================================================================
    // [FIX GIAO DIỆN] Đã gỡ bỏ setStyleSheet cứng ở đây để tôn trọng
    // "Công tắc tổng" ở MainWindow.
    // =======================================================================
    this->setStyleSheet("");

    this->setWindowTitle("Kết quả tính toán");
    this->setWindowState(Qt::WindowMaximized);
    this->setWindowIcon(QIcon(":/logo.png"));

    this->wd_show    = nullptr;
    this->wd_ChatBot = nullptr;

    // =======================================================================
    // [FIX] 3 NÚT DƯỚI CÙNG PHỦ HẾT CHIỀU NGANG THEO TỈ LỆ 6 : 2 : 2
    //      - Biểu diễn hình học: 6 phần
    //      - Hỏi/Đáp: 2 phần
    //      - OK/Quay lại: 2 phần
    //      - Có khoảng cách nhỏ giữa các nút
    // =======================================================================
    QPushButton* btnBack = this->findChild<QPushButton*>("pushButton");      // OK / Quay lại
    QPushButton* btnDraw = this->findChild<QPushButton*>("pushButton_2");    // Biểu diễn hình học
    QPushButton* btnChat = this->findChild<QPushButton*>("pushButton_3");    // Hỏi/Đáp

    QString btnStyle = "QPushButton { font-weight: bold; padding: 8px; border-radius: 4px; }";

    QList<QPushButton*> bottomButtons = { btnDraw, btnChat, btnBack };
    for (QPushButton* btn : bottomButtons) {
        if (!btn) continue;
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setMinimumWidth(0);
        btn->setMaximumWidth(QWIDGETSIZE_MAX);
        btn->setMinimumHeight(30);
        btn->setStyleSheet(btnStyle);
    }

    // Tìm đúng layout ngang đang chứa trực tiếp 3 nút này.
    // Sau đó bỏ các spacer cũ để 3 nút tự kéo giãn hết chiều ngang cửa sổ.
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

    if (buttonLayout && btnDraw && btnChat && btnBack) {
        while (buttonLayout->count() > 0) {
            QLayoutItem* item = buttonLayout->takeAt(0);
            delete item;
        }

        buttonLayout->setContentsMargins(0, 0, 0, 0);
        buttonLayout->setSpacing(8);

        buttonLayout->addWidget(btnDraw, 6);
        buttonLayout->addWidget(btnChat, 2);
        buttonLayout->addWidget(btnBack, 2);
    }
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
    QSettings settings(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
    bool isDark = settings.value("dark_mode", false).toBool();

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

                    commonIntroHtml += "<b>* Giải thích vô số nghiệm:</b> vì tồn tại hệ số của biến không cơ sở ở hàm mục tiêu bằng 0 ở từ vựng tối ưu nên bài toán có vô số nghiệm tối ưu.<br>";
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

                    commonIntroHtml += "<b>* Giải thích không giới nội:</b> các hệ số đứng trước biến ứng với hệ số âm nhất ở hàm mục tiêu trong các phương trình ở hệ ràng buộc đều dương.<br>";

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

// -----------------------------------------------------------------------
// NÚT QUAY LẠI
// -----------------------------------------------------------------------
void WdSolve::on_pushButton_clicked()
{
    if (this->parentWidget()) {
        this->parentWidget()->show();
    }
    this->hide();
}

// -----------------------------------------------------------------------
// NÚT VẼ HÌNH HỌC
// -----------------------------------------------------------------------
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

// -----------------------------------------------------------------------
// NÚT HỎI ĐÁP CHATBOT
// -----------------------------------------------------------------------
void WdSolve::on_pushButton_3_clicked()
{
    const LinearProgram& lp = currentOriginalLp;

    // =======================================================================
    // [FIX CHATBOT CONTEXT]
    // Dùng ký hiệu Unicode ≤, ≥ thay cho <=, >= để QTextEdit/HTML không hiểu nhầm
    // dấu < là thẻ HTML, đồng thời gửi đầy đủ dấu ràng buộc và vế phải cho chatbot.
    // =======================================================================
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
            if (std::abs(val) < 1e-9 && !keepZeroTerms) {
                continue;
            }

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
        if (!lp.c.empty()) {
            contextString += " + ";
        }
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
            contextString += varName + " "
                             + normalizeSign(lp.varBounds[i].sign) + " "
                             + formatNumber(lp.varBounds[i].value) + "\n";
        }
    }

    contextString += "\n4) KẾT QUẢ PHẦN MỀM ĐÃ GIẢI RA\n";
    contextString += "Giá trị hiển thị tại ô kết quả Z: " + ui->lineEdit_Z->text() + "\n";

    if (!currentSolution.empty()) {
        contextString += "Nghiệm tối ưu đang hiển thị:\n";
        for (size_t i = 0; i < currentSolution.size(); ++i) {
            contextString += QString("x%1 = %2\n")
                                 .arg((int)i + 1)
                                 .arg(formatNumber(currentSolution[i]));
        }
    }

    if (!currentAltSolution.empty()) {
        contextString += "Nghiệm tối ưu khác / nghiệm phụ nếu có:\n";
        for (size_t i = 0; i < currentAltSolution.size(); ++i) {
            contextString += QString("x%1 = %2\n")
                                 .arg((int)i + 1)
                                 .arg(formatNumber(currentAltSolution[i]));
        }
    }

    contextString += "\n--- HƯỚNG DẪN DÀNH CHO BẠN (TRỢ LÝ AI) ---\n";
    contextString += "Bạn là một AI hỗ trợ giải đáp toán học môn Quy hoạch tuyến tính. ";
    contextString += "Bạn CHỈ được phép giải thích hoặc trả lời các câu hỏi liên quan đến nội dung bài toán quy hoạch tuyến tính ở trên, phương pháp giải, hoặc các khái niệm toán học liên quan.\n";
    contextString += "Khi giải thích hệ ràng buộc, bắt buộc phải dùng đúng dấu ràng buộc đã cung cấp trong từng dòng R1, R2, ... Không được tự đổi chiều dấu và không được bỏ mất vế phải.\n";
    contextString += "QUAN TRỌNG: Nếu người dùng hỏi bất kỳ câu hỏi ngoài lề (không thuộc phạm vi của bài toán hoặc quy hoạch tuyến tính), bạn KHÔNG ĐƯỢC trả lời nội dung đó. Bạn BẮT BUỘC phải trả lời chính xác bằng câu sau và không giải thích gì thêm:\n";
    contextString += "\"Xin lỗi câu hỏi của bạn không thuộc phạm vi của bài toán\"";

    if (!this->wd_ChatBot) {
        this->wd_ChatBot = new WdChatBot(this);
    }

    this->wd_ChatBot->setProblemContext(contextString);
    this->wd_ChatBot->show();
    this->wd_ChatBot->raise();
    this->wd_ChatBot->activateWindow();
}
