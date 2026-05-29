#ifndef GPIOTESTWINDOW_H
#define GPIOTESTWINDOW_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>

class GpioTestWindow : public QWidget
{
    Q_OBJECT

public:
    GpioTestWindow(QWidget *parent = nullptr);
    ~GpioTestWindow();

private slots:
    void onOutButtonClicked();
    void updateInputs();

private:
    void setupUi();

    // 底层操作函数
    void writeGpioValue(int gpioNum, const QString &valStr);
    QString readGpioValue(int gpioNum);

    // 新增：状态栏更新函数
    void showStatus(const QString &msg, bool isError = false);

    QPushButton *btnOut1;
    QPushButton *btnOut2;
    QPushButton *btnOut3;
    QPushButton *btnOut4;

    QLabel *lblIn[4];

    // 新增：状态栏 Label
    QLabel *statusLabel;

    QTimer *pollTimer;
};

#endif // GPIOTESTWINDOW_H