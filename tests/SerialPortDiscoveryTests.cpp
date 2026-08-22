#include <QtTest/QtTest>

#include "WindowsSerialPortDiscovery.h"

#include <QUuid>

#include <cwchar>
#include <string>

using namespace PicoATE::Core::Internal;

namespace {

class RegistryTree final {
public:
    RegistryTree()
        : m_subKey((QStringLiteral("Software\\PicoATE.SerialPortDiscoveryTests.")
                    + QUuid::createUuid().toString(QUuid::WithoutBraces)).toStdWString())
    {
    }

    ~RegistryTree()
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, m_subKey.c_str());
    }

    const wchar_t* subKey() const { return m_subKey.c_str(); }

private:
    std::wstring m_subKey;
};

void setStringValue(HKEY key, const wchar_t* name, const wchar_t* value)
{
    const auto bytes = static_cast<DWORD>((std::wcslen(value) + 1) * sizeof(wchar_t));
    QCOMPARE(RegSetValueExW(key,
                            name,
                            0,
                            REG_SZ,
                            reinterpret_cast<const BYTE*>(value),
                            bytes),
             static_cast<LSTATUS>(ERROR_SUCCESS));
}

} // namespace

class SerialPortDiscoveryTests : public QObject {
    Q_OBJECT

private slots:
    void normalizesAndNaturallySortsPortNames();
    void readsBackslashValueNamesWithNativeRegistryApi();
};

void SerialPortDiscoveryTests::normalizesAndNaturallySortsPortNames()
{
    const auto names = normalizeSerialPortNames({
        QStringLiteral(" COM10 "),
        QStringLiteral("COM2"),
        QStringLiteral("com2"),
        QString(),
        QStringLiteral("COM1")
    });

    QCOMPARE(names, QStringList({QStringLiteral("COM1"),
                                 QStringLiteral("COM2"),
                                 QStringLiteral("COM10")}));
}

void SerialPortDiscoveryTests::readsBackslashValueNamesWithNativeRegistryApi()
{
    RegistryTree registryTree;
    HKEY key = nullptr;
    QCOMPARE(RegCreateKeyExW(HKEY_CURRENT_USER,
                             registryTree.subKey(),
                             0,
                             nullptr,
                             REG_OPTION_NON_VOLATILE,
                             KEY_SET_VALUE,
                             nullptr,
                             &key,
                             nullptr),
             static_cast<LSTATUS>(ERROR_SUCCESS));

    setStringValue(key, L"\\Device\\Silabser0", L"COM5");
    setStringValue(key, L"\\Device\\Virtual10", L"COM10");
    setStringValue(key, L"\\Device\\Virtual2", L"COM2");
    setStringValue(key, L"\\Device\\Duplicate", L" com5 ");
    setStringValue(key, L"\\Device\\Empty", L"");
    RegCloseKey(key);

    const auto result = serialPortNamesFromRegistry(HKEY_CURRENT_USER,
                                                     registryTree.subKey());
    QCOMPARE(result.status, static_cast<LSTATUS>(ERROR_SUCCESS));
    QCOMPARE(result.portNames,
             QStringList({QStringLiteral("COM2"),
                          QStringLiteral("COM5"),
                          QStringLiteral("COM10")}));
}

QTEST_APPLESS_MAIN(SerialPortDiscoveryTests)
#include "SerialPortDiscoveryTests.moc"
