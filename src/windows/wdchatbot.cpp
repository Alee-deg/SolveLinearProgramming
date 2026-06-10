#include "wdchatbot.h"
#include "ui_wdchatbot.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDesktopServices>
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
#include <QEvent>
#include <QMouseEvent>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
#include <QLineEdit>
#include <QMessageBox>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextBlockFormat>
#include <QTextDocument>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QProgressDialog>
#include <functional>



// =======================================================================
// [FIX CHAT MESSAGE LAYOUT]
// Căn nội dung hỏi/đáp qua trái và tăng cỡ chữ cho phần hội thoại.
// Chỉ tác động chatDisplay, không đổi logic API hay luồng chatbot.
// =======================================================================
static void applyChatMessageLayout(QTextEdit *chatDisplay)
{
    if (!chatDisplay) return;

    chatDisplay->setAlignment(Qt::AlignLeft);
    chatDisplay->setLayoutDirection(Qt::LeftToRight);
    chatDisplay->setStyleSheet(
        "QTextEdit {"
        " font-size: 18px;"
        " line-height: 1.6;"
        " padding: 14px 18px;"
        "}"
        );

    if (chatDisplay->document()) {
        chatDisplay->document()->setDefaultStyleSheet(
            "body {"
            " text-align: left;"
            " font-size: 18px;"
            " line-height: 1.6;"
            " margin: 0;"
            " padding: 0;"
            "}"
            "div.chat-message {"
            " display: block;"
            " width: 100%;"
            " text-align: left;"
            " font-size: 18px;"
            " line-height: 1.6;"
            " margin: 0 0 14px 0;"
            "}"
            "b { font-size: 18px; }"
            );
    }

    QTextCursor cursor = chatDisplay->textCursor();
    QTextBlockFormat blockFormat;
    blockFormat.setAlignment(Qt::AlignLeft);
    cursor.setBlockFormat(blockFormat);
    chatDisplay->setTextCursor(cursor);
}

static bool isChatPlaceholderScreen(QTextEdit *chatDisplay)
{
    if (!chatDisplay) return false;

    QString plain = chatDisplay->toPlainText();
    return plain.contains("Bạn có thể hỏi thêm về bài toán", Qt::CaseInsensitive) ||
           plain.contains("API Key đã được xác thực thành công", Qt::CaseInsensitive) ||
           plain.contains("Đang kiểm tra API Key", Qt::CaseInsensitive) ||
           plain.contains("Hướng dẫn cài đặt API Key", Qt::CaseInsensitive);
}

static void clearPlaceholderBeforeRealChat(QTextEdit *chatDisplay)
{
    if (!chatDisplay) return;

    if (isChatPlaceholderScreen(chatDisplay)) {
        chatDisplay->clear();
    }

    applyChatMessageLayout(chatDisplay);
}

static void appendChatMessageLeft(QTextEdit *chatDisplay, const QString& html)
{
    if (!chatDisplay) return;

    clearPlaceholderBeforeRealChat(chatDisplay);

    QTextCursor cursor = chatDisplay->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextBlockFormat blockFormat;
    blockFormat.setAlignment(Qt::AlignLeft);

    if (!chatDisplay->document()->isEmpty()) {
        cursor.insertBlock(blockFormat);
    } else {
        cursor.setBlockFormat(blockFormat);
    }

    cursor.insertHtml(html);
    chatDisplay->setTextCursor(cursor);
    chatDisplay->ensureCursorVisible();
}




// =======================================================================
// [FIX CLICK LINK TRONG CHAT]
// QTextEdit không tự mở link ngoài trình duyệt như QTextBrowser.
// Event filter này bắt click vào anchor và mở bằng trình duyệt mặc định.
// =======================================================================
class ChatLinkOpenFilter : public QObject
{
public:
    explicit ChatLinkOpenFilter(QTextEdit *edit, QObject *parent = nullptr)
        : QObject(parent), m_edit(edit)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched);

        if (!m_edit) return false;

        if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent && mouseEvent->button() == Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                const QPoint pos = mouseEvent->position().toPoint();
#else
                const QPoint pos = mouseEvent->pos();
#endif
                const QString href = m_edit->anchorAt(pos);
                if (!href.trimmed().isEmpty()) {
                    QDesktopServices::openUrl(QUrl(href));
                    return true;
                }
            }
        }

        return false;
    }

private:
    QTextEdit *m_edit = nullptr;
};

static void enableExternalLinksForChatDisplay(QTextEdit *chatDisplay)
{
    if (!chatDisplay) return;

    chatDisplay->setReadOnly(true);
    chatDisplay->setTextInteractionFlags(Qt::TextBrowserInteraction);

    if (chatDisplay->viewport()) {
        chatDisplay->viewport()->setMouseTracking(true);
        chatDisplay->viewport()->installEventFilter(new ChatLinkOpenFilter(chatDisplay, chatDisplay));
    }
}


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
// [FIX API KEY GUIDE]
// Hiển thị hướng dẫn lấy Groq API Key trực tiếp trong khung chat.
// Không dùng cửa sổ mới cho phần hướng dẫn, chỉ hiện trong chatDisplay.
// =======================================================================
static QString buildApiKeyGuideHtml(const QString& reasonHtml = QString())
{
    QString reasonBlock;
    if (!reasonHtml.trimmed().isEmpty()) {
        reasonBlock =
            "<div style='border:1px solid #f0c36d; border-radius:8px; padding:12px 16px; "
            "background:rgba(255,248,225,0.90); margin-bottom:14px; font-size:16px;'>"
            + reasonHtml +
            "</div>";
    }

    return QString(
               "<div style='max-width: 920px; margin: 36px auto; font-size: 16px; line-height: 1.65;'>"
               "<h2 style='text-align:center; margin-bottom: 12px; font-size:24px;'>🔑 Hướng dẫn cài đặt API Key cho Chatbot</h2>"
               "<p style='text-align:center; color:#666; margin-top:0; font-size:16px;'>"
               "Chatbot cần Groq API Key để gửi câu hỏi và nhận phản hồi."
               "</p>"
               ) + reasonBlock + QString(
               "<div style='border:1px solid #d9dee8; border-radius:10px; padding:18px 24px; background:rgba(245,247,250,0.75);'>"
               "<p><b>Bước 1:</b> Mở trình duyệt và truy cập "
               "<a style='color:#0B72D9; text-decoration:underline; font-weight:bold;' href='https://console.groq.com/'>https://console.groq.com/</a></p>"
               "<p><b>Bước 2:</b> Đăng nhập hoặc tạo tài khoản Groq.</p>"
               "<p><b>Bước 3:</b> Vào mục <b>API Keys</b> trên thanh điều hướng.</p>"
               "<p><b>Bước 4:</b> Nhấn <b>Create API Key</b>, đặt tên ví dụ "
               "<code>PhanMemQHTT-Chatbot</code>, sau đó nhấn <b>Submit</b>.</p>"
               "<p><b>Bước 5:</b> Nhấn <b>Copy</b> để sao chép API Key. "
               "<span style='color:#d9534f;'><b>Lưu ý:</b> Key chỉ hiển thị một lần, không chia sẻ cho người khác.</span></p>"
               "<p><b>Bước 6:</b> Gõ lệnh <code>/key</code> vào ô nhập bên dưới, dán API Key và nhấn <b>OK</b>.</p>"
               "</div>"

               "<p style='text-align:center; color:#666; margin-top:12px; font-size:16px;'>"
               "Sau khi xác thực thành công, khung chat sẽ tự trở về chế độ hỏi/đáp bình thường."
               "</p>"
               "</div>"
               );
}

static void showApiKeyGuideInChat(QTextEdit* chatDisplay, const QString& reasonHtml = QString())
{
    if (!chatDisplay) return;
    chatDisplay->clear();
    chatDisplay->setHtml(buildApiKeyGuideHtml(reasonHtml));
}

static void showChatReadyNormal(QTextEdit* chatDisplay)
{
    if (!chatDisplay) return;
    chatDisplay->clear();
    chatDisplay->setHtml(
        "<div style='height:100%; display:flex; align-items:center; justify-content:center;'>"
        "<div style='text-align:center; color:#666; font-size:16px; margin-top:220px;'>"
        "Bạn có thể hỏi thêm về bài toán"
        "</div>"
        "</div>"
        );
}

static void showChatReadyAfterApiKey(QTextEdit* chatDisplay)
{
    if (!chatDisplay) return;
    chatDisplay->clear();
    chatDisplay->setHtml(
        "<div style='height:100%; display:flex; align-items:center; justify-content:center;'>"
        "<div style='text-align:center; color:#666; font-size:16px; margin-top:220px;'>"
        "✅ API Key đã được xác thực thành công.<br>"
        "Bạn có thể hỏi thêm về bài toán."
        "</div>"
        "</div>"
        );
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

            // [FIX API KEY GUIDE]
            // Sau khi API Key hợp lệ, xóa màn hình hướng dẫn và trở về khung giao tiếp bình thường.
            showChatReadyAfterApiKey(chatDisplay);
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

// =======================================================================
// [FIX CHECK API KEY ON STARTUP]
// Khi mở cửa sổ Chatbot, kiểm tra API Key đã lưu có còn dùng được không.
// - Key OK: không hiện hướng dẫn, đưa về màn hình giao tiếp bình thường.
// - Key rỗng/sai/bị khóa: hiện hướng dẫn lấy API Key.
// =======================================================================
static void checkSavedApiKeyOnStartup(WdChatBot* parentWindow, QTextEdit* chatDisplay, QLineEdit* inputField)
{
    if (!parentWindow || !chatDisplay) return;

    QSettings apiSettings(getApiKeyPath(), QSettings::IniFormat);
    QString savedKey = apiSettings.value("api_key", "").toString().trimmed();

    if (savedKey.isEmpty()) {
        showApiKeyGuideInChat(
            chatDisplay,
            "⚠ <b>Chưa có API Key.</b><br>"
            "Vui lòng làm theo hướng dẫn bên dưới, sau đó gõ <code>/key</code> để dán API Key vào phần mềm."
            );

        if (inputField) {
            inputField->setEnabled(true);
            inputField->setPlaceholderText("Gõ /key để cài API Key hoặc nhập câu hỏi của bạn...");
            inputField->setFocus();
        }
        return;
    }

    chatDisplay->setHtml(
        "<div style='height:100%; display:flex; align-items:center; justify-content:center;'>"
        "<div style='text-align:center; color:#666; font-size:16px; margin-top:220px;'>"
        "Đang kiểm tra API Key đã lưu..."
        "</div>"
        "</div>"
        );

    if (inputField) {
        inputField->setEnabled(false);
        inputField->setPlaceholderText("Đang kiểm tra API Key đã lưu...");
    }

    QNetworkAccessManager *manager = new QNetworkAccessManager(parentWindow);
    QNetworkRequest request(QUrl("https://api.groq.com/openai/v1/models"));
    request.setRawHeader("Authorization", "Bearer " + savedKey.toUtf8());
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = manager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, parentWindow, [=]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray responseBody = reply->readAll();

        bool isValid = (reply->error() == QNetworkReply::NoError && statusCode == 200);

        if (isValid) {
            showChatReadyNormal(chatDisplay);
        } else {
            QString errorDetail = reply->errorString();

            QJsonDocument doc = QJsonDocument::fromJson(responseBody);
            if (!doc.isNull() && doc.object().contains("error")) {
                errorDetail = doc.object()["error"].toObject()["message"].toString();
            } else if (!responseBody.isEmpty()) {
                errorDetail += " | " + QString::fromUtf8(responseBody).left(150);
            }

            bool isAuthError =
                statusCode == 401 ||
                statusCode == 403 ||
                errorDetail.contains("invalid_api_key", Qt::CaseInsensitive) ||
                errorDetail.contains("Invalid API Key", Qt::CaseInsensitive);

            if (isAuthError) {
                QSettings apiSettings(getApiKeyPath(), QSettings::IniFormat);
                apiSettings.remove("api_key");
                apiSettings.sync();

                showApiKeyGuideInChat(
                    chatDisplay,
                    "⚠ <b>API Key đã lưu không còn hợp lệ hoặc đã bị khóa.</b><br>"
                    "Vui lòng tạo API Key mới theo hướng dẫn bên dưới, sau đó gõ <code>/key</code> để cập nhật."
                    );
            } else {
                showApiKeyGuideInChat(
                    chatDisplay,
                    "⚠ <b>Không kiểm tra được API Key đã lưu.</b><br>"
                    "Có thể máy chưa có Internet hoặc Groq đang tạm thời không phản hồi.<br>"
                    "Bạn có thể thử lại sau hoặc gõ <code>/key</code> để cập nhật API Key."
                    );
            }
        }

        if (inputField) {
            inputField->setEnabled(true);
            inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
            inputField->setFocus();
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

    // [FIX CLICK LINK TRONG CHAT]
    // Cho phép click link trong khung chat và mở bằng trình duyệt mặc định.
    enableExternalLinksForChatDisplay(ui->chatDisplay);
    applyChatMessageLayout(ui->chatDisplay);

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

    // [FIX CHECK API KEY ON STARTUP]
    // Khi mở Chatbot, kiểm tra API Key cũ có còn hợp lệ không.
    // Nếu key OK thì không hiện hướng dẫn; nếu key lỗi/rỗng thì hiện hướng dẫn lấy API Key.
    checkSavedApiKeyOnStartup(this, ui->chatDisplay, ui->inputField);
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

        // [FIX API KEY GUIDE]
        // Hiển thị hướng dẫn lấy API Key ngay trong khung chat trước khi người dùng nhập key.
        showApiKeyGuideInChat(ui->chatDisplay);
        this->isFirstMessage = false;

        QSettings apiSettings(getApiKeyPath(), QSettings::IniFormat);
        QString currentKey = apiSettings.value("api_key", "").toString();

        bool ok;
        QString newKey = QInputDialog::getText(this, "Cài đặt API Key",
                                               "Vui lòng dán API Key của Groq vào bên dưới:",
                                               QLineEdit::Password,
                                               currentKey, &ok);
        if (ok && !newKey.trimmed().isEmpty()) {
            ui->inputField->setEnabled(false);
            ui->inputField->setPlaceholderText("Đang xác thực API Key...");
            verifyAndSaveApiKey(this, ui->chatDisplay, newKey.trimmed(), [this](bool isValid) {
                ui->inputField->setEnabled(true);
                ui->inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
                ui->inputField->setFocus();

                if (isValid) {
                    // Lần hỏi thật tiếp theo sẽ xóa màn hình thông báo thành công,
                    // tránh tin nhắn bị dính căn giữa từ màn hình trạng thái.
                    this->isFirstMessage = true;
                }
            });
        }
        return;
    }

    if (ui->chatDisplay->toPlainText().contains("Hướng dẫn cài đặt API Key", Qt::CaseInsensitive)) {
        ui->chatDisplay->clear();
        applyChatMessageLayout(ui->chatDisplay);
    }

    if (isFirstMessage) {
        ui->chatDisplay->clear();
        applyChatMessageLayout(ui->chatDisplay);
        isFirstMessage = false;
    }

    clearPlaceholderBeforeRealChat(ui->chatDisplay);

    // In tin nhắn của người dùng: Tên màu xanh, nội dung tự động màu Trắng/Đen theo Theme
    appendChatMessageLeft(
        ui->chatDisplay,
        QString("<div class='chat-message' style='display:block; width:100%; text-align:left; font-size:18px; line-height:1.6; margin-bottom:14px;'>"
                "<b style='color:#0078D7; font-size:18px;'>Bạn:</b> %1"
                "</div>").arg(userMessage)
        );

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
        // [FIX API KEY GUIDE]
        // Nếu chưa có API Key, khung chat hiển thị hướng dẫn lấy key trước.
        showApiKeyGuideInChat(ui->chatDisplay);
        this->isFirstMessage = false;

        bool ok;
        QString newKey = QInputDialog::getText(this, "Yêu cầu API Key",
                                               "Bạn chưa thiết lập API Key.\nVui lòng dán mã API Key của Groq để kích hoạt Chatbot:",
                                               QLineEdit::Password, "", &ok);
        if (ok && !newKey.trimmed().isEmpty()) {
            ui->inputField->setPlaceholderText("Đang xác thực API Key...");
            verifyAndSaveApiKey(this, ui->chatDisplay, newKey.trimmed(), [this, question](bool isValid) {
                if (isValid) {
                    ui->inputField->setEnabled(true);
                    ui->inputField->setPlaceholderText("Nhập câu hỏi của bạn . . .");
                    ui->inputField->setFocus();

                    // Xác thực xong thì quay lại xử lý câu hỏi người dùng đã nhập trước đó.
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
                                    "<i>Hãy làm theo hướng dẫn phía trên, sau đó gõ lệnh <b>/key</b> để cấu hình lại.</i>"
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
            appendChatMessageLeft(
                ui->chatDisplay,
                QString("<div class='chat-message' style='display:block; width:100%; text-align:left; font-size:18px; line-height:1.6; margin-bottom:16px;'>"
                        "<b style='font-size:18px;'>Bot:</b> %1"
                        "</div>").arg(answer)
                );
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
