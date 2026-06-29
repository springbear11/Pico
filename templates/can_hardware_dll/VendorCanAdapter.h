#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

struct CanOpenOptions {
    int deviceIndex = 0;
    int channelIndex = 0;
    int arbitrationBitrate = 500000;
    int dataBitrate = 2000000;
    bool canFd = false;
    bool listenOnly = false;
};

struct CanFrame {
    quint32 id = 0;
    QByteArray data;
    bool extended = false;
    bool remote = false;
    bool canFd = false;
    bool bitrateSwitch = false;
    quint64 timestampUs = 0;
};

enum class CanReceiveStatus {
    Received,
    Timeout,
    Error
};

// Replace only this class implementation when adapting a vendor SDK.
// PicoATE JSON, error mapping, session locking, and ABI code stay unchanged.
class VendorCanAdapter {
public:
    bool open(const CanOpenOptions& options, QString& errorMessage);
    void close();
    bool isOpen() const;
    QString deviceDescription() const;

    bool transmit(const CanFrame& frame, QString& errorMessage);
    CanReceiveStatus receive(quint32 filterId,
                             quint32 filterMask,
                             int timeoutMs,
                             CanFrame& frame,
                             QString& errorMessage);

private:
    bool m_open = false;
    CanOpenOptions m_options;
    void* m_vendorHandle = nullptr;

#if PICOATE_CAN_TEMPLATE_SIMULATION
    QList<CanFrame> m_loopbackFrames;
#endif
};
