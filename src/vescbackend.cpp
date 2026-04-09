#include "vescbackend.h"

#include <QDebug>
#include <QProcess>
#include <QTimer>

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusVariant>

namespace {

// BlueZ device paths use underscores instead of colons.
QString bluezDevicePath(const QString &address)
{
    QString pathAddr = address;
    pathAddr.replace(":", "_");
    return QString("/org/bluez/hci0/dev_%1").arg(pathAddr);
}

// From your bluetoothctl attribute flags:
//   char0029 / 6e400002 -> write, write-without-response
//   char002c / 6e400003 -> notify
//
// So for THIS board / THIS stack:
//   app writes to char0029
//   app listens on char002c

QString nusWritePath(const QString &address)
{
    return bluezDevicePath(address) + "/service0028/char0029";
}

QString nusNotifyPath(const QString &address)
{
    return bluezDevicePath(address) + "/service0028/char002c";
}

quint16 crc16Ccitt(const QByteArray &data)
{
    quint16 crc = 0;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= (quint16(quint8(data.at(i))) << 8);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

} // namespace

VescBackend::VescBackend(QObject *parent)
    : QObject(parent)
{
    m_status = QStringLiteral("Idle");
    m_deviceName = QStringLiteral("None");

    loadSettings();

    if (!m_deviceAddress.isEmpty()) {
        setDeviceName(m_deviceAddress);
        qDebug() << "Restored device from settings:" << m_deviceAddress;
    }
}

QString VescBackend::status() const
{
    return m_status;
}

QString VescBackend::deviceName() const
{
    return m_deviceName;
}

double VescBackend::voltage() const
{
    return m_voltage;
}

double VescBackend::voltageDelta() const
{
    return m_voltageDelta;
}

double VescBackend::speedKph() const
{
    return m_speedKph;
}

void VescBackend::connectToDevice(const QString &deviceId)
{
    qDebug() << "Floatwatch selected device:" << deviceId;

    m_deviceAddress = deviceId;
    saveSettings();

    setDeviceName(deviceId);
    setStatus(QStringLiteral("Connecting"));


    QProcess *process = new QProcess(this);

    connect(process, &QProcess::started, this, []() {
        qDebug() << "bluetoothctl process started";
    });

    connect(process,
            static_cast<void (QProcess::*)(QProcess::ProcessError)>(&QProcess::errorOccurred),
            this,
            [process](QProcess::ProcessError error) {
        qDebug() << "bluetoothctl process error:" << error;
        qDebug() << "bluetoothctl error string:" << process->errorString();
    });

    connect(process,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            [process](int exitCode, QProcess::ExitStatus exitStatus) {
        qDebug() << "bluetoothctl finished exitCode:" << exitCode
                 << "exitStatus:" << exitStatus;
        qDebug() << "bluetoothctl remaining stdout:" << process->readAllStandardOutput();
        qDebug() << "bluetoothctl remaining stderr:" << process->readAllStandardError();
        process->deleteLater();
    });

    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process]() {
        const QByteArray out = process->readAllStandardOutput();
        qDebug() << out;

        if (!(out.contains("Connection successful") || out.contains("Connected: yes"))) {
            return;
        }

        setStatus(QStringLiteral("Connected"));

        const QString devicePath = bluezDevicePath(m_deviceAddress);

        QTimer *pollTimer = new QTimer(this);
        pollTimer->setInterval(300);

        connect(pollTimer, &QTimer::timeout, this, [this, pollTimer, devicePath]() {
            QDBusInterface deviceIface(
                "org.bluez",
                devicePath,
                "org.freedesktop.DBus.Properties",
                QDBusConnection::systemBus()
            );

            if (!deviceIface.isValid()) {
                qDebug() << "Device properties interface invalid for path:" << devicePath;
                return;
            }

            QDBusMessage reply = deviceIface.call(
                "Get",
                "org.bluez.Device1",
                "ServicesResolved"
            );

            if (reply.type() == QDBusMessage::ErrorMessage) {
                qDebug() << "ServicesResolved query error:" << reply.errorMessage();
                return;
            }

            if (reply.arguments().isEmpty()) {
                qDebug() << "ServicesResolved query returned no arguments";
                return;
            }

            QDBusVariant dbusVar = qvariant_cast<QDBusVariant>(reply.arguments().at(0));
            bool servicesResolved = dbusVar.variant().toBool();

            qDebug() << "ServicesResolved:" << servicesResolved;

            if (!servicesResolved) {
                return;
            }

            pollTimer->stop();
            pollTimer->deleteLater();

            const QString notifyPath = nusNotifyPath(m_deviceAddress);
            QDBusConnection bus = QDBusConnection::systemBus();

            bus.disconnect(
                "org.bluez",
                notifyPath,
                "org.freedesktop.DBus.Properties",
                "PropertiesChanged",
                this,
                SLOT(handleNotify(QString,QVariantMap,QStringList))
            );

            bool ok = bus.connect(
                "org.bluez",
                notifyPath,
                "org.freedesktop.DBus.Properties",
                "PropertiesChanged",
                this,
                SLOT(handleNotify(QString,QVariantMap,QStringList))
            );

            qDebug() << "UART RX listener attached:" << ok;
            qDebug() << "UART notify path:" << notifyPath;

            if (!ok) {
                setStatus(QStringLiteral("RX listener failed"));
                return;
            }

            QDBusInterface uartNotify(
                "org.bluez",
                notifyPath,
                "org.bluez.GattCharacteristic1",
                bus
            );

            if (!uartNotify.isValid()) {
                qDebug() << "UART notify interface invalid for path:" << notifyPath;
                setStatus(QStringLiteral("Notify interface invalid"));
                return;
            }

            QDBusMessage notifyReply = uartNotify.call("StartNotify");
            if (notifyReply.type() == QDBusMessage::ErrorMessage) {
                qDebug() << "StartNotify error:" << notifyReply.errorMessage();
                setStatus(QStringLiteral("StartNotify failed"));
                return;
            }

            qDebug() << "UART notifications enabled";

            // Clean up existing poll timer if any
            if (m_pollTimer) {
                m_pollTimer->stop();
                m_pollTimer->deleteLater();
            }

            // Create fresh poll timer
            m_pollTimer = new QTimer(this);
            m_pollTimer->setInterval(200);

            connect(m_pollTimer, &QTimer::timeout,
                    this, &VescBackend::requestVescValues);

            m_pollTimer->start();

            qDebug() << "Polling started (200 ms)";

            QTimer::singleShot(1000, this, [this]() {
                requestVescValuesCan(64);
            });
        });

        pollTimer->start();
    });

    connect(process, &QProcess::readyReadStandardError, this,
            [process]() {
        qDebug() << process->readAllStandardError();
    });


    qDebug() << "Starting /usr/bin/bluetoothctl connect" << deviceId;
    process->start("/usr/bin/bluetoothctl", QStringList() << "connect" << deviceId);
}

void VescBackend::refresh()
{
    if (m_deviceAddress.isEmpty()) {
        setStatus(QStringLiteral("No device selected"));
        return;
    }

    qDebug() << "Refresh: forcing reconnect";

    connectToDevice(m_deviceAddress);
}

void VescBackend::sendFwVersionRequest()
{
    if (m_deviceAddress.isEmpty()) {
        qDebug() << "No device address available for FW version request";
        return;
    }

    QByteArray packet;
    packet.append(char(0x02));
    packet.append(char(0x01));
    packet.append(char(0x00));  // COMM_FW_VERSION
    packet.append(char(0x00));  // CRC high
    packet.append(char(0x00));  // CRC low
    packet.append(char(0x03));

    const QString writePath = nusWritePath(m_deviceAddress);

//    qDebug() << "FW write path:" << writePath;
//    qDebug() << "FW packet hex:" << packet.toHex();

    QDBusInterface uartWrite(
        "org.bluez",
        writePath,
        "org.bluez.GattCharacteristic1",
        QDBusConnection::systemBus()
    );

    if (!uartWrite.isValid()) {
        qDebug() << "UART write interface invalid for path:" << writePath;
        return;
    }

    QVariantMap options;
    options.insert("type", "command");

    QList<QVariant> args;
    args << QVariant(packet)
         << QVariant(options);

    QDBusMessage reply =
        uartWrite.callWithArgumentList(QDBus::Block, "WriteValue", args);

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qDebug() << "FW WriteValue error:" << reply.errorMessage();
        return;
    }

//    qDebug() << "FW version request sent";
}

void VescBackend::sendFwVersionRequestCan(int canId)
{
    if (m_deviceAddress.isEmpty()) {
        qDebug() << "No device address available for CAN FW version request";
        return;
    }

    QByteArray payload;
    payload.append(char(0x22));               // COMM_FORWARD_CAN
    payload.append(char(canId & 0xFF));       // target CAN ID
    payload.append(char(0x00));               // COMM_FW_VERSION

    const quint16 crc = crc16Ccitt(payload);

    QByteArray packet;
    packet.append(char(0x02));                // short frame start
    packet.append(char(payload.size()));      // payload length
    packet.append(payload);
    packet.append(char((crc >> 8) & 0xFF));   // CRC high
    packet.append(char(crc & 0xFF));          // CRC low
    packet.append(char(0x03));                // frame end

    const QString writePath = nusWritePath(m_deviceAddress);

    qDebug() << "CAN FW write path:" << writePath;
    qDebug() << "CAN FW target ID:" << canId;
    qDebug() << "CAN FW packet hex:" << packet.toHex();

    QDBusInterface uartWrite(
        "org.bluez",
        writePath,
        "org.bluez.GattCharacteristic1",
        QDBusConnection::systemBus()
    );

    if (!uartWrite.isValid()) {
        qDebug() << "UART write interface invalid for path:" << writePath;
        return;
    }

    QVariantMap options;
    options.insert("type", "command");

    QList<QVariant> args;
    args << QVariant(packet)
         << QVariant(options);

    QDBusMessage reply =
        uartWrite.callWithArgumentList(QDBus::Block, "WriteValue", args);

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qDebug() << "CAN FW WriteValue error:" << reply.errorMessage();
        return;
    }

    qDebug() << "CAN-forwarded FW version request sent";
}

void VescBackend::requestVescValuesCan(int canId)
{
    if (m_deviceAddress.isEmpty()) {
        qDebug() << "No device address available for CAN GET_VALUES request";
        return;
    }

    QByteArray payload;
    payload.append(char(0x22));               // COMM_FORWARD_CAN
    payload.append(char(canId & 0xFF));       // target CAN ID
    payload.append(char(0x04));               // COMM_GET_VALUES

    const quint16 crc = crc16Ccitt(payload);

    QByteArray packet;
    packet.append(char(0x02));                // short frame start
    packet.append(char(payload.size()));      // payload length
    packet.append(payload);
    packet.append(char((crc >> 8) & 0xFF));   // CRC high
    packet.append(char(crc & 0xFF));          // CRC low
    packet.append(char(0x03));                // frame end

    const QString writePath = nusWritePath(m_deviceAddress);

    qDebug() << "CAN GET_VALUES write path:" << writePath;
    qDebug() << "CAN GET_VALUES target ID:" << canId;
    qDebug() << "CAN GET_VALUES packet hex:" << packet.toHex();

    QDBusInterface uartWrite(
        "org.bluez",
        writePath,
        "org.bluez.GattCharacteristic1",
        QDBusConnection::systemBus()
    );

    if (!uartWrite.isValid()) {
        qDebug() << "UART write interface invalid for path:" << writePath;
        return;
    }

    QVariantMap options;
    options.insert("type", "command");

    QList<QVariant> args;
    args << QVariant(packet)
         << QVariant(options);

    QDBusMessage reply =
        uartWrite.callWithArgumentList(QDBus::Block, "WriteValue", args);

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qDebug() << "CAN GET_VALUES WriteValue error:" << reply.errorMessage();
        return;
    }

    qDebug() << "CAN-forwarded GET_VALUES request sent";
}

void VescBackend::setStatus(const QString &value)
{
    if (m_status == value)
        return;

    m_status = value;
    emit statusChanged();
}

void VescBackend::setDeviceName(const QString &value)
{
    if (m_deviceName == value)
        return;

    m_deviceName = value;
    emit deviceNameChanged();
}

void VescBackend::setVoltage(double value)
{
    // Always compute delta
    m_voltageDelta = value - m_voltage;
    m_previousVoltage = m_voltage;

    // Only update stored voltage if changed
    if (!qFuzzyCompare(m_voltage, value)) {
        m_voltage = value;
    }

    // ALWAYS notify so UI updates (including delta = 0)
    emit voltageChanged();
}

void VescBackend::setSpeedKph(double value)
{
    if (qFuzzyCompare(m_speedKph, value))
        return;

    m_speedKph = value;
    emit speedKphChanged();
}

void VescBackend::requestVescValues()
{
    if (m_deviceAddress.isEmpty()) {
        qDebug() << "No device address available for VESC request";
        return;
    }

    const QString writePath = nusWritePath(m_deviceAddress);

    QDBusInterface uartWrite(
        "org.bluez",
        writePath,
        "org.bluez.GattCharacteristic1",
        QDBusConnection::systemBus()
    );

    if (!uartWrite.isValid()) {
        qDebug() << "UART write interface invalid for path:" << writePath;
        return;
    }

    auto sendPacket = [&](const QByteArray &packet, const char *label) {
        Q_UNUSED(label)
        Q_UNUSED(packet)

        QVariantMap options;
        options.insert("type", "command");

        QList<QVariant> args;
        args << QVariant(packet)
             << QVariant(options);

        QDBusMessage reply =
            uartWrite.callWithArgumentList(QDBus::Block, "WriteValue", args);

        if (reply.type() == QDBusMessage::ErrorMessage) {
            qDebug() << label << "WriteValue error:" << reply.errorMessage();
            return false;
        }

//        qDebug() << label << "sent";
        return true;
    };

    // COMM_ALIVE = 0x1E
    QByteArray alivePacket;
    alivePacket.append(char(0x02));
    alivePacket.append(char(0x01));
    alivePacket.append(char(0x1E));
    alivePacket.append(char(0xD4));
    alivePacket.append(char(0xC0));
    alivePacket.append(char(0x03));

    // COMM_GET_VALUES = 0x04
    QByteArray valuesPacket;
    valuesPacket.append(char(0x02));
    valuesPacket.append(char(0x01));
    valuesPacket.append(char(0x04));
    valuesPacket.append(char(0x40));
    valuesPacket.append(char(0x84));
    valuesPacket.append(char(0x03));

    if (!sendPacket(alivePacket, "COMM_ALIVE")) {
        return;
    }

    QTimer::singleShot(200, this, [this]() {
        const QString writePath2 = nusWritePath(m_deviceAddress);

        QDBusInterface uartWrite2(
            "org.bluez",
            writePath2,
            "org.bluez.GattCharacteristic1",
            QDBusConnection::systemBus()
        );

        if (!uartWrite2.isValid()) {
            qDebug() << "UART write interface invalid for path:" << writePath2;
            return;
        }

        QByteArray valuesPacket2;
        valuesPacket2.append(char(0x02));
        valuesPacket2.append(char(0x01));
        valuesPacket2.append(char(0x04));
        valuesPacket2.append(char(0x40));
        valuesPacket2.append(char(0x84));
        valuesPacket2.append(char(0x03));

//        qDebug() << "COMM_GET_VALUES" << valuesPacket2.toHex();

        QVariantMap options;
        options.insert("type", "command");

        QList<QVariant> args;
        args << QVariant(valuesPacket2)
             << QVariant(options);

        QDBusMessage reply =
            uartWrite2.callWithArgumentList(QDBus::Block, "WriteValue", args);

        if (reply.type() == QDBusMessage::ErrorMessage) {
            qDebug() << "COMM_GET_VALUES WriteValue error:" << reply.errorMessage();
            return;
        }

//        qDebug() << "COMM_GET_VALUES sent";
    });
}

void VescBackend::handleNotify(QString interface,
                               QVariantMap changed,
                               QStringList invalidated)
{
    Q_UNUSED(invalidated)

    qDebug() << "handleNotify interface:" << interface;
    qDebug() << "handleNotify changed keys:" << changed.keys();

    if (interface != "org.bluez.GattCharacteristic1")
        return;

    const bool hasValue = changed.contains("Value");
    QVariant valueVar = changed.value("Value");

    qDebug() << "handleNotify Value present:" << hasValue;
    qDebug() << "handleNotify Value type:" << valueVar.typeName();
    qDebug() << "handleNotify raw Value:" << valueVar;

    if (!hasValue)
        return;

    QByteArray data;

    if (valueVar.canConvert<QByteArray>()) {
        data = valueVar.toByteArray();
    } else if (valueVar.type() == QVariant::List) {
        const QVariantList list = valueVar.toList();
        for (int i = 0; i < list.size(); ++i) {
            data.append(char(list.at(i).toUInt()));
        }
    } else {
        qDebug() << "handleNotify: unsupported Value format";
        return;
    }

    qDebug() << "UART RX len:" << data.size();
    qDebug() << "UART RX:" << data.toHex();

    // Minimal decode for short-frame COMM_GET_VALUES reply
    if (data.size() >= 31 &&
        quint8(data.at(0)) == 0x02 &&
        quint8(data.at(2)) == 0x04) {

        auto readInt16BE = [](const QByteArray &ba, int offset) -> qint16 {
            return qint16((quint8(ba[offset]) << 8) | quint8(ba[offset + 1]));
        };

        const qint16 vinTenths = readInt16BE(data, 29);
        const double vin = double(vinTenths) / 10.0;

        qDebug() << "Decoded voltage:" << vin;
        setVoltage(vin);
    }
}

void VescBackend::loadSettings()
{
    QSettings settings;
    m_deviceAddress = settings.value("vesc/deviceAddress").toString();

    qDebug() << "Loaded device from settings:" << m_deviceAddress;
}

void VescBackend::saveSettings()
{
    QSettings settings;
    settings.setValue("vesc/deviceAddress", m_deviceAddress);

    qDebug() << "Saved device to settings:" << m_deviceAddress;
}
