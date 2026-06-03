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

WdChatBot::WdChatBot(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::WdChatBot)
{
    ui->setupUi(this);
    this->isFirstMessage = true;

    // Bắt sự kiện nhấn phím Enter (returnPressed) trên inputField
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

    if (isFirstMessage) {
        ui->chatDisplay->clear();
        isFirstMessage = false;
    }

    // In tin nhắn của người dùng
    ui->chatDisplay->append("<div style='margin-bottom: 5px; color: #0055ff;'>"
                            "<b>Bạn:</b> " + userMessage +
                            "</div>");

    ui->inputField->clear();

    // --- HIỆU ỨNG CHỜ ---
    ui->inputField->setPlaceholderText("AI đang suy nghĩ...");
    ui->inputField->setEnabled(false); // Khóa khung nhập để tránh người dùng spam phím Enter

    // Gọi hàm gửi lên Groq
    askGroq(userMessage);
}

void WdChatBot::askGroq(const QString& question) {
    // 1. ĐỌC API KEY TỪ FILE settings.ini NẰM CẠNH FILE CHẠY
    QString iniPath = QCoreApplication::applicationDirPath() + "/settings.ini";
    QSettings settings(iniPath, QSettings::IniFormat);
    QString apiKey = settings.value("api_key", "").toString().trimmed();

    // Nếu không tìm thấy file hoặc key trống, thông báo lỗi ngay trên màn hình chat và dừng lại
    if (apiKey.isEmpty()) {
        ui->chatDisplay->append("<div style='color: orange; margin-bottom: 15px;'>"
                                "<b>Hệ thống:</b> Không tìm thấy API Key! "
                                "Vui lòng tạo file <code>settings.ini</code> đặt cạnh file chạy ứng dụng với nội dung:<br>"
                                "<code>[General]</code><br><code>api_key=gsk_your_key_here</code>"
                                "</div>");

        // Mở khóa lại khung nhập liệu để người dùng thử lại sau
        ui->inputField->setEnabled(true);
        ui->inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
        ui->inputField->setFocus();
        return;
    }

    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl("https://api.groq.com/openai/v1/chat/completions"));

    // Sử dụng API Key vừa đọc được một cách động
    request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Tạo cấu trúc tin nhắn JSON
    QJsonObject message;
    message["role"] = "user";
    message["content"] = question;

    QJsonArray messages;

    // 1. CHÈN BỐI CẢNH BÀI TOÁN (NẾU CÓ) VỚI VAI TRÒ "SYSTEM"
    if (!problemContext.isEmpty()) {
        QJsonObject systemMessage;
        systemMessage["role"] = "system";
        systemMessage["content"] = "Bạn là một giáo sư toán học chuyêMn về Quy hoạch tuyến tính. "
                                   "Hãy trả lời câu hỏi của người dùng dựa trên bài toán họ đang giải dưới đây:\n\n"
                                   + problemContext;
        messages.append(systemMessage);
    }

    // 2. CHÈN CÂU HỎI CỦA NGƯỜI DÙNG
    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = question;
    messages.append(userMessage);

    QJsonObject body;
    body["model"] = "llama-3.1-8b-instant";
    body["messages"] = messages;

    // Gửi POST Request
    QNetworkReply *reply = manager->post(request, QJsonDocument(body).toJson());

    // Xử lý khi nhận được câu trả lời từ máy chủ
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {

        // Mở khóa thanh nhập liệu trở lại bình thường
        ui->inputField->setEnabled(true);
        ui->inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
        ui->inputField->setFocus();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray response = reply->readAll();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(response);
            QJsonObject root = jsonDoc.object();

            // Bóc tách lấy đúng phần văn bản AI trả lời
            QString answer = root["choices"].toArray()[0].toObject()["message"].toObject()["content"].toString();

            // Thay ký tự xuống dòng \n bằng thẻ <br> để Qt hiển thị đúng HTML
            answer.replace("\n", "<br>");

            // In câu trả lời lên màn hình
            ui->chatDisplay->append("<div style='margin-bottom: 15px; color: #222222;'>"
                                    "<b>Bot:</b> " + answer +
                                    "</div>");
        } else {
            // Xử lý nếu mất mạng hoặc API Key sai
            QByteArray errorData = reply->readAll();
            QString errorMessage = QString::fromUtf8(errorData);

            // In chi tiết lỗi lên màn hình Chat
            ui->chatDisplay->append("<div style='color: red; margin-bottom: 15px;'>"
                                    "<b>Lỗi mạng:</b> " + reply->errorString() + "<br>"
                                                             "<b>Chi tiết lỗi:</b> " + errorMessage +
                                    "</div>");
        }

        reply->deleteLater(); // Dọn dẹp bộ nhớ
    });
}

void WdChatBot::setProblemContext(const QString& context) {
    problemContext = context;
}
