#include "StepOutputCatalog.h"

#include <QJsonArray>

#include <algorithm>
#include <utility>

namespace PicoATE::Ui {

namespace {

QString stepNodeSegment(const QJsonObject& step, bool topLevel)
{
    const auto id = step.value(QStringLiteral("id")).toString().trimmed();
    const auto key = step.value(QStringLiteral("key")).toString().trimmed();
    return topLevel || key.isEmpty() ? id : key;
}

const PluginFunctionDefinition* findPluginFunction(
    const QJsonObject& step,
    const QVector<PluginManifest>& plugins,
    const QHash<QString, QString>& pluginByDeviceId)
{
    auto moduleId = step.value(QStringLiteral("moduleId")).toString().trimmed();
    if (moduleId == QStringLiteral("device")) {
        const auto deviceId = step.value(QStringLiteral("inputs")).toObject()
                                  .value(QStringLiteral("deviceId")).toString();
        moduleId = pluginByDeviceId.value(deviceId);
    }
    const auto functionId = step.value(QStringLiteral("function")).toString().trimmed();
    for (const auto& plugin : plugins) {
        if (plugin.moduleId != moduleId) {
            continue;
        }
        for (const auto& function : plugin.functions) {
            if (function.id == functionId) {
                return &function;
            }
        }
    }
    return nullptr;
}

bool collectOutputCandidates(
    const QJsonArray& steps,
    int groupIndex,
    QVector<int> parentIndices,
    const QString& parentNodePath,
    bool parentEnabled,
    const SequenceItemPath& currentPath,
    const QVector<PluginManifest>& plugins,
    const QHash<QString, QString>& pluginByDeviceId,
    QVector<StepOutputExpressionCandidate>& candidates)
{
    for (int row = 0; row < steps.size(); ++row) {
        if (!steps[row].isObject()) {
            continue;
        }
        auto path = SequenceItemPath{groupIndex, parentIndices};
        path.stepIndices.push_back(row);
        if (path == currentPath) {
            return true;
        }

        const auto step = steps[row].toObject();
        const bool enabled = parentEnabled &&
            step.value(QStringLiteral("enabled")).toBool(true);
        const auto segment = stepNodeSegment(step, parentNodePath.isEmpty());
        const auto nodePath = parentNodePath.isEmpty() || segment.isEmpty()
            ? segment
            : QStringLiteral("%1.%2").arg(parentNodePath, segment);
        if (enabled && !nodePath.isEmpty()) {
            if (const auto* function = findPluginFunction(
                    step, plugins, pluginByDeviceId)) {
                const auto stepName = step.value(QStringLiteral("name"))
                                          .toString(step.value(QStringLiteral("id")).toString());
                for (const auto& output : function->outputs) {
                    if (output.key.trimmed().isEmpty()) {
                        continue;
                    }
                    candidates.push_back({
                        nodePath,
                        stepName,
                        output.key,
                        output.name.isEmpty() ? output.key : output.name,
                        QStringLiteral("${step:%1.outputs.%2}").arg(nodePath, output.key),
                        output.type,
                        output.unit});
                }
            }
        }

        auto childIndices = parentIndices;
        childIndices.push_back(row);
        if (collectOutputCandidates(
                step.value(QStringLiteral("steps")).toArray(),
                groupIndex,
                std::move(childIndices),
                nodePath,
                enabled,
                currentPath,
                plugins,
                pluginByDeviceId,
                candidates)) {
            return true;
        }
    }
    return false;
}

int groupPhase(const QJsonObject& group)
{
    auto kind = group.value(QStringLiteral("kind")).toString().trimmed().toLower();
    kind.remove(QLatin1Char('-'));
    kind.remove(QLatin1Char('_'));
    if (kind == QStringLiteral("setup")) {
        return 0;
    }
    if (kind == QStringLiteral("cleanup")) {
        return 2;
    }
    return 1;
}

} // namespace

QVector<StepOutputExpressionCandidate> buildStepOutputExpressionCandidates(
    const QJsonObject& sequence,
    const SequenceItemPath& currentPath,
    const QVector<PluginManifest>& plugins,
    const QHash<QString, QString>& pluginByDeviceId)
{
    QVector<StepOutputExpressionCandidate> candidates;
    if (!currentPath.isValid() || currentPath.stepIndices.isEmpty()) {
        return candidates;
    }
    const auto groups = sequence.value(QStringLiteral("groups")).toArray();
    QVector<int> groupOrder;
    groupOrder.reserve(groups.size());
    for (int index = 0; index < groups.size(); ++index) {
        groupOrder.push_back(index);
    }
    std::stable_sort(groupOrder.begin(), groupOrder.end(), [&](int left, int right) {
        return groupPhase(groups[left].toObject()) < groupPhase(groups[right].toObject());
    });
    bool currentFound = false;
    for (const int groupIndex : groupOrder) {
        if (!groups[groupIndex].isObject()) {
            continue;
        }
        const auto group = groups[groupIndex].toObject();
        if (collectOutputCandidates(
                group.value(QStringLiteral("steps")).toArray(),
                groupIndex,
                {},
                {},
                group.value(QStringLiteral("enabled")).toBool(true),
                currentPath,
                plugins,
                pluginByDeviceId,
                candidates)) {
            currentFound = true;
            break;
        }
    }
    if (!currentFound) {
        candidates.clear();
    }
    return candidates;
}

} // namespace PicoATE::Ui
