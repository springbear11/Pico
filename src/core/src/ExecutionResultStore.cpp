#include "PicoATE/Core/ExecutionResultStore.h"

#include "PicoATE/Core/MeasurementTypes.h"

#include <QMetaType>

namespace PicoATE::Core {

namespace {

bool valueAtPath(QVariant current, const QString& path, QVariant& value)
{
    if (path.isEmpty()) {
        value = current;
        return true;
    }

    const auto parts = path.split('.', Qt::SkipEmptyParts);
    for (const auto& part : parts) {
        if (current.metaType().id() == QMetaType::QVariantMap) {
            const auto map = current.toMap();
            if (!map.contains(part)) {
                return false;
            }
            current = map.value(part);
            continue;
        }
        if (current.metaType().id() == QMetaType::QVariantList) {
            bool indexOk = false;
            const int index = part.toInt(&indexOk);
            const auto list = current.toList();
            if (!indexOk || index < 0 || index >= list.size()) {
                return false;
            }
            current = list[index];
            continue;
        }
        return false;
    }
    value = current;
    return true;
}

} // namespace

std::optional<StepResultReference> parseStepResultReference(const QString& expression)
{
    const auto normalized = expression.trimmed();
    if (!normalized.startsWith("step:")) {
        return std::nullopt;
    }

    const auto body = normalized.mid(5);
    struct FieldMarker {
        QString marker;
        StepResultField field;
    };
    const QVector<FieldMarker> markers = {
        {".outputs", StepResultField::Outputs},
        {".measurements", StepResultField::Measurements},
        {".outcome", StepResultField::Outcome},
    };

    int markerPosition = -1;
    FieldMarker selected;
    for (const auto& marker : markers) {
        const int position = body.indexOf(marker.marker);
        if (position > 0 && (markerPosition < 0 || position < markerPosition)) {
            markerPosition = position;
            selected = marker;
        }
    }
    if (markerPosition < 0) {
        return std::nullopt;
    }

    StepResultReference reference;
    reference.expression = normalized;
    reference.nodeAddress = body.left(markerPosition);
    reference.field = selected.field;
    const int valueStart = markerPosition + selected.marker.size();
    if (valueStart < body.size()) {
        if (body[valueStart] != '.') {
            return std::nullopt;
        }
        reference.valuePath = body.mid(valueStart + 1);
    }
    if (reference.field == StepResultField::Outcome && !reference.valuePath.isEmpty()) {
        return std::nullopt;
    }
    return reference;
}

std::optional<NodeId> resolveStepReferenceNode(const ExecutionPlan& plan,
                                               const NodeId& currentNodeId,
                                               const QString& nodeAddress)
{
    const auto address = nodeAddress.trimmed();
    if (address.isEmpty()) {
        return std::nullopt;
    }
    if (plan.node(address)) {
        return address;
    }

    const auto parent = plan.structuralParentOf(currentNodeId);
    if (parent) {
        const auto siblingPath = QString("%1.%2").arg(*parent, address);
        if (plan.node(siblingPath)) {
            return siblingPath;
        }
    }
    return std::nullopt;
}

ExecutionResultStore::ExecutionResultStore(const ExecutionPlan& plan)
    : m_plan(plan)
{
}

void ExecutionResultStore::commit(const UutId& uutId,
                                  const FrameId& frameId,
                                  const NodeId& nodeId,
                                  int attemptIndex,
                                  const NodeResult& result)
{
    m_results[resultKey(uutId, frameId, nodeId)].push_back(
        {uutId, frameId, nodeId, attemptIndex, result});
}

StepResultLookup ExecutionResultStore::lookup(
    const UutId& uutId,
    const FrameId& frameId,
    const NodeId& currentNodeId,
    const StepResultReference& reference) const
{
    const auto sourceNodeId = resolveStepReferenceNode(m_plan, currentNodeId, reference.nodeAddress);
    if (!sourceNodeId) {
        return {false, {}, "StepReferenceNotFound",
                QString("Referenced step does not exist: %1").arg(reference.nodeAddress)};
    }

    const auto source = latest(uutId, frameId, *sourceNodeId);
    if (!source) {
        return {false, {}, "StepResultUnavailable",
                QString("Referenced step has not produced a result: %1").arg(*sourceNodeId)};
    }

    if (reference.field == StepResultField::Outcome) {
        return {true, nodeOutcomeName(source->result.outcome), {}, {}};
    }
    if (source->result.outcome != NodeOutcome::Passed) {
        return {false, {}, "StepResultNotPassed",
                QString("Referenced step %1 finished as %2")
                    .arg(*sourceNodeId, nodeOutcomeName(source->result.outcome))};
    }

    QVariant root;
    if (reference.field == StepResultField::Outputs) {
        root = source->result.outputs;
    } else {
        root = measurementsToVariant(source->result.measurements);
    }

    QVariant value;
    if (!valueAtPath(root, reference.valuePath, value)) {
        return {false, {}, "StepResultValueNotFound",
                QString("Result field not found: %1 in %2")
                    .arg(reference.valuePath, *sourceNodeId)};
    }
    return {true, value, {}, {}};
}

std::optional<StoredStepResult> ExecutionResultStore::latest(
    const UutId& uutId,
    const FrameId& frameId,
    const NodeId& nodeId) const
{
    const auto values = m_results.constFind(resultKey(uutId, frameId, nodeId));
    if (values == m_results.constEnd() || values->isEmpty()) {
        return std::nullopt;
    }
    return values->last();
}

QVector<StoredStepResult> ExecutionResultStore::history(
    const UutId& uutId,
    const FrameId& frameId,
    const NodeId& nodeId) const
{
    return m_results.value(resultKey(uutId, frameId, nodeId));
}

QString ExecutionResultStore::resultKey(const UutId& uutId,
                                        const FrameId& frameId,
                                        const NodeId& nodeId) const
{
    return QString("%1\x1f%2\x1f%3").arg(uutId, frameId, nodeId);
}

} // namespace PicoATE::Core
