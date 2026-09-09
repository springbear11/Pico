#include "CanAdapter.h"

#include <array>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace PicoATE::Plugins::Can;
using Json = PicoATE::Plugin::Json;

extern "C" int PICOATE_PLUGIN_CALL PicoATE_Execute(const char*, char*, int);
extern "C" int PICOATE_PLUGIN_CALL PicoATE_Describe(char*, int);

namespace {

void check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

struct FakeState {
    bool opened = true;
    bool transmitError = false;
    bool receiveError = false;
    bool autoReply = false;
    int delayMs = 0;
    int transmissions = 0;
    int receives = 0;
    int timeoutMs = 0;
    std::uint32_t filterId = 0;
    std::uint32_t filterMask = 0;
    Frame transmittedFrame;
    std::vector<Frame> queue;
    std::vector<std::pair<char, int>> trace;
};

FakeState state;
std::mutex stateMutex;

class FakeAdapter final : public ICanAdapter {
public:
    DiscoveryResult findDevices(const DiscoveryOptions&) override { return {}; }
    OperationResult open(const OpenOptions&) override
    {
        state.opened = true;
        return OperationResult::passed();
    }
    OperationResult close(const OpenOptions&) override
    {
        state.opened = false;
        return OperationResult::passed();
    }
    bool isOpen(const OpenOptions&) const noexcept override { return state.opened; }
    std::string deviceDescription(const OpenOptions&) const override { return "Fake CAN"; }
    OperationResult transmit(const OpenOptions& options, const Frame& frame) override
    {
        std::scoped_lock lock(stateMutex);
        ++state.transmissions;
        state.transmittedFrame = frame;
        state.trace.emplace_back('T', options.deviceIndex);
        if (state.transmitError) return OperationResult::failed("CanTransmitFailed", "fake transmit error");
        if (state.autoReply) {
            auto reply = frame;
            ++reply.id;
            state.queue.push_back(reply);
        }
        return OperationResult::passed();
    }
    ReceiveResult receive(const OpenOptions& options, std::uint32_t id,
                          std::uint32_t mask, int timeoutMs) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(state.delayMs));
        std::scoped_lock lock(stateMutex);
        ++state.receives;
        state.filterId = id;
        state.filterMask = mask;
        state.timeoutMs = timeoutMs;
        state.trace.emplace_back('R', options.deviceIndex);
        if (state.receiveError) return {ReceiveStatus::Error, {}, "CanReceiveFailed", "fake receive error"};
        while (!state.queue.empty()) {
            auto frame = state.queue.front();
            state.queue.erase(state.queue.begin());
            if ((frame.id & mask) == (id & mask)) return {ReceiveStatus::Received, frame, {}, {}};
        }
        return {ReceiveStatus::Timeout, {}, "CanReceiveTimeout", "fake timeout"};
    }
};

Json invoke(const char* function, const Json& input)
{
    const auto request = Json{{"function", function}, {"context", {{"inputs", input}}}}.dump();
    std::array<char, 16384> response{};
    check(PicoATE_Execute(request.c_str(), response.data(), static_cast<int>(response.size())) == 0,
          "Execute ABI failure");
    return Json::parse(response.data());
}

Json requestInputs()
{
    return {{"id", "0x123"}, {"data", "01 02 FF"}, {"extended", false}, {"rxId", "0x456"}};
}

Frame reply(std::uint32_t id = 0x456)
{
    Frame frame;
    frame.id = id;
    frame.data = {0x12, 0x34};
    frame.extended = id > 0x7FF;
    frame.timestampUs = 123400;
    return frame;
}

void successfulExchange()
{
    state = {};
    state.queue = {reply(0x789), reply()};
    const auto result = invoke("sendAndRead", requestInputs());
    check(result["outcome"] == "Passed", "exchange should pass");
    check(state.transmissions == 1 && state.receives == 1, "one send then one receive");
    check(state.filterId == 0x456 && state.filterMask == 0x1FFFFFFF, "default exact receive filter");
    check(state.timeoutMs == 1500, "default receive timeout");
    check(state.transmittedFrame.id == 0x123 && state.transmittedFrame.data.back() == 255, "transmit frame mismatch");
    const auto& outputs = result["outputs"];
    check(outputs["transmitted"] == true && outputs["txId"] == "0x123", "missing transmit result");
    check(outputs["txDataHex"] == "01 02 FF", "wrong sent bytes");
    check(outputs["id"] == "0x456" && outputs["idNumeric"] == 0x456, "wrong response ID");
    check(outputs["dataHex"] == "12 34" && outputs["data"] == Json::array({18, 52}), "wrong response bytes");
    check(outputs["dlc"] == 2 && outputs["timestampUs"] == 123400, "wrong response metadata");
    check(result["measurements"]["name"] == "CAN_RX_FRAME", "existing RX measurement must be preserved");
}

void extendedAndMask()
{
    state = {};
    state.queue = {reply(0x18FFAB03)};
    auto input = requestInputs();
    input.update({{"id", "0x18FFAA01"}, {"extended", true}, {"data", Json::array({1, 255})},
                  {"rxId", "0x18FFAB00"}, {"rxMask", "0x1FFFFFF0"}, {"timeoutMs", 2500}});
    const auto result = invoke("SendAndRead", input);
    check(result["outcome"] == "Passed", "extended masked exchange should pass");
    check(result["outputs"]["id"] == "0x18FFAB03", "response extended ID lost");
    check(state.filterMask == 0x1FFFFFF0 && state.timeoutMs == 2500, "receive options lost");
}

void preflightDoesNotTransmit()
{
    const std::vector<Json> overrides = {
        {{"id", "0x20000000"}}, {{"id", "0x800"}}, {{"id", -1}},
        {{"rxId", "0x20000000"}}, {{"rxId", ""}}, {{"rxId", nullptr}},
        {{"rxMask", "0x20000000"}}, {{"rxMask", -1}},
        {{"timeoutMs", 0}}, {{"timeoutMs", -1}}, {{"timeoutMs", 60001}},
        {{"timeoutMs", 1.5}}, {{"timeoutMs", "1500"}}, {{"timeoutMs", 1e100}},
        {{"data", "GG"}}, {{"data", Json::array({256})}},
        {{"data", Json::array({4294967296ULL})}}, {{"data", Json::array({-1})}},
        {{"data", "00 01 02 03 04 05 06 07 08"}}
    };
    for (const auto& change : overrides) {
        state = {};
        auto input = requestInputs();
        input.update(change);
        const auto result = invoke("sendAndRead", input);
        check(result["outcome"] == "Error", "invalid input must fail");
        check(state.transmissions == 0 && state.receives == 0, "invalid receive/send input reached hardware");
        check(!result["errorMessage"].get<std::string>().empty(), "invalid input needs a useful error");
    }
    for (const auto* missing : {"rxId", "id", "data"}) {
        state = {};
        auto input = requestInputs();
        input.erase(missing);
        check(invoke("sendAndRead", input)["outcome"] == "Error", "required input not enforced");
        check(state.transmissions == 0, "missing input must not transmit");
    }
}

void errorsAndTimeout()
{
    state = {};
    state.opened = false;
    check(invoke("sendAndRead", requestInputs())["errorCode"] == "CanNotOpen", "closed channel not detected");
    check(state.transmissions == 0, "closed channel must not transmit");
    state = {};
    state.transmitError = true;
    auto result = invoke("sendAndRead", requestInputs());
    check(result["errorCode"] == "CanTransmitFailed", "transmit error lost");
    check(result["outputs"]["transmitted"] == false && state.receives == 0, "must not receive after failed send");
    state = {};
    state.receiveError = true;
    result = invoke("sendAndRead", requestInputs());
    check(result["errorCode"] == "CanReceiveFailed" && result["outputs"]["transmitted"] == true, "receive error lost");
    state = {};
    state.queue = {reply(0x1ABC)};
    result = invoke("sendAndRead", requestInputs());
    check(result["outcome"] == "Timeout" && result["errorCode"] == "CanReceiveTimeout", "unmatched reply must time out");
    check(result["outputs"]["transmitted"] == true, "timeout must retain sent flag");
    const auto message = result["errorMessage"].get<std::string>();
    check(message.find("TX=0x123") != std::string::npos && message.find("RX=0x456") != std::string::npos &&
          message.find("timeoutMs=1500") != std::string::npos, "timeout must identify the request");
}

void legacyCallsStillWork()
{
    state = {};
    auto input = requestInputs();
    check(invoke("write", input)["outcome"] == "Passed", "legacy write regression");
    state.queue = {reply()};
    check(invoke("read", {{"filterId", "0x456"}, {"filterMask", "0x7FF"}})["outcome"] == "Passed", "legacy read regression");
    state = {};
    state.queue = {reply(0x123)};
    input.erase("rxId");
    auto result = invoke("requestResponse", {{"tx", input}});
    check(result["outcome"] == "Passed" && state.filterId == 0x123 && state.filterMask == 0x7FF,
          "legacy nested TX/default RX regression");
    state = {};
    input["rxId"] = "0x20000000";
    check(invoke("requestResponse", input)["outcome"] == "Error" && state.transmissions == 0,
          "legacy invalid receive ID must also be checked before sending");
}

void exchangeIsSerialized()
{
    state = {};
    state.autoReply = true;
    state.delayMs = 5;
    std::array<std::thread, 4> threads;
    std::array<bool, 4> passed{};
    for (int i = 0; i < 4; ++i) {
        threads[i] = std::thread([i, &passed] {
            try {
                auto input = requestInputs();
                input.update({{"id", 0x100 + i * 16}, {"rxId", 0x101 + i * 16}, {"deviceIndex", i}});
                passed[i] = true;
                for (int repeat = 0; repeat < 5; ++repeat) {
                    const auto result = invoke("sendAndRead", input);
                    passed[i] = passed[i] && result["outcome"] == "Passed" &&
                        result["outputs"]["idNumeric"] == 0x101 + i * 16;
                }
            } catch (...) { passed[i] = false; }
        });
    }
    for (auto& thread : threads) thread.join();
    for (const auto ok : passed) check(ok, "concurrent exchange failed or got another response");
    check(state.trace.size() == 40, "missing concurrent I/O");
    for (std::size_t i = 0; i < state.trace.size(); i += 2) {
        check(state.trace[i].first == 'T' && state.trace[i + 1].first == 'R' &&
              state.trace[i].second == state.trace[i + 1].second, "another request interleaved between send and read");
    }
}

void descriptionIsDiscoverable()
{
    std::array<char, 16384> output{};
    check(PicoATE_Describe(output.data(), static_cast<int>(output.size())) == 0, "Describe ABI failure");
    const auto function = Json::parse(output.data())["functions"][0];
    check(function["id"] == "sendAndRead", "missing public function");
    bool requiredRxId = false;
    for (const auto& input : function["inputs"]) {
        if (input["key"] == "rxId") requiredRxId = input["required"].get<bool>();
    }
    check(requiredRxId, "receive ID should require user configuration");
    check(function["timeoutMs"].get<int>() > 60000, "step budget must exceed maximum receive wait");
}

} // namespace

namespace PicoATE::Plugins::Can {
std::unique_ptr<ICanAdapter> createCanAdapter() { return std::make_unique<FakeAdapter>(); }
Plugin::Json pluginDescription() { return {{"functions", Json::array({sendAndReadDescription()})}}; }
}

int main()
{
    try {
        for (const auto& test : {
                 std::pair{"successfulExchange", successfulExchange},
                 std::pair{"extendedAndMask", extendedAndMask},
                 std::pair{"preflightDoesNotTransmit", preflightDoesNotTransmit},
                 std::pair{"errorsAndTimeout", errorsAndTimeout},
                 std::pair{"legacyCallsStillWork", legacyCallsStillWork},
                 std::pair{"exchangeIsSerialized", exchangeIsSerialized},
                 std::pair{"descriptionIsDiscoverable", descriptionIsDiscoverable}}) {
            test.second();
            std::cout << "PASS: " << test.first << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
