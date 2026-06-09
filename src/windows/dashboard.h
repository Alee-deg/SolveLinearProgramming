#ifndef DASHBOARD_H
#define DASHBOARD_H
#include "wdsolve.h"
#include "Struct.h"
#include <QTimer>
#include <QMainWindow>

// Khai báo tiền đạo (Forward declaration) cho lớp MathInput mới
class MathInput;

namespace Ui {
class Dashboard;
}

class Dashboard : public QMainWindow
{
    Q_OBJECT

public:
    explicit Dashboard(QWidget *parent = nullptr);
    ~Dashboard();

    void setupObjectiveFunctionTable(int n);
    void setupConstraintsTable(int m, int n);
    void setupNumericCell(int row, int col);
    void setupVariableConstraints(int n);
    void getDataFromWd(LinearProgram &lb);

    // [ĐÃ SỬA] Chỉ giữ lại duy nhất 1 hàm tạo ô nhập liệu thông minh
    MathInput* createSpinBox(QWidget *parent = nullptr);

private slots:
    void on_pushButton_4_clicked();
    void on_pushButton_2_clicked();
    void on_pushButton_3_clicked();
    void on_pushButton_5_clicked();
    void on_btn_HuongDan_clicked();

private:
    Ui::Dashboard *ui;
    WdSolve* wd_solve;

protected:
    void closeEvent(QCloseEvent *event) override;
};

#endif // DASHBOARD_H
