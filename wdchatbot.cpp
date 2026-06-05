#include "wdchatbot.h"
#include "ui_wdchatbot.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QSettings>
#include <QCoreApplication>

// THÊM THƯ VIỆN ĐỂ LƯU FILE XUYÊN HỆ ĐIỀU HÀNH
#include <QStandardPaths>
#include <QDir>

// Các thư viện UI cần thiết
#include <QInputDialog>
#include <QMessageBox>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QProgressDialog>
#include <functional>

// =======================================================================
// Hàm lấy đường dẫn an toàn để lưu cấu hình (Settings & API Key)
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
// Hàm xác thực API Key ngầm (Không làm đơ màn hình)
// =======================================================================
static void verifyAndSaveApiKey(WdChatBot* parentWindow, QTextEdit* chatDisplay, const QString& newKey, std::function<void(bool)> onFinished) {
    QNetworkAccessManager *manager = new QNetworkAccessManager(parentWindow);
    QNetworkRequest request(QUrl("https://api.groq.com/openai/v1/models"));
    request.setRawHeader("Authorization", "Bearer " + newKey.toUtf8());

    QNetworkReply *reply = manager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, parentWindow, [=]() {
        bool isValid = (reply->error() == QNetworkReply::NoError);
        if (isValid) {
            QSettings settings(getSettingsPath(), QSettings::IniFormat);
            settings.setValue("api_key", newKey);

            // [FIX] Dùng màu Xanh lá tiêu chuẩn, hiển thị cực rõ trên cả 2 nền Sáng/Tối
            QString sysColor = "#28A745";

            chatDisplay->append(QString("<div style='color: %1; margin-bottom: 5px;'><b>Hệ thống:</b> API Key đã được cập nhật thành công!</div>").arg(sysColor));
        } else {
            QMessageBox::critical(parentWindow, "Lỗi xác thực", "API Key không hợp lệ, vui lòng kiểm tra lại!");
        }
        if (onFinished) onFinished(isValid);
        reply->deleteLater();
        manager->deleteLater();
    });
}

WdChatBot::WdChatBot(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::WdChatBot)
{
    ui->setupUi(this);
    this->isFirstMessage = true;

    // KHÔNG SỬ DỤNG setStyleSheet CỤC BỘ Ở ĐÂY NỮA ĐỂ KẾ THỪA CSS TỪ MAINWINDOW
    menuBar()->hide();

    connect(ui->inputField, &QLineEdit::returnPressed, this, &WdChatBot::onUserSendMessage);
    this->setWindowTitle("Hỏi/Đáp");
    this->setWindowState(Qt::WindowMaximized);
    this->setWindowIcon(QIcon(":/logo.png"));
}

WdChatBot::~WdChatBot()
{
    delete ui;
}

void WdChatBot::onUserSendMessage() {
    QString userMessage = ui->inputField->text().trimmed();
    if (userMessage.isEmpty()) return;

    // XỬ LÝ NHẬP KEY QUA LỆNH ẨN /key
    if (userMessage == "/key") {
        ui->inputField->clear();
        QString iniPath = getSettingsPath();
        QSettings settings(iniPath, QSettings::IniFormat);
        QString currentKey = settings.value("api_key", "").toString();

        bool ok;
        QString newKey = QInputDialog::getText(this, "Cài đặt API Key",
                                               "Vui lòng dán API Key của Groq vào bên dưới:",
                                               QLineEdit::Password,
                                               currentKey, &ok);
        if (ok && !newKey.trimmed().isEmpty()) {
            verifyAndSaveApiKey(this, ui->chatDisplay, newKey.trimmed(), nullptr);
        }
        return;
    }

    if (isFirstMessage) {
        ui->chatDisplay->clear();
        isFirstMessage = false;
    }

    // [FIX] Dùng màu Xanh dương trung tính (hiển thị rõ, không bị tàng hình trên nền Đen lẫn nền Trắng)
    QString userColor = "#2563EB";

    // In tin nhắn của người dùng
    ui->chatDisplay->append(QString("<div style='margin-bottom: 5px; color: %1;'>"
                                    "<b>Bạn:</b> %2"
                                    "</div>").arg(userColor, userMessage));

    ui->inputField->clear();
    ui->inputField->setPlaceholderText("AI đang suy nghĩ...");
    ui->inputField->setEnabled(false);

    // Gọi hàm gửi lên Groq
    askGroq(userMessage);
}

void WdChatBot::askGroq(const QString& question) {
    QString iniPath = getSettingsPath();
    QSettings settings(iniPath, QSettings::IniFormat);
    QString apiKey = settings.value("api_key", "").toString().trimmed();

    // [FIX] Dùng màu chung hiển thị tốt trên cả 2 nền
    QString sysColor = "#E67E22"; // Cam
    QString errColor = "#D32F2F"; // Đỏ

    // XỬ LÝ TỰ ĐỘNG HỎI NẾU KEY TRỐNG
    if (apiKey.isEmpty()) {
        bool ok;
        QString newKey = QInputDialog::getText(this, "Yêu cầu API Key",
                                               "Bạn chưa thiết lập API Key.\nVui lòng dán mã API Key của Groq để kích hoạt Chatbot:",
                                               QLineEdit::Password, "", &ok);
        if (ok && !newKey.trimmed().isEmpty()) {
            verifyAndSaveApiKey(this, ui->chatDisplay, newKey.trimmed(), [this, question](bool isValid) {
                if (isValid) {
                    this->askGroq(question);
                } else {
                    ui->inputField->setEnabled(true);
                    ui->inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
                    ui->inputField->setFocus();
                }
            });
        } else {
            ui->chatDisplay->append(QString("<div style='color: %1; margin-bottom: 15px;'>"
                                            "<b>Hệ thống:</b> Bạn chưa nhập API Key nên Chatbot không thể hoạt động.<br>"
                                            "<i>(Bạn có thể gõ lệnh <b>/key</b> vào khung chat để cấu hình lại)</i>"
                                            "</div>").arg(sysColor));

            ui->inputField->setEnabled(true);
            ui->inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
            ui->inputField->setFocus();
        }
        return;
    }

    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl("https://api.groq.com/openai/v1/chat/completions"));

    request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject message;
    message["role"] = "user";
    message["content"] = question;

    QJsonArray messages;

    if (!problemContext.isEmpty()) {
        QJsonObject systemMessage;
        systemMessage["role"] = "system";
        systemMessage["content"] = "Bạn là một giáo sư toán học chuyên về Quy hoạch tuyến tính. "
                                   "Hãy trả lời câu hỏi của người dùng dựa trên bài toán họ đang giải dưới đây:\n\n"
                                   + problemContext;
        messages.append(systemMessage);
    }

    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = question;
    messages.append(userMessage);

    QJsonObject body;
    body["model"] = "llama-3.1-8b-instant";
    body["messages"] = messages;

    QNetworkReply *reply = manager->post(request, QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, errColor, manager]() {
        // Mở khóa thanh nhập liệu
        ui->inputField->setEnabled(true);
        ui->inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
        ui->inputField->setFocus();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray response = reply->readAll();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(response);
            QJsonObject root = jsonDoc.object();

            QString answer = root["choices"].toArray()[0].toObject()["message"].toObject()["content"].toString();
            answer.replace("\n", "<br>");

            // [QUAN TRỌNG NHẤT] CỐ TÌNH XÓA THẺ 'color' CỦA BOT ĐỂ NÓ HOÀN TOÀN TỰ ĐỘNG ĐỔI MÀU THEO THEME
            // Nếu bạn chọn Theme Sáng -> Nó tự hiển thị chữ Đen
            // Nếu bạn chọn Theme Tối -> Nó tự hiển thị chữ Trắng ngay lập tức
            ui->chatDisplay->append(QString("<div style='margin-bottom: 15px;'>"
                                            "<b>Bot:</b> %1"
                                            "</div>").arg(answer));
        } else {
            QByteArray errorData = reply->readAll();
            QString errorMessage = QString::fromUtf8(errorData);

            if (errorMessage.contains("invalid_api_key", Qt::CaseInsensitive) || errorMessage.contains("Invalid API Key", Qt::CaseInsensitive)) {
                ui->chatDisplay->append(QString("<div style='color: %1; margin-bottom: 15px;'>"
                                                "<b>Lỗi xác thực:</b> API Key của bạn bị sai, không hợp lệ hoặc đã bị khóa!<br>"
                                                "<i>&rarr; Vui lòng gõ lệnh <b>/key</b> vào thanh chat để sửa lại Key.</i>"
                                                "</div>").arg(errColor));
            } else {
                ui->chatDisplay->append(QString("<div style='color: %1; margin-bottom: 15px;'>"
                                                "<b>Lỗi mạng:</b> %2<br>"
                                                "<b>Chi tiết lỗi:</b> %3"
                                                "</div>").arg(errColor, reply->errorString(), errorMessage));
            }
        }

        reply->deleteLater();
        manager->deleteLater();
    });
}

void WdChatBot::setProblemContext(const QString& context) {
    problemContext = context;
}
