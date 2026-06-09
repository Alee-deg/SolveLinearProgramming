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
#include <deque>
#include <algorithm> // Bổ sung thư viện để sort mảng khi xóa nhiều dòng

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDateTime>
#include <QSettings>
#include <QStandardPaths>
#include <QCompleter>
#include <QStringListModel>
#include <QInputDialog>
#include <QKeyEvent>
#include <QEvent>
#include <QTimer>

// =======================================================================
// Cấu trúc bọc bài toán kèm theo thời gian giải
// =======================================================================
struct HistoryEntry {
    LinearProgram lp;
    QDateTime timestamp;
    QString note; // Ghi chú riêng cho từng bài toán trong lịch sử
};

static std::deque<HistoryEntry> g_undoStack;

// Khi người dùng tải lại một bài toán từ lịch sử, chỉ đánh dấu vị trí cũ.
// Không lưu lại ngay; chỉ thay thế lịch sử khi người dùng bấm Solve.
static int g_pendingRestoreIndex = -1;

// HÀM TẠO ĐƯỜNG DẪN AN TOÀN
static QString getAppDataPath() {
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir;
    if (!dir.exists(dataDir)) {
        dir.mkpath(dataDir);
    }
    return dataDir;
}

static void saveHistoryToJson() {
    QJsonArray historyArray;
    for (const auto& entry : g_undoStack) {
        QJsonObject obj;
        obj["timestamp"] = entry.timestamp.toString(Qt::ISODate);
        obj["note"] = entry.note;

        const LinearProgram& lp = entry.lp;
        obj["algoType"] = lp.algoType;
        obj["isMaximize"] = lp.isMaximize;
        obj["c_0"] = lp.c_0;

        QJsonArray cArr; for (double val : lp.c) cArr.append(val);
        obj["c"] = cArr;

        QJsonArray aArr;
        for (const auto& row : lp.A) {
            QJsonArray rowArr; for (double val : row) rowArr.append(val);
            aArr.append(rowArr);
        }
        obj["A"] = aArr;

        QJsonArray signArr; for (const QString& s : lp.signs) signArr.append(s);
        obj["signs"] = signArr;

        QJsonArray bArr; for (double val : lp.b) bArr.append(val);
        obj["b"] = bArr;

        QJsonArray boundsArr;
        for (const auto& vb : lp.varBounds) {
            QJsonObject vbObj;
            vbObj["sign"] = vb.sign;
            vbObj["isFree"] = vb.isFree;
            vbObj["value"] = vb.value;
            boundsArr.append(vbObj);
        }
        obj["varBounds"] = boundsArr;

        historyArray.append(obj);
    }

    // LƯU VÀO APPDATA
    QString filePath = getAppDataPath() + "/history.json";
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(historyArray);
        file.write(doc.toJson());
        file.close();
    }
}

static void loadHistoryFromJson() {
    // ĐỌC TỪ APPDATA
    QString filePath = getAppDataPath() + "/history.json";
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return;

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) return;

    g_undoStack.clear();
    QJsonArray historyArray = doc.array();
    for (int i = 0; i < historyArray.size(); ++i) {
        QJsonObject obj = historyArray[i].toObject();
        HistoryEntry entry;

        if (obj.contains("timestamp")) {
            entry.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        } else {
            entry.timestamp = QDateTime::currentDateTime();
        }
        entry.note = obj["note"].toString();

        LinearProgram lp;
        lp.algoType = obj["algoType"].toInt();
        lp.isMaximize = obj["isMaximize"].toBool();
        lp.c_0 = obj["c_0"].toDouble();

        QJsonArray cArr = obj["c"].toArray();
        for (int j = 0; j < cArr.size(); ++j) lp.c.push_back(cArr[j].toDouble());

        QJsonArray aArr = obj["A"].toArray();
        for (int j = 0; j < aArr.size(); ++j) {
            QJsonArray rowArr = aArr[j].toArray();
            std::vector<double> row;
            for (int k = 0; k < rowArr.size(); ++k) row.push_back(rowArr[k].toDouble());
            lp.A.push_back(row);
        }

        QJsonArray signArr = obj["signs"].toArray();
        for (int j = 0; j < signArr.size(); ++j) lp.signs.push_back(signArr[j].toString());

        QJsonArray bArr = obj["b"].toArray();
        for (int j = 0; j < bArr.size(); ++j) lp.b.push_back(bArr[j].toDouble());

        QJsonArray boundsArr = obj["varBounds"].toArray();
        for (int j = 0; j < boundsArr.size(); ++j) {
            QJsonObject vbObj = boundsArr[j].toObject();
            VarBound vb;
            vb.sign = vbObj["sign"].toString();
            vb.isFree = vbObj["isFree"].toBool();
            vb.value = vbObj["value"].toDouble();
            lp.varBounds.push_back(vb);
        }

        entry.lp = lp;
        g_undoStack.push_back(entry);
    }
}

// -----------------------------------------------------------------------
// [BẢN MAX LEVEL] Ô nhập liệu thông minh Algebraic Parser
// -----------------------------------------------------------------------
class MathInput : public QLineEdit {
public:
    explicit MathInput(QWidget *parent = nullptr) : QLineEdit(parent) {}

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

    // [FIX TAB ĐIỀU HƯỚNG]
    // Dùng riêng cho ô hệ số cuối cùng của hàm mục tiêu:
    // khi người dùng nhấn Tab, focus sẽ nhảy thẳng xuống ô hệ số đầu tiên của bảng ràng buộc.
    void setCustomTabTarget(QWidget *target) {
        customTabTarget = target;
    }

protected:
    bool event(QEvent *event) override {
        // [FIX TAB ĐIỀU HƯỚNG - TRIỆT ĐỂ]
        // Qt thường xử lý phím Tab ở tầng event/focus traversal trước khi vào keyPressEvent,
        // nên phải chặn trực tiếp tại event() để đảm bảo đang đứng ở ô x_n của hàm mục tiêu
        // thì Tab luôn nhảy xuống ô đầu tiên của bảng ràng buộc.
        if (event->type() == QEvent::KeyPress && customTabTarget) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Tab &&
                keyEvent->modifiers() == Qt::NoModifier) {
                this->setValue(this->value());

                customTabTarget->setFocus(Qt::TabFocusReason);
                if (QLineEdit *targetInput = qobject_cast<QLineEdit*>(customTabTarget)) {
                    targetInput->selectAll();
                }

                event->accept();
                return true;
            }
        }

        return QLineEdit::event(event);
    }

    void keyPressEvent(QKeyEvent *event) override {
        QLineEdit::keyPressEvent(event);
    }

    void focusInEvent(QFocusEvent *event) override {
        QLineEdit::focusInEvent(event);
        QTimer::singleShot(0, this, [this](){ this->selectAll(); });
    }

    void focusOutEvent(QFocusEvent *event) override {
        QLineEdit::focusOutEvent(event);
        this->setValue(this->value());
    }

    void wheelEvent(QWheelEvent *event) override {
        double currentVal = this->value();
        int step = (event->angleDelta().y() > 0) ? 1 : -1;
        this->setValue(currentVal + step);
        this->selectAll();
        event->accept();
    }

private:
    QWidget *customTabTarget = nullptr;
};

// -----------------------------------------------------------------------
// [FIX TAB CHO QCOMBOBOX]
// QComboBox đôi khi tự xử lý phím Tab trước khi setTabOrder có hiệu lực,
// đặc biệt khi nằm trong QTableWidget::cellWidget.
// Vì vậy tạo ComboBox riêng để ép Tab đi đúng target mong muốn.
// -----------------------------------------------------------------------
class TabComboBox : public QComboBox {
public:
    explicit TabComboBox(QWidget *parent = nullptr) : QComboBox(parent) {
        setFocusPolicy(Qt::StrongFocus);
    }

    void setCustomTabTarget(QWidget *target) {
        customTabTarget = target;
    }

protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::KeyPress && customTabTarget) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

            if (keyEvent->key() == Qt::Key_Tab &&
                keyEvent->modifiers() == Qt::NoModifier) {
                customTabTarget->setFocus(Qt::TabFocusReason);

                if (QLineEdit *targetInput = qobject_cast<QLineEdit*>(customTabTarget)) {
                    targetInput->selectAll();
                }

                event->accept();
                return true;
            }
        }

        return QComboBox::event(event);
    }

private:
    QWidget *customTabTarget = nullptr;
};


// =======================================================================
// [FIX TAB ĐIỀU HƯỚNG]
// Thiết lập luồng Tab thống nhất cho toàn bộ màn hình nhập liệu.
// Luồng mong muốn:
// - Ô cuối cùng hàm mục tiêu -> ô đầu tiên bảng ràng buộc.
// - Ô b_i cuối cùng -> ô dấu biến đầu tiên.
// - Các ô dấu biến đi dọc từ trên xuống.
// - Ô dấu biến cuối cùng -> ô chọn phương pháp giải.
// - Ô chọn phương pháp giải -> nút Solve.
// =======================================================================
static void setupInputTabOrder(Ui::Dashboard *ui) {
    if (!ui) return;

    int objLastCol = ui->table_functionTarget->columnCount() - 1;
    if (objLastCol >= 0 && ui->matrix->rowCount() > 0 && ui->matrix->columnCount() > 0) {
        QWidget *lastObjectiveCell = ui->table_functionTarget->cellWidget(0, objLastCol);
        QWidget *firstConstraintCell = ui->matrix->cellWidget(0, 0);

        if (lastObjectiveCell && firstConstraintCell) {
            if (MathInput *lastInput = dynamic_cast<MathInput*>(lastObjectiveCell)) {
                lastInput->setCustomTabTarget(firstConstraintCell);
            }
            QWidget::setTabOrder(lastObjectiveCell, firstConstraintCell);
        }
    }

    int matrixRows = ui->matrix->rowCount();
    int matrixCols = ui->matrix->columnCount();
    if (matrixRows > 0 && matrixCols > 0 && ui->table_varConstraint->rowCount() > 0) {
        QWidget *lastConstraintCell = ui->matrix->cellWidget(matrixRows - 1, matrixCols - 1);
        QWidget *firstVarSignCell = ui->table_varConstraint->cellWidget(0, 1);

        if (lastConstraintCell && firstVarSignCell) {
            if (MathInput *lastInput = dynamic_cast<MathInput*>(lastConstraintCell)) {
                lastInput->setCustomTabTarget(firstVarSignCell);
            }
            QWidget::setTabOrder(lastConstraintCell, firstVarSignCell);
        }
    }

    int varRows = ui->table_varConstraint->rowCount();
    for (int row = 0; row < varRows; ++row) {
        QWidget *currentVarSignCell = ui->table_varConstraint->cellWidget(row, 1);
        if (!currentVarSignCell) continue;

        currentVarSignCell->setFocusPolicy(Qt::StrongFocus);

        if (row + 1 < varRows) {
            QWidget *nextVarSignCell = ui->table_varConstraint->cellWidget(row + 1, 1);
            if (nextVarSignCell) {
                nextVarSignCell->setFocusPolicy(Qt::StrongFocus);

                // Ép Tab ở combo hiện tại xuống combo dòng kế tiếp.
                if (TabComboBox *currentCombo = dynamic_cast<TabComboBox*>(currentVarSignCell)) {
                    currentCombo->setCustomTabTarget(nextVarSignCell);
                }

                QWidget::setTabOrder(currentVarSignCell, nextVarSignCell);
            }
        } else {
            if (ui->comboAlgorithm) {
                ui->comboAlgorithm->setFocusPolicy(Qt::StrongFocus);

                // Ép Tab ở combo cuối cùng nhảy sang ô phương pháp giải.
                if (TabComboBox *currentCombo = dynamic_cast<TabComboBox*>(currentVarSignCell)) {
                    currentCombo->setCustomTabTarget(ui->comboAlgorithm);
                }

                QWidget::setTabOrder(currentVarSignCell, ui->comboAlgorithm);
            }

            if (ui->comboAlgorithm && ui->pushButton_3) {
                ui->pushButton_3->setFocusPolicy(Qt::StrongFocus);
                QWidget::setTabOrder(ui->comboAlgorithm, ui->pushButton_3);
            }
        }
    }
}

Dashboard::Dashboard(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Dashboard)
{
    ui->setupUi(this);

    // Tự động tải lịch sử lên khi khởi động
    loadHistoryFromJson();

    ui->matrix->hide();
    ui->table_functionTarget->hide();
    ui->label_3->hide();
    ui->label_4->hide();
    ui->label_5->hide();
    ui->max_min->hide();
    ui->table_varConstraint->hide();
    this->setWindowIcon(QIcon(":/logo.png"));

    this->wd_solve = nullptr;
    this->setWindowTitle("Nhập liệu");
    this->setWindowState(Qt::WindowMaximized);

    // =======================================================================
    // MỞ CỬA SỔ CHỌN BÀI TOÁN KHI BẤM NÚT UNDO/LỊCH SỬ
    // =======================================================================
    QPushButton *btnUndo = this->findChild<QPushButton*>("btn_Undo");
    if (!btnUndo) {
        for (QPushButton *btn : this->findChildren<QPushButton*>()) {
            if (btn->text().contains("Quay lại") || btn->text().contains("Khôi phục") || btn->text() == "Undo" || btn->text().contains("Lịch sử")) {
                btnUndo = btn;
                break;
            }
        }
    }

    if (btnUndo) {
        connect(btnUndo, &QPushButton::clicked, this, [this]() {
            if (g_undoStack.empty()) {
                QMessageBox::information(this, "Thông báo", "Lịch sử trống! Bạn chưa giải bài toán nào.");
                return;
            }

            QDialog historyDialog(this);
            historyDialog.setWindowTitle("Lịch sử giải bài toán");
            historyDialog.resize(820, 520);

            // [FIX LINUX] Đọc settings từ AppData để HĐH Linux không bị lỗi Read-only
            QString iniPath = getAppDataPath() + "/settings.ini";
            QSettings settings(iniPath, QSettings::IniFormat);
            bool isDark = settings.value("dark_mode", false).toBool();

            if (isDark) {
                historyDialog.setStyleSheet(
                    "QDialog { background-color: #1E1E2E; }"
                    "QLabel { color: #CDD6F4; }"
                    "QLineEdit { background-color: #181825; color: #CDD6F4; border: 1px solid #45475A; border-radius: 5px; padding: 7px 10px; }"
                    "QLineEdit:focus { border: 1px solid #89B4FA; }"
                    "QListWidget { background-color: #181825; color: #CDD6F4; border: 1px solid #45475A; border-radius: 4px; outline: none; }"
                    "QListWidget::item { padding: 12px; border-bottom: 1px solid #313244; }"
                    "QListWidget::item:selected { background-color: #313244; color: #89B4FA; font-weight: bold; }"
                    // Bổ sung màu cho Checkbox
                    "QListWidget::indicator { width: 16px; height: 16px; }"
                    "QListWidget::indicator:unchecked { border: 1px solid #45475A; background-color: #1E1E2E; border-radius: 3px; }"
                    "QListWidget::indicator:checked { border: 1px solid #89B4FA; background-color: #89B4FA; border-radius: 3px; image: url(:/icons/check.png); }" // Bạn có thể bỏ dòng image nếu không có icon
                    );
            } else {
                historyDialog.setStyleSheet(
                    "QDialog { background-color: #F5F7FA; }"
                    "QLabel { color: #333333; }"
                    "QLineEdit { background-color: #FFFFFF; color: #333333; border: 1px solid #CCCCCC; border-radius: 5px; padding: 7px 10px; }"
                    "QLineEdit:focus { border: 1px solid #0078D7; }"
                    "QListWidget { background-color: #FFFFFF; color: #333333; border: 1px solid #CCCCCC; border-radius: 4px; outline: none; }"
                    "QListWidget::item { padding: 12px; border-bottom: 1px solid #EEEEEE; }"
                    "QListWidget::item:selected { background-color: #E6F0FA; color: #0056b3; font-weight: bold; }"
                    // Bổ sung màu cho Checkbox
                    "QListWidget::indicator { width: 16px; height: 16px; }"
                    "QListWidget::indicator:unchecked { border: 1px solid #CCCCCC; background-color: #FFFFFF; border-radius: 3px; }"
                    "QListWidget::indicator:checked { border: 1px solid #0078D7; background-color: #0078D7; border-radius: 3px; }"
                    );
            }

            QVBoxLayout *layout = new QVBoxLayout(&historyDialog);
            QLabel *lblTitle = new QLabel("Chọn một bài toán cũ để tải lại:", &historyDialog);
            lblTitle->setStyleSheet("font-weight: bold; font-size: 13pt; margin-bottom: 5px;");
            layout->addWidget(lblTitle);

            QLineEdit *searchEdit = new QLineEdit(&historyDialog);
            searchEdit->setPlaceholderText("🔎 Tìm theo hàm mục tiêu, ngày (dd/MM/yyyy), giờ (HH:mm:ss), ghi chú...");
            layout->addWidget(searchEdit);

            QListWidget *listWidget = new QListWidget(&historyDialog);

            auto buildObjectiveExpression = [](const LinearProgram& lp_item) -> QString {
                QString optStr = lp_item.isMaximize ? "Max Z =" : "Min Z =";
                QString zExpr = "";
                bool isFirst = true;

                if (std::abs(lp_item.c_0) > 1e-9) {
                    zExpr += QString::number(lp_item.c_0, 'g', 4);
                    isFirst = false;
                }

                for (size_t j = 0; j < lp_item.c.size(); ++j) {
                    double val = lp_item.c[j];
                    if (std::abs(val) > 1e-9) {
                        if (!isFirst) {
                            zExpr += (val > 0) ? " + " : " - ";
                        } else {
                            if (val < 0) zExpr += "-";
                        }
                        zExpr += QString::number(std::abs(val), 'g', 4) + "x" + QString::number(j + 1);
                        isFirst = false;
                    }
                }

                if (zExpr.isEmpty()) zExpr = "0";
                return optStr + " " + zExpr;
            };

            auto buildHistoryDescription = [&](const HistoryEntry& entry) -> QString {
                const LinearProgram& lp_item = entry.lp;
                QString itemTime = entry.timestamp.time().toString("HH:mm:ss");
                int vars = lp_item.c.size();
                int constraints = lp_item.A.size();

                QString algoName = "";
                if (lp_item.algoType == 0) algoName = "Đơn hình";
                else if (lp_item.algoType == 1) algoName = "Bland";
                else if (lp_item.algoType == 2) algoName = "2 Pha";
                else algoName = "Tự động";

                QString noteText = entry.note.trimmed();
                QString notePart = noteText.isEmpty() ? "" : " | 📝 " + noteText;

                // [NOTE HISTORY UI]
                // Ghi chú được hiển thị ngay sát bên phải cột/phần "Phương pháp giải"
                // để người dùng đọc cùng một dòng, không tạo thêm dòng lớn bên dưới.
                QString description = QString("   ▶ [%1] : [%2] | %3 Biến, %4 Ràng buộc | %5%6")
                                          .arg(itemTime)
                                          .arg(buildObjectiveExpression(lp_item))
                                          .arg(vars)
                                          .arg(constraints)
                                          .arg(algoName)
                                          .arg(notePart);

                return description;
            };

            auto buildSearchText = [&](const HistoryEntry& entry) -> QString {
                QString date1 = entry.timestamp.date().toString("dd/MM/yyyy");
                QString date2 = entry.timestamp.date().toString("yyyy-MM-dd");
                QString time1 = entry.timestamp.time().toString("HH:mm:ss");
                QString time2 = entry.timestamp.time().toString("HH:mm");

                return QString("%1 %2 %3 %4 %5 %6")
                    .arg(date1, date2, time1, time2, buildHistoryDescription(entry), entry.note);
            };

            QStringListModel *suggestionModel = new QStringListModel(&historyDialog);
            QCompleter *historyCompleter = new QCompleter(suggestionModel, &historyDialog);
            historyCompleter->setCaseSensitivity(Qt::CaseInsensitive);
            historyCompleter->setCompletionMode(QCompleter::PopupCompletion);
            historyCompleter->setMaxVisibleItems(8);
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
            historyCompleter->setFilterMode(Qt::MatchContains);
#endif
            searchEdit->setCompleter(historyCompleter);

            auto refreshSuggestions = [&]() {
                QStringList suggestions;
                for (int i = (int)g_undoStack.size() - 1; i >= 0; --i) {
                    const HistoryEntry& entry = g_undoStack[i];
                    suggestions << buildObjectiveExpression(entry.lp);
                    suggestions << entry.timestamp.date().toString("dd/MM/yyyy");
                    suggestions << entry.timestamp.date().toString("yyyy-MM-dd");
                    suggestions << entry.timestamp.time().toString("HH:mm:ss");
                    suggestions << buildHistoryDescription(entry).trimmed();
                    if (!entry.note.trimmed().isEmpty()) {
                        suggestions << entry.note.trimmed();
                    }
                }
                suggestions.removeDuplicates();
                suggestionModel->setStringList(suggestions);
            };

            auto isMatchedSearch = [&](const HistoryEntry& entry) -> bool {
                QString keyword = searchEdit->text().trimmed().toLower();
                if (keyword.isEmpty()) return true;

                QString searchText = buildSearchText(entry).toLower();
                QString searchTextNoSpace = searchText;
                searchTextNoSpace.remove(' ');

                QStringList terms = keyword.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                for (QString term : terms) {
                    term = term.trimmed().toLower();
                    if (term.isEmpty()) continue;

                    QString termNoSpace = term;
                    termNoSpace.remove(' ');

                    if (!searchText.contains(term) && !searchTextNoSpace.contains(termNoSpace)) {
                        return false;
                    }
                }

                return true;
            };

            // Hàm làm mới danh sách (Dùng cho cả lúc mới mở, lúc tìm kiếm & lúc xóa mục)
            auto populateList = [&]() {
                listWidget->clear();
                QString currentDateStr = "";
                int visibleCount = 0;

                for (int i = (int)g_undoStack.size() - 1; i >= 0; --i) {
                    const HistoryEntry& entry = g_undoStack[i];

                    if (!isMatchedSearch(entry)) {
                        continue;
                    }

                    QString itemDate = entry.timestamp.date().toString("dd/MM/yyyy");

                    if (itemDate != currentDateStr) {
                        QListWidgetItem *dateHeader = new QListWidgetItem("📅 Ngày: " + itemDate);
                        QFont f = dateHeader->font();
                        f.setBold(true);
                        dateHeader->setFont(f);
                        dateHeader->setFlags(dateHeader->flags() & ~Qt::ItemIsSelectable);
                        dateHeader->setData(Qt::UserRole, -1);

                        if (isDark) {
                            dateHeader->setBackground(QColor("#313244"));
                            dateHeader->setForeground(QColor("#A6ADC8"));
                        } else {
                            dateHeader->setBackground(QColor("#E4E7EB"));
                            dateHeader->setForeground(QColor("#333333"));
                        }

                        listWidget->addItem(dateHeader);
                        currentDateStr = itemDate;
                    }

                    QListWidgetItem *item = new QListWidgetItem(buildHistoryDescription(entry));
                    item->setData(Qt::UserRole, i);
                    item->setToolTip(buildHistoryDescription(entry));

                    // BẬT CHECKBOX CHO TỪNG DÒNG
                    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                    item->setCheckState(Qt::Unchecked);

                    // Ghi chú đã nằm cùng dòng với phương pháp giải nên giữ chiều cao gọn.
                    item->setSizeHint(QSize(item->sizeHint().width(), 44));

                    listWidget->addItem(item);
                    visibleCount++;
                }

                if (visibleCount == 0) {
                    QListWidgetItem *emptyItem = new QListWidgetItem("Không tìm thấy bài toán phù hợp với từ khóa tìm kiếm.");
                    emptyItem->setData(Qt::UserRole, -1);
                    emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
                    if (isDark) {
                        emptyItem->setForeground(QColor("#A6ADC8"));
                    } else {
                        emptyItem->setForeground(QColor("#6B7280"));
                    }
                    listWidget->addItem(emptyItem);
                    return;
                }

                // Tự động focus dòng đầu tiên hợp lệ
                for(int i = 0; i < listWidget->count(); ++i) {
                    if (listWidget->item(i)->data(Qt::UserRole).toInt() != -1) {
                        listWidget->setCurrentRow(i);
                        break;
                    }
                }
            };

            refreshSuggestions();
            connect(searchEdit, &QLineEdit::textChanged, &historyDialog, populateList);

            // Gọi hàm render danh sách
            populateList();
            layout->addWidget(listWidget);

            // GIAO DIỆN NÚT BẤM
            QHBoxLayout *btnLayout = new QHBoxLayout();
            btnLayout->setSpacing(8);
            QPushButton *btnClearAll = new QPushButton("🗑 Xóa tất cả", &historyDialog);
            QPushButton *btnDelete = new QPushButton("❌ Xóa mục chọn", &historyDialog);
            QPushButton *btnNote = new QPushButton("📝 Ghi chú", &historyDialog);
            QPushButton *btnRestore = new QPushButton("Tải lại dữ liệu", &historyDialog);
            QPushButton *btnCancel = new QPushButton("Hủy bỏ", &historyDialog);

            if (isDark) {
                btnCancel->setStyleSheet("QPushButton { background-color: #313244; color: #CDD6F4; border: 1px solid #45475A; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #45475A; }");
                btnDelete->setStyleSheet("QPushButton { background-color: #F38BA8; color: #1E1E2E; border: none; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #EBA0AC; }");
                btnClearAll->setStyleSheet("QPushButton { background-color: #F38BA8; color: #1E1E2E; border: none; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #EBA0AC; }");
                btnNote->setStyleSheet("QPushButton { background-color: #F9E2AF; color: #1E1E2E; border: none; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #F5C2E7; }");
                btnRestore->setStyleSheet("QPushButton { background-color: #89B4FA; color: #1E1E2E; border: none; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #B4BEFE; }");
            } else {
                btnCancel->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #4B5563; border: 1px solid #D1D5DB; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #F3F4F6; }");
                btnDelete->setStyleSheet("QPushButton { background-color: #D32F2F; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #B71C1C; }");
                btnClearAll->setStyleSheet("QPushButton { background-color: #D32F2F; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #B71C1C; }");
                btnNote->setStyleSheet("QPushButton { background-color: #F59E0B; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #D97706; }");
                btnRestore->setStyleSheet("QPushButton { background-color: #0078D7; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 15px; font-weight: bold; } QPushButton:hover { background-color: #005A9E; }");
            }

            btnCancel->setCursor(Qt::PointingHandCursor);
            btnDelete->setCursor(Qt::PointingHandCursor);
            btnClearAll->setCursor(Qt::PointingHandCursor);
            btnNote->setCursor(Qt::PointingHandCursor);
            btnRestore->setCursor(Qt::PointingHandCursor);

            // Ép các nút trong hộp thoại lịch sử có cùng kích thước
            QList<QPushButton*> historyButtons = {btnClearAll, btnDelete, btnNote, btnCancel, btnRestore};
            for (QPushButton *btn : historyButtons) {
                btn->setFixedSize(145, 38);
            }

            // Bố trí nút xóa sang trái, 2 nút kia sang phải
            btnLayout->addWidget(btnClearAll);
            btnLayout->addWidget(btnDelete);
            btnLayout->addWidget(btnNote);
            btnLayout->addStretch();
            btnLayout->addWidget(btnCancel);
            btnLayout->addWidget(btnRestore);
            layout->addLayout(btnLayout);

            connect(btnCancel, &QPushButton::clicked, &historyDialog, &QDialog::reject);

            // CHỨC NĂNG XÓA TẤT CẢ
            connect(btnClearAll, &QPushButton::clicked, [&]() {
                QMessageBox msgBox(&historyDialog);
                msgBox.setWindowTitle("Xóa toàn bộ");
                msgBox.setText("Bạn có chắc chắn muốn xóa TOÀN BỘ lịch sử? Hành động này không thể hoàn tác.");
                msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                msgBox.setDefaultButton(QMessageBox::No);

                if (isDark) {
                    msgBox.setStyleSheet("QMessageBox { background-color: #1E1E2E; } QLabel { color: #CDD6F4; } QPushButton { background-color: #313244; color: #CDD6F4; border: 1px solid #45475A; border-radius: 4px; padding: 5px 15px; } QPushButton:hover { background-color: #45475A; }");
                } else {
                    msgBox.setStyleSheet("QMessageBox { background-color: #F5F7FA; } QLabel { color: #333333; } QPushButton { background-color: #FFFFFF; color: #333333; border: 1px solid #CCCCCC; border-radius: 4px; padding: 5px 15px; } QPushButton:hover { background-color: #E8E8E8; }");
                }

                if (msgBox.exec() == QMessageBox::Yes) {
                    g_undoStack.clear();
                    saveHistoryToJson();
                    historyDialog.reject(); // Đóng luôn cửa sổ vì hết lịch sử
                }
            });

            // CHỨC NĂNG XÓA MỤC ĐÃ CHỌN
            connect(btnDelete, &QPushButton::clicked, [&]() {
                std::vector<int> toDelete;

                for(int i = 0; i < listWidget->count(); ++i) {
                    QListWidgetItem* item = listWidget->item(i);
                    if (item->data(Qt::UserRole).toInt() != -1 && item->checkState() == Qt::Checked) {
                        toDelete.push_back(item->data(Qt::UserRole).toInt());
                    }
                }

                if (toDelete.empty()) {
                    QMessageBox::warning(&historyDialog, "Thông báo", "Vui lòng chọn (đánh dấu tick) ít nhất một bài toán để xóa.");
                    return;
                }

                QMessageBox msgBox(&historyDialog);
                msgBox.setWindowTitle("Xác nhận");
                msgBox.setText(QString("Bạn có chắc chắn muốn xóa %1 bài toán đã chọn khỏi lịch sử?").arg(toDelete.size()));
                msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                msgBox.setDefaultButton(QMessageBox::No);

                if (isDark) {
                    msgBox.setStyleSheet("QMessageBox { background-color: #1E1E2E; } QLabel { color: #CDD6F4; } QPushButton { background-color: #313244; color: #CDD6F4; border: 1px solid #45475A; border-radius: 4px; padding: 5px 15px; } QPushButton:hover { background-color: #45475A; }");
                } else {
                    msgBox.setStyleSheet("QMessageBox { background-color: #F5F7FA; } QLabel { color: #333333; } QPushButton { background-color: #FFFFFF; color: #333333; border: 1px solid #CCCCCC; border-radius: 4px; padding: 5px 15px; } QPushButton:hover { background-color: #E8E8E8; }");
                }

                if (msgBox.exec() == QMessageBox::Yes) {
                    std::sort(toDelete.begin(), toDelete.end(), std::greater<int>());

                    for (int idx : toDelete) {
                        g_undoStack.erase(g_undoStack.begin() + idx);
                    }

                    saveHistoryToJson();
                    refreshSuggestions();
                    populateList();

                    if (g_undoStack.empty()) {
                        historyDialog.reject();
                    }
                }
            });

            // CHỨC NĂNG KHÔI PHỤC (Restore)
            int restoreRealIndex = -1;

            auto getCheckedHistoryIndexes = [&]() -> std::vector<int> {
                std::vector<int> checkedIndexes;

                for (int i = 0; i < listWidget->count(); ++i) {
                    QListWidgetItem *item = listWidget->item(i);
                    if (item && item->data(Qt::UserRole).toInt() != -1 && item->checkState() == Qt::Checked) {
                        checkedIndexes.push_back(item->data(Qt::UserRole).toInt());
                    }
                }

                return checkedIndexes;
            };

            auto handleNote = [&]() {
                std::vector<int> checkedIndexes = getCheckedHistoryIndexes();
                int noteIndex = -1;

                if (checkedIndexes.size() > 1) {
                    QMessageBox::warning(
                        &historyDialog,
                        "Thông báo",
                        "Bạn chỉ được chọn 1 bài toán để ghi chú.\n"
                        "Vui lòng bỏ chọn các bài toán còn lại."
                        );
                    return;
                }

                if (checkedIndexes.size() == 1) {
                    noteIndex = checkedIndexes[0];
                } else {
                    QListWidgetItem *selectedItem = listWidget->currentItem();
                    if (!selectedItem || selectedItem->data(Qt::UserRole).toInt() == -1) {
                        QMessageBox::warning(&historyDialog, "Thông báo", "Vui lòng chọn một bài toán để ghi chú.");
                        return;
                    }
                    noteIndex = selectedItem->data(Qt::UserRole).toInt();
                }

                if (noteIndex < 0 || noteIndex >= (int)g_undoStack.size()) return;

                bool ok = false;
                QString oldNote = g_undoStack[noteIndex].note;
                QString newNote = QInputDialog::getMultiLineText(
                    &historyDialog,
                    "Ghi chú bài toán",
                    "Nhập ghi chú cho bài toán đã chọn:",
                    oldNote,
                    &ok
                    );

                if (!ok) return;

                g_undoStack[noteIndex].note = newNote.trimmed();
                saveHistoryToJson();
                refreshSuggestions();
                populateList();

                // Giữ lại focus ở bài toán vừa ghi chú nếu nó còn đang hiển thị.
                for (int row = 0; row < listWidget->count(); ++row) {
                    QListWidgetItem *item = listWidget->item(row);
                    if (item && item->data(Qt::UserRole).toInt() == noteIndex) {
                        listWidget->setCurrentRow(row);
                        break;
                    }
                }
            };
            connect(btnNote, &QPushButton::clicked, &historyDialog, handleNote);

            auto handleRestore = [&]() {
                std::vector<int> checkedIndexes = getCheckedHistoryIndexes();

                if (checkedIndexes.size() > 1) {
                    QMessageBox::warning(
                        &historyDialog,
                        "Thông báo",
                        "Bạn chỉ được tích chọn 1 bài toán để tải lại dữ liệu.\n"
                        "Vui lòng bỏ chọn các bài toán còn lại."
                        );
                    return;
                }

                if (checkedIndexes.size() == 1) {
                    restoreRealIndex = checkedIndexes[0];
                    historyDialog.accept();
                    return;
                }

                QListWidgetItem *selectedItem = listWidget->currentItem();
                if (!selectedItem || selectedItem->data(Qt::UserRole).toInt() == -1) {
                    QMessageBox::warning(&historyDialog, "Thông báo", "Vui lòng chọn một bài toán để tải lại dữ liệu.");
                    return;
                }

                restoreRealIndex = selectedItem->data(Qt::UserRole).toInt();
                historyDialog.accept();
            };
            connect(btnRestore, &QPushButton::clicked, &historyDialog, handleRestore);
            connect(listWidget, &QListWidget::itemDoubleClicked, &historyDialog, handleRestore);

            // NẾU NGƯỜI DÙNG BẤM TẢI LẠI
            if (historyDialog.exec() == QDialog::Accepted) {
                int realIndex = restoreRealIndex;
                if (realIndex < 0 || realIndex >= (int)g_undoStack.size()) return;

                HistoryEntry selectedEntry = g_undoStack[realIndex];
                LinearProgram lp = selectedEntry.lp;

                g_pendingRestoreIndex = realIndex;

                // --- BẮT ĐẦU QUÁ TRÌNH KHÔI PHỤC LÊN GIAO DIỆN CHÍNH ---
                int n = lp.c.size();
                int m = lp.A.size();

                ui->spinBox->setValue(n);
                ui->spinBox_2->setValue(m);

                this->setupConstraintsTable(m, n);
                this->setupVariableConstraints(n);
                this->setupObjectiveFunctionTable(n);

                ui->label_3->show();
                ui->label_4->show();
                ui->label_5->show();
                ui->table_functionTarget->show();
                ui->table_varConstraint->show();
                ui->matrix->show();
                ui->max_min->show();

                ui->comboAlgorithm->setCurrentIndex(lp.algoType);
                ui->max_min->setCurrentText(lp.isMaximize ? "Max" : "Min");

                auto *spinC0 = dynamic_cast<MathInput*>(ui->table_functionTarget->cellWidget(0, 0));
                if (spinC0) spinC0->setValue(lp.c_0);
                for (int j = 0; j < n; ++j) {
                    auto *spinC = dynamic_cast<MathInput*>(ui->table_functionTarget->cellWidget(0, j + 1));
                    if (spinC) spinC->setValue(lp.c[j]);
                }

                for (int i = 0; i < m; ++i) {
                    for (int j = 0; j < n; ++j) {
                        auto *spinA = dynamic_cast<MathInput*>(ui->matrix->cellWidget(i, j));
                        if (spinA) spinA->setValue(lp.A[i][j]);
                    }
                    auto *comboSign = qobject_cast<QComboBox*>(ui->matrix->cellWidget(i, n));
                    if (comboSign && i < (int)lp.signs.size()) {
                        comboSign->setCurrentText(lp.signs[i]);
                    }
                    auto *spinB = dynamic_cast<MathInput*>(ui->matrix->cellWidget(i, n + 1));
                    if (spinB && i < (int)lp.b.size()) {
                        spinB->setValue(lp.b[i]);
                    }
                }

                for (int i = 0; i < n; ++i) {
                    if (i < (int)lp.varBounds.size()) {
                        auto *comboVarSign = qobject_cast<QComboBox*>(ui->table_varConstraint->cellWidget(i, 1));
                        if (comboVarSign) comboVarSign->setCurrentText(lp.varBounds[i].sign);

                        auto *spinVarVal = dynamic_cast<MathInput*>(ui->table_varConstraint->cellWidget(i, 2));
                        if (spinVarVal) {
                            // [FIX HISTORY RESTORE]
                            // Cột thứ 3 "Giá trị" của bảng ràng buộc dấu luôn phải đóng/khóa,
                            // kể cả khi tải lại bài toán từ lịch sử.
                            // Tránh trường hợp khi sign là >= hoặc <= thì ô này tự bật/mở lại.
                            spinVarVal->setValue(0.0);
                            spinVarVal->setReadOnly(true);
                            spinVarVal->setEnabled(false);
                            spinVarVal->clearFocus();
                        }
                    }
                }

                QMessageBox::information(this, "Tải thành công", "Đã nạp lại dữ liệu bài toán từ Lịch sử!");
            }
        });
    }
}

Dashboard::~Dashboard()
{
    delete ui;
}

MathInput* Dashboard::createSpinBox(QWidget *parent) {
    MathInput *input = new MathInput(parent);
    input->setValue(0.0);
    input->setAlignment(Qt::AlignCenter);

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

        // [FIX TAB ĐIỀU HƯỚNG]
        // Ô cuối cùng của hàm mục tiêu là hệ số của biến x_n.
        // Khi nhấn Tab tại ô này, chuyển focus xuống ô đầu tiên của bảng ràng buộc.
        if (i == n && ui->matrix->rowCount() > 0 && ui->matrix->columnCount() > 0) {
            spinBox->setCustomTabTarget(ui->matrix->cellWidget(0, 0));
        }

        ui->table_functionTarget->setCellWidget(0, i, spinBox);
    }
    ui->table_functionTarget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // [FIX TAB ĐIỀU HƯỚNG]
    // Sau khi tạo bảng hàm mục tiêu, cập nhật lại toàn bộ luồng Tab.
    setupInputTabOrder(ui);
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

    // [FIX TAB ĐIỀU HƯỚNG]
    // Sau khi tạo bảng ràng buộc, cập nhật lại luồng Tab.
    setupInputTabOrder(ui);
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

        TabComboBox *comboSign = new TabComboBox();
        comboSign->setFocusPolicy(Qt::StrongFocus);
        comboSign->addItems({">=", "<=", "free"});
        if (i < oldN) {
            int idx = comboSign->findText(oldSigns[i]);
            if (idx >= 0) comboSign->setCurrentIndex(idx);
        }
        ui->table_varConstraint->setCellWidget(i, 1, comboSign);

        MathInput *spinBox = createSpinBox();
        spinBox->setReadOnly(true);
        spinBox->setEnabled(false);
        spinBox->setValue(0.0);
        ui->table_varConstraint->setCellWidget(i, 2, spinBox);

        connect(comboSign, &QComboBox::currentTextChanged,
                [spinBox](const QString &text) {
                    spinBox->setValue(0.0);
                    spinBox->setEnabled(false);
                });
    }
    ui->table_varConstraint->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // [FIX TAB ĐIỀU HƯỚNG]
    // Sau khi tạo bảng ràng buộc dấu biến, cập nhật lại luồng Tab:
    // dấu biến đi dọc xuống, ô cuối sang phương pháp giải, rồi sang Solve.
    setupInputTabOrder(ui);
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

    HistoryEntry newEntry;
    newEntry.lp = local_lp;
    newEntry.timestamp = QDateTime::currentDateTime();

    // Nếu bài toán được tải từ lịch sử rồi Solve lại, giữ nguyên ghi chú cũ.
    QString preservedNote;
    if (g_pendingRestoreIndex >= 0 && g_pendingRestoreIndex < (int)g_undoStack.size()) {
        preservedNote = g_undoStack[g_pendingRestoreIndex].note;
        g_undoStack.erase(g_undoStack.begin() + g_pendingRestoreIndex);
    }
    newEntry.note = preservedNote;
    g_pendingRestoreIndex = -1;

    if (g_undoStack.size() >= 10000) {
        g_undoStack.pop_front();
    }
    g_undoStack.push_back(newEntry);
    saveHistoryToJson();

    LinearProgram originalLp = local_lp;
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
    g_pendingRestoreIndex = -1;

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
            sp->setEnabled(false);
        }
    }
}

void Dashboard::on_btn_HuongDan_clicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Hướng dẫn sử dụng");
    dialog.resize(800, 620);

    // [FIX LINUX] ĐỌC SETTINGS TỪ THƯ MỤC GỐC (GIỮ ĐÚNG THEME MAINWINDOW)
    QString iniPath = getAppDataPath() + "/settings.ini";
    QSettings settings(iniPath, QSettings::IniFormat);
    bool isDark = settings.value("dark_mode", false).toBool();

    if (isDark) {
        dialog.setStyleSheet("QDialog, QScrollArea, QWidget#scrollAreaWidgetContents { background-color: #1E1E2E; border: none; } "
                             "QLabel { color: #CDD6F4; }");
    } else {
        dialog.setStyleSheet("QDialog, QScrollArea, QWidget#scrollAreaWidgetContents { background-color: #F5F7FA; border: none; } "
                             "QLabel { color: #333333; }");
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setContentsMargins(30, 25, 30, 25);
    mainLayout->setSpacing(15);

    QLabel *titleLabel = new QLabel("HƯỚNG DẪN SỬ DỤNG PHẦN MỀM");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(isDark ? "color: #89B4FA; font-size: 22px; font-weight: bold; letter-spacing: 1px;"
                                     : "color: #0078D7; font-size: 22px; font-weight: bold; letter-spacing: 1px;");
    mainLayout->addWidget(titleLabel);

    QFrame *line = new QFrame(&dialog);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(isDark ? "background-color: #45475A; max-height: 1px; margin-bottom: 5px; border: none;"
                               : "background-color: #DDDDDD; max-height: 1px; margin-bottom: 5px; border: none;");
    mainLayout->addWidget(line);

    QScrollArea *scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *scrollContent = new QWidget();
    scrollContent->setObjectName("scrollAreaWidgetContents");
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(5, 0, 15, 0);

    QLabel *textLabel = new QLabel();
    textLabel->setTextFormat(Qt::RichText);
    textLabel->setWordWrap(true);

    textLabel->setStyleSheet(isDark ? "font-size: 18px; line-height: 1.6; color: #CDD6F4;"
                                    : "font-size: 18px; line-height: 1.6; color: #333333;");
    textLabel->setOpenExternalLinks(true);

    QString guideText = R"(
        <p style="margin-bottom: 12px;"><b>1. Khởi tạo bài toán:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Nhập <b>số biến</b> (n) và <b>số ràng buộc</b> (m).<br>
        &nbsp;&nbsp;&nbsp;&bull; Nhấn nút <b style="color: #0078D7;">OK</b> để phần mềm tạo bảng nhập liệu.</p>

        <p style="margin-bottom: 12px;"><b>2. Nhập Hàm mục tiêu (Z):</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Nhập hệ số cho từng biến (x1, x2...). Cột <b>x0</b> dùng để nhập hằng số tự do.<br>
        &nbsp;&nbsp;&nbsp;&bull; Chọn mục tiêu: <b style="color: #D9534F;">Max</b> (Tìm GTLN) hoặc <b style="color: #D9534F;">Min</b> (Tìm GTNN).</p>

        <p style="margin-bottom: 12px;"><b>3. Nhập Ma trận Ràng buộc:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Điền hệ số của các biến vào từng phương trình ràng buộc.<br>
        &nbsp;&nbsp;&nbsp;&bull; Tại cột <b>Sign</b>, chọn dấu tương ứng (<b>&lt;=</b>, <b>=</b>, <b>&gt;=</b>).<br>
        &nbsp;&nbsp;&nbsp;&bull; Điền hệ số vế phải vào cột <b>b_i</b>.</p>

        <div style="background-color: #E8E8E8; padding: 10px; border-radius: 5px; margin-bottom: 12px; border: 1px solid #CCCCCC;">
            <i>Phần mềm hỗ trợ tự động tính toán khi bạn nhập biểu thức phức tạp vào ô hệ số:</i><br>
            &nbsp;&nbsp;&nbsp;&bull; <b>Các phép tính cơ bản:</b> Nhập <code style="color: #0078D7;">pi - 1</code>, <code style="color: #0078D7;">3/7</code>, <code style="color: #0078D7;">2*e</code>...<br>
            &nbsp;&nbsp;&nbsp;&bull; <b>Lũy thừa:</b> Nhập <code style="color: #0078D7;">2^3</code>, <code style="color: #0078D7;">e^4</code>, <code style="color: #0078D7;">pi^4</code>...<br>
            &nbsp;&nbsp;&nbsp;&bull; <b>Căn số:</b> Nhập <code style="color: #0078D7;">sqrt(2)</code>, <code style="color: #0078D7;">root3(8)</code> (căn bậc 3)...<br>
            &nbsp;&nbsp;&nbsp;&bull; <b>Ngoặc đơn:</b> Nhập <code style="color: #0078D7;">(pi-1)/2</code>, <code style="color: #0078D7;">-(2^4)</code>...
        </div>

        <p style="margin-bottom: 12px;"><b>4. Ràng buộc dấu của biến:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Ở bảng góc dưới, bạn có thể chỉnh giới hạn biến (Mặc định x_i &gt;= 0).<br>
        &nbsp;&nbsp;&nbsp;&bull; Nếu biến tự do (không ràng buộc dấu), hãy chọn <b>free</b>.</p>

        <p style="margin-bottom: 20px;"><b>5. Giải bài toán:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Chọn thuật toán muốn sử dụng ở thanh menu thả xuống.<br>
        &nbsp;&nbsp;&nbsp;&bull; Nhấn nút <b style="color: #0078D7;">Solve</b> để xem chi tiết lời giải từng bước.</p>

        <p style="margin-bottom: 12px;"><b>6. Sử dụng chức năng Lịch sử và Ghi chú:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Sau mỗi lần nhấn <b style="color: #0078D7;">Solve</b> và bài toán được giải thành công, phần mềm sẽ tự động lưu bài toán vào <b>Lịch sử</b>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Nhấn nút <b>Lịch sử</b> để mở danh sách các bài toán đã giải trước đó.<br>
        &nbsp;&nbsp;&nbsp;&bull; Mỗi bài toán trong lịch sử sẽ hiển thị <b>thời gian giải</b>, <b>dạng Max/Min</b>, <b>hàm mục tiêu</b>, <b>số biến</b>, <b>số ràng buộc</b> và <b>phương pháp giải</b>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Để thêm ghi chú, chọn một bài toán trong danh sách rồi nhấn <b style="color: #0078D7;">Ghi chú</b>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Người dùng có thể nhập nội dung như: <i>bài quan trọng</i>, <i>cần kiểm tra lại</i>, <i>vô số nghiệm</i>, <i>bài kiểm tra cuối kỳ</i>...<br>
        &nbsp;&nbsp;&nbsp;&bull; Sau khi lưu, ghi chú sẽ hiển thị ngay bên phải phần <b>phương pháp giải</b> của bài toán, ví dụ: <code style="color: #0078D7;">| 2 Pha | 📝 cần xem lại</code>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Nếu bài toán được tải lại từ lịch sử rồi giải lại, ghi chú cũ vẫn được giữ để tránh mất thông tin.</p>

        <p style="margin-bottom: 12px;"><b>7. Tìm kiếm bài toán cũ trong Lịch sử:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Người dùng nhập từ khóa vào ô tìm kiếm để lọc nhanh các bài toán đã lưu.<br>
        &nbsp;&nbsp;&nbsp;&bull; Có thể tìm theo <b>hàm mục tiêu</b>, ví dụ: <code style="color: #0078D7;">Max Z</code>, <code style="color: #0078D7;">1x1 - 1x2</code>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Có thể tìm theo <b>ngày</b>, ví dụ: <code style="color: #0078D7;">06/06/2026</code> hoặc <code style="color: #0078D7;">2026-06-06</code>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Có thể tìm theo <b>giờ</b>, ví dụ: <code style="color: #0078D7;">14:07:49</code> hoặc <code style="color: #0078D7;">14:07</code>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Có thể tìm trực tiếp theo <b>nội dung ghi chú</b>, ví dụ: <code style="color: #0078D7;">bài kiểm tra</code>, <code style="color: #0078D7;">vô số nghiệm</code>, <code style="color: #0078D7;">cần xem lại</code>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Khi nhập từ khóa, phần mềm sẽ tự động gợi ý cả <b>bài toán</b> và <b>ghi chú</b> phù hợp để người dùng chọn nhanh hơn.</p>

        <p style="margin-bottom: 12px;"><b>8. Tải lại một bài toán từ Lịch sử:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Chọn một bài toán trong danh sách, sau đó nhấn <b style="color: #0078D7;">Tải lại dữ liệu</b> để đưa dữ liệu bài toán đó trở lại màn hình nhập liệu.<br>
        &nbsp;&nbsp;&nbsp;&bull; Người dùng cũng có thể tích chọn một bài toán rồi nhấn <b style="color: #0078D7;">Tải lại dữ liệu</b>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Nếu tích chọn nhiều hơn một bài toán, phần mềm sẽ yêu cầu người dùng chỉ chọn một bài toán để tải lại.<br>
        &nbsp;&nbsp;&nbsp;&bull; Sau khi tải lại, bài toán chỉ được đưa lên giao diện nhập liệu. Lịch sử <b>chưa được cập nhật ngay</b> tại bước này.</p>

        <p style="margin-bottom: 12px;"><b>9. Lưu lại bài toán sau khi tải từ Lịch sử:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Khi người dùng tải một bài toán cũ từ lịch sử, chỉnh sửa dữ liệu nếu cần, sau đó nhấn <b style="color: #0078D7;">Solve</b>, phần mềm mới cập nhật lại lịch sử.<br>
        &nbsp;&nbsp;&nbsp;&bull; Cách làm này giúp tránh việc lưu nhầm bài toán khi người dùng chỉ muốn xem lại dữ liệu cũ mà chưa giải lại.</p>

        <p style="margin-bottom: 12px;"><b>10. Xóa lịch sử bài toán:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Để xóa một hoặc nhiều bài toán, tích chọn các bài toán cần xóa rồi nhấn <b style="color: #D9534F;">Xóa mục chọn</b>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Để xóa toàn bộ lịch sử, nhấn <b style="color: #D9534F;">Xóa tất cả</b>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Khi xóa lịch sử, phần mềm sẽ hỏi xác nhận trước khi thực hiện để tránh xóa nhầm dữ liệu.</p>

        <p style="margin-bottom: 12px;"><b>11. Sử dụng cửa sổ Kết quả / WdSolve:</b><br>
        &nbsp;&nbsp;&nbsp;&bull; Sau khi nhấn <b style="color: #0078D7;">Solve</b>, cửa sổ <b>Kết quả tính toán</b> sẽ hiển thị <b>giá trị tối ưu</b>, <b>nghiệm tối ưu</b> và <b>các bước thực thi</b> của thuật toán.<br>
        &nbsp;&nbsp;&nbsp;&bull; Khu vực <b>Nghiệm tối ưu</b> cho biết giá trị của từng biến. Nếu bài toán có <b>vô số nghiệm</b>, phần mềm sẽ hiển thị thêm tập nghiệm hoặc hướng nghiệm tối ưu để người dùng dễ theo dõi.<br>
        &nbsp;&nbsp;&nbsp;&bull; Tab <b>[w, x, z] HIỂN THỊ DẠNG TỪ VỰNG</b> dùng để xem lời giải theo từng từ vựng, gồm biến vào, biến ra, phép xoay, kết luận và các giải thích kèm theo.<br>
        &nbsp;&nbsp;&nbsp;&bull; Tab <b>HIỂN THỊ DẠNG BẢNG</b> dùng để xem các bước giải theo dạng bảng đơn hình, phù hợp khi muốn kiểm tra hệ số, dòng trụ và cột trụ rõ hơn.<br>
        &nbsp;&nbsp;&nbsp;&bull; Nhấn <b>Biểu diễn hình học</b> để xem miền nghiệm và điểm tối ưu trên đồ thị. Chức năng này chỉ hỗ trợ cho bài toán có <b>2 biến</b>.<br>
        &nbsp;&nbsp;&nbsp;&bull; Nhấn <b>Hỏi/Đáp</b> để mở Chatbot hỗ trợ giải thích bài toán, cách đọc từ vựng, ý nghĩa nghiệm tối ưu và các bước biến đổi. Nếu chưa có API Key, gõ <code style="color: #0078D7;">/key</code> trong cửa sổ chat để cài đặt.<br>
        &nbsp;&nbsp;&nbsp;&bull; Tích chọn <b>Xem lời giải ở dạng PDF</b> rồi nhấn <b>OK</b> để mở cửa sổ xem lời giải.<br>
        &nbsp;&nbsp;&nbsp;&bull; Trong cửa sổ xem lời giải với PDF, người dùng có thể nhấn <b>Tải xuống PDF</b> để lưu hoặc nhấn <b>Tải xuống .tex</b> để lấy file XeLaTeX phục vụ chỉnh sửa, biên dịch hoặc nộp báo cáo.<br>
        &nbsp;&nbsp;&nbsp;&bull; Nhấn <b>OK</b> nếu chỉ muốn đóng cửa sổ kết quả và quay lại màn hình nhập liệu.</p>

        <p style="color: #666666; font-style: italic; margin-bottom: 15px;">* Nhấn nút <b>Reset</b> (mũi tên xoay) để dọn dẹp bảng và nhập bài toán mới.</p>

        <p style="font-size: 18px;">
            🌐 Xem chi tiết file hướng dẫn (PDF):
            <a href="https://github.com/Alee-deg/SolveLinearProgramming/blob/main/HDSD.pdf" style="color: #0078D7; text-decoration: none; font-weight: bold;">Tại đây</a>
        </p>
    )";

    if (isDark) {
        guideText.replace("#E8E8E8", "#313244");
        guideText.replace("#CCCCCC", "#45475A");
        guideText.replace("#0078D7", "#89B4FA");
        guideText.replace("#D9534F", "#F38BA8");
        guideText.replace("#666666", "#A6ADC8");
    }

    textLabel->setText(guideText);
    scrollLayout->addWidget(textLabel);
    scrollLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *closeBtn = new QPushButton("Đã hiểu");
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(isDark ?
                                "QPushButton { background-color: #89B4FA; color: #1E1E2E; border: none; border-radius: 6px; padding: 9px 35px; font-size: 15px; font-weight: bold; } QPushButton:hover { background-color: #B4BEFE; }" :
                                "QPushButton { background-color: #0078D7; color: #FFFFFF; border: none; border-radius: 6px; padding: 9px 35px; font-size: 15px; font-weight: bold; } QPushButton:hover { background-color: #005A9E; }"
                            );
    QObject::connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    dialog.exec();
}
