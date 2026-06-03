#ifndef WDCHATBOT_H
#define WDCHATBOT_H
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMainWindow>

namespace Ui {
class WdChatBot;
}

class WdChatBot : public QMainWindow
{
    Q_OBJECT
private slots:
    void onUserSendMessage(); // Hàm tự tạo để xử lý khi người dùng gửi tin nhắn
public:
    explicit WdChatBot(QWidget *parent = nullptr);
    void askGroq(const QString& question);
    void setProblemContext(const QString& context);
    ~WdChatBot();

private:
    Ui::WdChatBot *ui;
    bool isFirstMessage;
    QString problemContext;
};

#endif // WDCHATBOT_H
