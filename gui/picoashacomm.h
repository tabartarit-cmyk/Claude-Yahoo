#ifndef PICOASHACOMM_H
#define PICOASHACOMM_H

#include <QObject>
#include <QByteArray>
#include <QFile>
#include <QSerialPort>
#include <QString>
#include <QStringListModel>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <asha_comms.hpp>
#include "picoashamainwindow.h"

class PicoAshaComm : public QObject
{
    Q_OBJECT

public:
    explicit PicoAshaComm(QObject *parent = nullptr);
    virtual ~PicoAshaComm();

    void showUI() const;

    QString paFirmwareVers() const;
    void setPaFirmwareVers(const QString &newPaFirmwareVers);

    QString remoteError() const;
    void setRemoteError(const QString &newRemoteError);

    QString errMsg() const;
    void setErrMsg(const QString &newErrMsg);

private:
    void handleDecodedData(QByteArray const& decoded);
    void handleEventPacket(asha::comm::HeaderPacket const header, asha::comm::EventPacket const& pkt);
    bool checkError(asha::comm::HeaderPacket const header, asha::comm::EventPacket const& pkt, QString const& state);

    QString logHeader(asha::comm::HeaderPacket const header);
    bool appendLog(QString const& logLine);

    void closeSerial();

    bool sendCommandPacket(asha::comm::CmdPacket const& cmd_pkt);

    void writeHciPacket(const char* data, size_t len);

    // Automatic HCI log: HCI.log in the folder of PicoASHAGui.exe
    static QString defaultHciLogPath();
    bool openHciLog(QString const& path);
    void closeHciLog();
    bool startHciLogging();
    void stopHciLogging();
    void sendHciDumpCommand(bool enable);

    QSerialPort m_serial;
    QByteArray m_currPacket;

    QTimer connect_timer;
    QTimer intro_timer;

    PicoAshaMainWindow* m_ui;

    QFile m_hciLogFile;
    QFile m_logFile;
    bool m_logOpened;
    QTextStream m_logStream;

    bool m_serialConnected;

    QString m_paFirmwareVers;

    QString m_remoteError;

    QString m_hciLoggingPath;

    bool m_hciLoggingEnabled;

    // Number of automatic "HCI dump ON" requests which got no HCI packet back
    // (guard against a reboot loop of the Pico)
    int m_hciAutoEnableAttempts = 0;

    QString m_errMsg;

signals:

    void paFirmwareVersChanged();

    void remoteErrorChanged();

    void hciLoggingPathChanged();

    void hciLoggingEnabledChanged();

    void errMsgChanged();

public slots:
    void onConnectTimer();
    void onIntroTimer();
    void onSerialError(QSerialPort::SerialPortError error);
    void onSerialReadyRead();
    void onHciLogActionBtnClicked();
    void onCmdRestartBtnClicked();
    void onCmdConnAllowedBtnClicked(bool allowed);
    void onCmdStreamingEnabledBtnClicked(bool enabled);
    void onCmdRemoveBondBtnClicked();
    void onUsbSettingsBtnClicked(asha::comm::USBInfo const& usb_info);
    void onPairWithAddress(QByteArray const& addr, uint8_t addr_type);
};

#endif // PICOASHACOMM_H
