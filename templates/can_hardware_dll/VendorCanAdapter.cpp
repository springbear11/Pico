#include "VendorCanAdapter.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>

bool VendorCanAdapter::open(const CanOpenOptions& options, QString& errorMessage)
{
    errorMessage.clear();
    if (m_open) {
        return true;
    }

#if PICOATE_CAN_TEMPLATE_SIMULATION
    m_options = options;
    m_open = true;
    return true;
#else
    // TODO(VENDOR SDK):
    // 1. Include the vendor header in this .cpp file.
    // 2. Enumerate/open options.deviceIndex.
    // 3. Initialize options.channelIndex with arbitrationBitrate/dataBitrate.
    // 4. Store the returned vendor handle in m_vendorHandle.
    // 5. Set m_open only after every SDK call succeeds.
    Q_UNUSED(options);
    errorMessage = "Vendor CAN SDK open() is not implemented";
    return false;
#endif
}

void VendorCanAdapter::close()
{
#if PICOATE_CAN_TEMPLATE_SIMULATION
    m_loopbackFrames.clear();
#else
    // TODO(VENDOR SDK): stop the channel, close m_vendorHandle, then null it.
#endif
    m_vendorHandle = nullptr;
    m_open = false;
}

bool VendorCanAdapter::isOpen() const
{
    return m_open;
}

QString VendorCanAdapter::deviceDescription() const
{
#if PICOATE_CAN_TEMPLATE_SIMULATION
    return "PicoATE software CAN loopback";
#else
    // TODO(VENDOR SDK): return serial number / model when the SDK provides it.
    return m_open ? QString("Vendor CAN device") : QString("Disconnected");
#endif
}

bool VendorCanAdapter::transmit(const CanFrame& frame, QString& errorMessage)
{
    errorMessage.clear();
    if (!m_open) {
        errorMessage = "CAN device is not open";
        return false;
    }

#if PICOATE_CAN_TEMPLATE_SIMULATION
    CanFrame echoed = frame;
    echoed.timestampUs = static_cast<quint64>(
        QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    m_loopbackFrames.push_back(echoed);
    return true;
#else
    // TODO(VENDOR SDK): map id/data/extended/remote/CAN-FD/BRS to the vendor
    // frame structure and call its transmit function.
    Q_UNUSED(frame);
    errorMessage = "Vendor CAN SDK transmit() is not implemented";
    return false;
#endif
}

CanReceiveStatus VendorCanAdapter::receive(quint32 filterId,
                                           quint32 filterMask,
                                           int timeoutMs,
                                           CanFrame& frame,
                                           QString& errorMessage)
{
    errorMessage.clear();
    if (!m_open) {
        errorMessage = "CAN device is not open";
        return CanReceiveStatus::Error;
    }

#if PICOATE_CAN_TEMPLATE_SIMULATION
    QElapsedTimer timer;
    timer.start();
    do {
        for (auto it = m_loopbackFrames.begin(); it != m_loopbackFrames.end(); ++it) {
            if ((it->id & filterMask) != (filterId & filterMask)) {
                continue;
            }
            frame = *it;
            m_loopbackFrames.erase(it);
            return CanReceiveStatus::Received;
        }
        QThread::msleep(1);
    } while (timer.elapsed() < timeoutMs);
    return CanReceiveStatus::Timeout;
#else
    // TODO(VENDOR SDK): receive until timeoutMs. Apply filterId/filterMask here
    // when the SDK cannot configure a hardware acceptance filter.
    Q_UNUSED(filterId);
    Q_UNUSED(filterMask);
    Q_UNUSED(timeoutMs);
    Q_UNUSED(frame);
    errorMessage = "Vendor CAN SDK receive() is not implemented";
    return CanReceiveStatus::Error;
#endif
}
