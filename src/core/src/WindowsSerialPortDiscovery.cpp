#include "WindowsSerialPortDiscovery.h"

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#include <vector>
#endif

namespace PicoATE::Core::Internal {

namespace {

bool comPortNumber(const QString& name, qulonglong* number)
{
    if (!name.startsWith(QStringLiteral("COM"), Qt::CaseInsensitive)
        || name.size() == 3) {
        return false;
    }

    bool ok = false;
    const auto parsed = name.sliced(3).toULongLong(&ok);
    if (ok && number) {
        *number = parsed;
    }
    return ok;
}

#ifdef Q_OS_WIN
class RegistryKey final {
public:
    ~RegistryKey()
    {
        if (m_key) {
            RegCloseKey(m_key);
        }
    }

    HKEY* address() { return &m_key; }
    HKEY get() const { return m_key; }

private:
    HKEY m_key = nullptr;
};
#endif

} // namespace

QStringList normalizeSerialPortNames(QStringList names)
{
    QStringList normalized;
    normalized.reserve(names.size());
    for (auto& name : names) {
        name = name.trimmed();
        if (!name.isEmpty() && !normalized.contains(name, Qt::CaseInsensitive)) {
            normalized.push_back(name);
        }
    }

    std::sort(normalized.begin(), normalized.end(), [](const QString& left,
                                                       const QString& right) {
        qulonglong leftNumber = 0;
        qulonglong rightNumber = 0;
        const bool leftIsCom = comPortNumber(left, &leftNumber);
        const bool rightIsCom = comPortNumber(right, &rightNumber);
        if (leftIsCom && rightIsCom && leftNumber != rightNumber) {
            return leftNumber < rightNumber;
        }
        if (leftIsCom != rightIsCom) {
            return leftIsCom;
        }
        const int insensitive = QString::compare(left, right, Qt::CaseInsensitive);
        return insensitive == 0 ? left < right : insensitive < 0;
    });
    return normalized;
}

#ifdef Q_OS_WIN
WindowsSerialPortDiscoveryResult serialPortNamesFromRegistry(
    HKEY rootKey,
    const wchar_t* subKey)
{
    WindowsSerialPortDiscoveryResult result;
    RegistryKey key;
    result.status = RegOpenKeyExW(rootKey, subKey, 0, KEY_QUERY_VALUE, key.address());
    if (result.status == ERROR_FILE_NOT_FOUND) {
        result.status = ERROR_SUCCESS;
        return result;
    }
    if (!result.ok()) {
        return result;
    }

    DWORD valueCount = 0;
    DWORD maximumValueNameLength = 0;
    DWORD maximumValueDataLength = 0;
    result.status = RegQueryInfoKeyW(key.get(),
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     &valueCount,
                                     &maximumValueNameLength,
                                     &maximumValueDataLength,
                                     nullptr,
                                     nullptr);
    if (!result.ok()) {
        return result;
    }

    std::vector<wchar_t> valueName(maximumValueNameLength + 1, L'\0');
    std::vector<wchar_t> valueData(
        maximumValueDataLength / sizeof(wchar_t) + 2, L'\0');
    QStringList names;

    for (DWORD index = 0; index < valueCount; ++index) {
        std::fill(valueName.begin(), valueName.end(), L'\0');
        std::fill(valueData.begin(), valueData.end(), L'\0');
        DWORD valueNameLength = static_cast<DWORD>(valueName.size());
        DWORD valueDataLength = static_cast<DWORD>(valueData.size() * sizeof(wchar_t));
        DWORD valueType = 0;
        const auto status = RegEnumValueW(key.get(),
                                          index,
                                          valueName.data(),
                                          &valueNameLength,
                                          nullptr,
                                          &valueType,
                                          reinterpret_cast<LPBYTE>(valueData.data()),
                                          &valueDataLength);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status != ERROR_SUCCESS) {
            result.status = status;
            return result;
        }
        if (valueType != REG_SZ && valueType != REG_EXPAND_SZ) {
            continue;
        }

        int characterCount = static_cast<int>(valueDataLength / sizeof(wchar_t));
        while (characterCount > 0 && valueData[characterCount - 1] == L'\0') {
            --characterCount;
        }
        names.push_back(QString::fromWCharArray(valueData.data(), characterCount));
    }

    result.portNames = normalizeSerialPortNames(std::move(names));
    return result;
}

WindowsSerialPortDiscoveryResult discoverWindowsSerialPortNames()
{
    return serialPortNamesFromRegistry(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DEVICEMAP\\SERIALCOMM");
}
#endif

} // namespace PicoATE::Core::Internal
