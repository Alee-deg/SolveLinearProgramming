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
// [FIX LƯU TRỮ] Hàm lấy đường dẫn an toàn CHỈ DÀNH CHO API KEY
// =======================================================================
static QString getApiKeyPath() {
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir;
    if (!dir.exists(dataDir)) {
        dir.mkpath(dataDir);
    }
    return dataDir + "/apikey.ini";
}

// =======================================================================
// Hàm xác thực API Key ngầm (Không làm đơ màn hình)
// =======================================================================
static void verifyAndSaveApiKey(WdChatBot* parentWindow, QTextEdit* chatDisplay, const QString& newKey, std::function<void(bool)> onFinished) {
    QNetworkAccessManager *manager = new QNetworkAccessManager(parentWindow);
    QNetworkRequest request(QUrl("https://api.groq.com/openai/v1/models"));

    // [FIX LINUX WAF] Thêm các Header tiêu chuẩn để tránh bị Cloudflare/Groq chặn gói tin
    request.setRawHeader("Authorization", "Bearer " + newKey.trimmed().toUtf8());
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = manager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, parentWindow, [=]() {
        // Bắt chính xác mã HTTP Code từ máy chủ
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            // Lưu API Key vào thư mục AppData
            QSettings apiSettings(getApiKeyPath(), QSettings::IniFormat);
            apiSettings.setValue("api_key", newKey.trimmed());

            // Chỉ tô màu chữ "Hệ thống:", phần sau tự kế thừa màu Theme
            chatDisplay->append("<div style='margin-bottom: 5px;'><b style='color: #28A745;'>Hệ thống:</b> API Key đã được cập nhật thành công!</div>");
            if (onFinished) onFinished(true);
        } else {
            // Hiển thị chi tiết lỗi JSON từ Groq để người dùng biết chính xác lỗi gì
            QByteArray errorBody = reply->readAll();
            QString errorDetail = reply->errorString();

            QJsonDocument doc = QJsonDocument::fromJson(errorBody);
            if (!doc.isNull() && doc.object().contains("error")) {
                errorDetail = doc.object()["error"].toObject()["message"].toString();
            } else if (!errorBody.isEmpty()) {
                errorDetail += " | " + QString::fromUtf8(errorBody).left(150);
            }

            QMessageBox::critical(parentWindow, "Lỗi xác thực",
                                  QString("API Key không hợp lệ hoặc bị chặn kết nối!\n\n- Mã HTTP: %1\n- Chi tiết lỗi: %2")
                                      .arg(statusCode).arg(errorDetail));

            if (onFinished) onFinished(false);
        }
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

    // Xóa stylesheet cục bộ để kế thừa tự động toàn bộ css từ MainWindow
    this->setStyleSheet("");

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
        QSettings apiSettings(getApiKeyPath(), QSettings::IniFormat);
        QString currentKey = apiSettings.value("api_key", "").toString();

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

    // In tin nhắn của người dùng: Tên màu xanh, nội dung tự động màu Trắng/Đen theo Theme
    ui->chatDisplay->append(QString("<div style='margin-bottom: 5px;'>"
                                    "<b style='color: #0078D7;'>Bạn:</b> %1"
                                    "</div>").arg(userMessage));

    ui->inputField->clear();
    ui->inputField->setPlaceholderText("AI đang suy nghĩ...");
    ui->inputField->setEnabled(false);

    // Gọi hàm gửi lên Groq
    askGroq(userMessage);
}

void WdChatBot::askGroq(const QString& question) {
    QSettings apiSettings(getApiKeyPath(), QSettings::IniFormat);
    QString apiKey = apiSettings.value("api_key", "").toString().trimmed();

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
            ui->chatDisplay->append("<div style='margin-bottom: 15px;'>"
                                    "<b style='color: #E67E22;'>Hệ thống:</b> Bạn chưa nhập API Key nên Chatbot không thể hoạt động.<br>"
                                    "<i>(Bạn có thể gõ lệnh <b>/key</b> vào khung chat để cấu hình lại)</i>"
                                    "</div>");

            ui->inputField->setEnabled(true);
            ui->inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
            ui->inputField->setFocus();
        }
        return;
    }

    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl("https://api.groq.com/openai/v1/chat/completions"));

    // Thêm các Header tiêu chuẩn để tránh bị Cloudflare/Groq chặn gói tin
    request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    request.setRawHeader("Accept", "application/json");

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

    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        // Mở khóa thanh nhập liệu
        ui->inputField->setEnabled(true);
        ui->inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
        ui->inputField->setFocus();

        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray response = reply->readAll();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(response);
            QJsonObject root = jsonDoc.object();

            QString answer = root["choices"].toArray()[0].toObject()["message"].toObject()["content"].toString();
            answer.replace("\n", "<br>");

            // In tin nhắn của Bot: Phần trả lời tự động đổi màu theo Theme
            ui->chatDisplay->append(QString("<div style='margin-bottom: 15px;'>"
                                            "<b>Bot:</b> %1"
                                            "</div>").arg(answer));
        } else {
            QByteArray errorData = reply->readAll();
            QString errorMessage = QString::fromUtf8(errorData);

            QJsonDocument doc = QJsonDocument::fromJson(errorData);
            if (!doc.isNull() && doc.object().contains("error")) {
                errorMessage = doc.object()["error"].toObject()["message"].toString();
            }

            if (errorMessage.contains("invalid_api_key", Qt::CaseInsensitive) || errorMessage.contains("Invalid API Key", Qt::CaseInsensitive) || statusCode == 401) {
                ui->chatDisplay->append("<div style='margin-bottom: 15px;'>"
                                        "<b style='color: #D32F2F;'>Lỗi xác thực:</b> API Key của bạn bị sai, không hợp lệ hoặc đã bị khóa!<br>"
                                        "<i>&rarr; Vui lòng gõ lệnh <b>/key</b> vào thanh chat để sửa lại Key.</i>"
                                        "</div>");
            } else {
                ui->chatDisplay->append(QString("<div style='margin-bottom: 15px;'>"
                                                "<b style='color: #D32F2F;'>Lỗi mạng (Mã HTTP %1):</b><br>"
                                                "<b>Chi tiết:</b> %2"
                                                "</div>").arg(statusCode).arg(errorMessage.isEmpty() ? reply->errorString() : errorMessage));
            }
        }

        reply->deleteLater();
        manager->deleteLater();
    });
}

void WdChatBot::setProblemContext(const QString& context) {
    problemContext = context;
}
