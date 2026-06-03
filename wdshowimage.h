#ifndef WDSHOWIMAGE_H
#define WDSHOWIMAGE_H

#include <QMainWindow>
#include "Struct.h"
#include "qcustomplot.h"
#include "simplexsolver.h"

namespace Ui {
class WdShowImage;
}

class WdShowImage : public QMainWindow
{
    Q_OBJECT

public:
    explicit WdShowImage(QWidget *parent = nullptr);
    ~WdShowImage();
    void drawGraph(const LinearProgram& lp,
                   const LinearProgram& origLp,
                   const std::vector<double>& solution,
                   const std::vector<SimplexStep>& history);

private slots:
    void on_pushButton_clicked(); // Nút OK
    void on_btnPrev_clicked();    // Nút Lùi
    void on_btnNext_clicked();    // Nút Tiến

private:
    Ui::WdShowImage *ui;

    LinearProgram currentLp;
    LinearProgram originalLp;
    std::vector<SimplexStep> stepHistory;
    int currentStepIndex;

    QCPCurve *simplexPath;
    QCPCurve *zLineGraph;
    QCPItemText *stepLabel;

    QPointF getCoordinateFromStep(const SimplexStep& step);
    void renderStep(int stepIndex); // Hàm vẽ lại 1 bước cụ thể
};

#endif // WDSHOWIMAGE_H
