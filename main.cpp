#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    // Fix mờ chữ trên các màn hình có scale > 100% (High DPI)
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);


    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/logo.png"));


    // Kích hoạt giao diện native của Windows (viền nút sắc nét, bo góc)
    a.setStyle(QStyleFactory::create("windowsvista"));

    MainWindow w;
    w.show();
    return a.exec();
}
