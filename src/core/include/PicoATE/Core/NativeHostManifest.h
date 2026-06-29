#pragma once

#include "PicoATE/Core/VariableResolver.h"

#include <QVariantMap>

namespace PicoATE::Core {

struct NativeHostManifest {
    QString dllPath;
    QString symbol = "PicoATE_Execute";
    int bufferSize = 65536;
    int dllTimeoutMs = 30000;
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
