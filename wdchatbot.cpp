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

// Các thư viện UI cần thiết
#include <QInputDialog>
#include <QMessageBox>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QProgressDialog>
#include <functional>

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
            QSettings settings(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
            settings.setValue("api_key", newKey);
            chatDisplay->append("<div style='color: #89B4FA; margin-bottom: 5px;'><b>Hệ thống:</b> API Key đã được cập nhật thành công!</div>");
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
        QString iniPath = QCoreApplication::applicationDirPath() + "/settings.ini";
        QSettings settings(iniPath, QSettings::IniFormat);
        QString currentKey = settings.value("api_key", "").toString();

        bool ok;
        QString newKey = QInputDialog::getText(this, "Cài đặt API Key",
                                               "Vui lòng dán API Key của Groq vào bên dưới:",
                                               QLineEdit::Password,
                                               currentKey, &ok);
        if (ok && !newKey.trimmed().isEmpty()) {
            // [ÁP DỤNG MỚI] Đưa vào hàm kiểm tra trước khi lưu
            verifyAndSaveApiKey(this, ui->chatDisplay, newKey.trimmed(), nullptr);
        }
        return;
    }

    if (isFirstMessage) {
        ui->chatDisplay->clear();
        isFirstMessage = false;
    }

    // In tin nhắn của người dùng
    ui->chatDisplay->append("<div style='margin-bottom: 5px; color: #0055ff;'>"
                            "<b>Bạn:</b> " + userMessage +
                            "</div>");

    ui->inputField->clear();

    ui->inputField->setPlaceholderText("AI đang suy nghĩ...");
    ui->inputField->setEnabled(false);

    // Gọi hàm gửi lên Groq
    askGroq(userMessage);
}

void WdChatBot::askGroq(const QString& question) {
    QString iniPath = QCoreApplication::applicationDirPath() + "/settings.ini";
    QSettings settings(iniPath, QSettings::IniFormat);
    QString apiKey = settings.value("api_key", "").toString().trimmed();

    // XỬ LÝ TỰ ĐỘNG HỎI NẾU KEY TRỐNG
    if (apiKey.isEmpty()) {
        bool ok;
        QString newKey = QInputDialog::getText(this, "Yêu cầu API Key",
                                               "Bạn chưa thiết lập API Key.\nVui lòng dán mã API Key của Groq để kích hoạt Chatbot:",
                                               QLineEdit::Password, "", &ok);
        if (ok && !newKey.trimmed().isEmpty()) {
            // [ÁP DỤNG MỚI] Đưa vào hàm kiểm tra
            verifyAndSaveApiKey(this, ui->chatDisplay, newKey.trimmed(), [this, question](bool isValid) {
                if (isValid) {
                    // Nếu Key chuẩn, gọi vòng lặp lại hàm askGroq để đi chat tiếp!
                    this->askGroq(question);
                } else {
                    // Mở lại khung chat nếu key lỗi
                    ui->inputField->setEnabled(true);
                    ui->inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
                    ui->inputField->setFocus();
                }
            });
        } else {
            ui->chatDisplay->append("<div style='color: #E67E22; margin-bottom: 15px;'>"
                                    "<b>Hệ thống:</b> Bạn chưa nhập API Key nên Chatbot không thể hoạt động.<br>"
                                    "<i>(Bạn có thể cài đặt lại ở menu <b>⚙️ Cài đặt</b> trên góc màn hình, hoặc gõ lệnh <b>/key</b> vào khung chat)</i>"
                                    "</div>");

            ui->inputField->setEnabled(true);
            ui->inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
            ui->inputField->setFocus();
        }
        return; // Phải dừng lại chờ tín hiệu mạng Async phản hồi
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

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
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

            ui->chatDisplay->append("<div style='margin-bottom: 15px; color: #222222;'>"
                                    "<b>Bot:</b> " + answer +
                                    "</div>");
        } else {
            QByteArray errorData = reply->readAll();
            QString errorMessage = QString::fromUtf8(errorData);

            if (errorMessage.contains("invalid_api_key", Qt::CaseInsensitive) || errorMessage.contains("Invalid API Key", Qt::CaseInsensitive)) {
                ui->chatDisplay->append("<div style='color: #D32F2F; margin-bottom: 15px;'>"
                                        "<b>Lỗi xác thực:</b> API Key của bạn bị sai, không hợp lệ hoặc đã bị khóa!<br>"
                                        "<i>&rarr; Vui lòng gõ lệnh <b>/key</b>) vào thanh chat để sửa lại Key.</i>"
                                        "</div>");
            } else {
                ui->chatDisplay->append("<div style='color: #D32F2F; margin-bottom: 15px;'>"
                                        "<b>Lỗi mạng:</b> " + reply->errorString() + "<br>"
                                                                 "<b>Chi tiết lỗi:</b> " + errorMessage +
                                        "</div>");
            }
        }

        reply->deleteLater();
    });
}

void WdChatBot::setProblemContext(const QString& context) {
    problemContext = context;
}
