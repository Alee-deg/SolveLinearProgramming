#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QIcon>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->dashboard = nullptr;
    this->setWindowTitle("Phần mềm giải Quy Hoạch Tuyến Tính - Ver 1.0");
    this->setWindowState(Qt::WindowMaximized);
    this->setWindowIcon(QIcon(":/logo.png"));
    qDebug() << "Icon co bi rong khong?: " << this->windowIcon().isNull();
}

MainWindow::~MainWindow()
{
    delete this->dashboard;
    delete ui;
}

void MainWindow::on_pushButton_3_clicked()
{
    // 1. Tạo hiệu ứng mờ dần cho MainWindow
    QGraphicsOpacityEffect *fadeEffect = new QGraphicsOpacityEffect(this);
    this->centralWidget()->setGraphicsEffect(fadeEffect);

    QPropertyAnimation *animOut = new QPropertyAnimation(fadeEffect, "opacity");
    animOut->setDuration(150);
    animOut->setStartValue(1.0);
    animOut->setEndValue(0.0);

    connect(animOut, &QPropertyAnimation::finished, this, [=]() {
        this->hide();

        // 2. CHỈ TẠO MỚI NẾU CHƯA TỒN TẠI (Giúp giữ nguyên giao diện cũ)
        if (!this->dashboard) {
            this->dashboard = new Dashboard(this);
            connect(this->dashboard, &Dashboard::destroyed, this, [this](){
                this->dashboard = nullptr;
            });
        }

        this->dashboard->show();

        // 3. Làm sáng dần Dashboard (Fade-in)
        QGraphicsOpacityEffect *fadeInEffect = new QGraphicsOpacityEffect(this->dashboard);
        this->dashboard->centralWidget()->setGraphicsEffect(fadeInEffect);

        QPropertyAnimation *animIn = new QPropertyAnimation(fadeInEffect, "opacity");
        animIn->setDuration(150);
        animIn->setStartValue(0.0);
        animIn->setEndValue(1.0);
        animIn->start(QAbstractAnimation::DeleteWhenStopped);
    });

    animOut->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::on_btnGioiThieu_clicked()
{
    QDialog *policyDialog = new QDialog(this);
    policyDialog->setWindowTitle("Giới thiệu");
    policyDialog->resize(1150, 1050);

    policyDialog->setStyleSheet(
        "QDialog { background-color: #F8F9FA; }"
        "QTextBrowser { "
        "   background-color: #FFFFFF; "
        "   border: 1px solid #DEE2E6; "
        "   border-radius: 8px; "
        "   padding: 15px; "
        "   color: #212529; "
        "}"
        "QPushButton { "
        "   padding: 10px 30px; "
        "   background-color: #004085; "
        "   color: white; "
        "   font-size: 14pt; "
        "   font-weight: bold; "
        "   border-radius: 5px; "
        "   border: none; "
        "}"
        "QPushButton:hover { background-color: #0056B3; }"
        );

    QVBoxLayout *layout = new QVBoxLayout(policyDialog);
    layout->setContentsMargins(20, 20, 20, 20);

    QTextBrowser *textBrowser = new QTextBrowser(policyDialog);
    textBrowser->setOpenExternalLinks(true);

    // [FIX HTML] Đổi <h4> thành <p> để Qt nhận đúng cỡ chữ lớn và KHÔNG in đậm
    QString htmlContent = R"(
        <div style="font-family: 'Times New Roman', serif; font-size: 18pt; line-height: 1.6;">
            <h2 style="color: #0056B3; text-align: center; margin-bottom: 5px; font-size: 24pt;">GIỚI THIỆU</h2>
            <hr style="background-color: #CCCCCC; height: 1px; border: none; margin-bottom: 20px;">

            <h3 style="color: #D9534F; margin-bottom: 5px; font-size: 22pt;">1. GIỚI THIỆU CHUNG</h3>

            <p style="color: #0056B3; margin-top: 15px; margin-bottom: 5px; font-size: 15pt;">1.1 Mục đích và tính cấp thiết của đề tài</p>
            <p style="margin-top: 0px; text-align: justify;">Trong bối cảnh chuyển đổi số mạnh mẽ, các mô hình toán học tối ưu hóa ngày càng khẳng định được vai trò quan trọng. Quy hoạch tuyến tính (QHTT) là một phân nhánh cốt lõi của Tối ưu hóa. Tuy nhiên, việc giải thủ công bằng thuật toán Đơn hình đối mặt với nhiều rào cản về độ phức tạp tính toán và thời gian.</p>
            <p style="text-align: justify;">Do đó, phần mềm này được phát triển nhằm mục đích cung cấp một công cụ hỗ trợ tính toán chính xác, tự động hóa quy trình giải bài toán, từ đó tối ưu hóa hiệu suất nghiên cứu và học tập.</p>

            <p style="color: #0056B3; margin-top: 20px; margin-bottom: 5px; font-size: 15pt;">1.2 Đối tượng mục tiêu và phạm vi ứng dụng</p>
            <ul style="margin-top: 0px;">
                <li>Sinh viên:</b> Công cụ hỗ trợ tự học, kiểm tra lời giải, thực hành kỹ năng lập mô hình.</li>
                <li>Giảng viên:</b> Phương tiện trực quan để minh họa các bước thực hiện thuật toán.</li>
                <li>Nhà nghiên cứu:</b> Hỗ trợ tính toán sơ bộ và kiểm chứng các mô hình tối ưu hóa.</li>
            </ul>

            <p style="color: #0056B3; margin-top: 20px; margin-bottom: 5px; font-size: 15pt;">1.3 Tính chất nổi bật</p>
            <ul style="margin-top: 0px;">
                <li>Giải toán đa năng:</b> Xử lý bài toán Min/Max với số lượng biến và ràng buộc không giới hạn.</li>
                <li>Trực quan hóa hình học:</b> Tự động vẽ miền nghiệm cho bài toán 2 biến.</li>
                <li>Phân tích thuật toán chi tiết:</b> Hiển thị các bảng đơn hình qua từng bước lặp.</li>
                <li>Trợ lý ảo thông minh:</b> Tích hợp Chatbot AI để giải thích thêm về các bước giải.</li>
            </ul>
            <br>
            <h3 style="color: #D9534F; margin-bottom: 5px; font-size: 22pt;">2. THÔNG TIN PHÁT TRIỂN & HỖ TRỢ</h3>

            <p style="color: #0056B3; margin-top: 15px; margin-bottom: 5px; font-size: 15pt;">2.1 Thông tin tác giả</p>
            <p style="margin-top: 0px;">Dự án được nghiên cứu và phát triển bởi nhóm sinh viên Trường Đại học Khoa học Tự nhiên, ĐHQG-HCM (HCMUS)</b>. Chúng tôi luôn hoan nghênh các ý kiến đóng góp để hoàn thiện thuật toán và giao diện.</p>

            <p style="color: #0056B3; margin-top: 20px; margin-bottom: 5px; font-size: 15pt;">2.2 Nền tảng & Công nghệ sử dụng (Credits)</p>
            <ul style="margin-top: 0px;">
                <li>Nền tảng hỗ trợ:</b> Tương thích đa nền tảng (Cross-platform) bao gồm Windows, macOS và Linux.</li>
                <li>Ngôn ngữ & Framework:</b> Lõi thuật toán được viết hoàn toàn bằng C++ tiêu chuẩn. Giao diện đồ họa (GUI) được xây dựng dựa trên nền tảng mã nguồn mở Qt Framework (Qt6)</b>.</li>
                <li>Tích hợp AI:</b> Chatbot hướng dẫn sử dụng được trợ lực bởi Mô hình ngôn ngữ lớn (LLM).</li>
            </ul>

            <p style="color: #0056B3; margin-top: 20px; margin-bottom: 5px; font-size: 15pt;">2.3 Phản hồi & Báo lỗi (Feedback / Issues)</p>
            <p style="margin-top: 0px; margin-bottom: 20px;">Trong quá trình sử dụng, nếu phát hiện lỗi tính toán (Bug), xin vui lòng gửi báo cáo lỗi (kèm theo bài toán mẫu) thông qua kho lưu trữ dự án trên GitHub</b> hoặc liên hệ trực tiếp qua email của nhóm phát triển.</p>
        </div>
    )";

    textBrowser->setHtml(htmlContent);
    layout->addWidget(textBrowser);

    QPushButton *btnClose = new QPushButton("Đóng", policyDialog);
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, policyDialog, &QDialog::accept);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);
    btnLayout->addStretch();

    layout->addLayout(btnLayout);

    policyDialog->exec();
    delete policyDialog;
}


void MainWindow::on_btnChinhSach_clicked()
{
    QDialog *policyDialog = new QDialog(this);
    policyDialog->setWindowTitle("Chính sách sử dụng");
    policyDialog->resize(1150, 1050);

    policyDialog->setStyleSheet(
        "QDialog { background-color: #F8F9FA; }"
        "QTextBrowser { "
        "   background-color: #FFFFFF; "
        "   border: 1px solid #DEE2E6; "
        "   border-radius: 8px; "
        "   padding: 15px; "
        "   color: #212529; "
        "}"
        "QPushButton { "
        "   padding: 10px 30px; "
        "   background-color: #004085; "
        "   color: white; "
        "   font-size: 14pt; "
        "   font-weight: bold; "
        "   border-radius: 5px; "
        "   border: none; "
        "}"
        "QPushButton:hover { background-color: #0056B3; }"
        );

    QVBoxLayout *layout = new QVBoxLayout(policyDialog);
    layout->setContentsMargins(20, 20, 20, 20);

    QTextBrowser *textBrowser = new QTextBrowser(policyDialog);
    textBrowser->setOpenExternalLinks(true);

    // [FIX HTML] Đổi <h4> thành <p> để Qt nhận đúng cỡ chữ lớn và KHÔNG in đậm
    QString htmlContent = R"(
        <div style="font-family: 'Times New Roman', serif; font-size: 18pt; line-height: 1.6;">
            <br>
            <h3 style="color: #D9534F; margin-bottom: 5px; text-align: center; font-size: 24pt;">CHÍNH SÁCH VÀ ĐIỀU KHOẢN SỬ DỤNG</h3>

            <p style="color: #0056B3; margin-top: 25px; margin-bottom: 10px; font-size: 15pt;">- Quy định về bản quyền và Mục đích sử dụng</p>
            <ul style="margin-top: 0px;">
                <li>Tính chất dự án:</b> Đây là một sản phẩm trí tuệ phục vụ mục tiêu giáo dục và cộng đồng phi lợi nhuận. Mọi hành vi thương mại hóa phần mềm mà không có sự đồng ý của tác giả đều vi phạm điều khoản.</li>
                <li>Quyền tác giả:</b> Khuyến khích chia sẻ và học hỏi từ mã nguồn. Bất kỳ bài báo, nghiên cứu nào sử dụng kết quả từ phần mềm đều phải trích dẫn nguồn đầy đủ.</li>
            </ul>

            <p style="color: #0056B3; margin-top: 25px; margin-bottom: 10px; font-size: 15pt;">- Cam kết bảo mật và Giới hạn trách nhiệm</p>
            <ul style="margin-top: 0px; margin-bottom: 20px;">
                <li style="margin-bottom: 5px;">Độ tin cậy của kết quả:</b> Kết quả trả về chỉ mang tính chất tham khảo học thuật. Nhóm phát triển không chịu trách nhiệm pháp lý cho các quyết định thực tế phát sinh từ sai số tính toán.</li>
                <li>Bảo mật thông tin:</b>Cam kết tuyệt đối về tính riêng tư. Mọi dữ liệu bài toán đều được xử lý offline cục bộ (local memory)</b>. Phần mềm KHÔNG thu thập thông tin cá nhân hay đẩy dữ liệu lên máy chủ.</li>
            </ul>
        </div>
    )";

    textBrowser->setHtml(htmlContent);
    layout->addWidget(textBrowser);

    QPushButton *btnClose = new QPushButton("Đóng", policyDialog);
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, policyDialog, &QDialog::accept);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);
    btnLayout->addStretch();

    layout->addLayout(btnLayout);

    policyDialog->exec();
    delete policyDialog;
}

