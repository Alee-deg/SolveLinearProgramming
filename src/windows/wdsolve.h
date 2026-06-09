#ifndef WDSOLVE_H
#define WDSOLVE_H
#include "simplexsolver.h"
#include <QMainWindow>
#include "wdshowimage.h"
#include "wdchatbot.h"

namespace Ui {
class WdSolve;
}

#include <vector>

class WdSolve : public QMainWindow // hoặc QDialog/QWidget tùy bạn set up
{
    Q_OBJECT

public:
    explicit WdSolve(QWidget *parent = nullptr);
    ~WdSolve();

    // CẬP NHẬT: Thêm tham số altSolution vào hàm displayResults
    void displayResults(const LinearProgram& lp,
                        const LinearProgram& originalLp,
                        const QString& status,
                        double optimalZ,
                        const std::vector<double>& solution,
                        const std::vector<double>& altSolution, // Thêm nghiệm thứ 2
                        const std::vector<SimplexStep>& history);

private slots:
    void on_pushButton_clicked();

    void on_pushButton_2_clicked();

    void on_pushButton_3_clicked();

private:
    Ui::WdSolve *ui;
    WdShowImage* wd_show;
    LinearProgram currentLp;
    std::vector<double> currentSolution;
    std::vector<double> currentAltSolution; // Thêm biến lưu nghiệm thứ 2
    std::vector<SimplexStep> currentHistory;
    WdChatBot* wd_ChatBot;
    LinearProgram currentOriginalLp;
};

#endif // WDSOLVE_H
