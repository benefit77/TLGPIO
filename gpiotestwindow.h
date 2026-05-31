#ifndef GPIOTESTWINDOW_H
#define GPIOTESTWINDOW_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QList>

struct CanUiElement {
    QString ifaceName;
    QPushButton *testBtn;
    QLabel *statusLabel;
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
    void onCanTestButtonClicked();

private:
    void setupUi();

    void writeGpioValue(int gpioNum, const QString &valStr);
    QString readGpioValue(int gpioNum);
    void showStatus(const QString &msg, bool isError = false);

    QStringList getAvailableCanInterfaces();

    // 新增：自动配置 CAN 波特率并启动节点
    void autoConfigCanInterface(const QString &ifaceName, int bitrate);

    void performCanHandshake(const QString &ifaceName, QLabel *statusLabel);

    QPushButton *btnOut1;
    QPushButton *btnOut2;
    QPushButton *btnOut3;
    QPushButton *btnOut4;
    QLabel *lblIn[4];

    QList<CanUiElement> canElements;

    QLabel *statusLabel;
    QTimer *pollTimer;
};

#endif // GPIOTESTWINDOW_H