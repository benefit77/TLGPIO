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
#include <cstdlib> // 引入 system()

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
const int DEFAULT_CAN_BITRATE = 500000; // 默认 CAN 波特率 500k
// ==========================================================

GpioTestWindow::GpioTestWindow(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    setWindowTitle("GPIO与CAN测试程序");

    int windowHeight = 250 + (canElements.size() * 40);
    resize(700, windowHeight);

    pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, &GpioTestWindow::updateInputs);
    pollTimer->start(200);
}

GpioTestWindow::~GpioTestWindow()
{
}

QStringList GpioTestWindow::getAvailableCanInterfaces()
{
    QStringList canList;
#ifdef Q_OS_LINUX
    QDir netDir("/sys/class/net");
    if (netDir.exists()) {
        QStringList filters;
        filters << "can*";
        canList = netDir.entryList(filters, QDir::Dirs | QDir::NoDotAndDotDot);
        canList.sort();
    }
#else
    canList << "can0" << "can1"; // Windows 模拟
#endif
    return canList;
}

// ==========================================================
// 新增：自动执行 Linux 终端命令，配置 CAN 节点
// (注意：程序需要在 Linux 下以 root 权限运行，这在工控机上通常是默认的)
// ==========================================================
void GpioTestWindow::autoConfigCanInterface(const QString &ifaceName, int bitrate)
{
#ifdef Q_OS_LINUX
    // 拼接命令
    QString downCmd = QString("ip link set down %1").arg(ifaceName);
    QString cfgCmd  = QString("ip link set %1 type can bitrate %2").arg(ifaceName).arg(bitrate);
    QString upCmd   = QString("ip link set up %1").arg(ifaceName);

    // 依次执行：先 down掉 -> 配置波特率 -> 重新 up
    system(downCmd.toStdString().c_str());
    system(cfgCmd.toStdString().c_str());
    system(upCmd.toStdString().c_str());

    qDebug() << "Auto configured CAN interface:" << ifaceName << "at bitrate:" << bitrate;
#endif
}

void GpioTestWindow::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 10);

    // ==================== 上半部分：GPIO ====================
    QHBoxLayout *gpioLayout = new QHBoxLayout();

    QGridLayout *outLayout = new QGridLayout();
    outLayout->addWidget(new QLabel("输出 (DO)"), 0, 0, 1, 2);
    QPushButton* outButtons[] = {
        btnOut1 = new QPushButton("-"), btnOut2 = new QPushButton("-"),
        btnOut3 = new QPushButton("-"), btnOut4 = new QPushButton("-")
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

    QLabel *canTitle = new QLabel(QString("SocketCAN 通讯测试 (已自动配置为 %1 bps)").arg(DEFAULT_CAN_BITRATE));
    canTitle->setStyleSheet("font-weight: bold;");
    canLayout->addWidget(canTitle, 0, 0, 1, 3);

    QStringList availableCans = getAvailableCanInterfaces();

    if (availableCans.isEmpty()) {
        QLabel *noCanLabel = new QLabel("未在系统中探测到 CAN 接口");
        noCanLabel->setStyleSheet("color: red;");
        canLayout->addWidget(noCanLabel, 1, 0, 1, 3);
    } else {
        for (int i = 0; i < availableCans.size(); ++i) {
            QString iface = availableCans.at(i);

            // ==========================================
            // 核心：在创建UI时，顺便把底层的网卡配置好！
            // ==========================================
            autoConfigCanInterface(iface, DEFAULT_CAN_BITRATE);

            QLabel *lblIface = new QLabel(iface + " :");
            QPushButton *btnTest = new QPushButton("开始握手测试");
            btnTest->setFixedWidth(120);

            QLabel *lblStatus = new QLabel("已配置就绪");
            lblStatus->setFrameStyle(QFrame::Panel | QFrame::Sunken);
            lblStatus->setMinimumWidth(300);
            lblStatus->setStyleSheet("background-color: #f0f0f0; padding: 4px;");

            canLayout->addWidget(lblIface, i + 1, 0);
            canLayout->addWidget(btnTest, i + 1, 1);
            canLayout->addWidget(lblStatus, i + 1, 2);

            CanUiElement element = {iface, btnTest, lblStatus};
            canElements.append(element);

            connect(btnTest, &QPushButton::clicked, this, &GpioTestWindow::onCanTestButtonClicked);
        }
    }

    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    QPushButton *btnCancel = new QPushButton("关闭");
    btnCancel->setFixedWidth(100);
    bottomLayout->addWidget(btnCancel);
    connect(btnCancel, &QPushButton::clicked, this, &QWidget::close);

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

    statusLabel = new QLabel("就绪 (程序已启动，系统网络设备已自动拉起)");
    statusLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    statusLabel->setStyleSheet("padding: 2px; color: #333333;");
    mainLayout->addWidget(statusLabel);
}

// （以下函数与上个版本完全一致，为了不占篇幅，保持不变）
void GpioTestWindow::showStatus(const QString &msg, bool isError) {
    if (isError) statusLabel->setStyleSheet("padding: 2px; color: #D32F2F; font-weight: bold;");
    else statusLabel->setStyleSheet("padding: 2px; color: #388E3C;");
    statusLabel->setText(msg);
}

QString GpioTestWindow::readGpioValue(int gpioNum) {
    QString path = QString("/sys/class/gpio/gpio%1/value").arg(gpioNum);
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString val = QString(file.readAll()).trimmed();
        file.close();
        if (val == "0" || val == "1") return val;
    }
    return "";
}

void GpioTestWindow::writeGpioValue(int gpioNum, const QString &valStr) {
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

void GpioTestWindow::onOutButtonClicked() {
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

void GpioTestWindow::updateInputs() {
    for (int i = 0; i < 4; ++i) {
        QString val = readGpioValue(BOARD_DI_PINS[i]);
        if (!val.isEmpty()) lblIn[i]->setText(val);
    }
}

void GpioTestWindow::onCanTestButtonClicked() {
    QPushButton *clickedButton = qobject_cast<QPushButton*>(sender());
    if (!clickedButton) return;
    clickedButton->setEnabled(false);
    QString targetIface;
    QLabel *targetStatusLabel = nullptr;
    for (const CanUiElement &element : canElements) {
        if (element.testBtn == clickedButton) {
            targetIface = element.ifaceName;
            targetStatusLabel = element.statusLabel;
            break;
        }
    }
    if (targetStatusLabel) performCanHandshake(targetIface, targetStatusLabel);
    clickedButton->setEnabled(true);
}

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
    QCoreApplication::processEvents();

#ifdef Q_OS_LINUX
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        statusEdit->setStyleSheet("background-color: #ffcccc; color: red; padding: 4px;");
        statusEdit->setText("❌ 创建 Socket 失败");
        return;
    }

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

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct can_frame txFrame;
    memset(&txFrame, 0, sizeof(txFrame));
    txFrame.can_id = HANDSHAKE_ID_REQUEST;
    txFrame.can_dlc = DATA_LENGTH;

    QElapsedTimer totalTimer;
    totalTimer.start();

    for (int round = 1; round <= HANDSHAKE_ROUNDS; ++round)
    {
        txFrame.data[0] = round;
        for(int i = 1; i < DATA_LENGTH; ++i) {
            txFrame.data[i] = 0x10 + i;
        }

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

        while (roundTimer.elapsed() < ROUND_TIMEOUT_MS)
        {
            if (totalTimer.elapsed() >= TOTAL_TIMEOUT_MS) {
                statusEdit->setStyleSheet("background-color: #ffcccc; color: red; padding: 4px;");
                statusEdit->setText("⏱️ 总超时2秒，测试中止！");
                close(sock);
                return;
            }

            struct can_frame rxFrame;
            int nbytes = read(sock, &rxFrame, sizeof(struct can_frame));

            if (nbytes == sizeof(struct can_frame)) {
                if (rxFrame.can_id == HANDSHAKE_ID_REPLY &&
                    rxFrame.can_dlc == DATA_LENGTH &&
                    rxFrame.data[0] == round)
                {
                    replyReceived = true;
                    break;
                }
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }

        if (!replyReceived) {
            statusEdit->setStyleSheet("background-color: #ffcccc; color: red; padding: 4px;");
            statusEdit->setText(QString("❌ 第%1轮超时无回复").arg(round));
            close(sock);
            return;
        }
    }

    close(sock);
    statusEdit->setStyleSheet("background-color: #ccffcc; color: green; font-weight: bold; padding: 4px;");
    statusEdit->setText("🎉 握手成功！10轮全部完成");

#else
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