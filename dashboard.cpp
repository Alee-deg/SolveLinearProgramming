#include "dashboard.h"
#include "ui_dashboard.h"
#include <QStringList>
#include <QComboBox>
#include "wdsolve.h"
#include <QCloseEvent>
#include <QApplication>
#include <QMessageBox>
#include "Struct.h"
#include "simplexsolver.h"
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QLineEdit>
#include <QtMath>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QWheelEvent>

// -----------------------------------------------------------------------
// [BẢN MAX LEVEL] Ô nhập liệu thông minh Algebraic Parser
// Hỗ trợ: Cộng, Trừ, Nhân, Chia, Lũy thừa, Ngoặc đơn, Căn số, Pi, E
// -----------------------------------------------------------------------
class MathInput : public QLineEdit {
public:
    explicit MathInput(QWidget *parent = nullptr) : QLineEdit(parent) {}

    // Bộ máy dịch Toán học chuẩn Cây Cú Pháp (AST)
    double parseMath(QString s) const {
        s = s.trimmed().toLower();
        s.remove(' ');
        if (s.isEmpty()) return 0.0;

        while (s.startsWith('(') && s.endsWith(')')) {
            int checkParen = 0;
            bool canStrip = true;
            for (int i = 0; i < s.length() - 1; ++i) {
                if (s[i] == '(') checkParen++;
                else if (s[i] == ')') checkParen--;
                if (checkParen == 0) { canStrip = false; break; }
            }
            if (canStrip) s = s.mid(1, s.length() - 2);
            else break;
        }

        int parenCount = 0;
        for (int i = s.length() - 1; i > 0; --i) {
            QChar c = s[i];
            if (c == ')') parenCount++;
            else if (c == '(') parenCount--;
            else if (parenCount == 0) {
                if (c == '+') return parseMath(s.left(i)) + parseMath(s.mid(i + 1));
                if (c == '-') {
                    QChar prev = s[i-1];
                    if (prev != '/' && prev != '*' && prev != '^' && prev != '(') {
                        return parseMath(s.left(i)) - parseMath(s.mid(i + 1));
                    }
                }
            }
        }

        parenCount = 0;
        for (int i = s.length() - 1; i > 0; --i) {
            QChar c = s[i];
            if (c == ')') parenCount++;
            else if (c == '(') parenCount--;
            else if (parenCount == 0) {
                if (c == '*') return parseMath(s.left(i)) * parseMath(s.mid(i + 1));
                if (c == '/') {
                    double den = parseMath(s.mid(i + 1));
                    return den != 0 ? parseMath(s.left(i)) / den : 0.0;
                }
            }
        }

        if (s.startsWith('-')) return -parseMath(s.mid(1));

        parenCount = 0;
        for (int i = 0; i < s.length(); ++i) {
            QChar c = s[i];
            if (c == '(') parenCount++;
            else if (c == ')') parenCount--;
            else if (parenCount == 0 && c == '^') {
                return qPow(parseMath(s.left(i)), parseMath(s.mid(i + 1)));
            }
        }

        if (s == "pi") return 3.14159265358979323846;
        if (s == "e") return 2.71828182845904523536;

        bool ok;
        double val = s.toDouble(&ok);
        if (ok) return val;

        if (s.startsWith("sqrt(") && s.endsWith(")")) {
            double v = parseMath(s.mid(5, s.length() - 6));
            if (v >= 0) return qSqrt(v);
        }

        if (s.contains("root")) {
            static QRegularExpression re("^root(\\d+)\\((.*)\\)$");
            QRegularExpressionMatch match = re.match(s);
            if (match.hasMatch()) {
                int n = match.captured(1).toInt();
                double v = parseMath(match.captured(2));
                if (n <= 0) return 0.0;
                if (v < 0) {
                    if (n % 2 == 0) return 0.0;
                    return -qPow(-v, 1.0 / n);
                } else {
                    return qPow(v, 1.0 / n);
                }
            }
        }

        return 0.0;
    }

    double value() const {
        return parseMath(this->text());
    }

    void setValue(double val) {
        this->setText(QString::number(val, 'f', 2));
    }

protected:
    void focusInEvent(QFocusEvent *event) override {
        QLineEdit::focusInEvent(event);
        QTimer::singleShot(0, this, [this](){ this->selectAll(); });
    }

    // [ĐÃ THÊM MỚI] Sự kiện khi người dùng nhập xong và chuyển sang ô khác
    void focusOutEvent(QFocusEvent *event) override {
        QLineEdit::focusOutEvent(event);
        // Tự động tính toán (nếu gõ biểu thức) và bọc lại đúng 2 chữ số thập phân
        this->setValue(this->value());
    }

    void wheelEvent(QWheelEvent *event) override {
        double currentVal = this->value();
        int step = (event->angleDelta().y() > 0) ? 1 : -1;
        this->setValue(currentVal + step);
        this->selectAll();
        event->accept();
    }
};

Dashboard::Dashboard(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Dashboard)
{
    ui->setupUi(this);
    ui->matrix->hide();
    ui->table_functionTarget->hide();
    ui->label_3->hide();
    ui->label_4->hide();
    ui->label_5->hide();
    ui->max_min->hide();
    ui->table_varConstraint->hide();
    this->setWindowIcon(QIcon(":/logo.png"));

    this->wd_solve = nullptr;

    // Chỉ thay đổi dòng này: Set màu nền F5F7FA cho QMainWindow
    this->setStyleSheet(
        "#centralwidget { background-color: #F5F7FA; }" /* Ép màu nền xám dịu cho toàn bộ khu vực chính */
        "QWidget { font-family: 'Times New Roman'; font-size: 14pt; color: #333333; }"
        "QTableWidget { background-color: #FFFFFF; border: 1px solid #CCCCCC; }" /* Bảng vẫn giữ nền trắng để phần nhập liệu nổi bật */
        "QHeaderView::section { background-color: #E4E7EB; font-weight: bold; border: 1px solid #C0C0C0; padding: 4px; }"
        );

    QString headerStyle =
        "QHeaderView::section { background-color: #E8E8E8; font-weight: bold; "
        "border: 1px solid #C0C0C0; padding: 4px; }";
    ui->table_functionTarget->horizontalHeader()->setStyleSheet(headerStyle);
    ui->matrix->horizontalHeader()->setStyleSheet(headerStyle);
    ui->table_varConstraint->horizontalHeader()->setStyleSheet(headerStyle);
    ui->table_varConstraint->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    this->setWindowTitle("Nhập liệu");
    this->setWindowState(Qt::WindowMaximized);
}

Dashboard::~Dashboard()
{
    delete ui;
}

MathInput* Dashboard::createSpinBox(QWidget *parent) {
    MathInput *input = new MathInput(parent);
    input->setValue(0.0);
    input->setAlignment(Qt::AlignCenter);
    input->setStyleSheet(
        "QLineEdit { border: none; background: transparent; }"
        "QLineEdit:focus { border: 1px solid #0078D7; background: white; }");

    QString mathPattern = R"(^[0-9.pieqsrto\s+\-*/^()]*$)";
    QRegularExpression rx(mathPattern, QRegularExpression::CaseInsensitiveOption);
    QValidator *validator = new QRegularExpressionValidator(rx, input);
    input->setValidator(validator);

    return input;
}

void Dashboard::setupObjectiveFunctionTable(int n) {
    std::vector<double> oldData;
    int oldCols = ui->table_functionTarget->columnCount();
    for (int j = 0; j < oldCols; ++j) {
        auto *sp = dynamic_cast<MathInput*>(ui->table_functionTarget->cellWidget(0, j));
        oldData.push_back(sp ? sp->value() : 0.0);
    }

    ui->table_functionTarget->setRowCount(1);
    ui->table_functionTarget->setColumnCount(n + 1);

    QStringList headers;
    for (int i = 0; i <= n; i++) headers << QString("x%1").arg(i);
    ui->table_functionTarget->setHorizontalHeaderLabels(headers);
    ui->table_functionTarget->verticalHeader()->setVisible(false);

    for (int i = 0; i <= n; i++) {
        MathInput *spinBox = createSpinBox();
        if (i < (int)oldData.size()) spinBox->setValue(oldData[i]);
        ui->table_functionTarget->setCellWidget(0, i, spinBox);
    }
    ui->table_functionTarget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

void Dashboard::setupConstraintsTable(int m, int n) {
    int oldM = ui->matrix->rowCount();
    int oldN = ui->matrix->columnCount() > 1 ? ui->matrix->columnCount() - 2 : 0;
    std::vector<std::vector<double>> oldA(oldM, std::vector<double>(oldN, 0.0));
    std::vector<QString> oldSigns(oldM, "<=");
    std::vector<double>  oldB(oldM, 0.0);

    for (int i = 0; i < oldM; ++i) {
        for (int j = 0; j < oldN; ++j) {
            auto *sp = dynamic_cast<MathInput*>(ui->matrix->cellWidget(i, j));
            if (sp) oldA[i][j] = sp->value();
        }
        auto *combo = qobject_cast<QComboBox*>(ui->matrix->cellWidget(i, oldN));
        if (combo) oldSigns[i] = combo->currentText();
        auto *spB = dynamic_cast<MathInput*>(ui->matrix->cellWidget(i, oldN + 1));
        if (spB) oldB[i] = spB->value();
    }

    ui->matrix->setRowCount(m);
    ui->matrix->setColumnCount(n + 2);

    QStringList headers;
    for (int i = 1; i <= n; i++) headers << QString("x%1").arg(i);
    headers << "Sign" << "b_i";
    ui->matrix->setHorizontalHeaderLabels(headers);

    for (int row = 0; row < m; ++row) {
        for (int col = 0; col < n; ++col) {
            MathInput *sp = createSpinBox();
            if (row < oldM && col < oldN) sp->setValue(oldA[row][col]);
            ui->matrix->setCellWidget(row, col, sp);
        }

        QComboBox *comboSign = new QComboBox();
        comboSign->addItems({"<=", "=", ">="});
        comboSign->setStyleSheet(
            "QComboBox { border: none; background: transparent; }"
            "QComboBox:focus { border: 1px solid #0078D7; background: white; }");
        if (row < oldM) {
            int idx = comboSign->findText(oldSigns[row]);
            if (idx >= 0) comboSign->setCurrentIndex(idx);
        }
        ui->matrix->setCellWidget(row, n, comboSign);

        MathInput *spB = createSpinBox();
        if (row < oldM) spB->setValue(oldB[row]);
        ui->matrix->setCellWidget(row, n + 1, spB);
    }
    ui->matrix->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

void Dashboard::setupNumericCell(int row, int col) {
    ui->matrix->setCellWidget(row, col, createSpinBox());
}

void Dashboard::setupVariableConstraints(int n) {
    int oldN = ui->table_varConstraint->rowCount();
    std::vector<QString> oldSigns(oldN, ">=");
    std::vector<double>  oldVals(oldN, 0.0);
    for (int i = 0; i < oldN; ++i) {
        auto *combo = qobject_cast<QComboBox*>(ui->table_varConstraint->cellWidget(i, 1));
        if (combo) oldSigns[i] = combo->currentText();
        auto *sp = dynamic_cast<MathInput*>(ui->table_varConstraint->cellWidget(i, 2));
        if (sp) oldVals[i] = sp->value();
    }

    ui->table_varConstraint->setRowCount(n);
    ui->table_varConstraint->setColumnCount(3);
    ui->table_varConstraint->setHorizontalHeaderLabels({"Biến", "Dấu", "Giá trị"});

    for (int i = 0; i < n; ++i) {
        QLabel *labelVar = new QLabel(QString("x%1").arg(i + 1));
        labelVar->setAlignment(Qt::AlignCenter);
        ui->table_varConstraint->setCellWidget(i, 0, labelVar);

        QComboBox *comboSign = new QComboBox();
        comboSign->addItems({">=", "<=", "free"});
        comboSign->setStyleSheet(
            "QComboBox { border: none; background: transparent; }"
            "QComboBox:focus { border: 1px solid #0078D7; background: white; }");
        if (i < oldN) {
            int idx = comboSign->findText(oldSigns[i]);
            if (idx >= 0) comboSign->setCurrentIndex(idx);
        }
        ui->table_varConstraint->setCellWidget(i, 1, comboSign);

        MathInput *spinBox = createSpinBox();
        spinBox->setReadOnly(true);
        spinBox->setEnabled(false); // Khóa cứng
        spinBox->setStyleSheet("background-color: #E8E8E8; border: none;"); // Bôi xám để hiển thị trạng thái bị khóa

        spinBox->setValue(0.0); // Ép về 0.0 vì chỉ cho chọn dấu
        ui->table_varConstraint->setCellWidget(i, 2, spinBox);

        connect(comboSign, &QComboBox::currentTextChanged,
                [spinBox](const QString &text) {
                    spinBox->setValue(0.0);
                    spinBox->setEnabled(false);
                });
    }
    ui->table_varConstraint->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

void Dashboard::on_pushButton_4_clicked()
{
    QGraphicsOpacityEffect *fadeEffect = new QGraphicsOpacityEffect(this);
    this->centralWidget()->setGraphicsEffect(fadeEffect);

    QPropertyAnimation *animOut = new QPropertyAnimation(fadeEffect, "opacity");
    animOut->setDuration(150);
    animOut->setStartValue(1.0);
    animOut->setEndValue(0.0);

    connect(animOut, &QPropertyAnimation::finished, this, [=]() {
        this->hide();
        if (this->parentWidget()) {
            this->parentWidget()->show();
            QMainWindow *mainWindow = qobject_cast<QMainWindow*>(this->parentWidget());
            if (mainWindow) {
                QGraphicsOpacityEffect *fadeInEffect =
                    new QGraphicsOpacityEffect(mainWindow->centralWidget());
                mainWindow->centralWidget()->setGraphicsEffect(fadeInEffect);
                QPropertyAnimation *animIn =
                    new QPropertyAnimation(fadeInEffect, "opacity");
                animIn->setDuration(150);
                animIn->setStartValue(0.0);
                animIn->setEndValue(1.0);
                animIn->start(QAbstractAnimation::DeleteWhenStopped);
            }
        }
    });
    animOut->start(QAbstractAnimation::DeleteWhenStopped);
}

void Dashboard::on_pushButton_2_clicked()
{
    int n = ui->spinBox->value();
    int m = ui->spinBox_2->value();
    if (n > 0 && m > 0) {
        setupConstraintsTable(m, n);
        setupVariableConstraints(n);
        setupObjectiveFunctionTable(n);
        ui->label_3->show();
        ui->label_4->show();
        ui->label_5->show();
        ui->table_functionTarget->show();
        ui->table_varConstraint->show();
        ui->matrix->show();
        ui->max_min->show();
    } else {
        QMessageBox::warning(this, "Warning", "Vui lòng nhập số biến và số ràng buộc rõ ràng");
    }
}

void Dashboard::on_pushButton_3_clicked()
{
    LinearProgram local_lp;
    local_lp.algoType   = ui->comboAlgorithm->currentIndex();
    local_lp.isMaximize = (ui->max_min->currentText() != "Min");
    this->getDataFromWd(local_lp);

    LinearProgram originalLp = local_lp;

    SimplexSolver solver(local_lp);
    solver.solve();

    QString status = solver.getStatus();
    bool isValidStatus = (status == "Tối ưu" ||
                          status == "Vô số nghiệm" ||
                          status.contains("giới nội", Qt::CaseInsensitive) ||
                          status.contains("Vô nghiệm", Qt::CaseInsensitive));

    if (!isValidStatus) {
        QMessageBox::critical(this, "Cảnh báo Thuật toán", status);
        return;
    }

    if (!this->wd_solve) {
        this->wd_solve = new WdSolve(this);
    }

    this->wd_solve->displayResults(
        solver.getLp(),
        originalLp,
        status,
        solver.getOptimalZ(),
        solver.getFirstSolution(),
        solver.getAltSolution(),
        solver.getHistory()
        );

    this->wd_solve->show();
    this->wd_solve->raise();
    this->wd_solve->activateWindow();
}

void Dashboard::closeEvent(QCloseEvent *event)
{
    QApplication::quit();
    event->accept();
}

void Dashboard::getDataFromWd(LinearProgram &lp)
{
    int cols_obj = ui->table_functionTarget->columnCount();
    for (int j = 0; j < cols_obj; j++) {
        auto *spinC = dynamic_cast<MathInput*>(ui->table_functionTarget->cellWidget(0, j));
        if (spinC) lp.c.push_back(spinC->value());
    }
    if (!lp.c.empty()) {
        lp.c_0 = lp.c[0];
        lp.c.erase(lp.c.begin());
    } else {
        lp.c_0 = 0.0;
        return;
    }

    int m      = ui->matrix->rowCount();
    int n_vars = ui->matrix->columnCount() - 2;
    for (int i = 0; i < m; i++) {
        std::vector<double> rowA;
        for (int j = 0; j < n_vars; j++) {
            auto *spinA = dynamic_cast<MathInput*>(ui->matrix->cellWidget(i, j));
            if (spinA) rowA.push_back(spinA->value());
        }
        lp.A.push_back(rowA);

        auto *comboSign = qobject_cast<QComboBox*>(ui->matrix->cellWidget(i, n_vars));
        if (comboSign) lp.signs.push_back(comboSign->currentText());

        auto *spinB = dynamic_cast<MathInput*>(ui->matrix->cellWidget(i, n_vars + 1));
        if (spinB) lp.b.push_back(spinB->value());
    }

    int n_vars_count = ui->table_varConstraint->rowCount();
    for (int i = 0; i < n_vars_count; ++i) {
        VarBound vb;
        auto *comboVarSign = qobject_cast<QComboBox*>(ui->table_varConstraint->cellWidget(i, 1));
        auto *spinVarVal = dynamic_cast<MathInput*>(ui->table_varConstraint->cellWidget(i, 2));
        if (comboVarSign && spinVarVal) {
            vb.sign   = comboVarSign->currentText();
            vb.isFree = (vb.sign == "free");
            vb.value  = vb.isFree ? 0.0 : spinVarVal->value();
            lp.varBounds.push_back(vb);
        }
    }
}

void Dashboard::on_pushButton_5_clicked()
{
    ui->comboAlgorithm->setCurrentIndex(0);
    ui->max_min->setCurrentIndex(0);

    int objCols = ui->table_functionTarget->columnCount();
    for (int j = 0; j < objCols; ++j) {
        auto *sp = dynamic_cast<MathInput*>(ui->table_functionTarget->cellWidget(0, j));
        if (sp) sp->setValue(0.0);
    }

    int m = ui->matrix->rowCount();
    int matrixCols = ui->matrix->columnCount();
    if (matrixCols > 2) {
        int n_vars = matrixCols - 2;
        for (int i = 0; i < m; ++i) {
            for (int j = 0; j < n_vars; ++j) {
                auto *sp = dynamic_cast<MathInput*>(ui->matrix->cellWidget(i, j));
                if (sp) sp->setValue(0.0);
            }
            auto *combo = qobject_cast<QComboBox*>(ui->matrix->cellWidget(i, n_vars));
            if (combo) combo->setCurrentIndex(0);

            auto *spB = dynamic_cast<MathInput*>(ui->matrix->cellWidget(i, n_vars + 1));
            if (spB) spB->setValue(0.0);
        }
    }

    int varRows = ui->table_varConstraint->rowCount();
    for (int i = 0; i < varRows; ++i) {
        auto *combo = qobject_cast<QComboBox*>(ui->table_varConstraint->cellWidget(i, 1));
        if (combo) combo->setCurrentIndex(0);

        auto *sp = dynamic_cast<MathInput*>(ui->table_varConstraint->cellWidget(i, 2));
        if (sp) {
            sp->setValue(0.0);
            sp->setEnabled(false); // Cập nhật để tiếp tục giữ trạng thái khóa khi bấm Reset
        }
    }
}

void Dashboard::on_btn_HuongDan_clicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Hướng dẫn sử dụng");
    dialog.resize(800, 620);

    dialog.setStyleSheet(
        "QDialog { background-color: #1E1E2E; }"
        "QLabel { color: #CDD6F4; }"
        );

    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setContentsMargins(30, 25, 30, 25);
    mainLayout->setSpacing(15);

    QLabel *titleLabel = new QLabel("HƯỚNG DẪN SỬ DỤNG PHẦN MỀM");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("color: #89B4FA; font-size: 22px; font-weight: bold; letter-spacing: 1px;");
    mainLayout->addWidget(titleLabel);

    QFrame *line = new QFrame(&dialog);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("background-color: #313244; max-height: 1px; margin-bottom: 5px; border: none;");
    mainLayout->addWidget(line);

    QScrollArea *scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; } QWidget#scrollAreaWidgetContents { background: transparent; }");

    QWidget *scrollContent = new QWidget();
    scrollContent->setObjectName("scrollAreaWidgetContents");
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(5, 0, 15, 0);

    QLabel *textLabel = new QLabel();
    textLabel->setTextFormat(Qt::RichText);
    textLabel->setWordWrap(true);
    textLabel->setStyleSheet("font-size: 15px; line-height: 1.6;");
    textLabel->setOpenExternalLinks(true);

    QString guideText = R"(
        <p style="margin-bottom: 12px;"><b>1. Khởi tạo bài toán:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Nhập <b>số biến</b> (n) và <b>số ràng buộc</b> (m).<br>
        &nbsp;&nbsp;&nbsp;&bull; Nhấn nút <b style="color: #A6E3A1;">OK</b> để phần mềm tạo bảng nhập liệu.</p>

        <p style="margin-bottom: 12px;"><b>2. Nhập Hàm mục tiêu (Z):</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Nhập hệ số cho từng biến (x1, x2...). Cột <b>x0</b> dùng để nhập hằng số tự do.<br>
        &nbsp;&nbsp;&nbsp;&bull; Chọn mục tiêu: <b style="color: #F38BA8;">Max</b> (Tìm GTLN) hoặc <b style="color: #F38BA8;">Min</b> (Tìm GTNN).</p>

        <p style="margin-bottom: 12px;"><b>3. Nhập Ma trận Ràng buộc:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Điền hệ số của các biến vào từng phương trình ràng buộc.<br>
        &nbsp;&nbsp;&nbsp;&bull; Tại cột <b>Sign</b>, chọn dấu tương ứng (<b>&lt;=</b>, <b>=</b>, <b>&gt;=</b>).<br>
        &nbsp;&nbsp;&nbsp;&bull; Điền hệ số vế phải vào cột <b>b_i</b>.</p>

        <div style="background-color: #313244; padding: 10px; border-radius: 5px; margin-bottom: 12px;">
            <i>Phần mềm hỗ trợ tự động tính toán khi bạn nhập biểu thức phức tạp vào ô hệ số:</i><br>
            &nbsp;&nbsp;&nbsp;&bull; <b>Các phép tính cơ bản:</b> Nhập <code style="color: #89B4FA;">pi - 1</code>, <code style="color: #89B4FA;">3/7</code>, <code style="color: #89B4FA;">2*e</code>...<br>
            &nbsp;&nbsp;&nbsp;&bull; <b>Lũy thừa:</b> Nhập <code style="color: #89B4FA;">2^3</code>, <code style="color: #89B4FA;">e^4</code>, <code style="color: #89B4FA;">pi^4</code>...<br>
            &nbsp;&nbsp;&nbsp;&bull; <b>Căn số:</b> Nhập <code style="color: #89B4FA;">sqrt(2)</code>, <code style="color: #89B4FA;">root3(8)</code> (căn bậc 3)...<br>
            &nbsp;&nbsp;&nbsp;&bull; <b>Ngoặc đơn:</b> Nhập <code style="color: #89B4FA;">(pi-1)/2</code>, <code style="color: #89B4FA;">-(2^4)</code>...
        </div>

        <p style="margin-bottom: 12px;"><b>4. Ràng buộc dấu của biến:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Ở bảng góc dưới, bạn có thể chỉnh giới hạn biến (Mặc định x_i &gt;= 0).<br>
        &nbsp;&nbsp;&nbsp;&bull; Nếu biến tự do (không ràng buộc dấu), hãy chọn <b style="color: #F9E2AF;">free</b>.</p>

        <p style="margin-bottom: 20px;"><b>5. Giải bài toán:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Chọn thuật toán muốn sử dụng ở thanh menu thả xuống.<br>
        &nbsp;&nbsp;&nbsp;&bull; Nhấn nút <b style="color: #89B4FA;">Solve</b> để xem chi tiết lời giải từng bước.</p>

        <p style="color: #A6ADC8; font-style: italic; margin-bottom: 15px;">* Nhấn nút <b>Reset</b> (mũi tên xoay) để dọn dẹp bảng và nhập bài toán mới.</p>

        <p style="font-size: 15px; color: #CDD6F4;">
            🌐 Xem chi tiết file hướng dẫn (PDF):
            <a href="https://github.com/Alee-deg/Project-of-Alee/blob/Algorithm_of_Hau/LinkDonwLoadApp.pdf" style="color: #89B4FA; text-decoration: none; font-weight: bold;">Tại đây</a>
        </p>
    )";

    textLabel->setText(guideText);
    scrollLayout->addWidget(textLabel);
    scrollLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *closeBtn = new QPushButton("Đã hiểu");
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton {"
        "   background-color: #89B4FA; color: #1E1E2E; border: none;"
        "   border-radius: 6px; padding: 9px 35px; font-size: 15px; font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #B4BEFE; }"
        "QPushButton:pressed { background-color: #74C7EC; }"
        );
    QObject::connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    dialog.exec();
}
