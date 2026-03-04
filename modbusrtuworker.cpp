#include "modbusrtuworker.h"

#include <QSerialPort>

ModbusRtuWorker::ModbusRtuWorker(QObject *parent)
    : QObject(parent)
    , m_serial(new QSerialPort(this))
    , m_timeoutMs(300)
{
}

ModbusRtuWorker::~ModbusRtuWorker()
{
    closePort();
}

void ModbusRtuWorker::initializePort(const QString &portName,
                                     int baudRate,
                                     int dataBits,
                                     int parity,
                                     int stopBits,
                                     int timeoutMs)
{
    if (m_serial->isOpen()) {
        m_serial->close();
    }

    m_serial->setPortName(portName);
    m_serial->setBaudRate(baudRate);
    m_serial->setDataBits(static_cast<QSerialPort::DataBits>(dataBits));
    m_serial->setParity(static_cast<QSerialPort::Parity>(parity));
    m_serial->setStopBits(static_cast<QSerialPort::StopBits>(stopBits));
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    m_timeoutMs = timeoutMs;

    if (!m_serial->open(QIODevice::ReadWrite)) {
        emit portOpened(false, QString("串口打开失败: %1").arg(m_serial->errorString()));
        return;
    }

    emit portOpened(true, QString("串口已打开: %1").arg(portName));
}

void ModbusRtuWorker::closePort()
{
    if (m_serial->isOpen()) {
        m_serial->close();
    }
    emit portClosed();
}

void ModbusRtuWorker::readHoldingRegisters(quint8 slaveAddress, quint16 startAddress, quint16 quantity)
{
    if (!m_serial->isOpen()) {
        emit errorOccurred("串口未打开");
        return;
    }

    const QByteArray request = buildReadHoldingRegistersFrame(slaveAddress, startAddress, quantity);
    emit txFrameReady(request);

    m_serial->clear(QSerialPort::AllDirections);
    if (m_serial->write(request) != request.size()) {
        emit errorOccurred(QString("发送失败: %1").arg(m_serial->errorString()));
        return;
    }

    if (!m_serial->waitForBytesWritten(m_timeoutMs)) {
        emit errorOccurred(QString("发送超时: %1").arg(m_serial->errorString()));
        return;
    }

    QByteArray response;
    int expectedLength = -1;
    const int loopCount = 6;

    for (int i = 0; i < loopCount; ++i) {
        if (!m_serial->waitForReadyRead(m_timeoutMs)) {
            break;
        }
        response += m_serial->readAll();

        while (m_serial->waitForReadyRead(10)) {
            response += m_serial->readAll();
        }

        if (response.size() >= 3 && expectedLength < 0) {
            const quint8 byteCount = static_cast<quint8>(response.at(2));
            expectedLength = 3 + byteCount + 2;
        }

        if (expectedLength > 0 && response.size() >= expectedLength) {
            response = response.left(expectedLength);
            break;
        }
    }

    if (response.isEmpty()) {
        emit errorOccurred("接收超时: 未收到应答帧");
        return;
    }

    emit frameReceived(response);
}

QByteArray ModbusRtuWorker::buildReadHoldingRegistersFrame(quint8 slaveAddress,
                                                           quint16 startAddress,
                                                           quint16 quantity) const
{
    QByteArray frame;
    frame.reserve(8);
    frame.append(static_cast<char>(slaveAddress));
    frame.append(static_cast<char>(0x03));
    frame.append(static_cast<char>((startAddress >> 8) & 0xFF));
    frame.append(static_cast<char>(startAddress & 0xFF));
    frame.append(static_cast<char>((quantity >> 8) & 0xFF));
    frame.append(static_cast<char>(quantity & 0xFF));

    const quint16 crc = crc16Modbus(frame, frame.size());
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

quint16 ModbusRtuWorker::crc16Modbus(const QByteArray &data, int length) const
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < length; ++i) {
        crc ^= static_cast<quint8>(data.at(i));
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
