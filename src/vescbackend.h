#ifndef VESCBACKEND_H
#define VESCBACKEND_H

#include <QObject>
#include <QVariantMap>
#include <QStringList>
#include <QSettings>
#include <QTimer>

class VescBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(double voltage READ voltage NOTIFY voltageChanged)
    Q_PROPERTY(double speedKph READ speedKph NOTIFY speedKphChanged)
    Q_PROPERTY(double voltageDelta READ voltageDelta NOTIFY voltageChanged)

public:
    explicit VescBackend(QObject *parent = nullptr);

    QString status() const;
    QString deviceName() const;

    double voltage() const;
    double speedKph() const;
    double voltageDelta() const;

    Q_INVOKABLE void connectToDevice(const QString &deviceId);
    Q_INVOKABLE void refresh();

    void requestVescValues();

public slots:
    void handleNotify(QString interface, QVariantMap changed, QStringList invalidated);


signals:
    void statusChanged();
    void deviceNameChanged();
    void voltageChanged();
    void speedKphChanged();

private:
    QString m_status;
    QString m_deviceName;
    QString m_deviceAddress;
    double m_voltage = 0.0;
    double m_previousVoltage = 0.0;
    double m_voltageDelta = 0.0;
    double m_speedKph = 0.0;

    QTimer *m_pollTimer = nullptr;

    void sendFwVersionRequest();
    void sendFwVersionRequestCan(int canId);
    void requestVescValuesCan(int canId);
    void setStatus(const QString &value);
    void setDeviceName(const QString &value);
    void setVoltage(double value);
    void setSpeedKph(double value);
    void loadSettings();
    void saveSettings();
};

#endif
