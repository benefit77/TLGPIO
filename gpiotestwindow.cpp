#include "gpiotestwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QCoreApplication>

// ==========================================================
// 针对 Linux 环境引入 SocketCAN 头文件
// 使用宏隔离，保证在 Windows/MinGW 下代码依然能正常编译运行 UI
// ==========================================================
#ifdef Q_OS_LINUX
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <unistd.h>
#include <fcntl.h>
#endif

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
    setWindowTitle("东莞市拓朗工控设备有限公司 GPIO与CAN测试程序");

    // 根据探测到的 CAN 数量动态调整窗口高度
    int windowHeight = 250 + (canElements.size() * 40);
    resize(700, windowHeight);

    pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, &GpioTestWindow::updateInputs);
    pollTimer->start(200);
}

GpioTestWindow::~GpioTestWindow()
{
}

// 自动探测 Linux 系统下 `/sys/class/net/` 目录中的 can 设备
QStringList GpioTestWindow::getAvailableCanInterfaces()
{
    QStringList canList;
#ifdef Q_OS_LINUX
    QDir netDir("/sys/class/net");
    if (netDir.exists()) {
        QStringList filters;
        filters << "can*"; // 过滤以 can 开头的文件夹 (如 can0, can1)
        canList = netDir.entryList(filters, QDir::Dirs | QDir::NoDotAndDotDot);
        canList.sort();
    }
#else
    // 如果在 Windows 下开发测试，模拟出两个 CAN 接口方便调试 UI
    canList << "can0" << "can1";
#endif
    return canList;
}

void GpioTestWindow::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 10);

    // ==================== 上半部分：GPIO ====================
    QHBoxLayout *gpioLayout = new QHBoxLayout();

    // --- 左侧：输出 DO ---
    QGridLayout *outLayout = new QGridLayout();
    outLayout->addWidget(new QLabel("输出 (DO)"), 0, 0, 1, 2);

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
        outButtons[i]->setProperty("pin_name", QString("OUT%1").arg(i + 1));

        QString initialVal = readGpioValue(BOARD_DO_PINS[i]);
        outButtons[i]->setText(initialVal.isEmpty() ? "1" : initialVal);

        outLayout->addWidget(outButtons[i], i + 1, 1);
        connect(outButtons[i], &QPushButton::clicked, this, &GpioTestWindow::onOutButtonClicked);
    }

    // --- 右侧：输入 DI ---
    QGridLayout *inLayout = new QGridLayout();
    inLayout->addWidget(new QLabel("输入 (DI)"), 0, 0, 1, 2);

    for (int i = 0; i < 4; ++i) {
        QString labelText = QString("IN%1  (%2)").arg(i + 1).arg(BOARD_DI_PINS[i]);
        inLayout->addWidget(new QLabel(labelText), i + 1, 0);

        lblIn[i] = new QLabel("-");
        lblIn[i]->setStyleSheet("background-color: #e0e0e0; padding: 4px; border-radius: 2px;");
        lblIn[i]->setFixedSize(60, 28);
        lblIn[i]->setAlignment(Qt::AlignCenter);
        inLayout->addWidget(lblIn[i], i + 1, 1);
    }

    gpioLayout->addLayout(outLayout);
    gpioLayout->addSpacing(50);
    gpioLayout->addLayout(inLayout);
    gpioLayout->addStretch();

    // ==================== 下半部分：动态 CAN 测试 ====================
    QGridLayout *canLayout = new QGridLayout();
    canLayout->setContentsMargins(0, 30, 0, 10);

    QLabel *canTitle = new QLabel("SocketCAN 通讯测试 (自动探测系统接口)");
    canTitle->setStyleSheet("font-weight: bold;");
    canLayout->addWidget(canTitle, 0, 0, 1, 3);

    QStringList availableCans = getAvailableCanInterfaces();

    if (availableCans.isEmpty()) {
        QLabel *noCanLabel = new QLabel("未在系统中探测到 CAN 接口");
        noCanLabel->setStyleSheet("color: red;");
        canLayout->addWidget(noCanLabel, 1, 0, 1, 3);
    } else {
        // 根据探测到的 CAN 接口数量，动态生成对应的测试按钮和状态栏
        for (int i = 0; i < availableCans.size(); ++i) {
            QString iface = availableCans.at(i);

            QLabel *lblIface = new QLabel(iface + " :");
            QPushButton *btnTest = new QPushButton("开始握手测试");
            btnTest->setFixedWidth(120);

            QLabel *lblStatus = new QLabel("准备就绪");
            lblStatus->setFrameStyle(QFrame::Panel | QFrame::Sunken);
            lblStatus->setMinimumWidth(300);
            lblStatus->setStyleSheet("background-color: #f0f0f0; padding: 4px;");

            canLayout->addWidget(lblIface, i + 1, 0);
            canLayout->addWidget(btnTest, i + 1, 1);
            canLayout->addWidget(lblStatus, i + 1, 2);

            // 将控件信息保存到列表中，方便点击时查找
            CanUiElement element = {iface, btnTest, lblStatus};
            canElements.append(element);

            // 绑定点击事件
            connect(btnTest, &QPushButton::clicked, this, &GpioTestWindow::onCanTestButtonClicked);
        }
    }

    // ==================== 底部：取消按钮 ====================
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    QPushButton *btnCancel = new QPushButton("关闭");
    btnCancel->setFixedWidth(100);
    bottomLayout->addWidget(btnCancel);
    connect(btnCancel, &QPushButton::clicked, this, &QWidget::close);

    // ==================== 组合所有布局 ====================
    mainLayout->addLayout(gpioLayout);

    QFrame *line1 = new QFrame(this);
    line1->setFrameShape(QFrame::HLine);
    line1->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line1);

    mainLayout->addLayout(canLayout);
    mainLayout->addStretch();
    mainLayout->addLayout(bottomLayout);

    QFrame *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line2);

    statusLabel = new QLabel("就绪 (程序已启动)");
    statusLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    statusLabel->setStyleSheet("padding: 2px; color: #333333;");
    mainLayout->addWidget(statusLabel);
}

// ==========================================================
// GPIO 相关功能 (保持不变)
// ==========================================================
void GpioTestWindow::showStatus(const QString &msg, bool isError)
{
    if (isError) {
        statusLabel->setStyleSheet("padding: 2px; color: #D32F2F; font-weight: bold;");
    } else {
        statusLabel->setStyleSheet("padding: 2px; color: #388E3C;");
    }
    statusLabel->setText(msg);
}

QString GpioTestWindow::readGpioValue(int gpioNum)
{
    QString path = QString("/sys/class/gpio/gpio%1/value").arg(gpioNum);
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString val = QString(file.readAll()).trimmed();
        file.close();
        if (val == "0" || val == "1") return val;
    }
    return "";
}

void GpioTestWindow::writeGpioValue(int gpioNum, const QString &valStr)
{
    QString path = QString("/sys/class/gpio/gpio%1/value").arg(gpioNum);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(valStr.toUtf8());
        file.close();
        showStatus(QString("操作成功: 端口 GPIO %1 已设置为 %2").arg(gpioNum).arg(valStr), false);
    } else {
        showStatus(QString("写入失败: 找不到路径或无权限 %1").arg(path), true);
    }
}

void GpioTestWindow::onOutButtonClicked()
{
    QPushButton *clickedButton = qobject_cast<QPushButton*>(sender());
    if (!clickedButton) return;
    int gpioNum = clickedButton->property("gpio_num").toInt();
    QString pinName = clickedButton->property("pin_name").toString();
    showStatus(QString("正在读取 %1 (GPIO %2) 当前状态...").arg(pinName).arg(gpioNum), false);
    QString realSystemVal = readGpioValue(gpioNum);
    if (realSystemVal.isEmpty()) realSystemVal = clickedButton->text();
    QString newText = (realSystemVal == "1") ? "0" : "1";
    writeGpioValue(gpioNum, newText);
    clickedButton->setText(newText);
}

void GpioTestWindow::updateInputs()
{
    for (int i = 0; i < 4; ++i) {
        QString val = readGpioValue(BOARD_DI_PINS[i]);
        if (!val.isEmpty()) lblIn[i]->setText(val);
    }
}

// ==========================================================
// CAN 测试槽函数
// ==========================================================
void GpioTestWindow::onCanTestButtonClicked()
{
    QPushButton *clickedButton = qobject_cast<QPushButton*>(sender());
    if (!clickedButton) return;

    // 禁用按钮防止重复点击
    clickedButton->setEnabled(false);

    // 找出是被哪个 CAN 接口的按钮点击的
    QString targetIface;
    QLabel *targetStatusLabel = nullptr;
    for (const CanUiElement &element : canElements) {
        if (element.testBtn == clickedButton) {
            targetIface = element.ifaceName;
            targetStatusLabel = element.statusLabel;
            break;
        }
    }

    if (targetStatusLabel) {
        // 调用重写的 SocketCAN 测试核心逻辑
        performCanHandshake(targetIface, targetStatusLabel);
    }

    clickedButton->setEnabled(true);
}

// ==========================================================
// 核心：使用 Linux SocketCAN 重写握手测试逻辑
// ==========================================================
void GpioTestWindow::performCanHandshake(const QString &ifaceName, QLabel *statusEdit)
{
    constexpr uint32_t HANDSHAKE_ID_REQUEST = 0x100;
    constexpr uint32_t HANDSHAKE_ID_REPLY   = 0x101;
    constexpr int HANDSHAKE_ROUNDS = 10;
    constexpr int ROUND_TIMEOUT_MS = 200;
    constexpr int TOTAL_TIMEOUT_MS = 2000;
    constexpr int DATA_LENGTH = 8;

    statusEdit->setStyleSheet("background-color: yellow; color: black; padding: 4px;");
    statusEdit->setText(QString("正在打开接口 %1...").arg(ifaceName));
    QCoreApplication::processEvents(); // 刷新UI

#ifdef Q_OS_LINUX
    // 1. 创建 RAW Socket
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        statusEdit->setStyleSheet("background-color: #ffcccc; color: red; padding: 4px;");
        statusEdit->setText("❌ 创建 Socket 失败");
        return;
    }

    // 2. 将 Socket 绑定到指定的 CAN 接口 (例如 can0)
    struct ifreq ifr;
    strcpy(ifr.ifr_name, ifaceName.toStdString().c_str());
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        statusEdit->setStyleSheet("background-color: #ffcccc; color: red; padding: 4px;");
        statusEdit->setText("❌ 获取接口索引失败 (节点未UP?)");
        close(sock);
        return;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        statusEdit->setStyleSheet("background-color: #ffcccc; color: red; padding: 4px;");
        statusEdit->setText("❌ 绑定 Socket 失败");
        close(sock);
        return;
    }

    // 3. 将 Socket 设置为非阻塞模式 (为了手动用 elapsedTimer 做精确超时控制，且不卡UI)
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct can_frame txFrame;
    memset(&txFrame, 0, sizeof(txFrame));
    txFrame.can_id = HANDSHAKE_ID_REQUEST;
    txFrame.can_dlc = DATA_LENGTH;

    QElapsedTimer totalTimer;
    totalTimer.start();

    // 4. 开始 10 轮握手
    for (int round = 1; round <= HANDSHAKE_ROUNDS; ++round)
    {
        // 填充发送数据，首字节为轮次序号
        txFrame.data[0] = round;
        for(int i = 1; i < DATA_LENGTH; ++i) {
            txFrame.data[i] = 0x10 + i;
        }

        // 发送数据
        if (write(sock, &txFrame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
            statusEdit->setStyleSheet("background-color: #ffcccc; color: red; padding: 4px;");
            statusEdit->setText(QString("❌ 第%1轮发送失败").arg(round));
            close(sock);
            return;
        }

        bool replyReceived = false;
        QElapsedTimer roundTimer;
        roundTimer.start();

        statusEdit->setText(QString("正在进行第 %1 轮握手...").arg(round));

        // 接收数据的超时循环
        while (roundTimer.elapsed() < ROUND_TIMEOUT_MS)
        {
            if (totalTimer.elapsed() >= TOTAL_TIMEOUT_MS) {
                statusEdit->setStyleSheet("background-color: #ffcccc; color: red; padding: 4px;");
                statusEdit->setText("⏱️ 总超时2秒，测试中止！");
                close(sock);
                return;
            }

            struct can_frame rxFrame;
            // 非阻塞读取
            int nbytes = read(sock, &rxFrame, sizeof(struct can_frame));

            if (nbytes == sizeof(struct can_frame)) {
                // 判断是否是回复帧且轮次匹配
                if (rxFrame.can_id == HANDSHAKE_ID_REPLY &&
                    rxFrame.can_dlc == DATA_LENGTH &&
                    rxFrame.data[0] == round)
                {
                    replyReceived = true;
                    break;
                }
            }

            // ★核心：保证在 while 循环中界面不卡死，让用户仍能操作其他按钮
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }

        if (!replyReceived) {
            statusEdit->setStyleSheet("background-color: #ffcccc; color: red; padding: 4px;");
            statusEdit->setText(QString("❌ 第%1轮超时无回复").arg(round));
            close(sock);
            return;
        }
    }

    // 全部 10 轮成功
    close(sock);
    statusEdit->setStyleSheet("background-color: #ccffcc; color: green; font-weight: bold; padding: 4px;");
    statusEdit->setText("🎉 握手成功！10轮全部完成");

#else
    // Windows 下仅作 UI 模拟展示 (因为没有真实的 SocketCAN 环境)
    QElapsedTimer dummyTimer;
    dummyTimer.start();
    for (int round = 1; round <= HANDSHAKE_ROUNDS; ++round) {
        statusEdit->setText(QString("Windows 模拟测试: 第 %1 轮...").arg(round));
        while(dummyTimer.elapsed() < 100) { QCoreApplication::processEvents(QEventLoop::AllEvents, 5); }
        dummyTimer.restart();
    }
    statusEdit->setStyleSheet("background-color: #ccffcc; color: green; font-weight: bold; padding: 4px;");
    statusEdit->setText("🎉 (模拟)握手成功！10轮全部完成");
#endif
}