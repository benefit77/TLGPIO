#include "gpiotestwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置全局字体大小，让界面看起来更清晰
    QFont font = a.font();
    font.setPointSize(10);
    a.setFont(font);

    GpioTestWindow w;
    w.show();
    return a.exec();
}