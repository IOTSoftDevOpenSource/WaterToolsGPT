#ifndef MODBUSRTUWORKER_H
#define MODBUSRTUWORKER_H

#include <QObject>
#include <QByteArray>

class QSerialPort;

class ModbusRtuWorker : public QObject
{
    Q_OBJECT

public:
    explicit ModbusRtuWorker(QObject *parent = nullptr);
    ~ModbusRtuWorker();

public slots:
    void initializePort(const QString &portName,
                        int baudRate,
                        int dataBits,
                        int parity,
                        int stopBits,
                        int timeoutMs);
    void closePort();
    void readHoldingRegisters(quint8 slaveAddress, quint16 startAddress, quint16 quantity);

signals:
    void portOpened(bool ok, const QString &message);
    void portClosed();
    void frameReceived(const QByteArray &frame);
    void errorOccurred(const QString &message);
    void txFrameReady(const QByteArray &frame);

private:
    QByteArray buildReadHoldingRegistersFrame(quint8 slaveAddress, quint16 startAddress, quint16 quantity) const;
    quint16 crc16Modbus(const QByteArray &data, int length) const;

    QSerialPort *m_serial;
    int m_timeoutMs;
};

#endif // MODBUSRTUWORKER_H
