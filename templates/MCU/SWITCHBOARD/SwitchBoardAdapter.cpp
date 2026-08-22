#include "SwitchBoardAdapter.h"

#include "PicoATE/Plugin/PluginLog.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace PicoATE::Plugins::Mcu {

namespace {

constexpr std::uint8_t FunctionReadHoldingRegisters = 0x03;
constexpr std::uint8_t FunctionWriteMultipleRegisters = 0x10;
constexpr std::uint8_t FunctionInitializeAllIo = 0x39;
constexpr std::uint16_t FirstIoRegister = 0x0000;
constexpr std::uint16_t IoRegisterCount = 7;
constexpr std::uint16_t DeviceRegisterCount = 9;
constexpr std::uint16_t IoChannelsPerRegister = 16;
constexpr std::uint16_t TotalIoChannels = IoRegisterCount * IoChannelsPerRegister;

std::uint16_t readU16(const std::uint8_t* bytes)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
}

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

std::uint16_t crc16(const std::uint8_t* bytes, std::size_t size)
{
    std::uint16_t crc = 0xFFFF;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0
                ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001)
                : static_cast<std::uint16_t>(crc >> 1);
        }
    }
    return crc;
}

std::string serialPath(const std::string& endpoint)
{
    if (endpoint.rfind("\\\\.\\", 0) == 0) return endpoint;
    if (endpoint.size() >= 3 &&
        (endpoint[0] == 'C' || endpoint[0] == 'c') &&
        (endpoint[1] == 'O' || endpoint[1] == 'o') &&
        (endpoint[2] == 'M' || endpoint[2] == 'm')) {
        return "\\\\.\\" + endpoint;
    }
    return endpoint;
}

std::string frameText(const std::vector<std::uint8_t>& frame)
{
    std::string text;
    for (const auto byte : frame) {
        if (!text.empty()) text.push_back(' ');
        text += std::format("{:02X}", static_cast<unsigned int>(byte));
    }
    return text;
}

std::string windowsError(const char* operation)
{
    return std::format("{} failed or timed out (Windows error={})",
                       operation,
                       static_cast<unsigned long>(GetLastError()));
}

std::string exceptionText(std::uint8_t code)
{
    switch (code) {
    case 0x01: return "Illegal function";
    case 0x02: return "Illegal data address";
    case 0x03: return "Illegal data value";
    case 0x04: return "Device failure";
    case 0x05: return "Acknowledge";
    case 0x06: return "Device busy";
    default:
        return std::format("Vendor exception 0x{:02X}",
                           static_cast<unsigned int>(code));
    }
}

Plugin::Json registersToJson(const std::vector<std::uint16_t>& registers)
{
    auto values = Plugin::Json::array();
    for (const auto value : registers) values.push_back(value);
    return values;
}

Plugin::Json statesToJson(const std::vector<std::uint16_t>& registers)
{
    auto states = Plugin::Json::array();
    for (const auto value : registers) {
        for (std::uint16_t bit = 0; bit < IoChannelsPerRegister; ++bit) {
            states.push_back((value & (1U << bit)) != 0);
        }
    }
    return states;
}

std::size_t onCount(const std::vector<std::uint16_t>& registers)
{
    std::size_t count = 0;
    for (const auto value : registers) {
        for (std::uint16_t bit = 0; bit < IoChannelsPerRegister; ++bit) {
            if ((value & (1U << bit)) != 0) ++count;
        }
    }
    return count;
}

class SwitchBoardAdapter final : public IMcuAdapter
{
public:
    ~SwitchBoardAdapter() override { close(); }

    Result open(const std::string& endpoint,
                std::uint8_t defaultUnitId,
                int ioTimeoutMs) override;
    void close() noexcept override;
    bool isOpen() const noexcept override;
    Result readAllIo(std::uint8_t unitId) override;
    Result readIoBank(std::uint8_t unitId, std::uint8_t bank) override;
    Result writeIoBank(std::uint8_t unitId,
                       std::uint8_t bank,
                       std::uint16_t value) override;
    Result setIo(std::uint8_t unitId,
                 std::uint16_t channel,
                 bool state) override;
    Result initializeAllIo(std::uint8_t initializationUnitId) override;

private:
    Result exchangeLocked(std::uint8_t unitId,
                          const std::vector<std::uint8_t>& pdu,
                          std::vector<std::uint8_t>& responsePdu);
    Result readRegistersLocked(std::uint8_t unitId,
                               std::uint16_t address,
                               std::uint16_t count,
                               std::vector<std::uint16_t>& registers);
    Result writeRegistersLocked(std::uint8_t unitId,
                                std::uint16_t address,
                                const std::vector<std::uint16_t>& registers);
    bool writeAllLocked(const std::uint8_t* bytes, std::size_t size);
    bool readExactLocked(std::uint8_t* bytes, std::size_t size);
    void closeLocked() noexcept;

    HANDLE m_handle = INVALID_HANDLE_VALUE;
    int m_ioTimeoutMs = 5000;
    std::string m_endpoint;
    mutable std::mutex m_mutex;
};

Result SwitchBoardAdapter::open(const std::string& endpoint,
                                std::uint8_t defaultUnitId,
                                int ioTimeoutMs)
{
    std::scoped_lock lock(m_mutex);
    closeLocked();
    if (endpoint.empty()) {
        return Result::failed("InvalidMcuEndpoint", "Serial endpoint is empty");
    }
    if (ioTimeoutMs < 100 || ioTimeoutMs > 60000) {
        return Result::failed("InvalidMcuTimeout", "I/O timeout must be 100..60000 ms");
    }

    const auto path = serialPath(endpoint);
    const auto handle = CreateFileA(path.c_str(),
                                    GENERIC_READ | GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    OPEN_EXISTING,
                                    0,
                                    nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return Result::failed("McuSerialOpenFailed", windowsError("CreateFile"));
    }

    DCB state{};
    state.DCBlength = sizeof(state);
    if (!GetCommState(handle, &state)) {
        const auto message = windowsError("GetCommState");
        CloseHandle(handle);
        return Result::failed("McuSerialConfigureFailed", message);
    }
    state.BaudRate = CBR_19200;
    state.ByteSize = 8;
    state.Parity = NOPARITY;
    state.StopBits = ONESTOPBIT;
    state.fBinary = TRUE;
    state.fParity = FALSE;
    state.fDtrControl = DTR_CONTROL_ENABLE;
    state.fRtsControl = RTS_CONTROL_DISABLE;
    state.fOutX = FALSE;
    state.fInX = FALSE;
    state.fOutxCtsFlow = FALSE;
    state.fOutxDsrFlow = FALSE;
    if (!SetCommState(handle, &state)) {
        const auto message = windowsError("SetCommState");
        CloseHandle(handle);
        return Result::failed("McuSerialConfigureFailed", message);
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(ioTimeoutMs);
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(ioTimeoutMs);
    if (!SetCommTimeouts(handle, &timeouts)) {
        const auto message = windowsError("SetCommTimeouts");
        CloseHandle(handle);
        return Result::failed("McuSerialConfigureFailed", message);
    }

    SetupComm(handle, 4096, 4096);
    PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
    m_handle = handle;
    m_ioTimeoutMs = ioTimeoutMs;
    m_endpoint = endpoint;
    PicoATE_Log("MCU_SWITCHBOARD_SERIAL connected endpoint={} format=19200-8-N-1 defaultUnitId=0x{:02X} timeoutMs={}",
                endpoint,
                static_cast<unsigned int>(defaultUnitId),
                ioTimeoutMs);
    return Result::passed({
        {"connected", true},
        {"endpoint", endpoint},
        {"baudRate", 19200},
        {"dataBits", 8},
        {"parity", "none"},
        {"stopBits", 1},
        {"unitId", defaultUnitId},
    });
}

void SwitchBoardAdapter::close() noexcept
{
    std::scoped_lock lock(m_mutex);
    closeLocked();
}

bool SwitchBoardAdapter::isOpen() const noexcept
{
    std::scoped_lock lock(m_mutex);
    return m_handle != INVALID_HANDLE_VALUE;
}

Result SwitchBoardAdapter::readAllIo(std::uint8_t unitId)
{
    std::scoped_lock lock(m_mutex);
    std::vector<std::uint16_t> registers;
    const auto result = readRegistersLocked(unitId,
                                            FirstIoRegister,
                                            IoRegisterCount,
                                            registers);
    if (!result.success) return result;
    const auto values = registersToJson(registers);
    const auto states = statesToJson(registers);
    return Result::passed({
        {"unitId", unitId},
        {"firstRegister", FirstIoRegister},
        {"registerCount", IoRegisterCount},
        {"registers", values.dump()},
        {"ioStates", states.dump()},
        {"onCount", onCount(registers)},
        {"channelCount", TotalIoChannels},
    });
}

Result SwitchBoardAdapter::readIoBank(std::uint8_t unitId, std::uint8_t bank)
{
    if (bank < 1 || bank > IoRegisterCount) {
        return Result::failed("InvalidMcuBank", "Bank must be 1..7");
    }
    std::scoped_lock lock(m_mutex);
    const auto address = static_cast<std::uint16_t>(bank - 1);
    std::vector<std::uint16_t> registers;
    const auto result = readRegistersLocked(unitId, address, 1, registers);
    if (!result.success) return result;
    const auto firstChannel = static_cast<std::uint16_t>(address * 16 + 1);
    const auto states = statesToJson(registers);
    return Result::passed({
        {"unitId", unitId},
        {"bank", bank},
        {"registerAddress", address},
        {"firstChannel", firstChannel},
        {"lastChannel", firstChannel + 15},
        {"value", registers.front()},
        {"states", states.dump()},
    });
}

Result SwitchBoardAdapter::writeIoBank(std::uint8_t unitId,
                                       std::uint8_t bank,
                                       std::uint16_t value)
{
    if (bank < 1 || bank > IoRegisterCount) {
        return Result::failed("InvalidMcuBank", "Bank must be 1..7");
    }
    std::scoped_lock lock(m_mutex);
    const auto address = static_cast<std::uint16_t>(bank - 1);
    const auto writeResult = writeRegistersLocked(unitId, address, {value});
    if (!writeResult.success) return writeResult;
    std::vector<std::uint16_t> readback;
    const auto readResult = readRegistersLocked(unitId, address, 1, readback);
    if (!readResult.success) return readResult;
    if (readback.front() != value) {
        return Result::failed(
            "McuReadbackMismatch",
            std::format("Bank {} readback mismatch: wrote 0x{:04X}, read 0x{:04X}",
                        bank,
                        static_cast<unsigned int>(value),
                        static_cast<unsigned int>(readback.front())));
    }
    return Result::passed({
        {"unitId", unitId},
        {"bank", bank},
        {"registerAddress", address},
        {"value", value},
        {"readback", readback.front()},
        {"verified", true},
    });
}

Result SwitchBoardAdapter::setIo(std::uint8_t unitId,
                                 std::uint16_t channel,
                                 bool state)
{
    if (channel < 1 || channel > TotalIoChannels) {
        return Result::failed("InvalidMcuChannel", "Channel must be 1..112");
    }
    std::scoped_lock lock(m_mutex);
    const auto zeroBasedChannel = static_cast<std::uint16_t>(channel - 1);
    const auto address = static_cast<std::uint16_t>(zeroBasedChannel / 16);
    const auto bit = static_cast<std::uint16_t>(zeroBasedChannel % 16);
    std::vector<std::uint16_t> current;
    const auto readResult = readRegistersLocked(unitId, address, 1, current);
    if (!readResult.success) return readResult;
    const auto mask = static_cast<std::uint16_t>(1U << bit);
    const auto original = current.front();
    const auto updated = state
        ? static_cast<std::uint16_t>(original | mask)
        : static_cast<std::uint16_t>(original & ~mask);
    const auto writeResult = writeRegistersLocked(unitId, address, {updated});
    if (!writeResult.success) return writeResult;
    std::vector<std::uint16_t> readback;
    const auto verifyResult = readRegistersLocked(unitId, address, 1, readback);
    if (!verifyResult.success) return verifyResult;
    if (readback.front() != updated) {
        return Result::failed(
            "McuReadbackMismatch",
            std::format("IO {} readback mismatch: wrote 0x{:04X}, read 0x{:04X}",
                        channel,
                        static_cast<unsigned int>(updated),
                        static_cast<unsigned int>(readback.front())));
    }
    return Result::passed({
        {"unitId", unitId},
        {"channel", channel},
        {"state", state},
        {"bank", address + 1},
        {"bit", bit},
        {"registerAddress", address},
        {"originalValue", original},
        {"value", updated},
        {"readback", readback.front()},
        {"verified", true},
    });
}

Result SwitchBoardAdapter::initializeAllIo(std::uint8_t initializationUnitId)
{
    std::scoped_lock lock(m_mutex);
    const std::vector<std::uint8_t> request{
        FunctionInitializeAllIo,
        0x01,
        0x02,
        0x03,
        0x04,
    };
    std::vector<std::uint8_t> response;
    const auto result = exchangeLocked(initializationUnitId, request, response);
    if (!result.success) return result;
    if (response != request) {
        return Result::failed(
            "InvalidMcuInitializeResponse",
            "FC39 response did not echo 39 01 02 03 04");
    }
    return Result::passed({
        {"initializationUnitId", initializationUnitId},
        {"functionCode", "0x39"},
        {"responseVerified", true},
        {"stateVerified", false},
    });
}

Result SwitchBoardAdapter::readRegistersLocked(
    std::uint8_t unitId,
    std::uint16_t address,
    std::uint16_t count,
    std::vector<std::uint16_t>& registers)
{
    if (address > 6 || count == 0 || address + count > IoRegisterCount) {
        return Result::failed(
            "InvalidMcuRegisterRange",
            "Switch-board register range must stay within 0x0000..0x0006");
    }
    std::vector<std::uint8_t> request{FunctionReadHoldingRegisters};
    appendU16(request, address);
    appendU16(request, count);
    std::vector<std::uint8_t> response;
    const auto result = exchangeLocked(unitId, request, response);
    if (!result.success) return result;
    const auto requestedBytes = static_cast<std::size_t>(count * 2);
    const auto returnedBytes = static_cast<std::size_t>(response[1]);
    if (response[0] != FunctionReadHoldingRegisters ||
        returnedBytes < requestedBytes ||
        returnedBytes % 2 != 0 ||
        response.size() != returnedBytes + 2) {
        return Result::failed(
            "InvalidMcuResponse",
            "Unexpected FC03 response length or byte count");
    }
    if (returnedBytes > requestedBytes) {
        PicoATE_Log("MCU_SWITCHBOARD_FC03 returnedRegisters={} requestedRegisters={} behavior=fixed-tail-response",
                    returnedBytes / 2,
                    count);
    }
    registers.clear();
    registers.reserve(count);
    for (std::size_t index = 2; index < 2 + requestedBytes; index += 2) {
        registers.push_back(readU16(&response[index]));
    }
    return Result::passed();
}

Result SwitchBoardAdapter::writeRegistersLocked(
    std::uint8_t unitId,
    std::uint16_t address,
    const std::vector<std::uint16_t>& registers)
{
    if (registers.empty() || registers.size() > IoRegisterCount ||
        address > 6 || address + registers.size() > IoRegisterCount) {
        return Result::failed(
            "InvalidMcuRegisterRange",
            "Switch-board write range must stay within 0x0000..0x0006");
    }
    std::vector<std::uint8_t> request{FunctionWriteMultipleRegisters};
    appendU16(request, address);
    appendU16(request, static_cast<std::uint16_t>(registers.size()));
    request.push_back(static_cast<std::uint8_t>(registers.size() * 2));
    for (const auto value : registers) appendU16(request, value);
    std::vector<std::uint8_t> response;
    const auto result = exchangeLocked(unitId, request, response);
    if (!result.success) return result;
    if (response.size() != 5 ||
        response[0] != FunctionWriteMultipleRegisters ||
        readU16(&response[1]) != address ||
        readU16(&response[3]) != registers.size()) {
        return Result::failed(
            "InvalidMcuResponse",
            "Unexpected FC10 response address or register count");
    }
    return Result::passed();
}

Result SwitchBoardAdapter::exchangeLocked(
    std::uint8_t unitId,
    const std::vector<std::uint8_t>& pdu,
    std::vector<std::uint8_t>& responsePdu)
{
    if (m_handle == INVALID_HANDLE_VALUE) {
        return Result::failed("McuNotConnected", "Serial port is not open");
    }
    if (pdu.empty()) {
        return Result::failed("InvalidMcuRequest", "Request PDU is empty");
    }
    std::vector<std::uint8_t> frame{unitId};
    frame.insert(frame.end(), pdu.begin(), pdu.end());
    const auto checksum = crc16(frame.data(), frame.size());
    frame.push_back(static_cast<std::uint8_t>(checksum));
    frame.push_back(static_cast<std::uint8_t>(checksum >> 8));

    PurgeComm(m_handle, PURGE_RXABORT | PURGE_RXCLEAR);
    PicoATE_Log("MCU_SWITCHBOARD_TX endpoint={} frame={}",
                m_endpoint,
                frameText(frame));
    if (!writeAllLocked(frame.data(), frame.size())) {
        const auto message = windowsError("WriteFile");
        closeLocked();
        return Result::failed("McuSerialWriteFailed", message);
    }

    std::vector<std::uint8_t> response(2);
    if (!readExactLocked(response.data(), response.size())) {
        const auto message = windowsError("ReadFile header");
        closeLocked();
        return Result::failed("McuSerialReadFailed", message);
    }
    if (response[0] != unitId) {
        closeLocked();
        return Result::failed(
            "InvalidMcuResponse",
            "Response unit ID does not match request");
    }
    if (response[1] == static_cast<std::uint8_t>(pdu[0] | 0x80)) {
        response.resize(5);
        if (!readExactLocked(response.data() + 2, 3)) {
            const auto message = windowsError("ReadFile exception");
            closeLocked();
            return Result::failed("McuSerialReadFailed", message);
        }
    } else if (response[1] == FunctionReadHoldingRegisters &&
               pdu[0] == FunctionReadHoldingRegisters) {
        std::uint8_t byteCount = 0;
        if (!readExactLocked(&byteCount, 1)) {
            const auto message = windowsError("ReadFile byte count");
            closeLocked();
            return Result::failed("McuSerialReadFailed", message);
        }
        if (byteCount > DeviceRegisterCount * 2) {
            closeLocked();
            return Result::failed(
                "InvalidMcuResponse",
                "FC03 response byte count exceeds 18");
        }
        response.push_back(byteCount);
        response.resize(3 + byteCount + 2);
        if (!readExactLocked(response.data() + 3, byteCount + 2)) {
            const auto message = windowsError("ReadFile data");
            closeLocked();
            return Result::failed("McuSerialReadFailed", message);
        }
    } else if (response[1] == pdu[0] &&
               (pdu[0] == FunctionWriteMultipleRegisters ||
                pdu[0] == FunctionInitializeAllIo)) {
        response.resize(8);
        if (!readExactLocked(response.data() + 2, 6)) {
            const auto message = windowsError("ReadFile echo");
            closeLocked();
            return Result::failed("McuSerialReadFailed", message);
        }
    } else {
        closeLocked();
        return Result::failed(
            "InvalidMcuResponse",
            "Response function code does not match request");
    }

    const auto receivedCrc = static_cast<std::uint16_t>(
        response[response.size() - 2] |
        (static_cast<std::uint16_t>(response.back()) << 8));
    const auto expectedCrc = crc16(response.data(), response.size() - 2);
    if (receivedCrc != expectedCrc) {
        const auto message = std::format(
            "CRC mismatch: received 0x{:04X}, expected 0x{:04X}",
            static_cast<unsigned int>(receivedCrc),
            static_cast<unsigned int>(expectedCrc));
        closeLocked();
        return Result::failed("McuCrcError", message);
    }

    PicoATE_Log("MCU_SWITCHBOARD_RX endpoint={} frame={}",
                m_endpoint,
                frameText(response));
    if (response[1] == static_cast<std::uint8_t>(pdu[0] | 0x80)) {
        const auto code = response[2];
        return Result::failed(
            std::format("McuException0x{:02X}", static_cast<unsigned int>(code)),
            exceptionText(code));
    }
    responsePdu.assign(response.begin() + 1, response.end() - 2);
    return Result::passed();
}

bool SwitchBoardAdapter::writeAllLocked(const std::uint8_t* bytes,
                                        std::size_t size)
{
    while (size > 0) {
        DWORD written = 0;
        if (!WriteFile(m_handle,
                       bytes,
                       static_cast<DWORD>(size),
                       &written,
                       nullptr) || written == 0) {
            return false;
        }
        bytes += written;
        size -= written;
    }
    return true;
}

bool SwitchBoardAdapter::readExactLocked(std::uint8_t* bytes, std::size_t size)
{
    while (size > 0) {
        DWORD received = 0;
        if (!ReadFile(m_handle,
                      bytes,
                      static_cast<DWORD>(size),
                      &received,
                      nullptr) || received == 0) {
            return false;
        }
        bytes += received;
        size -= received;
    }
    return true;
}

void SwitchBoardAdapter::closeLocked() noexcept
{
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
    m_endpoint.clear();
}

} // namespace

std::unique_ptr<IMcuAdapter> createMcuAdapter()
{
    return std::make_unique<SwitchBoardAdapter>();
}

Plugin::Json pluginDescription()
{
    return Plugin::Json::parse(R"json(
{
  "schema": "picoate.plugin",
  "schemaVersion": 1,
  "pluginId": "picoate.mcu.switchboard",
  "moduleId": "plugin.mcu.switchboard",
  "name": "ATE MCU Switch Board",
  "category": "MCU",
  "connectionKinds": ["serialPort"],
  "vendor": "ATE",
  "version": "1.1.0",
  "functions": [
    {
      "id": "open",
      "name": "Open Switch Board",
      "description": "Open the RS485 Modbus RTU port using fixed 19200-8-N-1 settings, then initialize all IO with FC39 address 0x10.",
      "timeoutMs": 10000,
      "inputs": [
        {"key":"address","name":"Serial Port","type":"string","default":"COM3"},
        {"key":"unitId","name":"Unit ID","type":"string","default":"0x00"},
        {"key":"ioTimeoutMs","name":"I/O Timeout","type":"integer","default":5000,"minimum":100,"maximum":60000,"unit":"ms"}
      ],
      "outputs": [
        {"key":"connected","name":"Connected","type":"boolean"},
        {"key":"endpoint","name":"Serial Port","type":"string"},
        {"key":"unitId","name":"Unit ID","type":"integer"},
        {"key":"initialized","name":"Initialized","type":"boolean"}
      ]
    },
    {
      "id": "readAllIo",
      "name": "Read All 112 IO",
      "description": "FC03 read registers 0x0000..0x0006. Bit0 is the first IO in each bank.",
      "timeoutMs": 5000,
      "inputs": [
        {"key":"unitId","name":"Unit ID","type":"string","default":"0x00"}
      ],
      "outputs": [
        {"key":"registers","name":"Registers (JSON Array)","type":"string"},
        {"key":"ioStates","name":"IO States 1..112 (JSON Array)","type":"string"},
        {"key":"onCount","name":"ON Count","type":"integer"},
        {"key":"channelCount","name":"Channel Count","type":"integer"}
      ]
    },
    {
      "id": "readIoBank",
      "name": "Read IO Bank",
      "description": "FC03 read one 16-channel bank. Bank 1 maps to IO1..IO16.",
      "timeoutMs": 5000,
      "inputs": [
        {"key":"unitId","name":"Unit ID","type":"string","default":"0x00"},
        {"key":"bank","name":"Bank","type":"enum","default":1,"options":[{"label":"Bank 1 (IO1-IO16)","value":1},{"label":"Bank 2 (IO17-IO32)","value":2},{"label":"Bank 3 (IO33-IO48)","value":3},{"label":"Bank 4 (IO49-IO64)","value":4},{"label":"Bank 5 (IO65-IO80)","value":5},{"label":"Bank 6 (IO81-IO96)","value":6},{"label":"Bank 7 (IO97-IO112)","value":7}]}
      ],
      "outputs": [
        {"key":"value","name":"Register Value","type":"integer"},
        {"key":"states","name":"Bank States (JSON Array)","type":"string"},
        {"key":"firstChannel","name":"First Channel","type":"integer"},
        {"key":"lastChannel","name":"Last Channel","type":"integer"}
      ]
    },
    {
      "id": "writeIoBank",
      "name": "Write IO Bank",
      "description": "FC10 write one 16-channel bank and verify it with FC03 readback.",
      "timeoutMs": 10000,
      "inputs": [
        {"key":"unitId","name":"Unit ID","type":"string","default":"0x00"},
        {"key":"bank","name":"Bank","type":"enum","default":1,"options":[{"label":"Bank 1 (IO1-IO16)","value":1},{"label":"Bank 2 (IO17-IO32)","value":2},{"label":"Bank 3 (IO33-IO48)","value":3},{"label":"Bank 4 (IO49-IO64)","value":4},{"label":"Bank 5 (IO65-IO80)","value":5},{"label":"Bank 6 (IO81-IO96)","value":6},{"label":"Bank 7 (IO97-IO112)","value":7}]},
        {"key":"value","name":"Register Value","type":"string","default":"0x0000"}
      ],
      "outputs": [
        {"key":"value","name":"Written Value","type":"integer"},
        {"key":"readback","name":"Readback Value","type":"integer"},
        {"key":"verified","name":"Readback Verified","type":"boolean"}
      ]
    },
    {
      "id": "setIo",
      "name": "Set Single IO",
      "description": "Read-modify-write one IO channel with FC03/FC10 and verify the full register readback.",
      "timeoutMs": 10000,
      "inputs": [
        {"key":"unitId","name":"Unit ID","type":"string","default":"0x00"},
        {"key":"channel","name":"IO Channel","type":"integer","default":1,"minimum":1,"maximum":112},
        {"key":"state","name":"Output State","type":"enum","default":true,"options":[{"label":"ON","value":true},{"label":"OFF","value":false}]}
      ],
      "outputs": [
        {"key":"channel","name":"IO Channel","type":"integer"},
        {"key":"state","name":"Output State","type":"boolean"},
        {"key":"originalValue","name":"Original Register Value","type":"integer"},
        {"key":"readback","name":"Readback Value","type":"integer"},
        {"key":"verified","name":"Readback Verified","type":"boolean"}
      ]
    },
    {
      "id": "close",
      "name": "Close Switch Board",
      "description": "Close the serial port.",
      "stepKind": "cleanup",
      "timeoutMs": 3000,
      "inputs": [],
      "outputs": [
        {"key":"connected","name":"Connected","type":"boolean"}
      ]
    }
  ]
}
)json");
}

} // namespace PicoATE::Plugins::Mcu
