#include "gpiotestwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFile>
#include <QDebug>
#include <QVariant>

// ==========================================================
// 【硬件配置区】
// ==========================================================
const int BOARD_DO_PINS[4] = {21, 6, 22, 144};
const int BOARD_DI_PINS[4] = {91, 120, 89, 88};
// ==========================================================

GpioTestWindow::GpioTestWindow(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    setWindowTitle("东莞市拓朗工控设备有限公司 GPIO和看门狗测试程序");
    resize(700, 480); // 稍微调高一点高度容纳状态栏

    pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, &GpioTestWindow::updateInputs);
    pollTimer->start(200);
}

GpioTestWindow::~GpioTestWindow()
{
}

void GpioTestWindow::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 10); // 底部边距调小一点

    QHBoxLayout *gpioLayout = new QHBoxLayout();

    // ==================== 左侧：输出 DO ====================
    QGridLayout *outLayout = new QGridLayout();
    outLayout->addWidget(new QLabel("输出"), 0, 0, 1, 2);

    QPushButton* outButtons[] = {
        btnOut1 = new QPushButton("-"),
        btnOut2 = new QPushButton("-"),
        btnOut3 = new QPushButton("-"),
        btnOut4 = new QPushButton("-")
    };

    QString btnStyle = "QPushButton { background-color: #dcdcdc; border: 1px solid #8f8f91; padding: 4px; }"
                       "QPushButton:pressed { background-color: #a0a0a0; }";

    for (int i = 0; i < 4; ++i) {
        QString labelText = QString("OUT%1  (%2)").arg(i + 1).arg(BOARD_DO_PINS[i]);
        outLayout->addWidget(new QLabel(labelText), i + 1, 0);

        outButtons[i]->setStyleSheet(btnStyle);
        outButtons[i]->setFixedSize(120, 28);
        outButtons[i]->setProperty("gpio_num", BOARD_DO_PINS[i]);
        outButtons[i]->setProperty("pin_name", QString("OUT%1").arg(i + 1)); // 顺便存一下名字，打日志用

        QString initialVal = readGpioValue(BOARD_DO_PINS[i]);
        outButtons[i]->setText(initialVal.isEmpty() ? "1" : initialVal);

        outLayout->addWidget(outButtons[i], i + 1, 1);
        connect(outButtons[i], &QPushButton::clicked, this, &GpioTestWindow::onOutButtonClicked);
    }

    // ==================== 右侧：输入 DI ====================
    QGridLayout *inLayout = new QGridLayout();
    inLayout->addWidget(new QLabel("输入"), 0, 0, 1, 2);

    for (int i = 0; i < 4; ++i) {
        QString labelText = QString("IN%1  (%2)").arg(i + 1).arg(BOARD_DI_PINS[i]);
        inLayout->addWidget(new QLabel(labelText), i + 1, 0);

        lblIn[i] = new QLabel("-");
        lblIn[i]->setStyleSheet("background-color: #e0e0e0; padding: 4px;");
        lblIn[i]->setFixedSize(60, 28);
        lblIn[i]->setAlignment(Qt::AlignCenter);
        inLayout->addWidget(lblIn[i], i + 1, 1);
    }

    gpioLayout->addLayout(outLayout);
    gpioLayout->addSpacing(50);
    gpioLayout->addLayout(inLayout);
    gpioLayout->addStretch();

    // ==================== 下半部分：看门狗 ====================
    QGridLayout *watchdogLayout = new QGridLayout();
    watchdogLayout->setContentsMargins(0, 30, 0, 10);
    watchdogLayout->addWidget(new QLabel("看门狗测试"), 0, 0);

    QHBoxLayout *wdControlLayout = new QHBoxLayout();
    wdControlLayout->addWidget(new QLabel("超时时间:"));
    QLineEdit *timeoutEdit = new QLineEdit("60");
    timeoutEdit->setFixedWidth(60);
    wdControlLayout->addWidget(timeoutEdit);
    wdControlLayout->addSpacing(20);

    QPushButton *btnSetStart = new QPushButton("设置并启动");
    QPushButton *btnFeedDog = new QPushButton("喂狗");
    QPushButton *btnStopDog = new QPushButton("停止看门狗");
    wdControlLayout->addWidget(btnSetStart);
    wdControlLayout->addWidget(btnFeedDog);
    wdControlLayout->addWidget(btnStopDog);
    wdControlLayout->addStretch();
    watchdogLayout->addLayout(wdControlLayout, 1, 0, 1, 2);

    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    QPushButton *btnCancel = new QPushButton("取消");
    btnCancel->setFixedWidth(100);
    bottomLayout->addWidget(btnCancel);
    connect(btnCancel, &QPushButton::clicked, this, &QWidget::close);

    // ==================== 组合布局 ====================
    mainLayout->addLayout(gpioLayout);
    mainLayout->addSpacing(20);

    QFrame *line1 = new QFrame(this);
    line1->setFrameShape(QFrame::HLine);
    line1->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line1);

    mainLayout->addLayout(watchdogLayout);
    mainLayout->addStretch();
    mainLayout->addLayout(bottomLayout);

    QFrame *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line2);

    // ==================== 新增：底部状态栏 ====================
    statusLabel = new QLabel("就绪 (程序已启动)");
    statusLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken); // 凹陷的立体边框
    statusLabel->setStyleSheet("padding: 2px; color: #333333;");
    mainLayout->addWidget(statusLabel);
}

// ==========================================================
// 新增：更新状态栏UI
// ==========================================================
void GpioTestWindow::showStatus(const QString &msg, bool isError)
{
    if (isError) {
        // 报错时显示红色、加粗
        statusLabel->setStyleSheet("padding: 2px; color: #D32F2F; font-weight: bold;");
    } else {
        // 成功时显示绿色
        statusLabel->setStyleSheet("padding: 2px; color: #388E3C;");
    }
    statusLabel->setText(msg);
}

// ==========================================================
// 辅助函数：读取底层文件获取真实 GPIO 状态
// ==========================================================
QString GpioTestWindow::readGpioValue(int gpioNum)
{
    QString path = QString("/sys/class/gpio/gpio%1/value").arg(gpioNum);
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString val = QString(file.readAll()).trimmed();
        file.close();
        if (val == "0" || val == "1") {
            return val;
        }
    }
    return "";
}

// ==========================================================
// 修改：写入函数增加状态栏反馈
// ==========================================================
void GpioTestWindow::writeGpioValue(int gpioNum, const QString &valStr)
{
    QString path = QString("/sys/class/gpio/gpio%1/value").arg(gpioNum);
    QFile file(path);

    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(valStr.toUtf8());
        file.close();

        // 写入成功，显示绿色提示
        showStatus(QString("操作成功: 端口 GPIO %1 已设置为 %2").arg(gpioNum).arg(valStr), false);
    } else {
        // 写入失败，显示红色提示
        showStatus(QString("写入失败: 找不到路径或无权限 %1").arg(path), true);
    }
}

// ==========================================================
// DO 点击切换逻辑
// ==========================================================
void GpioTestWindow::onOutButtonClicked()
{
    QPushButton *clickedButton = qobject_cast<QPushButton*>(sender());
    if (!clickedButton) return;

    int gpioNum = clickedButton->property("gpio_num").toInt();
    QString pinName = clickedButton->property("pin_name").toString();

    // 提示正在操作（如果文件写入卡顿，能让用户知道程序没死）
    showStatus(QString("正在读取 %1 (GPIO %2) 当前状态...").arg(pinName).arg(gpioNum), false);

    QString realSystemVal = readGpioValue(gpioNum);
    if (realSystemVal.isEmpty()) {
        realSystemVal = clickedButton->text();
    }

    QString newText = (realSystemVal == "1") ? "0" : "1";
    writeGpioValue(gpioNum, newText);
    clickedButton->setText(newText);
}

// ==========================================================
// 定时器：刷新 DI 状态
// 注：这里不加状态栏提示，否则1秒钟刷新5次，状态栏会一直闪烁
// ==========================================================
void GpioTestWindow::updateInputs()
{
    for (int i = 0; i < 4; ++i) {
        QString val = readGpioValue(BOARD_DI_PINS[i]);
        if (!val.isEmpty()) {
            lblIn[i]->setText(val);
        }
    }
}