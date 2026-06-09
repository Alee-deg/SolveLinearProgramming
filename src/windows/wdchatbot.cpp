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
#include <QIcon>
#include <QApplication>
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
// Hàm lấy đường dẫn an toàn CHỈ DÀNH CHO API KEY
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
// Hàm xác thực API Key ngầm
// =======================================================================
static void verifyAndSaveApiKey(WdChatBot* parentWindow, QTextEdit* chatDisplay, const QString& newKey, std::function<void(bool)> onFinished) {
    QNetworkAccessManager *manager = new QNetworkAccessManager(parentWindow);
    QNetworkRequest request(QUrl("https://api.groq.com/openai/v1/models"));

    // Thêm các Header tiêu chuẩn để tránh bị Cloudflare/Groq chặn gói tin
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

            // Tô màu chữ "Hệ thống:", phần sau tự kế thừa màu Theme
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

    // ChatBot là cửa sổ độc lập để taskbar Windows luôn nhận icon riêng.
    this->setWindowFlag(Qt::Window, true);
    applyPhanMemQHTTWindowIcon(this);

    this->isFirstMessage = true;

    // Xóa stylesheet cục bộ để kế thừa tự động toàn bộ css từ MainWindow
    this->setStyleSheet("");

    menuBar()->hide();

    connect(ui->inputField, &QLineEdit::returnPressed, this, &WdChatBot::onUserSendMessage);
    this->setWindowTitle("Hỏi/Đáp");
    this->setWindowState(Qt::WindowMaximized);
    applyPhanMemQHTTWindowIcon(this);
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
