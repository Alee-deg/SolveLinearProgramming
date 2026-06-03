#include "wdshowimage.h"
#include "ui_wdshowimage.h"
#include "qcustomplot.h"

WdShowImage::WdShowImage(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::WdShowImage)
{
    ui->setupUi(this);
    this->setWindowTitle("Biểu diễn hình học miền nghiệm");
    this->setWindowState(Qt::WindowMaximized);
    this->setWindowIcon(QIcon(":/logo.png"));
}

WdShowImage::~WdShowImage()
{
    delete ui;
}

void WdShowImage::on_pushButton_clicked()
{
    if (this->parentWidget()) this->parentWidget()->show();
    this->close();
}

QPointF WdShowImage::getCoordinateFromStep(const SimplexStep& step) {
    double x1 = 0, x2 = 0;
    int m = step.matrix.size() - 1;
    int n = step.matrix[0].size() - 1;

    int col_x1 = -1, col_x2 = -1;
    int internalIdx = 0;
    for (int i = 0; i < (int)originalLp.varBounds.size(); ++i) {
        if (i == 0) col_x1 = internalIdx;
        if (i == 1) col_x2 = internalIdx;
        if (originalLp.varBounds[i].isFree || originalLp.varBounds[i].sign == "free")
            internalIdx += 2;
        else
            internalIdx += 1;
    }

    for (int i = 0; i < m; ++i) {
        if (col_x1 >= 0 && step.currentBasicVars[i] == col_x1)
            x1 = step.matrix[i][n];
        if (col_x2 >= 0 && step.currentBasicVars[i] == col_x2)
            x2 = step.matrix[i][n];
    }

    if (col_x2 >= 0 && (originalLp.varBounds.size() > 1) &&
        (originalLp.varBounds[1].isFree || originalLp.varBounds[1].sign == "free")) {
        int col_x2pp = col_x2 + 1;
        for (int i = 0; i < m; ++i) {
            if (step.currentBasicVars[i] == col_x2pp) x2 -= step.matrix[i][n];
        }
    }
    if (col_x1 >= 0 && (originalLp.varBounds.size() > 0) &&
        (originalLp.varBounds[0].isFree || originalLp.varBounds[0].sign == "free")) {
        int col_x1pp = col_x1 + 1;
        for (int i = 0; i < m; ++i) {
            if (step.currentBasicVars[i] == col_x1pp) x1 -= step.matrix[i][n];
        }
    }
    if (originalLp.varBounds.size() > 0 && originalLp.varBounds[0].sign == "<=") x1 = -x1;
    if (originalLp.varBounds.size() > 1 && originalLp.varBounds[1].sign == "<=") x2 = -x2;

    return QPointF(x1, x2);
}

// -----------------------------------------------------------------------
// HÀM VẼ TỔNG THỂ KHI MỚI MỞ CỬA SỔ
// -----------------------------------------------------------------------
void WdShowImage::drawGraph(const LinearProgram& lp,
                            const LinearProgram& origLp,
                            const std::vector<double>& solution,
                            const std::vector<SimplexStep>& history)
{
    currentLp  = lp;
    originalLp = origLp;
    stepHistory = history;
    currentStepIndex = 0; // Luôn bắt đầu từ Bảng 0

    ui->plot->clearGraphs();
    ui->plot->clearPlottables();
    ui->plot->clearItems();
    ui->plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    // -------------------------------------------------------------------
    // 1. VẼ CÁC ĐƯỜNG RÀNG BUỘC
    // -------------------------------------------------------------------
    QList<QColor> colors = {Qt::blue, Qt::darkCyan, Qt::darkMagenta,
                            Qt::darkYellow, Qt::darkRed};

    for (size_t i = 0; i < origLp.A.size(); ++i) {
        if (origLp.A[i].size() < 2) continue;
        double a1 = origLp.A[i][0], a2 = origLp.A[i][1], b = origLp.b[i];

        QVector<double> xVec, yVec, tVec = {0, 1};
        if (std::abs(a2) > 1e-9) {
            xVec = {-20, 50};
            yVec = {(b - a1 * xVec[0]) / a2, (b - a1 * xVec[1]) / a2};
        } else {
            double xVal = b / a1;
            xVec = {xVal, xVal};
            yVec = {-20, 50};
        }

        QCPCurve *constraint = new QCPCurve(ui->plot->xAxis, ui->plot->yAxis);
        constraint->setData(tVec, xVec, yVec);
        constraint->setPen(QPen(colors[i % colors.size()], 2, Qt::SolidLine));

        // Nhãn phương trình
        QString eqText;
        if (std::abs(a1) > 1e-9) {
            if (std::abs(a1 - 1.0) < 1e-9)       eqText += "x1";
            else if (std::abs(a1 + 1.0) < 1e-9)  eqText += "-x1";
            else eqText += QString::number(a1, 'g', 3) + "x1";
        }
        if (std::abs(a2) > 1e-9) {
            if (!eqText.isEmpty()) {
                eqText += (a2 > 0) ? " + " : " - ";
                if (std::abs(std::abs(a2) - 1.0) > 1e-9)
                    eqText += QString::number(std::abs(a2), 'g', 3);
                eqText += "x2";
            } else {
                if (std::abs(a2 - 1.0) < 1e-9)       eqText += "x2";
                else if (std::abs(a2 + 1.0) < 1e-9)  eqText += "-x2";
                else eqText += QString::number(a2, 'g', 3) + "x2";
            }
        }
        eqText += " = " + QString::number(b, 'g', 3);

        QCPItemText *label = new QCPItemText(ui->plot);
        if (std::abs(a2) > 1e-9) {
            label->position->setCoords(5, (b - a1 * 5) / a2 + 1.5);
        } else {
            label->position->setCoords(b / a1 + 1.5, 5);
        }
        label->setText(eqText);
        label->setColor(colors[i % colors.size()]);
        label->setFont(QFont("Arial", 10, QFont::Bold));
    }

    // -------------------------------------------------------------------
    // 2. TÔ MÀU MIỀN NGHIỆM
    // -------------------------------------------------------------------
    std::vector<QPointF> vertices;
    struct Ineq { double a, b, c; };
    std::vector<Ineq> ineqs;

    for (size_t i = 0; i < origLp.A.size(); ++i) {
        if (origLp.A[i].size() < 2) continue;
        double a = origLp.A[i][0], b = origLp.A[i][1], c = origLp.b[i];
        if (origLp.signs[i] == "<=")        ineqs.push_back({a,  b,  c});
        else if (origLp.signs[i] == ">=")  ineqs.push_back({-a, -b, -c});
        else if (origLp.signs[i] == "==") {
            ineqs.push_back({ a,  b,  c});
            ineqs.push_back({-a, -b, -c});
        }
    }

    if (origLp.varBounds.size() >= 1 && !origLp.varBounds[0].isFree) {
        double val = origLp.varBounds[0].value;
        if (origLp.varBounds[0].sign == ">=")       ineqs.push_back({-1, 0, -val});
        else if (origLp.varBounds[0].sign == "<=")  ineqs.push_back({ 1, 0,  val});
    }
    if (origLp.varBounds.size() >= 2 && !origLp.varBounds[1].isFree) {
        double val = origLp.varBounds[1].value;
        if (origLp.varBounds[1].sign == ">=")       ineqs.push_back({0, -1, -val});
        else if (origLp.varBounds[1].sign == "<=")  ineqs.push_back({0,  1,  val});
    }

    double BND = 1000.0;
    ineqs.push_back({ 1, 0, BND});
    ineqs.push_back({ 0, 1, BND});
    ineqs.push_back({-1, 0, BND});
    ineqs.push_back({ 0,-1, BND});

    for (size_t i = 0; i < ineqs.size(); ++i) {
        for (size_t j = i + 1; j < ineqs.size(); ++j) {
            double det = ineqs[i].a * ineqs[j].b - ineqs[i].b * ineqs[j].a;
            if (std::abs(det) < 1e-9) continue;

            double x1 = (ineqs[i].c * ineqs[j].b - ineqs[j].c * ineqs[i].b) / det;
            double x2 = (ineqs[i].a * ineqs[j].c - ineqs[j].a * ineqs[i].c) / det;

            bool isValid = true;
            for (size_t k = 0; k < ineqs.size(); ++k) {
                if (ineqs[k].a * x1 + ineqs[k].b * x2 > ineqs[k].c + 1e-6) {
                    isValid = false; break;
                }
            }
            if (!isValid) continue;

            bool exists = false;
            for (auto& p : vertices)
                if (std::abs(p.x() - x1) < 1e-5 && std::abs(p.y() - x2) < 1e-5)
                { exists = true; break; }
            if (!exists) vertices.push_back(QPointF(x1, x2));
        }
    }

    if (vertices.size() >= 3) {
        std::vector<QPointF> pts = vertices;
        std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) {
            if (std::abs(a.x() - b.x()) > 1e-9) return a.x() < b.x();
            return a.y() < b.y();
        });

        auto cross = [](QPointF O, QPointF A, QPointF B) {
            return (A.x()-O.x())*(B.y()-O.y()) - (A.y()-O.y())*(B.x()-O.x());
        };

        std::vector<QPointF> hull;
        for (auto& pt : pts) {
            while (hull.size() >= 2 && cross(hull[hull.size()-2], hull[hull.size()-1], pt) <= 0)
                hull.pop_back();
            hull.push_back(pt);
        }
        size_t lower_size = hull.size() + 1;
        for (int i = (int)pts.size() - 2; i >= 0; --i) {
            while (hull.size() >= lower_size && cross(hull[hull.size()-2], hull[hull.size()-1], pts[i]) <= 0)
                hull.pop_back();
            hull.push_back(pts[i]);
        }
        if (!hull.empty()) hull.pop_back();

        QCPCurve *region = new QCPCurve(ui->plot->xAxis, ui->plot->yAxis);
        QVector<double> pT, pX, pY;
        for (int i = 0; i < (int)hull.size(); ++i) {
            pT.push_back(i); pX.push_back(hull[i].x()); pY.push_back(hull[i].y());
        }
        pT.push_back(hull.size()); pX.push_back(hull[0].x()); pY.push_back(hull[0].y());

        region->setData(pT, pX, pY);
        region->setBrush(QBrush(QColor(255, 0, 0, 80)));
        region->setPen(Qt::NoPen);
    }

    // -------------------------------------------------------------------
    // 3. KHỞI TẠO ĐỐI TƯỢNG VẼ BƯỚC NHẢY (QCPCurve)
    // -------------------------------------------------------------------
    simplexPath = new QCPCurve(ui->plot->xAxis, ui->plot->yAxis);
    simplexPath->setPen(QPen(Qt::black, 2, Qt::DotLine));
    simplexPath->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, Qt::red, 8));

    zLineGraph = new QCPCurve(ui->plot->xAxis, ui->plot->yAxis);
    zLineGraph->setPen(QPen(Qt::red, 2, Qt::DashLine));

    stepLabel = new QCPItemText(ui->plot);
    stepLabel->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
    stepLabel->position->setType(QCPItemPosition::ptAxisRectRatio);
    stepLabel->position->setCoords(0.05, 0.05);
    stepLabel->setFont(QFont("Arial", 12, QFont::Bold));
    stepLabel->setColor(Qt::darkRed);

    ui->plot->xAxis->setLabel("x1");
    ui->plot->yAxis->setLabel("x2");

    // Tính toán viewport camera
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool first = true;
    for (auto& v : vertices) {
        if (std::abs(v.x()) < BND - 1 && std::abs(v.y()) < BND - 1) {
            if (first) { minX = maxX = v.x(); minY = maxY = v.y(); first = false; }
            else {
                minX = std::min(minX, v.x()); maxX = std::max(maxX, v.x());
                minY = std::min(minY, v.y()); maxY = std::max(maxY, v.y());
            }
        }
    }

    for (const auto& step : stepHistory) {
        QPointF p = getCoordinateFromStep(step);
        if (first) { minX = maxX = p.x(); minY = maxY = p.y(); first = false; }
        else {
            minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
        }
    }

    if (first) { minX = -10; maxX = 10; minY = -10; maxY = 10; }
    ui->plot->xAxis->setRange(minX - 3, maxX + 3);
    ui->plot->yAxis->setRange(minY - 3, maxY + 3);

    // [FIX] Vẽ ra luôn bước 0 đầu tiên thay vì dùng QTimer
    renderStep(currentStepIndex);
}

// -----------------------------------------------------------------------
// HÀM VẼ LẠI ĐỒ THỊ TẠI MỘT BƯỚC CỤ THỂ
// -----------------------------------------------------------------------
void WdShowImage::renderStep(int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= (int)stepHistory.size()) return;

    // 1. Cập nhật đường đi Simplex
    QVector<double> pathT, pathX, pathY;
    for (int i = 0; i <= stepIndex; ++i) {
        QPointF p = getCoordinateFromStep(stepHistory[i]);
        pathT.push_back(i);
        pathX.push_back(p.x());
        pathY.push_back(p.y());
    }
    simplexPath->setData(pathT, pathX, pathY);

    // 2. Cập nhật đường Z tại vị trí hiện tại
    const SimplexStep& currentStepInfo = stepHistory[stepIndex];
    QPointF currentPoint = getCoordinateFromStep(currentStepInfo);

    if (originalLp.c.size() >= 2) {
        double c1 = originalLp.c[0];
        double c2 = originalLp.c[1];
        double currentZ = c1 * currentPoint.x() + c2 * currentPoint.y();

        QVector<double> zX, zY, zT = {0, 1};
        if (std::abs(c2) > 1e-9) {
            zX = {-20, 50};
            zY = {(currentZ - c1 * zX[0]) / c2,
                  (currentZ - c1 * zX[1]) / c2};
        } else if (std::abs(c1) > 1e-9) {
            double xVal = currentZ / c1;
            zX = {xVal, xVal};
            zY = {-20, 50};
        }
        zLineGraph->setData(zT, zX, zY);
    }

    // -------------------------------------------------------------------
    // 3. CẬP NHẬT NHÃN TRẠNG THÁI
    // (Dùng toán học để nhận dạng Vô số nghiệm thay vì bắt chữ "Điểm tối ưu thứ 2")
    // -------------------------------------------------------------------
    bool isInfiniteProblem = false;
    if (stepHistory.size() >= 2) {
        int m = stepHistory.back().matrix.size() - 1;
        int n = stepHistory.back().matrix[0].size() - 1;
        double lastZ = stepHistory.back().matrix[m][n];
        double prevZ = stepHistory[stepHistory.size() - 2].matrix[m][n];

        // Nếu giá trị hàm Z ở 2 bước cuối cùng bằng nhau và không bị lỗi -> đây là trường hợp vô số nghiệm
        if (std::abs(lastZ - prevZ) < 1e-9 &&
            !stepHistory.back().isInfeasible &&
            !stepHistory.back().isUnbounded)
        {
            isInfiniteProblem = true;
        }
    }

    if (stepIndex == (int)stepHistory.size() - 1) {
        // Kiểm tra trực tiếp bằng cờ trạng thái từ Solver truyền sang
        if (currentStepInfo.isInfeasible) {
            stepLabel->setText(
                QString("%1\nBài toán VÔ NGHIỆM!.")
                    .arg(currentStepInfo.stepName)
                );
            stepLabel->setColor(Qt::darkRed);
        }
        else if (currentStepInfo.isUnbounded) {
            stepLabel->setText(
                QString("%1\nBài toán KHÔNG GIỚI NỘI!.")
                    .arg(currentStepInfo.stepName)
                );
            stepLabel->setColor(Qt::darkRed);
        }
        else {
            stepLabel->setText(
                QString("%1\n%2: (%3, %4)")
                    .arg(currentStepInfo.stepName)
                    .arg(isInfiniteProblem ? "Tọa độ tối ưu thứ hai" : "Tọa độ tối ưu")
                    .arg(currentPoint.x(), 0, 'f', 2)
                    .arg(currentPoint.y(), 0, 'f', 2)
                );
            stepLabel->setColor(Qt::blue);
        }
    }
    // Xử lý bước trước bước cuối cùng nếu có vô số nghiệm
    else if (stepIndex == (int)stepHistory.size() - 2 && isInfiniteProblem) {
        stepLabel->setText(
            QString("%1\nTọa độ tối ưu thứ nhất: (%2, %3)")
                .arg(currentStepInfo.stepName)
                .arg(currentPoint.x(), 0, 'f', 2)
                .arg(currentPoint.y(), 0, 'f', 2)
            );
        stepLabel->setColor(Qt::blue); // Dùng màu xanh lam giống tọa độ tối ưu
    }
    else {
        stepLabel->setText(
            QString("%1\nTọa độ: (%2, %3)")
                .arg(currentStepInfo.stepName)
                .arg(currentPoint.x(), 0, 'f', 2)
                .arg(currentPoint.y(), 0, 'f', 2)
            );
        stepLabel->setColor(Qt::darkMagenta);
    }

    // 4. Update hiển thị các nút bấm
    ui->btnPrev->setEnabled(stepIndex > 0);
    ui->btnNext->setEnabled(stepIndex < (int)stepHistory.size() - 1);

    ui->plot->replot();
}

void WdShowImage::on_btnPrev_clicked()
{
    if (currentStepIndex > 0) {
        currentStepIndex--;
        renderStep(currentStepIndex);
    }
}

void WdShowImage::on_btnNext_clicked()
{
    if (currentStepIndex < (int)stepHistory.size() - 1) {
        currentStepIndex++;
        renderStep(currentStepIndex);
    }
}
