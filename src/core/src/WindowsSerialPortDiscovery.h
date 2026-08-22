#pragma once

#include <QStringList>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace PicoATE::Core::Internal {

QStringList normalizeSerialPortNames(QStringList names);

#ifdef Q_OS_WIN
struct WindowsSerialPortDiscoveryResult {
    QStringList portNames;
    LSTATUS status = ERROR_SUCCESS;

    bool ok() const { return status == ERROR_SUCCESS; }
};

WindowsSerialPortDiscoveryResult serialPortNamesFromRegistry(
    HKEY rootKey,
    const wchar_t* subKey);

WindowsSerialPortDiscoveryResult discoverWindowsSerialPortNames();
#endif

} // namespace PicoATE::Core::Internal
