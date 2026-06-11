# Phần mềm Giải Quy Hoạch Tuyến Tính

## 1. Giới thiệu phần mềm

**SolveLinearProgramming / PhanMemQHTT** là phần mềm hỗ trợ nhập và giải các bài toán quy hoạch tuyến tính bằng phương pháp đơn hình, bland và đơn hình hai pha.

Phần mềm được xây dựng bằng **C++ / Qt / CMake**, có giao diện trực quan và hỗ trợ các chức năng chính như:

- Nhập hàm mục tiêu và hệ ràng buộc.
- Chọn dạng bài toán Max hoặc Min.
- Giải bài toán quy hoạch tuyến tính.
- Hiển thị nghiệm tối ưu và giá trị tối ưu.
- Hiển thị các bước giải theo dạng bảng và dạng từ vựng.
- Xuất lời giải ra file PDF hoặc `.tex`.
- Hỗ trợ chatbot để giải thích bài toán và các bước giải.

---

## 2. Cấu trúc thư mục

```text
SolveLinearProgramming/
│
├── .github/
│   └── workflows/
│       └── File YAML dùng để tự động build và đóng gói phần mềm trên GitHub Actions.
│
├── CMakeLists.txt
│   File cấu hình build chính của dự án bằng CMake.
│
├── README.md
│   File giới thiệu dự án và mô tả cấu trúc mã nguồn.
│
├── src/
│   ├── main.cpp
│   │   File khởi động chương trình.
│   │
│   ├── core/
│   │   ├── Struct.h
│   │   │   Chứa các cấu trúc dữ liệu dùng chung trong chương trình.
│   │   │
│   │   ├── simplexsolver.h
│   │   │   File khai báo lớp SimplexSolver và các hàm xử lý thuật toán.
│   │   │
│   │   └── simplexsolver.cpp
│   │       File cài đặt thuật toán giải bài toán quy hoạch tuyến tính.
│   │
│   └── windows/
│       ├── mainwindow.cpp / mainwindow.h
│       │   Quản lý cửa sổ chính của phần mềm.
│       │
│       ├── dashboard.cpp / dashboard.h
│       │   Quản lý giao diện nhập bài toán.
│       │
│       ├── wdsolve.cpp / wdsolve.h
│       │   Quản lý cửa sổ hiển thị kết quả và các bước giải.
│       │
│       ├── wdchatbot.cpp / wdchatbot.h
│       │   Quản lý chức năng hỏi đáp chatbot.
│       │
│       ├── wdshowimage.cpp / wdshowimage.h
│       │   Quản lý cửa sổ hiển thị hình ảnh hoặc biểu diễn hình học.
│       │
│       └── qcustomplot.cpp / qcustomplot.h
│           Thư viện hỗ trợ vẽ đồ thị trong Qt.
│
├── ui/
│   ├── mainwindow.ui
│   ├── dashboard.ui
│   ├── wdsolve.ui
│   ├── wdchatbot.ui
│   └── wdshowimage.ui
│
│   Các file `.ui` là giao diện được thiết kế bằng Qt Designer.
│
├── resources/
│   ├── resource.qrc
│   │   File quản lý tài nguyên Qt như logo, icon và hình ảnh.
│   │
│   ├── app_icon.rc
│   │   File icon dành cho bản build Windows.
│   │
│   ├── images/
│   │   Chứa logo, icon và hình ảnh minh họa.
│   │
│   └── icons/
│       Chứa các icon dùng trong giao diện.
│
└── docs/
    └── HDSD.pdf
        Tài liệu hướng dẫn sử dụng phần mềm.
```

Toàn bộ mã nguồn thuật toán(đơn hình, bland, 2 pha) được lưu trữ tại src/core/simplexsolver.cpp, 
File Báo cáo được lưu trữ tại thư mục docs 
