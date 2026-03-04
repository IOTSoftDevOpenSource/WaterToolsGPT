#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QVector>

class QThread;
class ModbusRtuWorker;

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void onParseClicked();
    void onLoadExampleClicked();
    void onOpenPortClicked();
    void onClosePortClicked();
    void onRefreshPortsClicked();
    void onReadRegistersClicked();
    void onPortOpened(bool ok, const QString &message);
    void onPortClosed();
    void onFrameReceived(const QByteArray &frame);
    void onWorkerError(const QString &message);
    void onTxFrameReady(const QByteArray &frame);

private:
    struct ParsedValues {
        quint16 doN = 0;
        quint16 doD = 0;
        quint16 tempM = 0;
        quint16 tempE = 0;
        double doValue = 0.0;
        double tempValue = 0.0;
    };

    bool parseHexFrame(const QString &text, QVector<quint8> &bytes, QString &error) const;
    QString bytesToHexString(const QByteArray &bytes) const;
    quint16 crc16Modbus(const QVector<quint8> &data, int length) const;
    bool parseDoAndTemperature(const QVector<quint8> &frame, ParsedValues &values, QString &error) const;
    bool parseDoAndTemperature(const QByteArray &frame, ParsedValues &values, QString &error) const;
    void setupWorkerThread();
    void refreshPortList(bool keepCurrent = true);
    void updateResultUi(const ParsedValues &values);

    Ui::Widget *ui;
    QThread *m_workerThread;
    ModbusRtuWorker *m_worker;
    bool m_portOpened;

signals:
    void requestOpenPort(const QString &portName,
                         int baudRate,
                         int dataBits,
                         int parity,
                         int stopBits,
                         int timeoutMs);
    void requestClosePort();
    void requestReadHoldingRegisters(quint8 slaveAddress, quint16 startAddress, quint16 quantity);
};

#endif // WIDGET_H
