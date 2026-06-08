#include "simplexsolver.h"
#include <set>
#include <cmath>

// Hằng số dùng để so sánh số thực, tránh các lỗi sai số dấu phẩy động (floating-point precision)
static const double CLEAN_EPS = 1e-10;

// Constructor: Khởi tạo bộ giải với bài toán quy hoạch tuyến tính đầu vào
SimplexSolver::SimplexSolver(const LinearProgram& inputLP){
    this->lp = inputLP;
    this->statusMsg = "Chưa giải";
}

// Lấy giá trị hàm mục tiêu (Z) tối ưu
double SimplexSolver::getOptimalZ() const{
    if(this->tableau.empty()) return 0.0;
    int m = this->tableau.size() - 1;
    int n = this->tableau[0].size() - 1;
    double z = this->tableau[m][n];
    // Nếu bài toán gốc là Maximize, giá trị Z trong bảng đang bị đảo dấu (do chuyển sang Min -Z), cần đảo lại
    return this->lp.isMaximize ? z : -z;
}

// Trích xuất nghiệm tối ưu cho các biến gốc của bài toán
std::vector<double> SimplexSolver::getSolution() const {
    int origCount = (int)originalVarBounds.size();
    std::vector<double> sol(origCount, 0.0);
    if (this->tableau.empty()) return sol;

    int m = this->tableau.size() - 1;
    int n = this->tableau[0].size() - 1;

    // Mảng chứa giá trị của tất cả các biến (bao gồm cả biến nội bộ do tách biến tùy ý)
    std::vector<double> extendedSol(this->tableau[0].size() - 1, 0.0);
    for (int i = 0; i < m; i++) {
        int basicVar = this->basicVariables[i];
        if (basicVar >= 0 && basicVar < (int)extendedSol.size())
            extendedSol[basicVar] = this->tableau[i][n]; // Lấy giá trị từ cột hệ số tự do (cột cuối)
    }

    // Ánh xạ lại nghiệm từ các biến nội bộ về các biến gốc ban đầu
    int internalIdx = 0;
    for (int i = 0; i < origCount; ++i) {
        const VarBound& vb = originalVarBounds[i];
        if (vb.isFree || vb.sign == "free") {
            // Biến tùy ý (free) được tách thành (x' - x'')
            sol[i] = extendedSol[internalIdx] - extendedSol[internalIdx + 1];
            internalIdx += 2;
        } else if (vb.sign == "<=") {
            // Biến <= 0 đã bị đổi dấu thành >= 0
            sol[i] = -extendedSol[internalIdx];
            internalIdx += 1;
        } else {
            // Biến >= 0 bình thường
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

// Chuyển đổi bài toán về dạng chuẩn (Standard Form) để áp dụng thuật toán Đơn hình
void SimplexSolver::convertToStandardForm() {
    this->originalVarsCount = lp.c.size();
    this->originalVarBounds = lp.varBounds;

    // Chuyển Maximize thành Minimize bằng cách đổi dấu hàm mục tiêu
    if (lp.isMaximize) {
        for (size_t i = 0; i < lp.c.size(); ++i) {
            lp.c[i] = -lp.c[i];
        }
        lp.c_0 = -lp.c_0;
    }

    // Đổi các ràng buộc >= thành <= bằng cách nhân -1 vào hai vế
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

// Xử lý các điều kiện của biến (tùy ý, <= 0) đưa về dạng chuẩn >= 0
void SimplexSolver::handleVariableBounds() {
    for (int j = (int)lp.varBounds.size() - 1; j >= 0; --j) {
        VarBound vb = lp.varBounds[j];
        if (vb.isFree || vb.sign == "free") {
            // Tách biến tùy ý x_j thành x_j' - x_j'' (chèn thêm cột mới vào A và c)
            for (int i = 0; i < (int)lp.A.size(); ++i)
                lp.A[i].insert(lp.A[i].begin() + j + 1, -lp.A[i][j]);
            lp.c.insert(lp.c.begin() + j + 1, -lp.c[j]);
            lp.varBounds[j].sign = ">=";
            lp.varBounds.insert(lp.varBounds.begin() + j + 1, {">=", 0.0, false});
            originalVarsCount++;
        }
        else if (vb.sign == "<=") {
            // Đổi dấu cột hệ số của biến <= 0
            for (int i = 0; i < (int)lp.A.size(); ++i)
                lp.A[i][j] = -lp.A[i][j];
            lp.c[j] = -lp.c[j];
            lp.varBounds[j].sign = ">=";
        }
    }
}

// -----------------------------------------------------------------------
// Tự động nhận diện ma trận cơ sở & thêm biến bù (slack) / biến giả (artificial)
// -----------------------------------------------------------------------
void SimplexSolver::addSlackAndSurplusVariables() {
    int m = lp.signs.size();
    this->basicVariables.assign(m, -1);

    for (int i = 0; i < m; ++i) {
        bool foundIdentity = false;

        // Nếu là ràng buộc '=', thử tìm xem có cột nào tạo thành vector đơn vị e_i không
        if (lp.signs[i] == "=" || lp.signs[i] == "==") {
            for (size_t j = 0; j < lp.c.size(); ++j) {
                if (std::abs(lp.A[i][j] - 1.0) < CLEAN_EPS && std::abs(lp.c[j]) < CLEAN_EPS) {
                    bool isBasicCol = true;
                    // Kiểm tra các phần tử khác trong cột có bằng 0 không
                    for (int r = 0; r < m; ++r) {
                        if (r != i && std::abs(lp.A[r][j]) > CLEAN_EPS) {
                            isBasicCol = false;
                            break;
                        }
                    }
                    if (isBasicCol) {
                        // Tránh dùng lại cột đã được chọn làm biến cơ sở cho dòng khác
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

        // Nếu không có biến nào thỏa mãn ma trận cơ sở, tiến hành thêm biến bù/biến giả
        if (!foundIdentity) {
            if (lp.signs[i] == "<=" || lp.signs[i] == "=" || lp.signs[i] == "==") {
                // Thêm một cột mới đại diện cho biến bù/giả
                for (int row = 0; row < m; ++row)
                    lp.A[row].push_back((row == i) ? 1.0 : 0.0);
                lp.c.push_back(0.0);
                basicVariables[i] = lp.c.size() - 1; // Gán biến này làm biến cơ sở cho dòng i
                lp.signs[i] = "=";
            }
        } else {
            lp.signs[i] = "=";
        }
    }
}

// Hàm chính điều phối việc giải bài toán
bool SimplexSolver::solve() {
    convertToStandardForm();

    // Kiểm tra xem có hệ số b nào âm không (nếu có, cần giải bằng Đơn hình 2 pha)
    bool needsPhase1 = false;
    for (double b_val : lp.b) {
        if (b_val < -CLEAN_EPS) { needsPhase1 = true; break; }
    }

    if (needsPhase1) {
        // Kiểm tra loại thuật toán người dùng chọn
        if (lp.algoType == 0 || lp.algoType == 1) {
            statusMsg = "Không giải được với thuật toán Đơn hình! "
                        "(Tồn tại hệ số b_i âm ở dạng chuẩn, bạn có thể sử dụng đơn hình 2 pha hoặc chế độ tự động).";
            return false;
        }
        return solveTwoPhase(); // Chạy 2 pha nếu có b âm
    } else {
        // Nếu không có b âm mà người dùng ép dùng 2 pha thì báo lỗi
        if (lp.algoType == 2) {
            statusMsg = "Không giải được với thuật toán 2 Pha! "
                        "(Bài toán dạng cơ bản chỉ cần Đơn hình tiêu chuẩn hoặc Bland).";
            return false;
        }

        buildTableau();
        bool success = runSimplexLoop();
        if (!success) return false;

        // Xử lý sau khi tìm được một nghiệm tối ưu (kiểm tra có vô số nghiệm không)
        if (checkAlternativeOptima()) {
            this->firstSolution = this->getSolution();
            findAndRecordAlternativeOptimum();

            // KIỂM TRA NGHIỆM ẢO ĐỂ QUYẾT ĐỊNH STATUS
            // So sánh nghiệm thứ nhất và nghiệm thứ hai xem có thực sự khác nhau không (chống suy biến)
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
                // Xóa lịch sử "Điểm tối ưu thứ 2" nếu đó chỉ là nghiệm ảo
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

// Xây dựng Bảng đơn hình (Tableau) ban đầu
void SimplexSolver::buildTableau() {
    int m = lp.A.size();
    int n = lp.c.size();
    tableau.assign(m + 1, std::vector<double>(n + 1, 0.0));

    // Đổ dữ liệu từ A và b vào bảng
    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < n; ++j) tableau[i][j] = lp.A[i][j];
        tableau[i][n] = lp.b[i];
    }
    // Đổ hàm mục tiêu c vào hàng cuối cùng
    for (int j = 0; j < n; ++j)
        tableau[m][j] = lp.c[j];

    iterationCount = 0;
    QString stepTitle = lp.isMaximize ? "Từ vựng 1 (Biến đổi Max Z → Min -Z)" : "Từ vựng 1 (Khởi tạo)";
    recordStep(stepTitle);
}

// Tìm Cột xoay (Biến vào)
int SimplexSolver::findPivotColumn() {
    int m = tableau.size() - 1;
    int n = tableau[0].size() - 1;

    // algoType == 1: Quy tắc Bland (chống xoay vòng, chọn biến có chỉ số nhỏ nhất có c_j < 0)
    if (lp.algoType == 1) {
        for (int j = 0; j < n; ++j)
            if (tableau[m][j] < -CLEAN_EPS) return j;
    } else {
        // Mặc định: Quy tắc Dantzig (chọn biến có c_j âm nhất, tốc độ hội tụ nhanh hơn)
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
    return -1; // Không tìm thấy cột xoay -> Đã đạt tối ưu
}

// Tìm Hàng xoay (Biến ra) sử dụng bài toán tỷ số nhỏ nhất (Minimum Ratio Test)
int SimplexSolver::findPivotRow(int pivotCol) {
    int m = tableau.size() - 1;
    int n = tableau[0].size() - 1;
    int pivotRow    = -1;
    double minRatio = 1e9;
    int minVarIndex = (int)1e9;

    for (int i = 0; i < m; ++i) {
        double a_ij = tableau[i][pivotCol];
        if (a_ij > CLEAN_EPS) { // Chỉ xét các hệ số a_ij dương
            double ratio = tableau[i][n] / a_ij;
            if (ratio < minRatio - CLEAN_EPS) {
                minRatio    = ratio;
                pivotRow    = i;
                minVarIndex = basicVariables[i];
            }
            // Xử lý trường hợp tỷ số hòa (Ties)
            else if (std::abs(ratio - minRatio) <= CLEAN_EPS) {
                // Nếu dùng quy tắc Bland, ưu tiên biến có chỉ số nhỏ hơn rời khỏi cơ sở
                if (lp.algoType == 1 && basicVariables[i] < minVarIndex) {
                    pivotRow    = i;
                    minVarIndex = basicVariables[i];
                }
            }
        }
    }
    return pivotRow;
}

// Thực hiện phép biến đổi Gauss-Jordan (Xoay/Pivot)
void SimplexSolver::performPivot(int pivotRow, int pivotCol) {
    int m = tableau.size();
    int n = tableau[0].size();
    double pivotValue = tableau[pivotRow][pivotCol];

    // Chia toàn bộ hàng xoay cho phần tử xoay (để biến phần tử xoay thành 1)
    for (int j = 0; j < n; ++j) {
        tableau[pivotRow][j] /= pivotValue;
        if (std::abs(tableau[pivotRow][j]) < CLEAN_EPS)
            tableau[pivotRow][j] = 0.0;
    }
    // Biến các phần tử khác trong cột xoay thành 0
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
    // Cập nhật lại danh sách biến cơ sở
    basicVariables[pivotRow] = pivotCol;
    iterationCount++;
    recordStep(QString("Từ vựng %1").arg(iterationCount + 1));
}

// Vòng lặp chính của Đơn hình
bool SimplexSolver::runSimplexLoop(bool isPhaseOne) {
    const int MAX_ITERATIONS = 500;
    while (true) {
        // Tránh bị lặp vô hạn (lặp vòng - Cycling)
        if (iterationCount > MAX_ITERATIONS) {
            statusMsg = "Hiện tượng xoay vòng (Cycling)!\nThuật toán bị lặp vô hạn.";
            return false;
        }

        int pivotCol = findPivotColumn();
        if (pivotCol == -1) return true; // Dấu hiệu dừng: Tất cả hệ số dòng mục tiêu >= 0

        int pivotRow = findPivotRow(pivotCol);
        if (pivotRow == -1) { // Không tìm thấy hàng xoay -> Bài toán không giới nội (Unbounded)
            statusMsg = "Bài toán không giới nội";
            if (!history.empty()) {
                history.back().isUnbounded = true;
            }
            return false;
        }

        // Cập nhật lịch sử để giao diện (UI) vẽ ô tô sáng
        if (!history.empty()) {
            history.back().pivotRow  = pivotRow;
            history.back().pivotCol  = pivotCol;
        }
        performPivot(pivotRow, pivotCol);
    }
}

// Thuật toán Đơn hình 2 pha (Giải quyết các bài toán có điểm xuất phát không khả thi / b < 0)
bool SimplexSolver::solveTwoPhase() {
    int m = lp.A.size();
    int n_slack = lp.c.size();

    buildTableau();
    if (!history.empty()) history.pop_back();

    int cols = tableau[0].size() - 1;

    // Chèn thêm 1 biến giả duy nhất (a_0) để xử lý mọi b < 0
    for (int i = 0; i <= m; ++i) {
        tableau[i].insert(tableau[i].begin() + cols, 0.0);
    }
    int a_col = cols;
    cols++; // Cập nhật lại số lượng cột sau khi chèn

    // Gán hệ số -1 cho biến giả ở các phương trình ràng buộc
    for (int i = 0; i < m; ++i) {
        tableau[i][a_col] = -1.0;
    }

    // Thiết lập hàm mục tiêu Pha 1: Minimize a_0 (Bằng cách đẩy hàm mục tiêu = a_0)
    for (int j = 0; j <= cols; ++j) tableau[m][j] = 0.0;
    tableau[m][a_col] = 1.0;

    recordStep("Từ vựng 1 (Khởi tạo Pha 1)");

    // Xoay đặc biệt (Special Pivot): Đưa biến giả vào cơ sở ở hàng có b âm nhất
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

    // Chạy Đơn hình cho Pha 1
    if (!this->runSimplexLoop()) return false;

    // Kết thúc Pha 1: Nếu giá trị tối ưu của biến giả > 0 -> Bài toán vô nghiệm (Infeasible)
    if (std::abs(tableau[m][cols]) > CLEAN_EPS) {
        statusMsg = "Vô nghiệm (Min ε > 0)";
        if (!history.empty()) {
            history.back().isInfeasible = true;
        }
        return false;
    }

    // ----------------------------------------------------
    // KHỞI TẠO PHA 2: Khôi phục lại hàm mục tiêu ban đầu
    // ----------------------------------------------------
    iterationCount = 0;

    for (int j = 0; j < n_slack; ++j) {
        tableau[m][j] = lp.c[j];
    }
    // Phạt biến giả (loại nó khỏi quá trình tối ưu)
    tableau[m][a_col] = 1e9;
    tableau[m][cols] = lp.c_0;

    // Biến đổi hàm mục tiêu mới để tương thích với các biến cơ sở hiện tại
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

    // Chạy Đơn hình cho Pha 2
    bool success = this->runSimplexLoop();

    // Xử lý logic tương tự như solve() ở trên (Kiểm tra vô số nghiệm vs nghiệm ảo)
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

// Lưu lại trạng thái của mỗi bước lặp (để phục vụ tính năng hiển thị từng bước cho người dùng)
void SimplexSolver::recordStep(const QString& name) {
    SimplexStep step;
    step.stepName         = name;
    step.matrix           = this->tableau;
    step.currentBasicVars = this->basicVariables;
    step.solution         = this->getSolution();
    history.push_back(step);
}

// Kiểm tra xem bài toán có Vô số nghiệm hay không
bool SimplexSolver::checkAlternativeOptima() {
    int m = tableau.size() - 1;
    int n = tableau[0].size() - 1;

    // Bỏ qua các cột thuộc về các biến nội bộ (được sinh ra do tách biến tự do)
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

    // Nếu tồn tại một biến phi cơ sở (non-basic) có hệ số mục tiêu = 0 -> Có thể đổi cơ sở mà không làm giảm Z
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

// Tìm điểm tối ưu thứ 2 (để tạo ra đoạn thẳng chứa vô số nghiệm)
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

    // Sao lưu lại toàn bộ trạng thái để có thể rollback khi tìm thấy nghiệm ảo (chống suy biến)
    auto backupTableau = this->tableau;
    auto backupBasicVars = this->basicVariables;
    auto backupHistory = this->history;

    bool foundDifferent = false;

    // Vòng lặp quét tất cả các cột có khả năng sinh ra vô số nghiệm
    for (int j = 0; j < n; ++j) {
        if (ignoredCols.count(j)) continue;

        bool isBasic = false;
        for (int i = 0; i < m; ++i) {
            if (basicVariables[i] == j) { isBasic = true; break; }
        }

        // Nếu phát hiện biến phi cơ sở có hệ số z = 0
        if (!isBasic && std::abs(tableau[m][j]) <= CLEAN_EPS) {
            int altPivotRow = findPivotRow(j); // Tìm hàng để đưa biến này vào
            if (altPivotRow != -1) {
                if (!history.empty()) {
                    history.back().pivotRow  = altPivotRow;
                    history.back().pivotCol  = j;
                }

                // Thực hiện Pivot (Xoay) để sinh ra nghiệm mới
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

    // Nếu đã quét sạch mọi cột mà không tìm ra điểm nào khác -> Không có điểm thứ 2 thực sự
    if (!foundDifferent) {
        this->altSolution = this->firstSolution;
    }
}
