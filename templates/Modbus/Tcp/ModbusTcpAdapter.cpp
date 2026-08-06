#include "ModbusAdapter.h"

#include "PicoATE/Plugin/PluginLog.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace PicoATE::Plugins::Modbus {

namespace {

constexpr std::uint8_t ReadCoils = 0x01;
constexpr std::uint8_t ReadDiscreteInputs = 0x02;
constexpr std::uint8_t ReadHoldingRegisters = 0x03;
constexpr std::uint8_t ReadInputRegisters = 0x04;
constexpr std::uint8_t WriteSingleCoil = 0x05;
constexpr std::uint8_t WriteSingleRegister = 0x06;
constexpr std::uint8_t WriteMultipleCoils = 0x0F;
constexpr std::uint8_t WriteMultipleRegisters = 0x10;
constexpr std::uint16_t DefaultPort = 502;

std::uint16_t readU16(const std::uint8_t* bytes)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
}

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

std::string hexFrame(const std::uint8_t* bytes, std::size_t size)
{
    std::string text;
    text.reserve(size * 3);
    for (std::size_t index = 0; index < size; ++index) {
        if (index > 0) {
            text.push_back(' ');
        }
        text += std::format("{:02X}", static_cast<unsigned int>(bytes[index]));
    }
    return text;
}

std::string winsockError(int error)
{
    return std::format("Winsock error {}", error);
}

std::string exceptionDescription(std::uint8_t code)
{
    switch (code) {
    case 0x01: return "IllegalFunction";
    case 0x02: return "IllegalDataAddress";
    case 0x03: return "IllegalDataValue";
    case 0x04: return "SlaveDeviceFailure";
    case 0x05: return "Acknowledge";
    case 0x06: return "SlaveDeviceBusy";
    case 0x07: return "NegativeAcknowledge";
    case 0x08: return "MemoryParityError";
    case 0x0A: return "GatewayPathUnavailable";
    case 0x0B: return "GatewayTargetDeviceFailedToRespond";
    default: return std::format("VendorException0x{:02X}", code);
    }
}

bool parseEndpoint(const std::string& endpoint, std::string& host, std::string& port)
{
    if (endpoint.empty()) return false;
    host = endpoint;
    port = std::to_string(DefaultPort);
    if (endpoint.front() == '[') {
        const auto closing = endpoint.find(']');
        if (closing == std::string::npos) return false;
        host = endpoint.substr(1, closing - 1);
        if (closing + 1 == endpoint.size()) return !host.empty();
        if (endpoint[closing + 1] != ':') return false;
        port = endpoint.substr(closing + 2);
        return !host.empty() && !port.empty();
    }
    const auto first = endpoint.find(':');
    if (first != std::string::npos && first == endpoint.rfind(':')) {
        host = endpoint.substr(0, first);
        port = endpoint.substr(first + 1);
    }
    return !host.empty() && !port.empty();
}

class ModbusTcpAdapter final : public IModbusAdapter
{
public:
    ModbusTcpAdapter()
    {
        WSADATA data{};
        m_wsaStarted = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~ModbusTcpAdapter() override
    {
        disconnect();
        if (m_wsaStarted) WSACleanup();
    }

    Result connect(const std::string& endpoint,
                   int slaveId,
                   const Plugin::Json& options) override
    {
        (void)slaveId;
        disconnect();
        if (!m_wsaStarted) {
            return Result::failed("WinsockInitializeFailed", "WSAStartup failed");
        }

        std::string host;
        std::string port;
        if (!parseEndpoint(endpoint, host, port)) {
            return Result::failed("InvalidModbusEndpoint", "Use host:port, host, or [IPv6]:port");
        }

        const auto connectTimeout = Plugin::numberValue<int>(options, "connectTimeoutMs", 3000);
        m_ioTimeoutMs = Plugin::numberValue<int>(options, "ioTimeoutMs", 2000);
        if (connectTimeout <= 0 || m_ioTimeoutMs <= 0) {
            return Result::failed("InvalidModbusTimeout", "connectTimeoutMs and ioTimeoutMs must be positive");
        }

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* addresses = nullptr;
        const auto resolveResult = getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses);
        if (resolveResult != 0) {
            return Result::failed("ModbusResolveFailed", gai_strerrorA(resolveResult));
        }

        std::string lastError = "No TCP endpoint was reachable";
        for (auto* address = addresses; address; address = address->ai_next) {
            const auto socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (socket == INVALID_SOCKET) {
                lastError = winsockError(WSAGetLastError());
                continue;
            }
            int connectError = 0;
            if (connectSocket(socket,
                              address->ai_addr,
                              static_cast<int>(address->ai_addrlen),
                              connectTimeout,
                              connectError)) {
                m_socket = socket;
                break;
            }
            lastError = winsockError(connectError);
            closesocket(socket);
        }
        freeaddrinfo(addresses);
        if (m_socket == INVALID_SOCKET) {
            return Result::failed("ModbusConnectFailed", lastError);
        }

        const DWORD timeout = static_cast<DWORD>(m_ioTimeoutMs);
        setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        PicoATE_Log("MODBUS_TCP connected endpoint={}", endpoint);
        return Result::passed({{"connected", true}, {"endpoint", endpoint}});
    }

    void disconnect() noexcept override
    {
        std::scoped_lock lock(m_socketMutex);
        closeSocket();
    }

    bool isConnected() const noexcept override
    {
        std::scoped_lock lock(m_socketMutex);
        return m_socket != INVALID_SOCKET;
    }

    Result readCoils(std::uint8_t unitId,
                     std::uint16_t address,
                     std::uint16_t count) override
    {
        return readBits(unitId, address, count, ReadCoils, "Read Coils");
    }

    Result readDiscreteInputs(std::uint8_t unitId,
                              std::uint16_t address,
                              std::uint16_t count) override
    {
        return readBits(unitId, address, count, ReadDiscreteInputs, "Read Discrete Inputs");
    }

    Result readHoldingRegisters(std::uint8_t unitId,
                                std::uint16_t address,
                                std::uint16_t count) override
    {
        if (count == 0 || count > 125) {
            return Result::failed("InvalidModbusCount", "Read Holding Registers count must be 1..125");
        }
        std::vector<std::uint8_t> response;
        const auto exchange = request(unitId, {ReadHoldingRegisters,
                                               static_cast<std::uint8_t>(address >> 8),
                                               static_cast<std::uint8_t>(address),
                                               static_cast<std::uint8_t>(count >> 8),
                                               static_cast<std::uint8_t>(count)}, response);
        if (!exchange.success) return exchange;
        if (response.size() != static_cast<std::size_t>(2 + count * 2) ||
            response[0] != ReadHoldingRegisters || response[1] != count * 2) {
            return Result::failed("InvalidModbusResponse", "Unexpected Read Holding Registers response");
        }
        auto registers = Plugin::Json::array();
        for (std::size_t index = 2; index < response.size(); index += 2) {
            registers.push_back(readU16(&response[index]));
        }
        return Result::passed({
            {"unitId", unitId},
            {"address", address},
            {"count", count},
            {"registers", registers.dump()},
        });
    }

    Result readInputRegisters(std::uint8_t unitId,
                              std::uint16_t address,
                              std::uint16_t count) override
    {
        return readRegisters(unitId, address, count, ReadInputRegisters, "Read Input Registers");
    }

    Result writeSingleCoil(std::uint8_t unitId,
                           std::uint16_t address,
                           bool value) override
    {
        const auto encoded = static_cast<std::uint16_t>(value ? 0xFF00 : 0x0000);
        std::vector<std::uint8_t> response;
        const auto exchange = request(unitId, {WriteSingleCoil,
                                               static_cast<std::uint8_t>(address >> 8),
                                               static_cast<std::uint8_t>(address),
                                               static_cast<std::uint8_t>(encoded >> 8),
                                               static_cast<std::uint8_t>(encoded)}, response);
        if (!exchange.success) return exchange;
        if (response.size() != 5 || response[0] != WriteSingleCoil ||
            readU16(&response[1]) != address || readU16(&response[3]) != encoded) {
            return Result::failed("InvalidModbusResponse", "Unexpected Write Single Coil response");
        }
        return Result::passed({{"unitId", unitId}, {"address", address}, {"value", value}});
    }

    Result writeSingleRegister(std::uint8_t unitId,
                               std::uint16_t address,
                               std::uint16_t value) override
    {
        std::vector<std::uint8_t> response;
        const auto exchange = request(unitId, {WriteSingleRegister,
                                               static_cast<std::uint8_t>(address >> 8),
                                               static_cast<std::uint8_t>(address),
                                               static_cast<std::uint8_t>(value >> 8),
                                               static_cast<std::uint8_t>(value)}, response);
        if (!exchange.success) return exchange;
        if (response.size() != 5 || response[0] != WriteSingleRegister ||
            readU16(&response[1]) != address || readU16(&response[3]) != value) {
            return Result::failed("InvalidModbusResponse", "Unexpected Write Single Register response");
        }
        return Result::passed({{"unitId", unitId}, {"address", address}, {"value", value}});
    }

    Result writeMultipleCoils(std::uint8_t unitId,
                              std::uint16_t address,
                              const std::vector<bool>& values) override
    {
        if (values.empty() || values.size() > 1968) {
            return Result::failed("InvalidModbusValues", "Write Multiple Coils accepts 1..1968 values");
        }
        std::vector<std::uint8_t> pdu{WriteMultipleCoils};
        appendU16(pdu, address);
        appendU16(pdu, static_cast<std::uint16_t>(values.size()));
        const auto byteCount = static_cast<std::size_t>((values.size() + 7) / 8);
        pdu.push_back(static_cast<std::uint8_t>(byteCount));
        pdu.resize(pdu.size() + byteCount, 0);
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (values[index]) {
                pdu[6 + index / 8] |= static_cast<std::uint8_t>(1U << (index % 8));
            }
        }
        std::vector<std::uint8_t> response;
        const auto exchange = request(unitId, pdu, response);
        if (!exchange.success) return exchange;
        if (response.size() != 5 || response[0] != WriteMultipleCoils ||
            readU16(&response[1]) != address || readU16(&response[3]) != values.size()) {
            return Result::failed("InvalidModbusResponse", "Unexpected Write Multiple Coils response");
        }
        return Result::passed({{"unitId", unitId}, {"address", address}, {"count", values.size()}});
    }

    Result writeMultipleRegisters(std::uint8_t unitId,
                                  std::uint16_t address,
                                  const std::vector<std::uint16_t>& values) override
    {
        if (values.empty() || values.size() > 123) {
            return Result::failed("InvalidModbusValues", "Write Multiple Registers accepts 1..123 values");
        }
        std::vector<std::uint8_t> pdu{WriteMultipleRegisters};
        appendU16(pdu, address);
        appendU16(pdu, static_cast<std::uint16_t>(values.size()));
        pdu.push_back(static_cast<std::uint8_t>(values.size() * 2));
        for (const auto value : values) appendU16(pdu, value);

        std::vector<std::uint8_t> response;
        const auto exchange = request(unitId, pdu, response);
        if (!exchange.success) return exchange;
        if (response.size() != 5 || response[0] != WriteMultipleRegisters ||
            readU16(&response[1]) != address || readU16(&response[3]) != values.size()) {
            return Result::failed("InvalidModbusResponse", "Unexpected Write Multiple Registers response");
        }
        return Result::passed({
            {"unitId", unitId},
            {"address", address},
            {"count", values.size()},
        });
    }

private:
    Result readBits(std::uint8_t unitId,
                    std::uint16_t address,
                    std::uint16_t count,
                    std::uint8_t function,
                    std::string_view functionName)
    {
        if (count == 0 || count > 2000) {
            return Result::failed("InvalidModbusCount", std::string(functionName) + " count must be 1..2000");
        }
        std::vector<std::uint8_t> response;
        const auto exchange = request(unitId, {function,
                                               static_cast<std::uint8_t>(address >> 8),
                                               static_cast<std::uint8_t>(address),
                                               static_cast<std::uint8_t>(count >> 8),
                                               static_cast<std::uint8_t>(count)}, response);
        if (!exchange.success) return exchange;
        const auto byteCount = static_cast<std::size_t>((count + 7) / 8);
        if (response.size() != byteCount + 2 || response[0] != function || response[1] != byteCount) {
            return Result::failed("InvalidModbusResponse", "Unexpected " + std::string(functionName) + " response");
        }
        auto values = Plugin::Json::array();
        for (std::uint16_t index = 0; index < count; ++index) {
            values.push_back((response[2 + index / 8] & (1U << (index % 8))) != 0);
        }
        return Result::passed({
            {"unitId", unitId},
            {"address", address},
            {"count", count},
            {"values", values.dump()},
        });
    }

    Result readRegisters(std::uint8_t unitId,
                         std::uint16_t address,
                         std::uint16_t count,
                         std::uint8_t function,
                         std::string_view functionName)
    {
        if (count == 0 || count > 125) {
            return Result::failed("InvalidModbusCount", std::string(functionName) + " count must be 1..125");
        }
        std::vector<std::uint8_t> response;
        const auto exchange = request(unitId, {function,
                                               static_cast<std::uint8_t>(address >> 8),
                                               static_cast<std::uint8_t>(address),
                                               static_cast<std::uint8_t>(count >> 8),
                                               static_cast<std::uint8_t>(count)}, response);
        if (!exchange.success) return exchange;
        if (response.size() != static_cast<std::size_t>(2 + count * 2) ||
            response[0] != function || response[1] != count * 2) {
            return Result::failed("InvalidModbusResponse", "Unexpected " + std::string(functionName) + " response");
        }
        auto registers = Plugin::Json::array();
        for (std::size_t index = 2; index < response.size(); index += 2) {
            registers.push_back(readU16(&response[index]));
        }
        return Result::passed({
            {"unitId", unitId},
            {"address", address},
            {"count", count},
            {"registers", registers.dump()},
        });
    }

    void closeSocket() noexcept
    {
        if (m_socket != INVALID_SOCKET) {
            shutdown(m_socket, SD_BOTH);
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }
    static Result unsupported(std::string_view function)
    {
        return Result::failed("UnsupportedModbusFunction", std::string(function) + " is not supported by this protocol");
    }

    static bool connectSocket(SOCKET socket,
                              const sockaddr* address,
                              int length,
                              int timeoutMs,
                              int& error)
    {
        error = 0;
        u_long nonBlocking = 1;
        if (ioctlsocket(socket, FIONBIO, &nonBlocking) != 0) {
            error = WSAGetLastError();
            return false;
        }
        const auto connectResult = ::connect(socket, address, length);
        if (connectResult == SOCKET_ERROR) {
            error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) return false;
        }
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(socket, &writeSet);
        timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        const auto selectResult = select(0, nullptr, &writeSet, nullptr, &timeout);
        if (selectResult != 1) {
            error = selectResult == 0 ? WSAETIMEDOUT : WSAGetLastError();
            return false;
        }
        int socketError = 0;
        int socketErrorSize = sizeof(socketError);
        if (getsockopt(socket,
                       SOL_SOCKET,
                       SO_ERROR,
                       reinterpret_cast<char*>(&socketError),
                       &socketErrorSize) != 0) {
            error = WSAGetLastError();
            return false;
        }
        if (socketError != 0) {
            error = socketError;
            return false;
        }
        nonBlocking = 0;
        if (ioctlsocket(socket, FIONBIO, &nonBlocking) != 0) {
            error = WSAGetLastError();
            return false;
        }
        return true;
    }

    Result request(std::uint8_t unitId,
                   const std::vector<std::uint8_t>& pdu,
                   std::vector<std::uint8_t>& response)
    {
        if (pdu.empty()) return Result::failed("InvalidModbusRequest", "Modbus PDU is empty");
        std::scoped_lock lock(m_socketMutex);
        if (m_socket == INVALID_SOCKET) {
            return Result::failed("ModbusNotConnected", "TCP socket is not connected");
        }

        const auto transactionId = static_cast<std::uint16_t>(++m_transactionId);
        std::vector<std::uint8_t> request;
        request.reserve(7 + pdu.size());
        appendU16(request, transactionId);
        appendU16(request, 0);
        appendU16(request, static_cast<std::uint16_t>(pdu.size() + 1));
        request.push_back(unitId);
        request.insert(request.end(), pdu.begin(), pdu.end());
        PicoATE_Log("MODBUS_TCP_TX transactionId=0x{:04X} ({}) unitId=0x{:02X} ({}) function=0x{:02X} frame={}",
                    transactionId, transactionId, static_cast<unsigned int>(unitId),
                    static_cast<unsigned int>(unitId), static_cast<unsigned int>(pdu[0]),
                    hexFrame(request.data(), request.size()));
        if (!sendAll(request.data(), request.size())) {
            PicoATE_Log("MODBUS_TCP_TX_FAILED transactionId=0x{:04X} error={}",
                        transactionId, winsockError(WSAGetLastError()));
            return socketFailure("ModbusSendFailed");
        }

        std::array<std::uint8_t, 7> header{};
        if (!receiveAll(header.data(), header.size())) {
            PicoATE_Log("MODBUS_TCP_RX_HEADER_FAILED transactionId=0x{:04X} error={}",
                        transactionId, winsockError(WSAGetLastError()));
            return socketFailure("ModbusReceiveFailed");
        }
        const auto responseTransaction = readU16(header.data());
        const auto protocolId = readU16(header.data() + 2);
        const auto length = readU16(header.data() + 4);
        if (responseTransaction != transactionId || protocolId != 0 || header[6] != unitId || length < 2 || length > 254) {
            PicoATE_Log("MODBUS_TCP_RX_INVALID_MBAP expectedTransactionId=0x{:04X} expectedUnitId=0x{:02X} receivedHeader={} protocolId=0x{:04X} length={}",
                        transactionId, static_cast<unsigned int>(unitId),
                        hexFrame(header.data(), header.size()), protocolId, length);
            return Result::failed("InvalidModbusResponse", "Invalid Modbus TCP MBAP header");
        }
        response.resize(length - 1);
        if (!receiveAll(response.data(), response.size())) {
            PicoATE_Log("MODBUS_TCP_RX_BODY_FAILED transactionId=0x{:04X} expectedBodyBytes={} header={} error={}",
                        transactionId, response.size(), hexFrame(header.data(), header.size()),
                        winsockError(WSAGetLastError()));
            return socketFailure("ModbusReceiveFailed");
        }
        std::vector<std::uint8_t> fullResponse;
        fullResponse.reserve(header.size() + response.size());
        fullResponse.insert(fullResponse.end(), header.begin(), header.end());
        fullResponse.insert(fullResponse.end(), response.begin(), response.end());
        PicoATE_Log("MODBUS_TCP_RX transactionId=0x{:04X} ({}) unitId=0x{:02X} ({}) function=0x{:02X} frame={}",
                    responseTransaction, responseTransaction,
                    static_cast<unsigned int>(header[6]), static_cast<unsigned int>(header[6]),
                    static_cast<unsigned int>(response[0]),
                    hexFrame(fullResponse.data(), fullResponse.size()));
        if (response.size() == 2 && response[0] == static_cast<std::uint8_t>(pdu[0] | 0x80)) {
            const auto code = response[1];
            PicoATE_Log("MODBUS_TCP_EXCEPTION transactionId=0x{:04X} requestFunction=0x{:02X} exceptionCode=0x{:02X} description={}",
                        transactionId, static_cast<unsigned int>(pdu[0]),
                        static_cast<unsigned int>(code), exceptionDescription(code));
            return Result::failed(std::format("ModbusException0x{:02X}", code), exceptionDescription(code));
        }
        return Result::passed();
    }

    bool sendAll(const std::uint8_t* bytes, std::size_t size)
    {
        while (size > 0) {
            const auto sent = send(m_socket, reinterpret_cast<const char*>(bytes), static_cast<int>(size), 0);
            if (sent == SOCKET_ERROR || sent == 0) return false;
            bytes += sent;
            size -= static_cast<std::size_t>(sent);
        }
        return true;
    }

    bool receiveAll(std::uint8_t* bytes, std::size_t size)
    {
        while (size > 0) {
            const auto received = recv(m_socket, reinterpret_cast<char*>(bytes), static_cast<int>(size), 0);
            if (received == SOCKET_ERROR || received == 0) return false;
            bytes += received;
            size -= static_cast<std::size_t>(received);
        }
        return true;
    }

    Result socketFailure(const char* code)
    {
        const auto error = WSAGetLastError();
        closeSocket();
        return Result::failed(code, winsockError(error));
    }

    SOCKET m_socket = INVALID_SOCKET;
    std::uint16_t m_transactionId = 0;
    int m_ioTimeoutMs = 2000;
    bool m_wsaStarted = false;
    mutable std::mutex m_socketMutex;
};

} // namespace

std::unique_ptr<IModbusAdapter> createModbusAdapter()
{
    return std::make_unique<ModbusTcpAdapter>();
}

Plugin::Json pluginDescription()
{
    using Json = Plugin::Json;
    return {
        {"schema", "picoate.plugin"},
        {"schemaVersion", 1},
        {"pluginId", "picoate.modbus.tcp"},
        {"moduleId", "plugin.modbus.tcp"},
        {"name", "Modbus TCP"},
        {"category", "MODBUS"},
        {"connectionKinds", Json::array({"tcpIp"})},
        {"vendor", "Generic"},
        {"version", "1.0.0"},
        {"functions", Json::array({
            {{"id", "open"}, {"name", "Open Modbus TCP"},
             {"description", "Open one persistent Modbus TCP connection"},
             {"timeoutMs", 5000},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Default Unit ID"}, {"type", "string"}, {"default", 0}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "connectTimeoutMs"}, {"name", "Connect Timeout"}, {"type", "integer"}, {"default", 3000}, {"minimum", 1}, {"maximum", 600000}, {"unit", "ms"}},
                 {{"key", "ioTimeoutMs"}, {"name", "I/O Timeout"}, {"type", "integer"}, {"default", 2000}, {"minimum", 1}, {"maximum", 600000}, {"unit", "ms"}}
             })}, {"outputs", Json::array({{{"key", "connected"}, {"name", "Connected"}, {"type", "boolean"}}})}},
            {{"id", "readCoils"}, {"name", "Read Coils"}, {"description", "FC01"}, {"timeoutMs", 3000},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "count"}, {"name", "Coil Count"}, {"type", "integer"}, {"required", true}, {"minimum", 1}, {"maximum", 2000}}
             })}, {"outputs", Json::array({{{"key", "values"}, {"name", "Coil Values (JSON Array)"}, {"type", "string"}}})}},
            {{"id", "readDiscreteInputs"}, {"name", "Read Discrete Inputs"}, {"description", "FC02"}, {"timeoutMs", 3000},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "count"}, {"name", "Input Count"}, {"type", "integer"}, {"required", true}, {"minimum", 1}, {"maximum", 2000}}
             })}, {"outputs", Json::array({{{"key", "values"}, {"name", "Input Values (JSON Array)"}, {"type", "string"}}})}},
            {{"id", "readHoldingRegisters"}, {"name", "Read Holding Registers"}, {"timeoutMs", 3000},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "count"}, {"name", "Register Count"}, {"type", "integer"}, {"required", true}, {"minimum", 1}, {"maximum", 125}}
             })}, {"outputs", Json::array({{{"key", "registers"}, {"name", "Registers (JSON Array)"}, {"type", "string"}}})}},
            {{"id", "readInputRegisters"}, {"name", "Read Input Registers"}, {"description", "FC04"}, {"timeoutMs", 3000},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "count"}, {"name", "Register Count"}, {"type", "integer"}, {"required", true}, {"minimum", 1}, {"maximum", 125}}
             })}, {"outputs", Json::array({{{"key", "registers"}, {"name", "Registers (JSON Array)"}, {"type", "string"}}})}},
            {{"id", "writeSingleCoil"}, {"name", "Write Single Coil"}, {"description", "FC05"}, {"timeoutMs", 3000},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "value"}, {"name", "Coil Value"}, {"type", "boolean"}, {"required", true}}
             })}, {"outputs", Json::array({{{"key", "value"}, {"name", "Coil Value"}, {"type", "boolean"}}})}},
            {{"id", "writeSingleRegister"}, {"name", "Write Single Register"}, {"description", "FC06"}, {"timeoutMs", 3000},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "value"}, {"name", "Register Value"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}}
             })}, {"outputs", Json::array({{{"key", "value"}, {"name", "Register Value"}, {"type", "integer"}}})}},
            {{"id", "writeMultipleCoils"}, {"name", "Write Multiple Coils"}, {"description", "FC0F"}, {"timeoutMs", 3000},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "values"}, {"name", "Coil Values (JSON Array)"}, {"type", "string"}, {"required", true}, {"description", "Example: [true, false, true]"}}
             })}, {"outputs", Json::array({{{"key", "count"}, {"name", "Written Coil Count"}, {"type", "integer"}}})}},
            {{"id", "writeMultipleRegisters"}, {"name", "Write Multiple Registers"},
             {"description", "FC10; default dataFormat=registers, optionally encode text"}, {"timeoutMs", 3000},
             {"inputs", Json::array({
                 {{"key", "unitId"}, {"name", "Unit ID"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 255}},
                 {{"key", "address"}, {"name", "Start Address"}, {"type", "string"}, {"required", true}, {"minimum", 0}, {"maximum", 65535}},
                 {{"key", "dataFormat"}, {"name", "Data Format"}, {"type", "enum"}, {"default", "registers"}, {"options", Json::array({
                     {{"label", "Registers"}, {"value", "registers"}},
                     {{"label", "ASCII Text"}, {"value", "asciiText"}},
                     {{"label", "UTF-8 Text"}, {"value", "utf8Text"}}
                 })}},
                 {{"key", "values"}, {"name", "Register Values (JSON Array)"}, {"type", "string"}, {"required", true}, {"description", "Required when dataFormat=registers. Example: [1, 100, 0xFFFF]"}},
                 {{"key", "text"}, {"name", "Text"}, {"type", "string"}, {"description", "Required when dataFormat=asciiText or utf8Text. Variables such as ${var.serialNumber} are supported."}},
                 {{"key", "registerCount"}, {"name", "Register Count"}, {"type", "integer"}, {"minimum", 1}, {"maximum", 123}, {"description", "Required for text formats; number of FC10 registers to write."}},
                 {{"key", "byteOrder"}, {"name", "Byte Order"}, {"type", "string"}, {"default", "highByteFirst"}},
                 {{"key", "padByte"}, {"name", "Padding Byte"}, {"type", "integer"}, {"default", 0}, {"minimum", 0}, {"maximum", 255}}
             })}, {"outputs", Json::array({
                 {{"key", "count"}, {"name", "Written Register Count"}, {"type", "integer"}},
                 {{"key", "dataFormat"}, {"name", "Data Format"}, {"type", "string"}},
                 {{"key", "text"}, {"name", "Written Text"}, {"type", "string"}},
                 {{"key", "byteCount"}, {"name", "Text Byte Count"}, {"type", "integer"}},
                 {{"key", "registers"}, {"name", "Encoded Registers"}, {"type", "string"}}
             })}},
            {{"id", "close"}, {"name", "Close Modbus TCP"}, {"stepKind", "cleanup"}, {"timeoutMs", 3000},
             {"inputs", Json::array()}, {"outputs", Json::array({{{"key", "connected"}, {"name", "Connected"}, {"type", "boolean"}}})}}
        })}
    };
}

} // namespace PicoATE::Plugins::Modbus
