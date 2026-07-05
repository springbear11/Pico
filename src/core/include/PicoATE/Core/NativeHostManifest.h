#pragma once

#include "PicoATE/Core/VariableResolver.h"

#include <QVariantMap>

namespace PicoATE::Core {

enum class NativeHostVendorStdioMode {
    Strict,
    Discard
};

QString nativeHostVendorStdioModeToString(NativeHostVendorStdioMode mode);

struct NativeHostDiagnosticsConfig {
    NativeHostVendorStdioMode vendorStdioMode = NativeHostVendorStdioMode::Strict;
    int maximumBufferedLogs = 1024;
    int maximumMessageCharacters = 4096;
    int maximumBatchRecords = 64;
    int maximumBatchBytes = 16384;
    int batchFlushMs = 20;
};

struct NativeHostManifest {
    QString dllPath;
    QString symbol = "PicoATE_Execute";
    int bufferSize = 65536;
    int dllTimeoutMs = 30000;
    NativeHostDiagnosticsConfig diagnostics;
    QVariantMap metadata;
};

struct NativeHostManifestError {
    QString path;
    QString message;
    QString suggestion;
};

struct NativeHostManifestResult {
    NativeHostManifest manifest;
    QVector<NativeHostManifestError> errors;

    bool ok() const { return errors.isEmpty(); }
};

NativeHostManifestResult loadNativeHostManifest(
    const QString& manifestPath,
    VariableResolverOptions resolverOptions = {});

} // namespace PicoATE::Core