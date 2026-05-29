#ifndef GPIOTESTWINDOW_H
#define GPIOTESTWINDOW_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QList>

// 定义一个结构体用来保存动态生成的 CAN 控件映射
struct CanUiElement {
    QString ifaceName;       // 接口名，例如 "can0"
    QPushButton *testBtn;    // 测试按钮
    QLabel *statusLabel;     // 状态显示框
};

class GpioTestWindow : public QWidget
{
    Q_OBJECT

public:
    GpioTestWindow(QWidget *parent = nullptr);
    ~GpioTestWindow();

private slots:
    void onOutButtonClicked();
    void updateInputs();

    // 新增：CAN 测试按钮被点击的槽函数
    void onCanTestButtonClicked();

private:
    void setupUi();

    // 辅助函数
    void writeGpioValue(int gpioNum, const QString &valStr);
    QString readGpioValue(int gpioNum);
    void showStatus(const QString &msg, bool isError = false);

    // 新增：获取系统中所有的 CAN 接口名称
    QStringList getAvailableCanInterfaces();
    // 新增：执行 SocketCAN 握手测试核心逻辑
    void performCanHandshake(const QString &ifaceName, QLabel *statusLabel);

    // GPIO 控件
    QPushButton *btnOut1;
    QPushButton *btnOut2;
    QPushButton *btnOut3;
    QPushButton *btnOut4;
    QLabel *lblIn[4];

    // CAN 控件列表 (因为数量是动态的，所以用 QList 保存)
    QList<CanUiElement> canElements;

    QLabel *statusLabel; // 底部全局状态栏
    QTimer *pollTimer;
};

#endif // GPIOTESTWINDOW_H