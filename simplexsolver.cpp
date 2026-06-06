#include "simplexsolver.h"
#include <set>
#include <cmath>

static const double CLEAN_EPS = 1e-10;

SimplexSolver::SimplexSolver(const LinearProgram& inputLP){
    this->lp = inputLP;
    this->statusMsg = "Chưa giải";
}

double SimplexSolver::getOptimalZ() const{
    if(this->tableau.empty()) return 0.0;
    int m = this->tableau.size() - 1;
    int n = this->tableau[0].size() - 1;
    double z = this->tableau[m][n];
    return this->lp.isMaximize ? z : -z;
}

std::vector<double> SimplexSolver::getSolution() const {
    int origCount = (int)originalVarBounds.size();
    std::vector<double> sol(origCount, 0.0);
    if (this->tableau.empty()) return sol;

    int m = this->tableau.size() - 1;
    int n = this->tableau[0].size() - 1;

    std::vector<double> extendedSol(this->tableau[0].size() - 1, 0.0);
    for (int i = 0; i < m; i++) {
        int basicVar = this->basicVariables[i];
        if (basicVar >= 0 && basicVar < (int)extendedSol.size())
            extendedSol[basicVar] = this->tableau[i][n];
    }

    int internalIdx = 0;
    for (int i = 0; i < origCount; ++i) {
        const VarBound& vb = originalVarBounds[i];
        if (vb.isFree || vb.sign == "free") {
            sol[i] = extendedSol[internalIdx] - extendedSol[internalIdx + 1];
            internalIdx += 2;
        } else if (vb.sign == "<=") {
            sol[i] = -extendedSol[internalIdx];
            internalIdx += 1;
        } else {
            sol[i] = extendedSol[internalIdx];
            internalIdx += 1;
        }
    }
    return sol;
}

std::vector<double> SimplexSolver::getFirstSolution() const {
    return firstSolution;
}

std::vector<double> SimplexSolver::getAltSolution() const {
    return altSolution;
}

QString SimplexSolver::getStatus() const { return this->statusMsg; }

void SimplexSolver::convertToStandardForm() {
    this->originalVarsCount = lp.c.size();
    this->originalVarBounds = lp.varBounds;

    if (lp.isMaximize) {
        for (size_t i = 0; i < lp.c.size(); ++i) {
            lp.c[i] = -lp.c[i];
        }
        lp.c_0 = -lp.c_0;
    }

    for (int i = 0; i < (int)lp.signs.size(); ++i) {
        if (lp.signs[i] == ">=") {
            lp.signs[i] = "<=";
            lp.b[i] = -lp.b[i];
            for (int j = 0; j < (int)lp.A[i].size(); ++j)
                lp.A[i][j] = -lp.A[i][j];
        }
    }

    this->handleVariableBounds();
    this->addSlackAndSurplusVariables();
}

void SimplexSolver::handleNegativeB() { }

void SimplexSolver::handleVariableBounds() {
    for (int j = (int)lp.varBounds.size() - 1; j >= 0; --j) {
        VarBound vb = lp.varBounds[j];
        if (vb.isFree || vb.sign == "free") {
            for (int i = 0; i < (int)lp.A.size(); ++i)
                lp.A[i].insert(lp.A[i].begin() + j + 1, -lp.A[i][j]);
            lp.c.insert(lp.c.begin() + j + 1, -lp.c[j]);
            lp.varBounds[j].sign = ">=";
            lp.varBounds.insert(lp.varBounds.begin() + j + 1, {">=", 0.0, false});
            originalVarsCount++;
        }
        else if (vb.sign == "<=") {
            for (int i = 0; i < (int)lp.A.size(); ++i)
                lp.A[i][j] = -lp.A[i][j];
            lp.c[j] = -lp.c[j];
            lp.varBounds[j].sign = ">=";
        }
    }
}

// -----------------------------------------------------------------------
// Tự động nhận diện ma trận
// -----------------------------------------------------------------------
void SimplexSolver::addSlackAndSurplusVariables() {
    int m = lp.signs.size();
    this->basicVariables.assign(m, -1);

    for (int i = 0; i < m; ++i) {
        bool foundIdentity = false;

        if (lp.signs[i] == "=" || lp.signs[i] == "==") {
            for (size_t j = 0; j < lp.c.size(); ++j) {
                if (std::abs(lp.A[i][j] - 1.0) < CLEAN_EPS && std::abs(lp.c[j]) < CLEAN_EPS) {
                    bool isBasicCol = true;
                    for (int r = 0; r < m; ++r) {
                        if (r != i && std::abs(lp.A[r][j]) > CLEAN_EPS) {
                            isBasicCol = false;
                            break;
                        }
                    }
                    if (isBasicCol) {
                        bool alreadyUsed = false;
                        for(int r = 0; r < i; ++r) {
                            if (basicVariables[r] == (int)j) alreadyUsed = true;
                        }
                        if (!alreadyUsed) {
                            basicVariables[i] = j;
                            foundIdentity = true;
                            break;
                        }
                    }
                }
            }
        }

        if (!foundIdentity) {
            if (lp.signs[i] == "<=" || lp.signs[i] == "=" || lp.signs[i] == "==") {
                for (int row = 0; row < m; ++row)
                    lp.A[row].push_back((row == i) ? 1.0 : 0.0);
                lp.c.push_back(0.0);
                basicVariables[i] = lp.c.size() - 1;
                lp.signs[i] = "=";
            }
        } else {
            lp.signs[i] = "=";
        }
    }
}

bool SimplexSolver::solve() {
    convertToStandardForm();

    bool needsPhase1 = false;
    for (double b_val : lp.b) {
        if (b_val < -CLEAN_EPS) { needsPhase1 = true; break; }
    }

    if (needsPhase1) {
        if (lp.algoType == 0 || lp.algoType == 1) {
            statusMsg = "Không giải được với thuật toán Đơn hình! "
                        "(Tồn tại hệ số b_i âm ở dạng chuẩn, bạn có thể sử dụng đơn hình 2 pha hoặc chế độ tự động).";
            return false;
        }
        return solveTwoPhase();
    } else {
        if (lp.algoType == 2) {
            statusMsg = "Không giải được với thuật toán 2 Pha! "
                        "(Bài toán dạng cơ bản chỉ cần Đơn hình tiêu chuẩn hoặc Bland).";
            return false;
        }

        buildTableau();
        bool success = runSimplexLoop();
        if (!success) return false;

        if (checkAlternativeOptima()) {
            this->firstSolution = this->getSolution();
            findAndRecordAlternativeOptimum();

            // KIỂM TRA NGHIỆM ẢO ĐỂ QUYẾT ĐỊNH STATUS
            bool isReallyDifferent = false;
            for (size_t i = 0; i < firstSolution.size(); ++i) {
                if (std::abs(firstSolution[i] - altSolution[i]) > CLEAN_EPS) {
                    isReallyDifferent = true;
                    break;
                }
            }

            if (isReallyDifferent) {
                statusMsg = "Vô số nghiệm";
            } else {
                statusMsg = "Tối ưu";
                if (!history.empty() && history.back().stepName == "Điểm tối ưu thứ 2") {
                    history.pop_back();
                }
            }
        } else {
            statusMsg = "Tối ưu";
            this->firstSolution = this->getSolution();
            this->altSolution   = this->firstSolution;
        }
        return true;
    }
}

void SimplexSolver::buildTableau() {
    int m = lp.A.size();
    int n = lp.c.size();
    tableau.assign(m + 1, std::vector<double>(n + 1, 0.0));

    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < n; ++j) tableau[i][j] = lp.A[i][j];
        tableau[i][n] = lp.b[i];
    }
    for (int j = 0; j < n; ++j)
        tableau[m][j] = lp.c[j];

    iterationCount = 0;
    QString stepTitle = lp.isMaximize ? "Từ vựng 1 (Biến đổi Max Z → Min -Z)" : "Từ vựng 1 (Khởi tạo)";
    recordStep(stepTitle);
}

int SimplexSolver::findPivotColumn() {
    int m = tableau.size() - 1;
    int n = tableau[0].size() - 1;

    if (lp.algoType == 1) {
        for (int j = 0; j < n; ++j)
            if (tableau[m][j] < -CLEAN_EPS) return j;
    } else {
        double minVal = -CLEAN_EPS;
        int bestCol = -1;
        for (int j = 0; j < n; ++j) {
            if (tableau[m][j] < minVal) {
                minVal  = tableau[m][j];
                bestCol = j;
            }
        }
        return bestCol;
    }
    return -1;
}

int SimplexSolver::findPivotRow(int pivotCol) {
    int m = tableau.size() - 1;
    int n = tableau[0].size() - 1;
    int pivotRow    = -1;
    double minRatio = 1e9;
    int minVarIndex = (int)1e9;

    for (int i = 0; i < m; ++i) {
        double a_ij = tableau[i][pivotCol];
        if (a_ij > CLEAN_EPS) {
            double ratio = tableau[i][n] / a_ij;
            if (ratio < minRatio - CLEAN_EPS) {
                minRatio    = ratio;
                pivotRow    = i;
                minVarIndex = basicVariables[i];
            }
            else if (std::abs(ratio - minRatio) <= CLEAN_EPS) {
                if (lp.algoType == 1 && basicVariables[i] < minVarIndex) {
                    pivotRow    = i;
                    minVarIndex = basicVariables[i];
                }
            }
        }
    }
    return pivotRow;
}

void SimplexSolver::performPivot(int pivotRow, int pivotCol) {
    int m = tableau.size();
    int n = tableau[0].size();
    double pivotValue = tableau[pivotRow][pivotCol];

    for (int j = 0; j < n; ++j) {
        tableau[pivotRow][j] /= pivotValue;
        if (std::abs(tableau[pivotRow][j]) < CLEAN_EPS)
            tableau[pivotRow][j] = 0.0;
    }
    for (int i = 0; i < m; ++i) {
        if (i != pivotRow) {
            double factor = tableau[i][pivotCol];
            for (int j = 0; j < n; ++j) {
                tableau[i][j] -= factor * tableau[pivotRow][j];
                if (std::abs(tableau[i][j]) < CLEAN_EPS)
                    tableau[i][j] = 0.0;
            }
        }
    }
    basicVariables[pivotRow] = pivotCol;
    iterationCount++;
    recordStep(QString("Từ vựng %1").arg(iterationCount + 1));
}

bool SimplexSolver::runSimplexLoop(bool isPhaseOne) {
    const int MAX_ITERATIONS = 500;
    while (true) {
        if (iterationCount > MAX_ITERATIONS) {
            statusMsg = "Hiện tượng xoay vòng (Cycling)!\nThuật toán bị lặp vô hạn.";
            return false;
        }
        int pivotCol = findPivotColumn();
        if (pivotCol == -1) return true;

        int pivotRow = findPivotRow(pivotCol);
        if (pivotRow == -1) {
            statusMsg = "Bài toán không giới nội";
            if (!history.empty()) {
                history.back().isUnbounded = true;
            }
            return false;
        }
        if (!history.empty()) {
            history.back().pivotRow  = pivotRow;
            history.back().pivotCol  = pivotCol;
        }
        performPivot(pivotRow, pivotCol);
    }
}

bool SimplexSolver::solveTwoPhase() {
    int m = lp.A.size();
    int n_slack = lp.c.size();

    buildTableau();
    if (!history.empty()) history.pop_back();

    int cols = tableau[0].size() - 1;

    for (int i = 0; i <= m; ++i) {
        tableau[i].insert(tableau[i].begin() + cols, 0.0);
    }
    int a_col = cols;
    cols++;

    for (int i = 0; i < m; ++i) {
        tableau[i][a_col] = -1.0;
    }

    for (int j = 0; j <= cols; ++j) tableau[m][j] = 0.0;
    tableau[m][a_col] = 1.0;

    recordStep("Từ vựng 1 (Khởi tạo Pha 1)");

    int pivotRow = -1;
    double minRhs = 0.0;
    for (int i = 0; i < m; ++i) {
        if (tableau[i][cols] < minRhs - CLEAN_EPS) {
            minRhs = tableau[i][cols];
            pivotRow = i;
        }
    }

    if (pivotRow != -1) {
        if (!history.empty()) {
            history.back().pivotRow = pivotRow;
            history.back().pivotCol = a_col;
        }
        performPivot(pivotRow, a_col);
    }

    if (!this->runSimplexLoop()) return false;

    if (std::abs(tableau[m][cols]) > CLEAN_EPS) {
        statusMsg = "Vô nghiệm (Min ε > 0)";
        if (!history.empty()) {
            history.back().isInfeasible = true;
        }
        return false;
    }

    iterationCount = 0;

    for (int j = 0; j < n_slack; ++j) {
        tableau[m][j] = lp.c[j];
    }
    tableau[m][a_col] = 1e9;
    tableau[m][cols] = lp.c_0;

    for (int i = 0; i < m; ++i) {
        int b_col = basicVariables[i];
        double factor = tableau[m][b_col];
        if (std::abs(factor) > CLEAN_EPS) {
            for (int j = 0; j <= cols; ++j) {
                tableau[m][j] -= factor * tableau[i][j];
            }
        }
    }

    this->recordStep("Từ vựng 1 (Khởi tạo Pha 2)");

    bool success = this->runSimplexLoop();

    if (success) {
        if (this->checkAlternativeOptima()) {
            this->firstSolution = this->getSolution();
            findAndRecordAlternativeOptimum();

            // KIỂM TRA NGHIỆM ẢO ĐỂ QUYẾT ĐỊNH STATUS
            bool isReallyDifferent = false;
            for (size_t i = 0; i < firstSolution.size(); ++i) {
                if (std::abs(firstSolution[i] - altSolution[i]) > CLEAN_EPS) {
                    isReallyDifferent = true;
                    break;
                }
            }

            if (isReallyDifferent) {
                statusMsg = "Vô số nghiệm";
            } else {
                statusMsg = "Tối ưu";
                if (!history.empty() && history.back().stepName == "Điểm tối ưu thứ 2") {
                    history.pop_back();
                }
            }
        } else {
            statusMsg = "Tối ưu";
            this->firstSolution = this->getSolution();
            this->altSolution   = this->firstSolution;
        }
    }
    return success;
}

void SimplexSolver::recordStep(const QString& name) {
    SimplexStep step;
    step.stepName         = name;
    step.matrix           = this->tableau;
    step.currentBasicVars = this->basicVariables;
    step.solution         = this->getSolution();
    history.push_back(step);
}

bool SimplexSolver::checkAlternativeOptima() {
    int m = tableau.size() - 1;
    int n = tableau[0].size() - 1;

    std::set<int> ignoredCols;
    int internalIdx = 0;
    for (size_t i = 0; i < lp.varBounds.size(); ++i) {
        if (lp.varBounds[i].isFree || lp.varBounds[i].sign == "free") {
            ignoredCols.insert(internalIdx);
            ignoredCols.insert(internalIdx + 1);
            internalIdx += 2;
        } else {
            internalIdx += 1;
        }
    }

    for (int j = 0; j < n; ++j) {
        if (ignoredCols.count(j)) continue;
        bool isBasic = false;
        for (int i = 0; i < m; ++i)
            if (basicVariables[i] == j) { isBasic = true; break; }
        if (!isBasic && std::abs(tableau[m][j]) <= CLEAN_EPS
            && std::abs(tableau[m][j]) < 1e8)
            return true;
    }
    return false;
}

void SimplexSolver::findAndRecordAlternativeOptimum() {
    int m = tableau.size() - 1;
    int n = tableau[0].size() - 1;

    std::set<int> ignoredCols;
    int internalIdx = 0;
    for (size_t i = 0; i < lp.varBounds.size(); ++i) {
        if (lp.varBounds[i].isFree || lp.varBounds[i].sign == "free") {
            ignoredCols.insert(internalIdx);
            ignoredCols.insert(internalIdx + 1);
            internalIdx += 2;
        } else {
            internalIdx += 1;
        }
    }

    // Sao lưu lại toàn bộ trạng thái để có thể rollback khi tìm thấy nghiệm ảo
    auto backupTableau = this->tableau;
    auto backupBasicVars = this->basicVariables;
    auto backupHistory = this->history;

    bool foundDifferent = false;

    // [FIX MỚI] Vòng lặp quét tất cả các cột có khả năng sinh ra vô số nghiệm
    for (int j = 0; j < n; ++j) {
        if (ignoredCols.count(j)) continue;

        bool isBasic = false;
        for (int i = 0; i < m; ++i) {
            if (basicVariables[i] == j) { isBasic = true; break; }
        }

        // Nếu phát hiện biến phi cơ sở có hệ số z = 0
        if (!isBasic && std::abs(tableau[m][j]) <= CLEAN_EPS) {
            int altPivotRow = findPivotRow(j);
            if (altPivotRow != -1) {
                if (!history.empty()) {
                    history.back().pivotRow  = altPivotRow;
                    history.back().pivotCol  = j;
                }

                // Thực hiện Pivot (Xoay)
                performPivot(altPivotRow, j);
                std::vector<double> tempSol = this->getSolution();

                // Kiểm tra xem nghiệm mới có THỰC SỰ KHÁC nghiệm cũ không
                bool isDifferent = false;
                for (size_t k = 0; k < firstSolution.size(); ++k) {
                    if (std::abs(firstSolution[k] - tempSol[k]) > CLEAN_EPS) {
                        isDifferent = true;
                        break;
                    }
                }

                if (isDifferent) {
                    // Nếu thực sự khác, lưu nghiệm thứ 2 và dừng tìm kiếm
                    this->altSolution = tempSol;
                    if (!history.empty()) {
                        history.back().stepName = "Điểm tối ưu thứ 2";
                    }
                    foundDifferent = true;

                    // Khôi phục lại trạng thái bảng để không ảnh hưởng luồng chính
                    this->tableau = backupTableau;
                    this->basicVariables = backupBasicVars;
                    break;
                } else {
                    // Nếu xoay xong mà nghiệm vẫn giữ nguyên (nghiệm ảo), rollback lại và đi thử cột tiếp theo
                    this->tableau = backupTableau;
                    this->basicVariables = backupBasicVars;
                    this->history = backupHistory;
                }
            }
        }
    }

    // Nếu đã quét sạch mọi cột mà không tìm ra điểm nào khác
    if (!foundDifferent) {
        this->altSolution = this->firstSolution;
    }
}
