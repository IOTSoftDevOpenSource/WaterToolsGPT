#include "widget.h"
#include "modbusrtuworker.h"
#include "ui_widget.h"

#include <QRegularExpression>
#include <QThread>
#include <QSerialPort>
#include <QSerialPortInfo>

#include <cmath>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
    , m_portOpened(false)
{
    ui->setupUi(this);

    connect(ui->btnParse, &QPushButton::clicked, this, &Widget::onParseClicked);
    connect(ui->btnLoadExample, &QPushButton::clicked, this, &Widget::onLoadExampleClicked);
    connect(ui->btnOpenPort, &QPushButton::clicked, this, &Widget::onOpenPortClicked);
    connect(ui->btnClosePort, &QPushButton::clicked, this, &Widget::onClosePortClicked);
    connect(ui->btnRefreshPorts, &QPushButton::clicked, this, &Widget::onRefreshPortsClicked);
    connect(ui->btnReadRegisters, &QPushButton::clicked, this, &Widget::onReadRegistersClicked);

    refreshPortList(false);
    setupWorkerThread();

    onLoadExampleClicked();
}

Widget::~Widget()
{
    emit requestClosePort();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(1000);
    }
    delete ui;
}

void Widget::onLoadExampleClicked()
{
    ui->lineEditFrame->setText("05 03 08 01 02 00 02 00 B0 00 01 DB 0C");
    onParseClicked();
}

void Widget::onParseClicked()
{
    QVector<quint8> frame;
    QString error;
    if (!parseHexFrame(ui->lineEditFrame->text(), frame, error)) {
        ui->labelStatus->setText("状态: " + error);
        return;
    }

    ParsedValues values;
    if (!parseDoAndTemperature(frame, values, error)) {
        ui->labelStatus->setText("状态: " + error);
        return;
    }

    updateResultUi(values);
    ui->labelStatus->setText("状态: 解析成功");
}

void Widget::onOpenPortClicked()
{
    const QString portName = ui->comboPortName->currentText().trimmed();
    if (portName.isEmpty()) {
        ui->labelStatus->setText("状态: 串口名为空");
        return;
    }

    const int baudRate = ui->comboBaudRate->currentText().toInt();
    const int dataBits = ui->comboDataBits->currentText().toInt() == 7
                           ? static_cast<int>(QSerialPort::Data7)
                           : static_cast<int>(QSerialPort::Data8);

    int parity = static_cast<int>(QSerialPort::NoParity);
    const QString parityText = ui->comboParity->currentText();
    if (parityText == "E") {
        parity = static_cast<int>(QSerialPort::EvenParity);
    } else if (parityText == "O") {
        parity = static_cast<int>(QSerialPort::OddParity);
    }

    const int stopBits = ui->comboStopBits->currentText() == "2"
                           ? static_cast<int>(QSerialPort::TwoStop)
                           : static_cast<int>(QSerialPort::OneStop);

    emit requestOpenPort(portName,
                         baudRate,
                         dataBits,
                         parity,
                         stopBits,
                         ui->spinTimeoutMs->value());
    ui->labelStatus->setText("状态: 正在打开串口...");
}

void Widget::onClosePortClicked()
{
    emit requestClosePort();
}

void Widget::onRefreshPortsClicked()
{
    refreshPortList(true);
}

void Widget::onReadRegistersClicked()
{
    if (!m_portOpened) {
        ui->labelStatus->setText("状态: 请先打开串口");
        return;
    }

    const quint8 slaveAddress = static_cast<quint8>(ui->spinSlaveAddress->value());
    const quint16 startAddress = static_cast<quint16>(ui->spinStartAddress->value());
    const quint16 quantity = static_cast<quint16>(ui->spinQuantity->value());

    emit requestReadHoldingRegisters(slaveAddress, startAddress, quantity);
    ui->labelStatus->setText("状态: 已发送读寄存器请求");
}

void Widget::onPortOpened(bool ok, const QString &message)
{
    m_portOpened = ok;
    ui->labelStatus->setText("状态: " + message);
}

void Widget::onPortClosed()
{
    m_portOpened = false;
    ui->labelStatus->setText("状态: 串口已关闭");
}

void Widget::onFrameReceived(const QByteArray &frame)
{
    ui->lineEditFrame->setText(bytesToHexString(frame));
    ui->labelTxRx->setText("TX/RX: RX=" + bytesToHexString(frame));

    ParsedValues values;
    QString error;
    if (!parseDoAndTemperature(frame, values, error)) {
        ui->labelStatus->setText("状态: " + error);
        return;
    }

    updateResultUi(values);
    ui->labelStatus->setText("状态: 串口读取并解析成功");
}

void Widget::onWorkerError(const QString &message)
{
    ui->labelStatus->setText("状态: " + message);
}

void Widget::onTxFrameReady(const QByteArray &frame)
{
    ui->labelTxRx->setText("TX/RX: TX=" + bytesToHexString(frame));
}

bool Widget::parseHexFrame(const QString &text, QVector<quint8> &bytes, QString &error) const
{
    QString cleaned = text.trimmed().toUpper();
    cleaned.replace(QRegularExpression("[，,;；\\s]+"), " ");

    if (cleaned.isEmpty()) {
        error = "输入为空";
        return false;
    }

    const QStringList tokens = cleaned.split(' ', Qt::SkipEmptyParts);
    bytes.clear();
    bytes.reserve(tokens.size());

    for (const QString &token : tokens) {
        bool ok = false;
        int value = token.toInt(&ok, 16);
        if (!ok || value < 0 || value > 0xFF) {
            error = "非法字节: " + token;
            return false;
        }
        bytes.push_back(static_cast<quint8>(value));
    }

    return true;
}

quint16 Widget::crc16Modbus(const QVector<quint8> &data, int length) const
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

bool Widget::parseDoAndTemperature(const QByteArray &frame, ParsedValues &values, QString &error) const
{
    QVector<quint8> data;
    data.reserve(frame.size());
    for (char ch : frame) {
        data.push_back(static_cast<quint8>(ch));
    }
    return parseDoAndTemperature(data, values, error);
}

bool Widget::parseDoAndTemperature(const QVector<quint8> &frame, ParsedValues &values, QString &error) const
{
    if (frame.size() < 13) {
        error = "帧长度不足";
        return false;
    }

    const quint8 addr = frame[0];
    const quint8 func = frame[1];
    const quint8 byteCount = frame[2];

    if (func != 0x03) {
        error = QString("功能码错误: 0x%1").arg(func, 2, 16, QLatin1Char('0')).toUpper();
        return false;
    }

    if (byteCount != 0x08) {
        error = QString("字节数错误: %1").arg(byteCount);
        return false;
    }

    const int expectedLength = 3 + byteCount + 2;
    if (frame.size() != expectedLength) {
        error = QString("帧长度应为 %1，实际 %2").arg(expectedLength).arg(frame.size());
        return false;
    }

    const quint16 calc = crc16Modbus(frame, frame.size() - 2);
    const quint16 recv = static_cast<quint16>(frame[frame.size() - 2])
                       | (static_cast<quint16>(frame[frame.size() - 1]) << 8);
    if (calc != recv) {
        error = QString("CRC校验失败: 计算=0x%1, 接收=0x%2")
                    .arg(calc, 4, 16, QLatin1Char('0'))
                    .arg(recv, 4, 16, QLatin1Char('0'))
                    .toUpper();
        return false;
    }

    Q_UNUSED(addr);

    values.doN = static_cast<quint16>((frame[3] << 8) | frame[4]);
    values.doD = static_cast<quint16>((frame[5] << 8) | frame[6]);
    values.tempM = static_cast<quint16>((frame[7] << 8) | frame[8]);
    values.tempE = static_cast<quint16>((frame[9] << 8) | frame[10]);

    values.doValue = values.doN / std::pow(10.0, static_cast<int>(values.doD));
    values.tempValue = values.tempM / std::pow(10.0, static_cast<int>(values.tempE));

    return true;
}

QString Widget::bytesToHexString(const QByteArray &bytes) const
{
    QStringList parts;
    parts.reserve(bytes.size());
    for (char ch : bytes) {
        parts << QString("%1").arg(static_cast<quint8>(ch), 2, 16, QLatin1Char('0')).toUpper();
    }
    return parts.join(' ');
}

void Widget::setupWorkerThread()
{
    m_workerThread = new QThread(this);
    m_worker = new ModbusRtuWorker();
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(this,
            &Widget::requestOpenPort,
            m_worker,
            &ModbusRtuWorker::initializePort,
            Qt::QueuedConnection);
    connect(this,
            &Widget::requestClosePort,
            m_worker,
            &ModbusRtuWorker::closePort,
            Qt::QueuedConnection);
    connect(this,
            &Widget::requestReadHoldingRegisters,
            m_worker,
            &ModbusRtuWorker::readHoldingRegisters,
            Qt::QueuedConnection);

    connect(m_worker, &ModbusRtuWorker::portOpened, this, &Widget::onPortOpened);
    connect(m_worker, &ModbusRtuWorker::portClosed, this, &Widget::onPortClosed);
    connect(m_worker, &ModbusRtuWorker::frameReceived, this, &Widget::onFrameReceived);
    connect(m_worker, &ModbusRtuWorker::errorOccurred, this, &Widget::onWorkerError);
    connect(m_worker, &ModbusRtuWorker::txFrameReady, this, &Widget::onTxFrameReady);

    m_workerThread->start();
}

void Widget::refreshPortList(bool keepCurrent)
{
    const QString current = ui->comboPortName->currentText().trimmed();
    ui->comboPortName->clear();

    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        ui->comboPortName->addItem(info.portName());
    }

    if (keepCurrent && !current.isEmpty()) {
        int index = ui->comboPortName->findText(current, Qt::MatchFixedString);
        if (index >= 0) {
            ui->comboPortName->setCurrentIndex(index);
        } else {
            ui->comboPortName->setEditText(current);
        }
    } else if (ui->comboPortName->count() > 0) {
        ui->comboPortName->setCurrentIndex(0);
    }

    ui->labelStatus->setText(QString("状态: 已枚举串口 %1 个").arg(ports.size()));
}

void Widget::updateResultUi(const ParsedValues &values)
{
    ui->valueDoRaw->setText(QString("%1 %2").arg(values.doN).arg(values.doD));
    ui->valueTempRaw->setText(QString("%1 %2").arg(values.tempM).arg(values.tempE));
    ui->valueDo->setText(QString::number(values.doValue, 'f', 3) + " mg/L");
    ui->valueTemp->setText(QString::number(values.tempValue, 'f', 3) + " ℃");
}
