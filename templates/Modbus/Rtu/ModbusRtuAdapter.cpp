#include "ModbusAdapter.h"
#include "PicoATE/Plugin/PluginLog.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace PicoATE::Plugins::Modbus {
namespace {
constexpr std::uint8_t FC01 = 0x01, FC02 = 0x02, FC03 = 0x03, FC04 = 0x04;
constexpr std::uint8_t FC05 = 0x05, FC06 = 0x06, FC0F = 0x0F, FC10 = 0x10;

std::uint16_t readU16(const std::uint8_t* bytes) { return static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1]); }
void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) { bytes.push_back(static_cast<std::uint8_t>(value >> 8)); bytes.push_back(static_cast<std::uint8_t>(value)); }
std::uint16_t crc16(const std::uint8_t* data, std::size_t size)
{
    std::uint16_t crc = 0xFFFF;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) crc = (crc & 1) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001) : static_cast<std::uint16_t>(crc >> 1);
    }
    return crc;
}
std::string lower(std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return value; }
std::string frameText(const std::vector<std::uint8_t>& frame) { std::string text; for (auto byte : frame) { if (!text.empty()) text.push_back(' '); text += std::format("{:02X}", static_cast<unsigned int>(byte)); } return text; }
std::string winError(const char* operation) { return std::format("{} failed (Windows error={})", operation, static_cast<unsigned long>(GetLastError())); }
std::string serialPath(const std::string& endpoint) { if (endpoint.rfind("\\\\.\\", 0) == 0) return endpoint; if (lower(endpoint).rfind("com", 0) == 0) return "\\\\.\\" + endpoint; return endpoint; }
std::string exceptionText(std::uint8_t code)
{
    switch (code) { case 1: return "IllegalFunction"; case 2: return "IllegalDataAddress"; case 3: return "IllegalDataValue"; case 4: return "SlaveDeviceFailure"; case 5: return "Acknowledge"; case 6: return "SlaveDeviceBusy"; case 7: return "NegativeAcknowledge"; case 8: return "MemoryParityError"; case 10: return "GatewayPathUnavailable"; case 11: return "GatewayTargetDeviceFailedToRespond"; default: return std::format("VendorException0x{:02X}", static_cast<unsigned int>(code)); }
}
bool parseParity(const std::string& value, BYTE& result) { const auto text = lower(value); if (text.empty() || text == "none" || text == "n") { result = NOPARITY; return true; } if (text == "odd" || text == "o") { result = ODDPARITY; return true; } if (text == "even" || text == "e") { result = EVENPARITY; return true; } return false; }
bool parseStopBits(const std::string& value, BYTE& result) { const auto text = lower(value); if (text.empty() || text == "1") { result = ONESTOPBIT; return true; } if (text == "1.5") { result = ONE5STOPBITS; return true; } if (text == "2") { result = TWOSTOPBITS; return true; } return false; }
bool parseRts(const std::string& value, DWORD& result) { const auto text = lower(value); if (text.empty() || text == "disable") { result = RTS_CONTROL_DISABLE; return true; } if (text == "enable") { result = RTS_CONTROL_ENABLE; return true; } if (text == "handshake") { result = RTS_CONTROL_HANDSHAKE; return true; } return false; }

class ModbusRtuAdapter final : public IModbusAdapter
{
public:
    ~ModbusRtuAdapter() override { disconnect(); }
    Result connect(const std::string& endpoint, int slaveId, const Plugin::Json& options) override;
    void disconnect() noexcept override;
    bool isConnected() const noexcept override;
    Result readCoils(std::uint8_t, std::uint16_t, std::uint16_t) override;
    Result readDiscreteInputs(std::uint8_t, std::uint16_t, std::uint16_t) override;
    Result readHoldingRegisters(std::uint8_t, std::uint16_t, std::uint16_t) override;
    Result readInputRegisters(std::uint8_t, std::uint16_t, std::uint16_t) override;
    Result writeSingleCoil(std::uint8_t, std::uint16_t, bool) override;
    Result writeSingleRegister(std::uint8_t, std::uint16_t, std::uint16_t) override;
    Result writeMultipleCoils(std::uint8_t, std::uint16_t, const std::vector<bool>&) override;
    Result writeMultipleRegisters(std::uint8_t, std::uint16_t, const std::vector<std::uint16_t>&) override;
private:
    Result readBits(std::uint8_t, std::uint16_t, std::uint16_t, std::uint8_t, std::string_view);
    Result readRegisters(std::uint8_t, std::uint16_t, std::uint16_t, std::uint8_t, std::string_view);
    Result request(std::uint8_t, const std::vector<std::uint8_t>&, std::vector<std::uint8_t>&);
    bool writeAll(const std::uint8_t*, std::size_t);
    bool readExact(std::uint8_t*, std::size_t);
    Result serialFailure(const char*);
    void closePort() noexcept;
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    int m_ioTimeoutMs = 2000;
    std::string m_endpoint;
    mutable std::mutex m_mutex;
};

Result ModbusRtuAdapter::connect(const std::string& endpoint, int slaveId, const Plugin::Json& options)
{
    disconnect();
    if (endpoint.empty()) return Result::failed("InvalidModbusEndpoint", "Use a serial endpoint such as COM3 or \\\\.\\COM10");
    const auto baudRate = Plugin::numberValue<int>(options, "baudRate", 9600);
    const auto dataBits = Plugin::numberValue<int>(options, "dataBits", 8);
    m_ioTimeoutMs = Plugin::numberValue<int>(options, "ioTimeoutMs", 2000);
    const auto parityText = Plugin::stringValue(options, "parity", "none");
    const auto stopBitsText = Plugin::stringValue(options, "stopBits", "1");
    const auto rtsText = Plugin::stringValue(options, "rtsControl", "disable");
    if (baudRate <= 0 || dataBits < 5 || dataBits > 8 || m_ioTimeoutMs <= 0) return Result::failed("InvalidModbusSerialOptions", "baudRate and ioTimeoutMs must be positive; dataBits must be 5..8");
    BYTE parity = NOPARITY;
    BYTE stopBits = ONESTOPBIT;
    DWORD rtsControl = RTS_CONTROL_DISABLE;
    if (!parseParity(parityText, parity)) return Result::failed("InvalidModbusParity", "parity must be none, odd, or even");
    if (!parseStopBits(stopBitsText, stopBits)) return Result::failed("InvalidModbusStopBits", "stopBits must be 1, 1.5, or 2");
    if (!parseRts(rtsText, rtsControl)) return Result::failed("InvalidModbusRtsControl", "rtsControl must be disable, enable, or handshake");

    const auto handle = CreateFileA(serialPath(endpoint).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return Result::failed("ModbusSerialOpenFailed", winError("CreateFile"));
    DCB state{};
    state.DCBlength = sizeof(state);
    if (!GetCommState(handle, &state)) {
        const auto message = winError("GetCommState");
        CloseHandle(handle);
        return Result::failed("ModbusSerialConfigureFailed", message);
    }
    state.BaudRate = static_cast<DWORD>(baudRate);
    state.ByteSize = static_cast<BYTE>(dataBits);
    state.Parity = parity;
    state.StopBits = stopBits;
    state.fBinary = TRUE;
    state.fParity = parity != NOPARITY;
    state.fDtrControl = DTR_CONTROL_ENABLE;
    state.fRtsControl = rtsControl;
    state.fOutX = FALSE;
    state.fInX = FALSE;
    state.fOutxCtsFlow = FALSE;
    state.fOutxDsrFlow = FALSE;
    if (!SetCommState(handle, &state)) {
        const auto message = winError("SetCommState");
        CloseHandle(handle);
        return Result::failed("ModbusSerialConfigureFailed", message);
    }
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(m_ioTimeoutMs);
    timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(m_ioTimeoutMs);
    if (!SetCommTimeouts(handle, &timeouts)) {
        const auto message = winError("SetCommTimeouts");
        CloseHandle(handle);
        return Result::failed("ModbusSerialConfigureFailed", message);
    }
    SetupComm(handle, 4096, 4096);
    PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
    {
        std::scoped_lock lock(m_mutex);
        m_handle = handle;
        m_endpoint = endpoint;
    }
    PicoATE_Log("MODBUS_RTU connected endpoint={} baudRate={} dataBits={} parity={} stopBits={} rtsControl={} defaultUnitId=0x{:02X}", endpoint, baudRate, dataBits, parityText, stopBitsText, rtsText, static_cast<unsigned int>(slaveId & 0xFF));
    return Result::passed({{"connected", true}, {"endpoint", endpoint}, {"baudRate", baudRate}, {"dataBits", dataBits}, {"parity", parityText}, {"stopBits", stopBitsText}, {"rtsControl", rtsText}});
}

void ModbusRtuAdapter::disconnect() noexcept
{
    std::scoped_lock lock(m_mutex);
    closePort();
}

bool ModbusRtuAdapter::isConnected() const noexcept
{
    std::scoped_lock lock(m_mutex);
    return m_handle != INVALID_HANDLE_VALUE;
}

Result ModbusRtuAdapter::readCoils(std::uint8_t unitId, std::uint16_t address, std::uint16_t count) { return readBits(unitId, address, count, FC01, "Read Coils"); }
Result ModbusRtuAdapter::readDiscreteInputs(std::uint8_t unitId, std::uint16_t address, std::uint16_t count) { return readBits(unitId, address, count, FC02, "Read Discrete Inputs"); }
Result ModbusRtuAdapter::readHoldingRegisters(std::uint8_t unitId, std::uint16_t address, std::uint16_t count) { return readRegisters(unitId, address, count, FC03, "Read Holding Registers"); }
Result ModbusRtuAdapter::readInputRegisters(std::uint8_t unitId, std::uint16_t address, std::uint16_t count) { return readRegisters(unitId, address, count, FC04, "Read Input Registers"); }

Result ModbusRtuAdapter::writeSingleCoil(std::uint8_t unitId, std::uint16_t address, bool value)
{
    const auto encoded = static_cast<std::uint16_t>(value ? 0xFF00 : 0x0000);
    std::vector<std::uint8_t> response;
    const auto exchange = request(unitId, {FC05, static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address), static_cast<std::uint8_t>(encoded >> 8), static_cast<std::uint8_t>(encoded)}, response);
    if (!exchange.success) return exchange;
    if (response.size() != 5 || response[0] != FC05 || readU16(&response[1]) != address || readU16(&response[3]) != encoded) return Result::failed("InvalidModbusResponse", "Unexpected Write Single Coil response");
    return Result::passed({{"unitId", unitId}, {"address", address}, {"value", value}});
}

Result ModbusRtuAdapter::writeSingleRegister(std::uint8_t unitId, std::uint16_t address, std::uint16_t value)
{
    std::vector<std::uint8_t> response;
    const auto exchange = request(unitId, {FC06, static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address), static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)}, response);
    if (!exchange.success) return exchange;
    if (response.size() != 5 || response[0] != FC06 || readU16(&response[1]) != address || readU16(&response[3]) != value) return Result::failed("InvalidModbusResponse", "Unexpected Write Single Register response");
    return Result::passed({{"unitId", unitId}, {"address", address}, {"value", value}});
}

Result ModbusRtuAdapter::writeMultipleCoils(std::uint8_t unitId, std::uint16_t address, const std::vector<bool>& values)
{
    if (values.empty() || values.size() > 1968) return Result::failed("InvalidModbusValues", "Write Multiple Coils accepts 1..1968 values");
    std::vector<std::uint8_t> pdu{FC0F};
    appendU16(pdu, address);
    appendU16(pdu, static_cast<std::uint16_t>(values.size()));
    const auto byteCount = (values.size() + 7) / 8;
    pdu.push_back(static_cast<std::uint8_t>(byteCount));
    pdu.resize(pdu.size() + byteCount, 0);
    for (std::size_t index = 0; index < values.size(); ++index) if (values[index]) pdu[6 + index / 8] |= static_cast<std::uint8_t>(1U << (index % 8));
    std::vector<std::uint8_t> response;
    const auto exchange = request(unitId, pdu, response);
    if (!exchange.success) return exchange;
    if (response.size() != 5 || response[0] != FC0F || readU16(&response[1]) != address || readU16(&response[3]) != values.size()) return Result::failed("InvalidModbusResponse", "Unexpected Write Multiple Coils response");
    return Result::passed({{"unitId", unitId}, {"address", address}, {"count", values.size()}});
}

Result ModbusRtuAdapter::writeMultipleRegisters(std::uint8_t unitId, std::uint16_t address, const std::vector<std::uint16_t>& values)
{
    if (values.empty() || values.size() > 123) return Result::failed("InvalidModbusValues", "Write Multiple Registers accepts 1..123 values");
    std::vector<std::uint8_t> pdu{FC10};
    appendU16(pdu, address);
    appendU16(pdu, static_cast<std::uint16_t>(values.size()));
    pdu.push_back(static_cast<std::uint8_t>(values.size() * 2));
    for (const auto value : values) appendU16(pdu, value);
    std::vector<std::uint8_t> response;
    const auto exchange = request(unitId, pdu, response);
    if (!exchange.success) return exchange;
    if (response.size() != 5 || response[0] != FC10 || readU16(&response[1]) != address || readU16(&response[3]) != values.size()) return Result::failed("InvalidModbusResponse", "Unexpected Write Multiple Registers response");
    return Result::passed({{"unitId", unitId}, {"address", address}, {"count", values.size()}});
}

Result ModbusRtuAdapter::readBits(std::uint8_t unitId, std::uint16_t address, std::uint16_t count, std::uint8_t function, std::string_view functionName)
{
    if (count == 0 || count > 2000) return Result::failed("InvalidModbusCount", std::string(functionName) + " count must be 1..2000");
    std::vector<std::uint8_t> response;
    const auto exchange = request(unitId, {function, static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address), static_cast<std::uint8_t>(count >> 8), static_cast<std::uint8_t>(count)}, response);
    if (!exchange.success) return exchange;
    const auto byteCount = (count + 7) / 8;
    if (response.size() != byteCount + 2 || response[0] != function || response[1] != byteCount) return Result::failed("InvalidModbusResponse", "Unexpected " + std::string(functionName) + " response");
    auto values = Plugin::Json::array();
    for (std::uint16_t index = 0; index < count; ++index) values.push_back((response[2 + index / 8] & (1U << (index % 8))) != 0);
    return Result::passed({{"unitId", unitId}, {"address", address}, {"count", count}, {"values", values.dump()}});
}

Result ModbusRtuAdapter::readRegisters(std::uint8_t unitId, std::uint16_t address, std::uint16_t count, std::uint8_t function, std::string_view functionName)
{
    if (count == 0 || count > 125) return Result::failed("InvalidModbusCount", std::string(functionName) + " count must be 1..125");
    std::vector<std::uint8_t> response;
    const auto exchange = request(unitId, {function, static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address), static_cast<std::uint8_t>(count >> 8), static_cast<std::uint8_t>(count)}, response);
    if (!exchange.success) return exchange;
    if (response.size() != static_cast<std::size_t>(2 + count * 2) || response[0] != function || response[1] != count * 2) return Result::failed("InvalidModbusResponse", "Unexpected " + std::string(functionName) + " response");
    auto registers = Plugin::Json::array();
    for (std::size_t index = 2; index < response.size(); index += 2) registers.push_back(readU16(&response[index]));
    return Result::passed({{"unitId", unitId}, {"address", address}, {"count", count}, {"registers", registers.dump()}});
}

Result ModbusRtuAdapter::request(std::uint8_t unitId, const std::vector<std::uint8_t>& pdu, std::vector<std::uint8_t>& response)
{
    if (pdu.empty()) return Result::failed("InvalidModbusRequest", "Modbus PDU is empty");
    std::scoped_lock lock(m_mutex);
    if (m_handle == INVALID_HANDLE_VALUE) return Result::failed("ModbusNotConnected", "Serial port is not connected");
    std::vector<std::uint8_t> frame{unitId};
    frame.insert(frame.end(), pdu.begin(), pdu.end());
    const auto checksum = crc16(frame.data(), frame.size());
    frame.push_back(static_cast<std::uint8_t>(checksum));
    frame.push_back(static_cast<std::uint8_t>(checksum >> 8));
    PurgeComm(m_handle, PURGE_RXABORT | PURGE_RXCLEAR);
    PicoATE_Log("MODBUS_RTU_TX endpoint={} frame={}", m_endpoint, frameText(frame));
    if (!writeAll(frame.data(), frame.size())) return serialFailure("ModbusSerialWriteFailed");

    std::vector<std::uint8_t> received(2);
    if (!readExact(received.data(), received.size())) return serialFailure("ModbusSerialReadFailed");
    if (received[0] != unitId) { closePort(); return Result::failed("InvalidModbusResponse", "Modbus RTU response unit ID does not match request"); }
    if (received[1] == static_cast<std::uint8_t>(pdu[0] | 0x80)) {
        received.resize(5);
        if (!readExact(received.data() + 2, 3)) return serialFailure("ModbusSerialReadFailed");
    } else if (received[1] == pdu[0] && pdu[0] >= FC01 && pdu[0] <= FC04) {
        std::uint8_t byteCount = 0;
        if (!readExact(&byteCount, 1)) return serialFailure("ModbusSerialReadFailed");
        if (byteCount > 250) { closePort(); return Result::failed("InvalidModbusResponse", "Modbus RTU response byte count is invalid"); }
        received.push_back(byteCount);
        received.resize(3 + byteCount + 2);
        if (!readExact(received.data() + 3, byteCount + 2)) return serialFailure("ModbusSerialReadFailed");
    } else if (received[1] == pdu[0]) {
        received.resize(8);
        if (!readExact(received.data() + 2, 6)) return serialFailure("ModbusSerialReadFailed");
    } else {
        closePort();
        return Result::failed("InvalidModbusResponse", "Modbus RTU response function does not match request");
    }
    const auto receivedCrc = static_cast<std::uint16_t>(received[received.size() - 2] | (static_cast<std::uint16_t>(received.back()) << 8));
    const auto expectedCrc = crc16(received.data(), received.size() - 2);
    if (receivedCrc != expectedCrc) {
        const auto message = std::format("CRC mismatch (received=0x{:04X}, expected=0x{:04X})", receivedCrc, expectedCrc);
        closePort();
        return Result::failed("ModbusCrcError", message);
    }
    PicoATE_Log("MODBUS_RTU_RX endpoint={} frame={}", m_endpoint, frameText(received));
    if (received[1] == static_cast<std::uint8_t>(pdu[0] | 0x80)) {
        const auto code = received[2];
        return Result::failed(std::format("ModbusException0x{:02X}", static_cast<unsigned int>(code)), exceptionText(code));
    }
    response.assign(received.begin() + 1, received.end() - 2);
    return Result::passed();
}

bool ModbusRtuAdapter::writeAll(const std::uint8_t* bytes, std::size_t size)
{
    while (size > 0) {
        DWORD written = 0;
        if (!WriteFile(m_handle, bytes, static_cast<DWORD>(size), &written, nullptr) || written == 0) return false;
        bytes += written;
        size -= written;
    }
    return true;
}

bool ModbusRtuAdapter::readExact(std::uint8_t* bytes, std::size_t size)
{
    while (size > 0) {
        DWORD received = 0;
        if (!ReadFile(m_handle, bytes, static_cast<DWORD>(size), &received, nullptr) || received == 0) return false;
        bytes += received;
        size -= received;
    }
    return true;
}

Result ModbusRtuAdapter::serialFailure(const char* code)
{
    const auto message = winError(code);
    closePort();
    return Result::failed(code, message);
}

void ModbusRtuAdapter::closePort() noexcept
{
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
    m_endpoint.clear();
}

} // namespace

std::unique_ptr<IModbusAdapter> createModbusAdapter() { return std::make_unique<ModbusRtuAdapter>(); }

Plugin::Json pluginDescription()
{
    using Json = Plugin::Json;
    return {
        {"schema", "picoate.plugin"}, {"schemaVersion", 1},
        {"pluginId", "picoate.modbus.rtu"}, {"moduleId", "plugin.modbus.rtu"},
        {"name", "Modbus RTU"}, {"category", "MODBUS"},
        {"connectionKinds", Json::array({"serialPort"})}, {"vendor", "Generic"}, {"version", "1.0.0"},
        {"functions", Json::array({
            {{"id", "open"}, {"name", "Open Modbus RTU"}, {"timeoutMs", 5000},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Default Unit ID"}, {"type", "string"}, {"default", 0}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "baudRate"}, {"name", "Baud Rate"}, {"type", "integer"}, {"default", 9600}},
                 {{"key", "dataBits"}, {"name", "Data Bits"}, {"type", "enum"}, {"default", 8}, {"options", Json::array({
                     {{"label", "5"}, {"value", 5}},
                     {{"label", "6"}, {"value", 6}},
                     {{"label", "7"}, {"value", 7}},
                     {{"label", "8"}, {"value", 8}}
                 })}},
                 {{"key", "parity"}, {"name", "Parity"}, {"type", "enum"}, {"default", "none"}, {"options", Json::array({
                     {{"label", "None"}, {"value", "none"}},
                     {{"label", "Odd"}, {"value", "odd"}},
                     {{"label", "Even"}, {"value", "even"}}
                 })}},
                 {{"key", "stopBits"}, {"name", "Stop Bits"}, {"type", "enum"}, {"default", "1"}, {"options", Json::array({
                     {{"label", "1"}, {"value", "1"}},
                     {{"label", "1.5"}, {"value", "1.5"}},
                     {{"label", "2"}, {"value", "2"}}
                 })}},
                 {{"key", "rtsControl"}, {"name", "RTS Control"}, {"type", "enum"}, {"default", "disable"}, {"options", Json::array({
                     {{"label", "Disable"}, {"value", "disable"}},
                     {{"label", "Enable"}, {"value", "enable"}},
                     {{"label", "Handshake"}, {"value", "handshake"}}
                 })}},
                 {{"key", "ioTimeoutMs"}, {"name", "I/O Timeout"}, {"type", "integer"}, {"default", 2000}}
             })}, {"outputs", Json::array({{{"key", "connected"}, {"name", "Connected"}, {"type", "boolean"}}})}},
            {{"id", "readCoils"}, {"name", "Read Coils"}, {"description", "FC01"},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "count"}, {"name", "Coil Count"}, {"type", "integer"}, {"required", true}, {"minimum", 1}, {"maximum", 2000}}
             })}, {"outputs", Json::array({{{"key", "values"}, {"name", "Coil Values (JSON Array)"}, {"type", "string"}}})}},
            {{"id", "readDiscreteInputs"}, {"name", "Read Discrete Inputs"}, {"description", "FC02"},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "count"}, {"name", "Input Count"}, {"type", "integer"}, {"required", true}, {"minimum", 1}, {"maximum", 2000}}
             })}, {"outputs", Json::array({{{"key", "values"}, {"name", "Input Values (JSON Array)"}, {"type", "string"}}})}},
            {{"id", "readHoldingRegisters"}, {"name", "Read Holding Registers"}, {"description", "FC03"},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "count"}, {"name", "Register Count"}, {"type", "integer"}, {"required", true}, {"minimum", 1}, {"maximum", 125}}
             })}, {"outputs", Json::array({{{"key", "registers"}, {"name", "Registers (JSON Array)"}, {"type", "string"}}})}},
            {{"id", "readInputRegisters"}, {"name", "Read Input Registers"}, {"description", "FC04"},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "count"}, {"name", "Register Count"}, {"type", "integer"}, {"required", true}, {"minimum", 1}, {"maximum", 125}}
             })}, {"outputs", Json::array({{{"key", "registers"}, {"name", "Registers (JSON Array)"}, {"type", "string"}}})}},
            {{"id", "writeSingleCoil"}, {"name", "Write Single Coil"}, {"description", "FC05"},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "value"}, {"name", "Coil Value"}, {"type", "boolean"}, {"required", true}}
             })}, {"outputs", Json::array({{{"key", "value"}, {"name", "Coil Value"}, {"type", "boolean"}}})}},
            {{"id", "writeSingleRegister"}, {"name", "Write Single Register"}, {"description", "FC06"},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "value"}, {"name", "Register Value"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}}
             })}, {"outputs", Json::array({{{"key", "value"}, {"name", "Register Value"}, {"type", "integer"}}})}},
            {{"id", "writeMultipleCoils"}, {"name", "Write Multiple Coils"}, {"description", "FC0F"},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "values"}, {"name", "Coil Values (JSON Array)"}, {"type", "string"}, {"required", true}, {"description", "Example: [true, false, true]"}}
             })}, {"outputs", Json::array({{{"key", "count"}, {"name", "Written Coil Count"}, {"type", "integer"}}})}},
            {{"id", "writeMultipleRegisters"}, {"name", "Write Multiple Registers"},
             {"description", "FC10; default dataFormat=registers, optionally encode text"},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "dataFormat"}, {"name", "Data Format"}, {"type", "enum"}, {"default", "registers"}, {"options", Json::array({
                     {{"label", "Registers"}, {"value", "registers"}},
                     {{"label", "ASCII Text"}, {"value", "asciiText"}},
                     {{"label", "UTF-8 Text"}, {"value", "utf8Text"}}
                 })}},
                 {{"key", "values"}, {"name", "Register Values (JSON Array)"}, {"type", "string"}, {"required", true}, {"description", "Required when dataFormat=registers. Example: [1, 100, 0xFFFF]"}, {"visibleWhen", {{"key", "dataFormat"}, {"values", Json::array({"registers"})}}}},
                 {{"key", "text"}, {"name", "Text"}, {"type", "string"}, {"required", true}, {"description", "Required when dataFormat=asciiText or utf8Text. Variables such as ${var.serialNumber} are supported."}, {"visibleWhen", {{"key", "dataFormat"}, {"values", Json::array({"asciiText", "utf8Text"})}}}},
                 {{"key", "registerCount"}, {"name", "Register Count"}, {"type", "integer"}, {"required", true}, {"minimum", 1}, {"maximum", 123}, {"description", "Required for text formats; number of FC10 registers to write."}, {"visibleWhen", {{"key", "dataFormat"}, {"values", Json::array({"asciiText", "utf8Text"})}}}},
                 {{"key", "byteOrder"}, {"name", "Byte Order"}, {"type", "enum"}, {"default", "highByteFirst"}, {"visibleWhen", {{"key", "dataFormat"}, {"values", Json::array({"asciiText", "utf8Text"})}}}, {"options", Json::array({
                      {{"label", "High Byte First"}, {"value", "highByteFirst"}},
                      {{"label", "Low Byte First"}, {"value", "lowByteFirst"}}
                  })}},
                 {{"key", "padByte"}, {"name", "Padding Byte"}, {"type", "integer"}, {"default", 0}, {"minimum", 0}, {"maximum", 255}, {"visibleWhen", {{"key", "dataFormat"}, {"values", Json::array({"asciiText", "utf8Text"})}}}}
             })}, {"outputs", Json::array({
                 {{"key", "count"}, {"name", "Written Register Count"}, {"type", "integer"}},
                 {{"key", "dataFormat"}, {"name", "Data Format"}, {"type", "string"}},
                 {{"key", "text"}, {"name", "Written Text"}, {"type", "string"}},
                 {{"key", "byteCount"}, {"name", "Text Byte Count"}, {"type", "integer"}},
                 {{"key", "registers"}, {"name", "Encoded Registers"}, {"type", "string"}}
             })}},
            {{"id", "close"}, {"name", "Close Modbus RTU"}, {"stepKind", "cleanup"}, {"timeoutMs", 3000}, {"inputs", Json::array()}, {"outputs", Json::array({{{"key", "connected"}, {"name", "Connected"}, {"type", "boolean"}}})}}
        })}
    };
}

} // namespace PicoATE::Plugins::Modbus
