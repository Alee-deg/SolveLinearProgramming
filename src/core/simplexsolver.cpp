#include "simplexsolver.h"
#include <set>
#include <cmath>

/*
================================================================================
 TỔNG QUAN FILE SIMPLEXSOLVER.CPP
--------------------------------------------------------------------------------
 File này cài đặt lớp SimplexSolver, chịu trách nhiệm giải bài toán Quy hoạch
 tuyến tính bằng thuật toán Đơn hình.

 Luồng xử lý chính:
  1. Nhận bài toán gốc LinearProgram từ giao diện.
  2. Chuyển bài toán về dạng chuẩn:
     - Max Z được đổi thành Min(-Z).
     - Ràng buộc >= được nhân -1 để chuyển thành <=.
     - Biến tự do được tách thành hiệu của hai biến không âm.
     - Biến <= 0 được đổi dấu để trở thành biến >= 0.
     - Thêm biến slack cho ràng buộc <=.
     - Với ràng buộc =, cố gắng chọn biến hiện có làm biến cơ sở.
  3. Xây dựng tableau đơn hình ban đầu.
  4. Nếu cần, chạy Pha 1 với duy nhất một biến giả x0.
  5. Chạy vòng lặp Simplex:
     - Chọn cột xoay.
     - Chọn hàng xoay.
     - Thực hiện pivot.
     - Lưu lại từng bước vào history để hiển thị ở giao diện.
  6. Sau khi tối ưu, kiểm tra vô số nghiệm và tìm điểm tối ưu thứ hai nếu có.

 Quy ước quan trọng:
  - CLEAN_EPS dùng để so sánh số thực thay vì so sánh trực tiếp với 0.
  - tableau[m][n] là hằng số tự do của hàng mục tiêu.
  - basicVariables[i] lưu chỉ số biến cơ sở của dòng ràng buộc i.
  - history lưu toàn bộ các bước từ vựng/tableau để phục vụ giao diện và PDF.
  - Với bài toán Max, thuật toán giải qua Min(-Z), nên một số chỗ phải đảo dấu
    khi trả kết quả về cho người dùng.

 Lưu ý bảo trì:
  - File này là phần lõi thuật toán, không xử lý giao diện trực tiếp.
  - Không nên thay đổi dấu của tableau nếu chưa kiểm tra lại getOptimalZ(),
    getSolution(), runSimplexLoop() và phần xuất báo cáo.
================================================================================
*/


// Hằng số dùng để so sánh số thực, tránh các lỗi sai số dấu phẩy động (floating-point precision).
// Không nên dùng so sánh trực tiếp kiểu value == 0.0 trong thuật toán Simplex,
// vì các phép chia/pivot có thể tạo ra sai số rất nhỏ như 1e-15.
// CLEAN_EPS được dùng để:
// - Xem một số rất nhỏ là 0.
// - Kiểm tra hệ số âm/dương trong chọn cột xoay và hàng xoay.
// - Tránh pivot trên phần tử gần 0 gây mất ổn định số học.
static const double CLEAN_EPS = 1e-10;

// Constructor: Khởi tạo bộ giải với bài toán quy hoạch tuyến tính đầu vào.
// inputLP là bài toán người dùng nhập ở giao diện, gồm:
// - c: hệ số hàm mục tiêu.
// - c_0: hằng số tự do của hàm mục tiêu.
// - A, b, signs: hệ ràng buộc.
// - varBounds: điều kiện dấu của biến.
// - isMaximize: true nếu bài toán gốc là Max Z.
// - algoType: phương pháp giải người dùng chọn.
SimplexSolver::SimplexSolver(const LinearProgram& inputLP){
    this->lp = inputLP;
    this->statusMsg = "Chưa giải";
}

// Lấy giá trị hàm mục tiêu tối ưu để trả về cho giao diện.
// Chú ý:
// - Bên trong thuật toán, bài toán Max được chuyển thành Min(-Z).
// - Vì vậy khi bài toán gốc là Max, cần đảo dấu/hệ quy chiếu để hiển thị lại Z gốc.
// - Nếu tableau chưa được tạo, trả 0.0 để tránh truy cập mảng rỗng.
double SimplexSolver::getOptimalZ() const{
    if(this->tableau.empty()) return 0.0;
    int m = this->tableau.size() - 1;
    int n = this->tableau[0].size() - 1;
    double z = this->tableau[m][n];
    // Nếu bài toán gốc là Maximize, giá trị Z trong bảng đang bị đảo dấu (do chuyển sang Min -Z), cần đảo lại
    return this->lp.isMaximize ? z : -z;
}

// Trích xuất nghiệm tối ưu cho các biến gốc của bài toán.
// Đây là bước ánh xạ ngược từ biến nội bộ của dạng chuẩn về biến người dùng nhập.
//
// Ví dụ:
// - Nếu x_j là biến bình thường >= 0:
//      x_j = giá trị biến nội bộ tương ứng.
// - Nếu x_j là biến tự do:
//      x_j được tách thành x_j^+ - x_j^-,
//      nên nghiệm gốc là x_j = value(x_j^+) - value(x_j^-).
// - Nếu x_j có điều kiện x_j <= 0:
//      thuật toán đổi biến x_j = -u_j với u_j >= 0,
//      nên nghiệm gốc là x_j = -value(u_j).
//
// Hàm này chỉ đọc tableau hiện tại, không làm thay đổi trạng thái solver.
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

// Trả về nghiệm tối ưu thứ nhất.
// Biến này được lưu sau khi thuật toán tìm được nghiệm tối ưu đầu tiên.
// Nếu bài toán có vô số nghiệm, firstSolution là một đầu mút/điểm tối ưu đầu tiên.
std::vector<double> SimplexSolver::getFirstSolution() const {
    return firstSolution;
}

// Trả về nghiệm tối ưu thứ hai nếu phát hiện bài toán có vô số nghiệm.
// Nếu không có nghiệm thứ hai thực sự khác, altSolution sẽ bằng firstSolution.
std::vector<double> SimplexSolver::getAltSolution() const {
    return altSolution;
}

// Trả về trạng thái cuối cùng của thuật toán để giao diện hiển thị.
// Ví dụ: "Tối ưu", "Vô số nghiệm", "Bài toán không giới nội", "Vô nghiệm".
QString SimplexSolver::getStatus() const { return this->statusMsg; }

// Chuyển đổi bài toán về dạng chuẩn (Standard Form) để áp dụng thuật toán Đơn hình.
//
// Mục tiêu của dạng chuẩn trong file này:
// - Toàn bộ bài toán được xử lý theo hướng Min.
// - Ràng buộc chính được đưa về dạng phương trình.
// - Các biến nội bộ đều không âm.
// - Có một cơ sở ban đầu nếu có thể.
//
// Các bước chính:
// 1. Lưu lại số biến gốc và điều kiện dấu gốc để cuối cùng ánh xạ nghiệm trở lại.
// 2. Nếu bài toán gốc là Max Z, đổi thành Min(-Z).
// 3. Đổi ràng buộc >= thành <= bằng cách nhân cả hai vế với -1.
// 4. Xử lý biến tự do và biến <= 0.
// 5. Thêm biến slack hoặc chọn cơ sở cho các ràng buộc.
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

// Hàm dự phòng cho xử lý vế phải âm.
// Hiện tại logic vế phải âm được xử lý trong solve():
// - Nếu tồn tại b_i âm, solver sẽ yêu cầu chạy Pha 1.
// - Với Pha 1, code sử dụng duy nhất một biến giả x0.
// Hàm này được giữ lại để không phá vỡ interface/header cũ.
void SimplexSolver::handleNegativeB() { }

// Xử lý các điều kiện của biến để đưa tất cả biến nội bộ về dạng >= 0.
//
// Trường hợp 1: Biến tự do.
//   x_j không bị ràng buộc dấu, có thể âm/dương.
//   Đổi thành:
//      x_j = x_j^+ - x_j^-
//      x_j^+ >= 0, x_j^- >= 0
//   Vì vậy cần chèn thêm một cột mới vào A và một hệ số mới vào c.
//
// Trường hợp 2: Biến có điều kiện x_j <= 0.
//   Đổi biến:
//      x_j = -u_j, u_j >= 0
//   Khi đó toàn bộ cột hệ số của x_j và hệ số mục tiêu tương ứng phải đổi dấu.
//
// Vòng lặp chạy từ cuối về đầu để khi insert cột mới không làm lệch chỉ số
// của các biến chưa xử lý phía trước.
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
// Tự động nhận diện ma trận cơ sở và thêm biến bù slack khi cần.
// -----------------------------------------------------------------------
//
// Hàm này thực hiện phần rất quan trọng: tạo cơ sở ban đầu cho tableau.
//
// Quy tắc trong bản hiện tại:
// - Ràng buộc "<=":
//     Thêm biến slack w_i với hệ số +1 ở dòng tương ứng.
//     Biến slack này trở thành biến cơ sở ban đầu của dòng đó.
//
// - Ràng buộc "=":
//     KHÔNG thêm slack riêng, vì thêm slack cho dấu "=" sẽ làm thay đổi
//     bản chất bài toán.
//     Thay vào đó, solver cố gắng:
//       1. Tìm một cột đơn vị sẵn có trong A.
//       2. Nếu chưa có, chọn một biến hiện có và dùng khử Gauss-Jordan
//          để biến cột đó thành cột đơn vị.
//
// Lưu ý:
// - Đây không phải Pha 1.
// - Biến giả duy nhất x0 chỉ được tạo trong solveTwoPhase() khi cần.
// - basicVariables[i] lưu chỉ số biến cơ sở của dòng i.
// -----------------------------------------------------------------------
void SimplexSolver::addSlackAndSurplusVariables() {
    int m = (int)lp.signs.size();
    this->basicVariables.assign(m, -1);

    // ===================================================================
    // KHÔNG thêm slack/biến giả riêng cho ràng buộc "=".
    // - Ràng buộc <=: thêm slack +1 như bình thường để có biến cơ sở.
    // - Ràng buộc = : giữ đúng bản chất phương trình, sau đó chọn một
    //   biến hiện có làm biến cơ sở bằng phép khử Gauss-Jordan.
    //
    // Lý do: nếu thêm cột +1 cho ràng buộc "=", bài toán bị nới lỏng
    // thành dạng <= và có thể cho nghiệm sai.
    // ===================================================================

    // 1) Thêm slack chỉ cho các ràng buộc <=.
    for (int i = 0; i < m; ++i) {
        QString s = lp.signs[i].trimmed();

        if (s == "<=") {
            for (int row = 0; row < m; ++row)
                lp.A[row].push_back((row == i) ? 1.0 : 0.0);

            lp.c.push_back(0.0);
            basicVariables[i] = (int)lp.c.size() - 1;
            lp.signs[i] = "=";
        } else if (s == "=" || s == "==") {
            lp.signs[i] = "=";
        }
    }

    // 2) Nhận diện sẵn các cột đơn vị có thể làm cơ sở cho hàng "=".
    std::set<int> usedBasicCols;
    for (int i = 0; i < m; ++i) {
        if (basicVariables[i] >= 0)
            usedBasicCols.insert(basicVariables[i]);
    }

    for (int i = 0; i < m; ++i) {
        if (basicVariables[i] != -1) continue;

        for (int j = 0; j < (int)lp.c.size(); ++j) {
            if (usedBasicCols.count(j)) continue;
            if (std::abs(lp.A[i][j] - 1.0) > CLEAN_EPS) continue;

            bool isIdentity = true;
            for (int r = 0; r < m; ++r) {
                if (r != i && std::abs(lp.A[r][j]) > CLEAN_EPS) {
                    isIdentity = false;
                    break;
                }
            }

            if (isIdentity) {
                basicVariables[i] = j;
                usedBasicCols.insert(j);
                break;
            }
        }
    }

    // 3) Với các hàng "=" chưa có biến cơ sở, chọn một biến hiện có
    //    rồi khử để tạo cột đơn vị. Đây KHÔNG phải biến giả pha 1.
    for (int i = 0; i < m; ++i) {
        if (basicVariables[i] != -1) continue;

        int pivotCol = -1;
        double bestAbs = 0.0;

        for (int j = 0; j < (int)lp.c.size(); ++j) {
            if (usedBasicCols.count(j)) continue;
            double v = std::abs(lp.A[i][j]);
            if (v > bestAbs + CLEAN_EPS) {
                bestAbs = v;
                pivotCol = j;
            }
        }

        if (pivotCol == -1 || bestAbs <= CLEAN_EPS) {
            statusMsg = "Không tạo được cơ sở ban đầu cho ràng buộc bằng (=).";
            continue;
        }

        double pivot = lp.A[i][pivotCol];
        for (int j = 0; j < (int)lp.c.size(); ++j) {
            lp.A[i][j] /= pivot;
            if (std::abs(lp.A[i][j]) < CLEAN_EPS) lp.A[i][j] = 0.0;
        }
        lp.b[i] /= pivot;
        if (std::abs(lp.b[i]) < CLEAN_EPS) lp.b[i] = 0.0;

        for (int r = 0; r < m; ++r) {
            if (r == i) continue;
            double factor = lp.A[r][pivotCol];
            if (std::abs(factor) <= CLEAN_EPS) continue;

            for (int j = 0; j < (int)lp.c.size(); ++j) {
                lp.A[r][j] -= factor * lp.A[i][j];
                if (std::abs(lp.A[r][j]) < CLEAN_EPS) lp.A[r][j] = 0.0;
            }
            lp.b[r] -= factor * lp.b[i];
            if (std::abs(lp.b[r]) < CLEAN_EPS) lp.b[r] = 0.0;
        }

        basicVariables[i] = pivotCol;
        usedBasicCols.insert(pivotCol);
    }
}


// Hàm chính điều phối toàn bộ quá trình giải bài toán.
//
// Luồng xử lý:
// 1. Chuyển bài toán về dạng chuẩn.
// 2. Nếu không tạo được cơ sở ban đầu thì dừng.
// 3. Kiểm tra vế phải b_i có âm hay không.
//    - Nếu có b_i âm, cần Pha 1 để tìm điểm xuất phát khả thi.
//    - Nếu người dùng chọn thuật toán không hỗ trợ Pha 1, báo lỗi gợi ý.
// 4. Nếu không cần Pha 1, xây tableau và chạy Simplex bình thường.
// 5. Sau khi tối ưu, kiểm tra vô số nghiệm.
// 6. Lưu nghiệm đầu tiên và nghiệm thứ hai nếu có.
//
// Giá trị trả về:
// - true: solver kết thúc hợp lệ.
// - false: vô nghiệm, không giới nội, không tạo được cơ sở hoặc lỗi thuật toán.
bool SimplexSolver::solve() {
    convertToStandardForm();

    if (statusMsg.contains("Không tạo được cơ sở", Qt::CaseInsensitive)) {
        return false;
    }

    // Kiểm tra xem có hệ số b nào âm không. Nếu có, cần pha 1.
    bool needsPhase1 = false;
    for (double b_val : lp.b) {
        if (b_val < -CLEAN_EPS) { needsPhase1 = true; break; }
    }

    // Nếu người dùng chọn 2 pha thì vẫn cho chạy 2 pha.
    // Pha 1 ở đây chỉ dùng MỘT biến giả duy nhất x0/a0.
    if (needsPhase1 || lp.algoType == 2) {
        if (needsPhase1 && (lp.algoType == 0 || lp.algoType == 1)) {
            statusMsg = "Không giải được với thuật toán Đơn hình! "
                        "(Tồn tại hệ số b_i âm ở dạng chuẩn, bạn có thể sử dụng đơn hình 2 pha hoặc chế độ tự động).";
            return false;
        }
        return solveTwoPhase();
    }

    buildTableau();
    bool success = runSimplexLoop();
    if (!success) return false;

    // Xử lý sau khi tìm được một nghiệm tối ưu (kiểm tra có vô số nghiệm không)
    if (checkAlternativeOptima()) {
        this->firstSolution = this->getSolution();
        findAndRecordAlternativeOptimum();

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


// Xây dựng bảng đơn hình ban đầu từ bài toán đã ở dạng chuẩn.
//
// Cấu trúc tableau:
// - Các dòng 0..m-1: hệ ràng buộc.
// - Dòng m: hàm mục tiêu.
// - Các cột 0..n-1: hệ số các biến.
// - Cột n: vế phải / hằng số tự do.
//
// Sau khi điền dữ liệu, cần canonical hóa hàng mục tiêu theo cơ sở hiện tại:
// Nếu biến cơ sở có hệ số khác 0 trong hàng mục tiêu, ta khử nó về 0.
// Điều này đảm bảo từ vựng ban đầu đúng dạng:
//      biến cơ sở = hằng số + tổ hợp biến không cơ sở
//      Z hoặc -Z = hằng số + tổ hợp biến không cơ sở
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
    tableau[m][n] = lp.c_0;

    // ===================================================================
    // Canonical hóa hàng mục tiêu theo cơ sở hiện tại.
    // Trước đây code chỉ đúng khi cơ sở là slack có c_j = 0. Với ràng buộc
    // "=", ta có thể chọn biến gốc/biến tách làm biến cơ sở, c_j có thể khác 0.
    // Nếu không khử cột cơ sở khỏi hàng mục tiêu, simplex sẽ giải sai.
    // ===================================================================
    for (int i = 0; i < m; ++i) {
        int b_col = basicVariables[i];
        if (b_col >= 0 && b_col < n) {
            double factor = tableau[m][b_col];
            if (std::abs(factor) > CLEAN_EPS) {
                for (int j = 0; j <= n; ++j) {
                    tableau[m][j] -= factor * tableau[i][j];
                    if (std::abs(tableau[m][j]) < CLEAN_EPS) tableau[m][j] = 0.0;
                }
            }
        }
    }

    iterationCount = 0;
    QString stepTitle = lp.isMaximize ? "Từ vựng 1 (Biến đổi Max Z → Min -Z)" : "Từ vựng 1 (Khởi tạo)";
    recordStep(stepTitle);
}


// Tìm cột xoay, tức biến không cơ sở sẽ đi vào cơ sở.
//
// Vì solver đang làm việc với bài toán Min:
// - Nếu còn hệ số âm ở hàng mục tiêu, tăng biến đó sẽ làm giảm giá trị mục tiêu.
// - Do đó, cột xoay là cột có hệ số âm.
//
// Hai quy tắc chọn:
// - Bland: chọn cột âm đầu tiên, giúp hạn chế xoay vòng.
// - Dantzig: chọn cột có hệ số âm nhất, thường hội tụ nhanh hơn.
//
// Nếu không còn hệ số âm, bài toán đã đạt tối ưu.
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

// Tìm hàng xoay, tức biến cơ sở sẽ rời khỏi cơ sở.
//
// Sử dụng kiểm tra tỷ số nhỏ nhất:
//      ratio_i = b_i / a_ij
// chỉ xét các dòng có a_ij > 0.
//
// Ý nghĩa:
// - Khi tăng biến vào, các biến cơ sở thay đổi.
// - Dòng nào đạt 0 trước sẽ là dòng giới hạn, biến cơ sở ở dòng đó phải rời cơ sở.
// - Nếu không có a_ij > 0 cho cột xoay, biến vào có thể tăng vô hạn,
//   nên bài toán không giới nội.
//
// Nếu dùng Bland và có tỷ số hòa, ưu tiên biến cơ sở có chỉ số nhỏ hơn để chống cycling.
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

// Thực hiện phép biến đổi Gauss-Jordan tại phần tử pivot.
//
// Mục tiêu:
// - Biến cột pivotCol trở thành biến cơ sở mới ở dòng pivotRow.
// - Cột pivotCol được biến thành cột đơn vị:
//      phần tử pivot = 1,
//      các phần tử còn lại trong cột = 0.
//
// Các bước:
// 1. Chia toàn bộ hàng pivot cho phần tử pivot.
// 2. Dùng hàng pivot để khử cột pivot ở tất cả dòng khác.
// 3. Cập nhật basicVariables.
// 4. Tăng iterationCount.
// 5. Ghi lại bước mới vào history.
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

// Vòng lặp chính của thuật toán Đơn hình.
//
// Tham số isPhaseOne:
// - Được dùng để đánh dấu vòng lặp đang chạy cho Pha 1 hay Pha 2.
// - Hiện tại trong thân hàm chưa cần phân nhánh theo isPhaseOne,
//   nhưng vẫn giữ tham số để thuận tiện mở rộng và không phá interface.
//
// Mỗi vòng lặp:
// 1. Kiểm tra giới hạn số vòng để tránh cycling vô hạn.
// 2. Tìm cột xoay.
// 3. Nếu không có cột xoay, đã tối ưu.
// 4. Tìm hàng xoay.
// 5. Nếu không có hàng xoay, bài toán không giới nội.
// 6. Lưu pivot vào history để giao diện tô sáng.
// 7. Thực hiện pivot.
bool SimplexSolver::runSimplexLoop(bool isPhaseOne) {
    const int MAX_ITERATIONS = 500;
    while (true) {
        // Tránh bị lặp vô hạn (lặp vòng - Cycling)
        if (iterationCount > MAX_ITERATIONS) {
            statusMsg = "Hiện tượng xoay vòng (Cycling)!\nThuật toán bị lặp vô hạn.";
            return false;
        }

        int pivotCol = findPivotColumn();
        // Nếu không còn cột có hệ số âm ở hàng mục tiêu,
        // điều kiện tối ưu của bài toán Min đã thỏa mãn.
        if (pivotCol == -1) return true; // Dấu hiệu dừng: Tất cả hệ số dòng mục tiêu >= 0

        int pivotRow = findPivotRow(pivotCol);
        // Nếu có cột xoay nhưng không có hàng xoay hợp lệ,
        // biến vào có thể tăng vô hạn mà không làm biến cơ sở nào âm.
        // Do đó hàm mục tiêu cải thiện vô hạn => bài toán không giới nội.
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

// Thuật toán Đơn hình 2 pha.
//
// Mục đích:
// - Xử lý bài toán chưa có điểm xuất phát khả thi rõ ràng,
//   đặc biệt khi có vế phải b_i âm sau khi chuyển dạng chuẩn.
//
// Điểm đặc biệt của bản cài đặt này:
// - Pha 1 chỉ dùng MỘT biến giả duy nhất x0.
// - Không tạo a1, a2, ... cho từng ràng buộc.
// - x0 được thêm vào tất cả phương trình với hệ số -1.
// - Nếu có dòng b_i âm, pivot x0 vào dòng có b_i âm nhất.
// - Sau đó tối thiểu hóa x0.
// - Nếu Min x0 > 0, hệ gốc vô nghiệm.
// - Nếu Min x0 = 0, bài toán có nghiệm khả thi và chuyển sang Pha 2.
//
// Pha 2:
// - Khôi phục lại hàm mục tiêu gốc.
// - Không cho x0 quay lại cơ sở bằng cách đặt hệ số rất lớn 1e9.
// - Canonical hóa hàng mục tiêu theo cơ sở hiện tại.
// - Chạy Simplex bình thường.
bool SimplexSolver::solveTwoPhase() {
    int m = lp.A.size();
    int n_slack = lp.c.size();

    buildTableau();
    if (!history.empty()) history.pop_back();

    int cols = tableau[0].size() - 1;

    // ===================================================================
    // [FIX] PHA 1 CHỈ DÙNG 1 BIẾN GIẢ DUY NHẤT x0/a0.
    // Không tạo a1, a2, ... cho từng ràng buộc.
    // Cách làm: thêm x0 với hệ số -1 vào tất cả phương trình, nếu có b_i âm
    // thì pivot x0 vào hàng có b_i âm nhất, rồi tối thiểu hóa x0.
    // ===================================================================
    for (int i = 0; i <= m; ++i) {
        tableau[i].insert(tableau[i].begin() + cols, 0.0);
    }
    int a_col = cols;
    cols++;

    for (int i = 0; i < m; ++i) {
        tableau[i][a_col] = -1.0;
    }

    for (int j = 0; j <= cols; ++j) tableau[m][j] = 0.0;
    tableau[m][a_col] = 1.0;   // Min x0

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

    if (!this->runSimplexLoop(true)) return false;

    // Nếu Min x0 > 0 thì bài toán gốc vô nghiệm.
    // Với quy ước getOptimalZ ở pha 1, hằng số hàng mục tiêu có thể mang dấu âm
    // sau các phép biến đổi, nên dùng trị tuyệt đối giá trị x0 cơ sở/giá trị mục tiêu.
    double auxValue = 0.0;
    // Sau Pha 1, nếu x0 vẫn còn trong cơ sở với giá trị 0,
    // ta cố gắng pivot x0 ra khỏi cơ sở để Pha 2 không phụ thuộc vào biến giả.
    for (int i = 0; i < m; ++i) {
        if (basicVariables[i] == a_col) {
            auxValue = tableau[i][cols];
            break;
        }
    }
    if (std::abs(tableau[m][cols]) > std::abs(auxValue)) auxValue = tableau[m][cols];

    if (std::abs(auxValue) > CLEAN_EPS) {
        statusMsg = "Vô nghiệm (Min x0 > 0)";
        if (!history.empty()) {
            history.back().isInfeasible = true;
        }
        return false;
    }

    // Nếu x0 còn trong cơ sở với giá trị 0, cố gắng pivot nó ra khỏi cơ sở.
    for (int i = 0; i < m; ++i) {
        if (basicVariables[i] == a_col) {
            int replacementCol = -1;
            for (int j = 0; j < cols; ++j) {
                if (j == a_col) continue;
                if (std::abs(tableau[i][j]) > CLEAN_EPS) {
                    replacementCol = j;
                    break;
                }
            }
            if (replacementCol != -1) {
                performPivot(i, replacementCol);
            }
        }
    }

    // KHỞI TẠO PHA 2: Khôi phục hàm mục tiêu gốc.
    iterationCount = 0;

    for (int j = 0; j <= cols; ++j) tableau[m][j] = 0.0;
    for (int j = 0; j < n_slack; ++j) {
        tableau[m][j] = lp.c[j];
    }
    tableau[m][a_col] = 1e9; // Không cho x0 quay lại cơ sở
    tableau[m][cols] = lp.c_0;

    // Canonical hóa hàng mục tiêu pha 2 theo cơ sở hiện tại.
    for (int i = 0; i < m; ++i) {
        int b_col = basicVariables[i];
        if (b_col >= 0 && b_col <= cols) {
            double factor = tableau[m][b_col];
            if (std::abs(factor) > CLEAN_EPS) {
                for (int j = 0; j <= cols; ++j) {
                    tableau[m][j] -= factor * tableau[i][j];
                    if (std::abs(tableau[m][j]) < CLEAN_EPS) tableau[m][j] = 0.0;
                }
            }
        }
    }

    this->recordStep("Từ vựng 1 (Khởi tạo Pha 2)");

    bool success = this->runSimplexLoop(false);

    if (success) {
        if (this->checkAlternativeOptima()) {
            this->firstSolution = this->getSolution();
            findAndRecordAlternativeOptimum();

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


// Lưu lại trạng thái của mỗi bước lặp.
//
// history được dùng cho:
// - Bảng các bước thực thi.
// - Dạng từ vựng [w, x, z].
// - Xuất báo cáo PDF/.tex.
// - Chatbot giải thích từng bước.
// - Tô màu biến vào/biến ra trên giao diện.
//
// Mỗi SimplexStep lưu:
// - tên bước,
// - ma trận tableau tại thời điểm đó,
// - danh sách biến cơ sở,
// - nghiệm hiện tại của biến gốc.
void SimplexSolver::recordStep(const QString& name) {
    SimplexStep step;
    step.stepName         = name;
    step.matrix           = this->tableau;
    step.currentBasicVars = this->basicVariables;
    step.solution         = this->getSolution();
    history.push_back(step);
}

// Kiểm tra bài toán có vô số nghiệm tối ưu hay không.
//
// Tiêu chuẩn cơ bản trong Simplex:
// - Sau khi đạt tối ưu, nếu tồn tại biến không cơ sở có hệ số mục tiêu bằng 0,
//   thì có thể đưa biến đó vào cơ sở mà không làm thay đổi giá trị tối ưu.
// - Khi đó bài toán có khả năng có nhiều nghiệm tối ưu.
//
// Lưu ý quan trọng:
// - Với biến tự do được tách thành x_i^+ và x_i^-,
//   không dùng riêng hai nửa biến này để kết luận vô số nghiệm,
//   vì chúng chỉ là biểu diễn nội bộ.
// - Do đó các cột sinh ra từ biến tự do được đưa vào ignoredCols.
bool SimplexSolver::checkAlternativeOptima() {
    int m = tableau.size() - 1;
    int n = tableau[0].size() - 1;

    // ===================================================================
    // [FIX VÔ SỐ NGHIỆM VỚI BIẾN TỰ DO]
    //
    // Không được bỏ qua các cột sinh ra từ biến tự do
    // x_j = x_j^+ - x_j^-.
    //
    // Với bài toán:
    //     Max Z = 2x1 - 4x2
    //     x1 - 2x2 <= 5
    //    -x1 + 2x2 <= 5
    //     x1, x2 tự do
    //
    // tập nghiệm tối ưu là cả đường thẳng x1 - 2x2 = 5.
    // Hướng tối ưu xuất hiện ở chính các cột x_j^+, x_j^- có reduced cost = 0.
    // Nếu bỏ qua các cột này, solver sẽ kết luận sai là "nghiệm tối ưu duy nhất".
    //
    // Vì vậy ta quét TẤT CẢ biến phi cơ sở có reduced cost = 0.
    // Cột biến giả x0 ở Pha 2 nếu bị khóa bằng Big-M sẽ được bỏ qua.
    // ===================================================================
    for (int j = 0; j < n; ++j) {
        if (std::abs(tableau[m][j]) >= 1e8) continue; // bỏ qua biến giả x0 bị khóa Big-M

        bool isBasic = false;
        for (int i = 0; i < m; ++i) {
            if (basicVariables[i] == j) {
                isBasic = true;
                break;
            }
        }

        if (!isBasic && std::abs(tableau[m][j]) <= CLEAN_EPS) {
            return true;
        }
    }

    return false;
}

// Tìm điểm tối ưu thứ hai nếu bài toán có vô số nghiệm.
//
// Ý tưởng:
// - Quét các biến không cơ sở có hệ số mục tiêu bằng 0.
// - Thử pivot biến đó vào cơ sở.
// - Nếu nghiệm mới khác nghiệm đầu tiên, lưu lại làm altSolution.
// - Sau đó khôi phục tableau về trạng thái tối ưu ban đầu để không làm thay đổi
//   luồng chính của solver.
//
// Vì sao cần backup/rollback:
// - Một số pivot có thể chỉ tạo nghiệm suy biến, tức nghiệm không đổi.
// - Nếu giữ pivot đó, lịch sử và tableau chính có thể bị sai lệch.
// - Do đó mỗi lần thử phải có khả năng hoàn tác.
void SimplexSolver::findAndRecordAlternativeOptimum() {
    int m = tableau.size() - 1;
    int n = tableau[0].size() - 1;

    // Sao lưu lại toàn bộ trạng thái để có thể rollback khi tìm thấy nghiệm ảo (chống suy biến)
    auto backupTableau = this->tableau;
    auto backupBasicVars = this->basicVariables;
    auto backupHistory = this->history;

    bool foundDifferent = false;

    // ===================================================================
    // [FIX VÔ SỐ NGHIỆM VỚI BIẾN TỰ DO]
    //
    // Không bỏ qua x_j^+, x_j^- nữa. Với biến tự do, hướng tối ưu có thể
    // nằm ở chính các cột này. Sau khi pivot thử, getSolution() sẽ ánh xạ
    // nghiệm nội bộ về nghiệm gốc để kiểm tra nghiệm có thật sự khác không.
    // ===================================================================
    for (int j = 0; j < n; ++j) {
        if (std::abs(tableau[m][j]) >= 1e8) continue; // bỏ qua biến giả x0 bị khóa Big-M

        bool isBasic = false;
        for (int i = 0; i < m; ++i) {
            if (basicVariables[i] == j) {
                isBasic = true;
                break;
            }
        }

        // Nếu phát hiện biến phi cơ sở có reduced cost = 0
        if (!isBasic && std::abs(tableau[m][j]) <= CLEAN_EPS) {
            int altPivotRow = findPivotRow(j);

            // Nếu cột này không có hàng xoay hợp lệ, không pivot vào cột đó.
            // Tiếp tục thử các cột reduced cost = 0 khác.
            if (altPivotRow == -1) {
                continue;
            }

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
                // Nếu xoay xong mà nghiệm vẫn giữ nguyên, rollback lại và thử cột tiếp theo
                this->tableau = backupTableau;
                this->basicVariables = backupBasicVars;
                this->history = backupHistory;
            }
        }
    }

    // Nếu đã quét sạch mọi cột mà không tìm ra điểm nào khác -> Không có điểm thứ 2 thực sự
    if (!foundDifferent) {
        this->altSolution = this->firstSolution;
    }
}
