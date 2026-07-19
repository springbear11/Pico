#pragma once

#include "PluginCatalog.h"
#include "SequenceDocument.h"

#include <QHash>

namespace PicoATE::Ui {

struct StepOutputExpressionCandidate {
    QString stepPath;
    QString stepName;
    QString outputKey;
    QString outputName;
    QString expression;
    PluginParameterType type = PluginParameterType::String;
    QString unit;
};

struct FollowingStepReferenceCandidate {
    QString stepPath;
    QString stepName;
    QString kind;
};

QVector<StepOutputExpressionCandidate> buildStepOutputExpressionCandidates(
    const QJsonObject& sequence,
    const SequenceItemPath& currentPath,
    const QVector<PluginManifest>& plugins,
    const QHash<QString, QString>& pluginByDeviceId = {});

QVector<FollowingStepReferenceCandidate> buildFollowingStepReferenceCandidates(
    const QJsonObject& sequence,
    const SequenceItemPath& currentPath);

} // namespace PicoATE::Ui
