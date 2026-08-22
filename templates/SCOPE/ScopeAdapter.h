#pragma once

#include "PicoATE/Plugin/PluginAbi.h"

#include <memory>
#include <string>
#include <utility>

// ============================================================================
// Oscilloscope abstract interface (model agnostic)
//
// Design principles:
//   1. The interface exposes business semantics only, never raw SCPI strings
//   2. Parameters use engineering units (V/s/Hz), not raw SCPI parameters
//   3. Mode parameters are int; legal values are defined at the top of this
//      header, and every subclass validates the range on entry
//   4. Every method returns Result; on failure it carries errorCode/errorMessage
//   5. Model-specific behavior lives in each vendor directory (e.g. RIGOL)
// ============================================================================

namespace PicoATE::Plugins::Scope {

struct Result {
    bool success = true;
    std::string errorCode;
    std::string errorMessage;
    Plugin::Json value;

    static Result passed(Plugin::Json value = {})
    {
        return {true, {}, {}, std::move(value)};
    }

    static Result failed(std::string code, std::string message)
    {
        return {false, std::move(code), std::move(message), {}};
    }
};

// ============================================================================
// Mode constants (subclasses validate ranges and fail with Result::failed)
// ============================================================================

// ---- Channel coupling ----
inline constexpr int CouplingAc = 0;   // AC        AC coupling
inline constexpr int CouplingDc = 1;   // DC        DC coupling (default)
inline constexpr int CouplingGnd = 2;  // GND       ground

// ---- Trigger coupling ----
inline constexpr int TriggerCouplingAc = 0;        // AC        AC coupling
inline constexpr int TriggerCouplingDc = 1;        // DC        DC coupling (default)
inline constexpr int TriggerCouplingLfReject = 2;  // LFReject  low-frequency reject
inline constexpr int TriggerCouplingHfReject = 3;  // HFReject  high-frequency reject

// ---- Trigger edge / slope ----
inline constexpr int EdgeRising = 0;   // Rising   rising edge (default, POSitive)
inline constexpr int EdgeFalling = 1;  // Falling  falling edge (NEGative)
inline constexpr int EdgeEither = 2;   // Either   either edge (RFALl)

// ---- Trigger sweep ----
inline constexpr int SweepAuto = 0;    // Auto    auto trigger (default)
inline constexpr int SweepNormal = 1;  // Normal  normal trigger
inline constexpr int SweepSingle = 2;  // Single  single trigger

// ---- Trigger type (:TRIGger:MODE) ----
inline constexpr int TriggerEdge = 0;    // EDGE    edge trigger (default)
inline constexpr int TriggerPulse = 1;   // PULSe   pulse width trigger
inline constexpr int TriggerRunt = 2;    // RUNT    runt trigger
inline constexpr int TriggerWindow = 3;  // WIND    window trigger
inline constexpr int TriggerSlope = 4;   // SLOPe   slope trigger
inline constexpr int TriggerVideo = 5;   // VIDeo   video trigger
inline constexpr int TriggerPattern = 6; // PATTern pattern trigger
inline constexpr int TriggerDelay = 7;   // DELay   delay trigger
inline constexpr int TriggerTimeout = 8; // TIMeout timeout trigger
inline constexpr int TriggerDuration = 9;   // DURation duration trigger
inline constexpr int TriggerHold = 10;      // SHOLd    setup/hold trigger
inline constexpr int TriggerRs232 = 11;     // RS232    RS232 trigger
inline constexpr int TriggerI2C = 12;       // IIC      I2C trigger
inline constexpr int TriggerSpi = 13;       // SPI      SPI trigger

// ---- Bandwidth limit ----
inline constexpr int BandwidthOff = 0;  // OFF   full bandwidth (default)
inline constexpr int Bandwidth20M = 1;  // 20M   20MHz bandwidth limit

// ============================================================================
// Core abstract interface
// ============================================================================

class IScopeAdapter
{
public:
    virtual ~IScopeAdapter() = default;

    // ========== Lifecycle ==========

    /// Open a VISA connection
    /// @param visaAddress  VISA resource address string
    /// @param options      Station options (visaLibrary/ioTimeoutMs etc.)
    virtual Result connect(const std::string& visaAddress,
                           const Plugin::Json& options) = 0;
    virtual void disconnect() noexcept = 0;
    virtual bool isConnected() const noexcept = 0;

    // ========== IEEE488.2 basics ==========

    /// Query instrument identity (*IDN?)
    virtual Result identity() = 0;

    /// Reset to factory defaults (*RST)
    virtual Result reset() = 0;

    /// Clear status (*CLS)
    virtual Result clear() = 0;

    /// Lock/unlock the front panel (:SYSTem:LOCKed)
    /// @param lock  0=unlock (default), 1=lock
    virtual Result lockFrontPanel(int lock) = 0;

    // ========== Run control ==========

    /// Start acquisition (:RUN)
    virtual Result run() = 0;

    /// Stop acquisition (:STOP)
    virtual Result stop() = 0;

    /// Single acquisition (:SINGle)
    virtual Result single() = 0;

    /// Force a trigger (:TFORce)
    virtual Result forceTrigger() = 0;

    // ========== Channel configuration ==========

    /// Enable/disable channel display (:CHANnel<n>:DISPlay)
    /// @param channel  channel 1~4
    /// @param enable   1=on, 0=off
    virtual Result enableChannel(int channel, int enable) = 0;

    /// Set channel coupling (:CHANnel<n>:COUPling)
    /// @param coupling  CouplingAc/CouplingDc/CouplingGnd
    virtual Result setChannelCoupling(int channel, int coupling) = 0;

    /// Set probe attenuation (:CHANnel<n>:PROBe)
    /// @param attenuation  0.01~1000, default 10
    virtual Result setProbe(int channel, double attenuation) = 0;

    /// Set voltage per division (:CHANnel<n>:SCALe), in V/div
    virtual Result setVoltageDiv(int channel, double voltsPerDiv) = 0;

    /// Set vertical offset (:CHANnel<n>:OFFSet), in V
    virtual Result setChannelOffset(int channel, double offset) = 0;

    /// Set channel invert (:CHANnel<n>:INVert)
    /// @param invert  1=invert, 0=normal
    virtual Result setInvert(int channel, int invert) = 0;

    /// Set bandwidth limit (:CHANnel<n>:BWLimit)
    /// @param limit  BandwidthOff/Bandwidth20M
    virtual Result setBandwidthLimit(int channel, int limit) = 0;

    // ========== Timebase ==========

    /// Set timebase scale (:TIMebase:MAIN:SCALe), in s/div
    virtual Result setTimebaseScale(double secondsPerDiv) = 0;

    // ========== Trigger ==========

    /// Set trigger type (:TRIGger:MODE)
    /// @param mode  TriggerEdge ~ TriggerSpi
    virtual Result setTriggerMode(int mode) = 0;

    /// Set trigger sweep (:TRIGger:SWEep)
    /// @param sweep  SweepAuto/SweepNormal/SweepSingle
    virtual Result setTriggerSweep(int sweep) = 0;

    /// Set trigger coupling (:TRIGger:COUPling)
    /// @param coupling  TriggerCouplingAc/Dc/LfReject/HfReject
    virtual Result setTriggerCoupling(int coupling) = 0;

    /// Set edge trigger source (:TRIGger:EDGe:SOURce)
    /// @param channel  channel 1~4
    virtual Result setEdgeTriggerSource(int channel) = 0;

    /// Set edge trigger level (:TRIGger:EDGe:LEVel), in V
    virtual Result setEdgeTriggerLevel(double level) = 0;

    /// Set edge trigger slope (:TRIGger:EDGe:SLOPe)
    /// @param slope  EdgeRising/EdgeFalling/EdgeEither
    virtual Result setEdgeTriggerSlope(int slope) = 0;

    // ========== Measurement ==========

    /// Read a single measurement item (:MEASure:ITEM?)
    /// @param item     item name: "VPP"/"VMAX"/"VMIN"/"VAVG"/"VRMS"/"FREQuency"/"PERiod" ...
    /// @param source   channel 1~4, <=0 means use the default source
    /// @param source2  second channel for delay/phase items, <=0 means not used
    /// @return value holds the measurement (V/Hz/s/%)
    virtual Result measure(const std::string& item, int source, int source2) = 0;

    // ========== Auto setup ==========

    /// Auto scale (:AUToscale)
    virtual Result autoScale() = 0;

    // ========== Custom commands ==========

    /// Write a command and read the response (SCPI query)
    virtual Result query(const std::string& command) = 0;

    /// Write only (SCPI set)
    virtual Result write(const std::string& command) = 0;
};

/// Factory: implemented in each model DLL
std::unique_ptr<IScopeAdapter> createScopeAdapter();

/// Plugin description (used by the PicoATE_Describe export)
Plugin::Json pluginDescription();

} // namespace PicoATE::Plugins::Scope
