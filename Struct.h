#ifndef STRUCT_H
#define STRUCT_H
#include <vector>
#include <QString>
struct VarBound{
    QString sign;
    double value;
    bool isFree;
};

struct LinearProgram{
    std::vector<double> c;
    std::vector<std::vector<double>> A;
    std::vector<QString> signs;
    std::vector<double> b;
    double c_0;
    std::vector<VarBound> varBounds;
    bool isMaximize = true;
    // 0: Đơn hình chuẩn, 1: Bland, 2: 2 Pha, 3: Tự động
    int algoType = 3;
};

#endif // STRUCT_H
