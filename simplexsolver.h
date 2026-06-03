#ifndef SIMPLEXSOLVER_H
#define SIMPLEXSOLVER_H
#include "Struct.h"
#include <vector>
#include <QString>
#include <QDebug>
#include <cmath>

struct SimplexStep {
    QString stepName;                        // Tên bước (VD: "Bảng khởi tạo", "Vòng lặp 1")
    std::vector<std::vector<double>> matrix;
    int pivotRow = -1;
    int pivotCol = -1;
    std::vector<int> currentBasicVars;
    std::vector<double> solution;
    bool isUnbounded = false;
    bool isInfeasible = false;        // Biến cơ sở tại bước đó
};

class SimplexSolver
{
public:
    // Khoi tao thuat toan voi du lieu tho
    SimplexSolver(const LinearProgram& Input);
    // Kich hoat giai bai toan. Tra ve true neu tim duoc nghiem toi uu
    bool solve();
    // Cac ham lay ket qua de hien thi
    double getOptimalZ() const;
    std::vector<double> getSolution() const;
    QString getStatus() const;
    const LinearProgram& getLp() const { return this->lp; }
    const std::vector<SimplexStep>& getHistory() const { return this->history; }

    // Bổ sung để lấy tập nghiệm vô số
    bool hasAlternativeOptima() const { return alternativeOptimaFound; }
    std::vector<double> getAlternativeSolution() const { return altSolution; }
    std::vector<double> getFirstSolution() const; // nghiệm đỉnh 1
    std::vector<double> getAltSolution()   const; // nghiệm đỉnh 2

private:
    std::vector<VarBound> originalVarBounds;
    std::vector<SimplexStep> history; // Mảng lưu toàn bộ quá trình
    int iterationCount = 0;           // Bộ đếm vòng lặp
    LinearProgram lp;
    std::vector<std::vector<double>> tableau; // Bang don hinh luu tru tinh toan
    std::vector<int> basicVariables; //Luu vet cac bien co so theo tung bang
    QString statusMsg;
    const double EPSILON = 1e-9; // Sai so cuc nho de so sanh so thuc, chong loi
    int originalVarsCount; // Luu so luong bien goc de trich xuat nghiem chuan xac
    void recordStep(const QString& name);

    //Cac ham chuan hoa
    void convertToStandardForm();
    void handleNegativeB();
    void handleVariableBounds();
    void addSlackAndSurplusVariables();

    //Cac ham don hinh & hai pha
    void buildTableau();
    bool runSimplexLoop(bool isPhaseOne = false);
    int findPivotColumn();
    int findPivotColumn(bool isPhaseOne);
    int findPivotRow(int pivotCol);
    void performPivot(int pivotRow, int pivotCol);
    // Thuat toan hai phan cho cac bai phuc tap
    bool solveTwoPhase();
    bool checkAlternativeOptima();
    void findAndRecordAlternativeOptimum();

    // Các biến hỗ trợ nghiệm vô số
    bool alternativeOptimaFound = false;
    std::vector<double> altSolution;
    std::vector<double> firstSolution; // BẮT BUỘC THÊM ĐỂ KHỚP VỚI .CPP
};

#endif // SIMPLEXSOLVER_H
