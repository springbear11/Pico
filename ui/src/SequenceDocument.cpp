#include "SequenceDocument.h"

#include "PicoATE/Core/SequenceCompiler.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>
#include <QSaveFile>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>

#include <algorithm>
#include <utility>

namespace PicoATE::Ui {

namespace {

bool mutateNestedSteps(QJsonObject& owner,
                       const QVector<int>& parentSteps,
                       int depth,
                       const std::function<bool(QJsonArray&)>& mutation)
{
    auto steps = owner.value("steps").toArray();
    if (depth == parentSteps.size()) {
        if (!mutation(steps)) {
            return false;
        }
        owner.insert("steps", steps);
        return true;
    }

    const int index = parentSteps[depth];
    if (index < 0 || index >= steps.size() || !steps[index].isObject()) {
        return false;
    }

    auto child = steps[index].toObject();
    if (!mutateNestedSteps(child, parentSteps, depth + 1, mutation)) {
        return false;
    }
    steps[index] = child;
    owner.insert("steps", steps);
    return true;
}

QJsonObject nestedObject(const QJsonObject& group, const QVector<int>& stepIndices)
{
    QJsonObject current = group;
    for (const int index : stepIndices) {
        const auto steps = current.value("steps").toArray();
        if (index < 0 || index >= steps.size() || !steps[index].isObject()) {
            return {};
        }
        current = steps[index].toObject();
    }
    return current;
}

bool isCompositeKind(QString kind)
{
    kind = kind.trimmed().toLower();
    kind.remove('-');
    kind.remove('_');
    return kind == "testitem" || kind == "composite" ||
           kind == "loop" || kind == "forloop";
}

QString normalizedGroupKind(const QJsonObject& group)
{
    auto kind = group.value(QStringLiteral("kind")).toString(
        group.value(QStringLiteral("type")).toString());
    kind = kind.trimmed().toLower();
    kind.remove(QLatin1Char('-'));
    kind.remove(QLatin1Char('_'));
    kind.remove(QLatin1Char(' '));
    return kind;
}

bool isStandardGroupKind(const QString& kind)
{
    return kind == QStringLiteral("setup") ||
           kind == QStringLiteral("main") ||
           kind == QStringLiteral("cleanup");
}

QJsonObject standardGroup(const QString& kind, const QString& name)
{
    return QJsonObject{{QStringLiteral("id"), kind},
                       {QStringLiteral("name"), name},
                       {QStringLiteral("kind"), kind},
                       {QStringLiteral("steps"), QJsonArray{}}};
}

bool pathStartsWith(const QVector<int>& path, const QVector<int>& prefix)
{
    if (prefix.size() > path.size()) {
        return false;
    }
    return std::equal(prefix.cbegin(), prefix.cend(), path.cbegin());
}

bool pathLess(const SequenceItemPath& left, const SequenceItemPath& right)
{
    if (left.groupIndex != right.groupIndex) {
        return left.groupIndex < right.groupIndex;
    }
    return std::lexicographical_compare(
        left.stepIndices.cbegin(), left.stepIndices.cend(),
        right.stepIndices.cbegin(), right.stepIndices.cend());
}

QJsonObject objectAtRoot(const QJsonObject& root, const SequenceItemPath& path)
{
    const auto groups = root.value(QStringLiteral("groups")).toArray();
    if (!path.isValid() || path.groupIndex < 0 || path.groupIndex >= groups.size() ||
        !groups[path.groupIndex].isObject()) {
        return {};
    }
    return nestedObject(groups[path.groupIndex].toObject(), path.stepIndices);
}

QString idScopeKey(const SequenceItemPath& parentPath)
{
    if (parentPath.stepIndices.isEmpty()) {
        return QStringLiteral("top-level");
    }
    QString key = QString::number(parentPath.groupIndex);
    for (const int index : parentPath.stepIndices) {
        key += QLatin1Char('/') + QString::number(index);
    }
    return key;
}

constexpr auto clipboardSourceNodePathKey = "__picoateClipboardSourceNodePath";

QString sequenceNodePath(const QJsonObject& root, const SequenceItemPath& path)
{
    const auto groups = root.value(QStringLiteral("groups")).toArray();
    if (!path.isValid() || path.groupIndex < 0 || path.groupIndex >= groups.size() ||
        !groups[path.groupIndex].isObject()) {
        return {};
    }
    if (path.stepIndices.isEmpty()) {
        return {};
    }

    auto owner = groups[path.groupIndex].toObject();
    QStringList segments;
    for (int depth = 0; depth < path.stepIndices.size(); ++depth) {
        const auto steps = owner.value(QStringLiteral("steps")).toArray();
        const int index = path.stepIndices[depth];
        if (index < 0 || index >= steps.size() || !steps[index].isObject()) {
            return {};
        }
        const auto step = steps[index].toObject();
        const auto id = step.value(QStringLiteral("id")).toString().trimmed();
        const auto key = step.value(QStringLiteral("key")).toString().trimmed();
        const auto segment = depth == 0 || key.isEmpty() ? id : key;
        if (segment.isEmpty()) {
            return {};
        }
        segments.push_back(segment);
        owner = step;
    }
    return segments.join(QLatin1Char('.'));
}

QString childNodePath(const QString& parentPath, const QString& segment)
{
    return parentPath.isEmpty() ? segment
                                : QStringLiteral("%1.%2").arg(parentPath, segment);
}

bool insertStepCopy(QJsonObject& root,
                    const SequenceItemPath& sourcePath,
                    const QJsonObject& copy)
{
    auto groups = root.value(QStringLiteral("groups")).toArray();
    if (!sourcePath.isValid() || sourcePath.stepIndices.isEmpty() ||
        sourcePath.groupIndex < 0 || sourcePath.groupIndex >= groups.size() ||
        !groups[sourcePath.groupIndex].isObject()) {
        return false;
    }

    auto group = groups[sourcePath.groupIndex].toObject();
    auto parentSteps = sourcePath.stepIndices;
    const int sourceRow = parentSteps.takeLast();
    if (!mutateNestedSteps(group, parentSteps, 0, [&](QJsonArray& steps) {
            if (sourceRow < 0 || sourceRow >= steps.size()) {
                return false;
            }
            steps.insert(sourceRow + 1, copy);
            return true;
        })) {
        return false;
    }
    groups[sourcePath.groupIndex] = group;
    root.insert(QStringLiteral("groups"), groups);
    return true;
}

bool mutateStepInRoot(
    QJsonObject& root,
    const SequenceItemPath& path,
    const std::function<bool(QJsonObject&)>& mutation)
{
    auto groups = root.value(QStringLiteral("groups")).toArray();
    if (!path.isValid() || path.stepIndices.isEmpty() ||
        path.groupIndex < 0 || path.groupIndex >= groups.size() ||
        !groups[path.groupIndex].isObject()) {
        return false;
    }

    auto group = groups[path.groupIndex].toObject();
    auto parentSteps = path.stepIndices;
    const int row = parentSteps.takeLast();
    if (!mutateNestedSteps(group, parentSteps, 0, [&](QJsonArray& steps) {
            if (row < 0 || row >= steps.size() || !steps[row].isObject()) {
                return false;
            }
            auto step = steps[row].toObject();
            if (!mutation(step)) {
                return false;
            }
            steps[row] = step;
            return true;
        })) {
        return false;
    }
    groups[path.groupIndex] = group;
    root.insert(QStringLiteral("groups"), groups);
    return true;
}

struct ResourceRegionLocation {
    QString id;
    int groupIndex = -1;
    QVector<int> parentSteps;
    int entryRow = -1;
    int exitRow = -1;
};

void collectResourceRegionLocations(const QJsonArray& steps,
                                    int groupIndex,
                                    const QVector<int>& parentSteps,
                                    QVector<ResourceRegionLocation>& locations,
                                    int& nextNumber)
{
    for (int row = 0; row < steps.size(); ++row) {
        if (!steps[row].isObject()) {
            continue;
        }
        const auto step = steps[row].toObject();
        const auto start = step.value(QStringLiteral("resourceRegionStart"))
                               .toObject();
        const auto regionId = start.value(QStringLiteral("id")).toString();
        if (!regionId.isEmpty()) {
            ResourceRegionLocation location;
            location.id = regionId;
            location.groupIndex = groupIndex;
            location.parentSteps = parentSteps;
            location.entryRow = row;
            location.exitRow = step.value(QStringLiteral("resourceRegionEnd"))
                                       .toString() == regionId
                ? row
                : -1;
            for (int candidate = row + 1;
                 location.exitRow < 0 && candidate < steps.size(); ++candidate) {
                if (steps[candidate].toObject()
                        .value(QStringLiteral("resourceRegionEnd"))
                        .toString() == regionId) {
                    location.exitRow = candidate;
                    break;
                }
            }
            locations.push_back(std::move(location));
            const auto suffix = regionId.section(QLatin1Char('-'), -1).toInt();
            nextNumber = qMax(nextNumber, suffix + 1);
        }

        auto childParent = parentSteps;
        childParent.push_back(row);
        collectResourceRegionLocations(step.value(QStringLiteral("steps")).toArray(),
                                       groupIndex,
                                       childParent,
                                       locations,
                                       nextNumber);
    }
}

QVector<ResourceRegionLocation> resourceRegionLocations(const QJsonObject& root,
                                                        int* nextNumber = nullptr)
{
    QVector<ResourceRegionLocation> locations;
    int number = 1;
    const auto groups = root.value(QStringLiteral("groups")).toArray();
    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        collectResourceRegionLocations(
            groups[groupIndex].toObject().value(QStringLiteral("steps")).toArray(),
            groupIndex,
            {},
            locations,
            number);
    }
    if (nextNumber) {
        *nextNumber = number;
    }
    return locations;
}

SequenceItemPath resourceRegionPath(const ResourceRegionLocation& location,
                                    int row)
{
    SequenceItemPath path;
    path.groupIndex = location.groupIndex;
    path.stepIndices = location.parentSteps;
    path.stepIndices.push_back(row);
    return path;
}

bool sameResourceRegionParent(const SequenceItemPath& path,
                              const ResourceRegionLocation& location)
{
    if (path.groupIndex != location.groupIndex || path.stepIndices.isEmpty()) {
        return false;
    }
    auto parentSteps = path.stepIndices;
    parentSteps.removeLast();
    return parentSteps == location.parentSteps;
}

bool pathFallsInsideResourceRegion(const SequenceItemPath& path,
                                   const ResourceRegionLocation& location)
{
    if (path.groupIndex != location.groupIndex ||
        path.stepIndices.size() <= location.parentSteps.size()) {
        return false;
    }
    for (int depth = 0; depth < location.parentSteps.size(); ++depth) {
        if (path.stepIndices[depth] != location.parentSteps[depth]) {
            return false;
        }
    }
    const int branchRow = path.stepIndices[location.parentSteps.size()];
    if (location.exitRow == location.entryRow) {
        return branchRow == location.entryRow;
    }
    if (location.exitRow < 0) {
        return branchRow >= location.entryRow;
    }
    if (path.stepIndices.size() == location.parentSteps.size() + 1) {
        return branchRow >= location.entryRow && branchRow <= location.exitRow;
    }
    // The UNLOCK row releases after its own node; its child subtree is a
    // separate execution scope and is not painted as part of the interval.
    return branchRow >= location.entryRow && branchRow < location.exitRow;
}

void stripResourceRegionMarkers(QJsonObject& step)
{
    step.remove(QStringLiteral("resourceRegionStart"));
    step.remove(QStringLiteral("resourceRegionEnd"));
    auto children = step.value(QStringLiteral("steps")).toArray();
    for (int index = 0; index < children.size(); ++index) {
        if (!children[index].isObject()) {
            continue;
        }
        auto child = children[index].toObject();
        stripResourceRegionMarkers(child);
        children[index] = child;
    }
    if (step.contains(QStringLiteral("steps"))) {
        step.insert(QStringLiteral("steps"), children);
    }
}

bool removeStepFromRoot(QJsonObject& root, const SequenceItemPath& path)
{
    auto groups = root.value(QStringLiteral("groups")).toArray();
    if (!path.isValid() || path.stepIndices.isEmpty() ||
        path.groupIndex < 0 || path.groupIndex >= groups.size() ||
        !groups[path.groupIndex].isObject()) {
        return false;
    }

    auto group = groups[path.groupIndex].toObject();
    auto parentSteps = path.stepIndices;
    const int row = parentSteps.takeLast();
    if (!mutateNestedSteps(group, parentSteps, 0, [&](QJsonArray& steps) {
            if (row < 0 || row >= steps.size()) {
                return false;
            }
            steps.removeAt(row);
            return true;
        })) {
        return false;
    }
    groups[path.groupIndex] = group;
    root.insert(QStringLiteral("groups"), groups);
    return true;
}

bool normalizeSiblingPaths(QVector<SequenceItemPath> paths,
                           SequenceItemPath& parentPath,
                           QVector<int>& rows,
                           bool requireContiguous = true)
{
    if (paths.isEmpty()) {
        return false;
    }
    std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        if (left.groupIndex != right.groupIndex) {
            return left.groupIndex < right.groupIndex;
        }
        return std::lexicographical_compare(
            left.stepIndices.cbegin(), left.stepIndices.cend(),
            right.stepIndices.cbegin(), right.stepIndices.cend());
    });
    parentPath = paths.first();
    if (!parentPath.isValid() || parentPath.stepIndices.isEmpty()) {
        return false;
    }
    parentPath.stepIndices.removeLast();
    for (const auto& path : paths) {
        if (!path.isValid() || path.stepIndices.isEmpty()) {
            return false;
        }
        auto candidateParent = path;
        const int row = candidateParent.stepIndices.takeLast();
        if (candidateParent != parentPath || row < 0 || rows.contains(row)) {
            return false;
        }
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end());
    if (requireContiguous) {
        for (int index = 1; index < rows.size(); ++index) {
            if (rows[index] != rows[index - 1] + 1) {
                return false;
            }
        }
    }
    return true;
}

QString normalizedAbsolutePath(const QString& path)
{
    return path.trimmed().isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

QVector<UiDiagnostic> diagnosticsForRoot(const QJsonObject& root)
{
    QVector<UiDiagnostic> diagnostics;
    if (root.isEmpty()) {
        return diagnostics;
    }

    PicoATE::Core::SequenceCompiler compiler;
    const auto result = compiler.compileJson(root);
    diagnostics.reserve(result.errors.size() + result.warnings.size());
    for (const auto& diagnostic : result.errors) {
        diagnostics.push_back({UiDiagnosticSeverity::Error,
                               diagnostic.path,
                               diagnostic.message,
                               diagnostic.suggestion});
    }
    for (const auto& diagnostic : result.warnings) {
        diagnostics.push_back({UiDiagnosticSeverity::Warning,
                               diagnostic.path,
                               diagnostic.message,
                               diagnostic.suggestion});
    }
    return diagnostics;
}

} // namespace

class SequenceRootCommand final : public QUndoCommand
{
public:
    SequenceRootCommand(SequenceDocument* document,
                        QJsonObject before,
                        QJsonObject after,
                        QString text,
                        QVector<SequenceItemPath> changedItemPaths)
        : m_document(document)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_changedItemPaths(std::move(changedItemPaths))
    {
        setText(std::move(text));
    }

    void undo() override
    {
        m_document->applyCommandRoot(m_before, m_changedItemPaths);
    }

    void redo() override
    {
        m_document->applyCommandRoot(m_after, m_changedItemPaths);
    }

private:
    SequenceDocument* m_document = nullptr;
    QJsonObject m_before;
    QJsonObject m_after;
    QVector<SequenceItemPath> m_changedItemPaths;
};

QString SequenceItemPath::jsonPath() const
{
    if (!isValid()) {
        return {};
    }
    QString path = QString("groups[%1]").arg(groupIndex);
    for (const int index : stepIndices) {
        path += QString(".steps[%1]").arg(index);
    }
    return path;
}

SequenceDiagnosticTarget parseSequenceDiagnosticTarget(const QString& path)
{
    SequenceDiagnosticTarget target;
    const auto groupsPrefix = QStringLiteral("groups[");
    const auto stepsPrefix = QStringLiteral(".steps[");
    if (!path.startsWith(groupsPrefix)) {
        return target;
    }

    int position = groupsPrefix.size();
    const int groupEnd = path.indexOf(']', position);
    if (groupEnd < position) {
        return {};
    }
    bool groupOk = false;
    const int groupIndex = path.mid(position, groupEnd - position).toInt(&groupOk);
    if (!groupOk || groupIndex < 0) {
        return {};
    }
    target.itemPath.groupIndex = groupIndex;
    position = groupEnd + 1;

    while (path.mid(position).startsWith(stepsPrefix)) {
        position += stepsPrefix.size();
        const int stepEnd = path.indexOf(']', position);
        if (stepEnd < position) {
            return {};
        }
        bool stepOk = false;
        const int stepIndex = path.mid(position, stepEnd - position).toInt(&stepOk);
        if (!stepOk || stepIndex < 0) {
            return {};
        }
        target.itemPath.stepIndices.push_back(stepIndex);
        position = stepEnd + 1;
    }

    if (position == path.size()) {
        return target;
    }
    if (path.at(position) != QLatin1Char('.')) {
        return {};
    }
    target.fieldPath = path.mid(position + 1);
    return target;
}

SequenceDocument::SequenceDocument(QObject* parent)
    : QObject(parent)
{
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(200);
    connect(m_undoStack, &QUndoStack::cleanChanged,
            this, [this](bool clean) { setModified(!clean); });

    m_validationTimer = new QTimer(this);
    m_validationTimer->setSingleShot(true);
    connect(m_validationTimer, &QTimer::timeout,
            this, &SequenceDocument::startAsyncValidation);

    m_validationPool = new QThreadPool(this);
    m_validationPool->setMaxThreadCount(1);
    m_validationPool->setExpiryTimeout(30000);
}

SequenceDocument::~SequenceDocument()
{
    if (m_validationTimer) {
        m_validationTimer->stop();
    }
    if (m_validationPool) {
        m_validationPool->clear();
        m_validationPool->waitForDone();
    }
    if (!m_undoStack) {
        return;
    }
    QObject::disconnect(m_undoStack, nullptr, nullptr, nullptr);
    m_undoStack->clear();
}

QString SequenceDocument::filePath() const
{
    return m_filePath;
}

QString SequenceDocument::displayName() const
{
    if (!m_filePath.isEmpty()) {
        return QFileInfo(m_filePath).fileName();
    }
    return m_root.value("name").toString(tr("Untitled Sequence"));
}

bool SequenceDocument::isModified() const
{
    return m_modified;
}

bool SequenceDocument::isEmpty() const
{
    return m_root.isEmpty();
}

quint64 SequenceDocument::revision() const
{
    return m_revision;
}

QVector<SequenceItemPath> SequenceDocument::lastChangedItemPaths() const
{
    return m_lastChangedItemPaths;
}

QJsonObject SequenceDocument::rootObject() const
{
    return m_root;
}

QJsonArray SequenceDocument::sequenceVariables() const
{
    return m_root.value(QStringLiteral("variables")).toArray();
}

QVector<UiDiagnostic> SequenceDocument::diagnostics() const
{
    return m_diagnostics;
}

SequenceDocumentSnapshot SequenceDocument::snapshot() const
{
    SequenceDocumentSnapshot result;
    result.filePath = m_filePath;
    result.root = m_root;
    result.json = m_root.isEmpty()
        ? QByteArray{}
        : QJsonDocument(m_root).toJson(QJsonDocument::Compact);
    result.revision = m_revision;
    return result;
}

QUndoStack* SequenceDocument::undoStack() const
{
    return m_undoStack;
}

QJsonValue rewriteScopedStepReferences(const QJsonValue& value,
                                       const QString& oldRootPath,
                                       const QString& newRootPath)
{
    if (oldRootPath.isEmpty() || newRootPath.isEmpty() ||
        oldRootPath == newRootPath) {
        return value;
    }
    if (value.isString()) {
        auto text = value.toString();
        text.replace(QStringLiteral("${step:%1.").arg(oldRootPath),
                     QStringLiteral("${step:%1.").arg(newRootPath));
        text.replace(QStringLiteral("${step:%1}").arg(oldRootPath),
                     QStringLiteral("${step:%1}").arg(newRootPath));
        return text;
    }
    if (value.isArray()) {
        auto array = value.toArray();
        for (int index = 0; index < array.size(); ++index) {
            array[index] = rewriteScopedStepReferences(array[index], oldRootPath, newRootPath);
        }
        return array;
    }
    if (value.isObject()) {
        auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            it.value() = rewriteScopedStepReferences(it.value(), oldRootPath, newRootPath);
        }
        return object;
    }
    return value;
}

void assignCopiedRootId(QJsonObject& copy,
                        QString oldRootPath,
                        const QString& newParentPath,
                        const QString& newRootId)
{
    const auto oldRootId = copy.value(QStringLiteral("id")).toString().trimmed();
    const auto oldRootSegment = oldRootPath.section(QLatin1Char('.'), -1);
    if (oldRootPath.isEmpty()) {
        oldRootPath = oldRootId;
    }
    const auto newRootPath = childNodePath(newParentPath, newRootId);
    copy.remove(QString::fromLatin1(clipboardSourceNodePathKey));
    copy = rewriteScopedStepReferences(copy, oldRootPath, newRootPath).toObject();
    if (!oldRootSegment.isEmpty() && oldRootSegment != oldRootPath) {
        copy = rewriteScopedStepReferences(copy, oldRootSegment, newRootId).toObject();
    }
    if (!oldRootId.isEmpty() && oldRootId != oldRootSegment) {
        copy = rewriteScopedStepReferences(copy, oldRootId, newRootId).toObject();
    }
    copy.insert(QStringLiteral("id"), newRootId);
}

bool SequenceDocument::replaceRootObject(QJsonObject root)
{
    if (root.isEmpty()) {
        return false;
    }
    return commitRoot(std::move(root), tr("Update Sequence References"));
}

bool SequenceDocument::setSequenceVariables(QJsonArray variables)
{
    if (m_root.isEmpty()) {
        return false;
    }
    auto root = m_root;
    if (variables.isEmpty()) {
        root.remove(QStringLiteral("variables"));
    } else {
        root.insert(QStringLiteral("variables"), std::move(variables));
    }
    if (root == m_root) {
        return true;
    }
    return commitRoot(std::move(root), tr("Edit Sequence Variables"));
}

bool SequenceDocument::load(const QString& filePath)
{
    const auto absolutePath = normalizedAbsolutePath(filePath);
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setLoadError({}, tr("Failed to open sequence file: %1").arg(absolutePath),
                     file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setLoadError(QString("offset %1").arg(parseError.offset),
                     parseError.errorString(),
                     tr("Fix the JSON syntax before opening the document"));
        return false;
    }
    if (!document.isObject()) {
        setLoadError({}, tr("Sequence JSON root must be an object"));
        return false;
    }

    acceptRoot(document.object(), absolutePath);
    return true;
}

bool SequenceDocument::save(QString* errorMessage)
{
    if (m_filePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Sequence file path is empty");
        }
        return false;
    }
    return saveAs(m_filePath, errorMessage);
}

bool SequenceDocument::saveAs(const QString& filePath, QString* errorMessage)
{
    const auto absolutePath = normalizedAbsolutePath(filePath);
    if (absolutePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Sequence file path is empty");
        }
        return false;
    }

    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const auto bytes = QJsonDocument(m_root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const bool pathChanged = m_filePath != absolutePath;
    m_filePath = absolutePath;
    m_undoStack->setClean();
    if (pathChanged) {
        emit filePathChanged(m_filePath);
    }
    scheduleValidation(0);
    return true;
}

void SequenceDocument::clear()
{
    const bool hadPath = !m_filePath.isEmpty();
    m_undoStack->clear();
    m_filePath.clear();
    m_root = {};
    m_lastChangedItemPaths.clear();
    m_diagnostics.clear();
    ++m_revision;
    ++m_validationGeneration;
    if (m_validationTimer) {
        m_validationTimer->stop();
    }
    if (m_validationPool) {
        m_validationPool->clear();
    }
    setModified(false);
    if (hadPath) {
        emit filePathChanged({});
    }
    emit diagnosticsChanged();
    emit documentChanged();
}

bool SequenceDocument::ensureStandardGroups()
{
    if (m_root.isEmpty() || !m_root.value(QStringLiteral("groups")).isArray()) {
        return false;
    }

    auto groups = m_root.value(QStringLiteral("groups")).toArray();
    bool changed = false;
    QSet<QString> kinds;
    for (int index = 0; index < groups.size(); ++index) {
        if (!groups[index].isObject()) {
            continue;
        }
        auto group = groups[index].toObject();
        const auto kind = normalizedGroupKind(group);
        kinds.insert(kind);
        if (isStandardGroupKind(kind) && group.contains(QStringLiteral("enabled"))) {
            group.remove(QStringLiteral("enabled"));
            groups[index] = group;
            changed = true;
        }
    }

    if (!kinds.contains(QStringLiteral("setup"))) {
        groups.insert(0, standardGroup(QStringLiteral("setup"), tr("Setup")));
        kinds.insert(QStringLiteral("setup"));
        changed = true;
    }
    if (!kinds.contains(QStringLiteral("main"))) {
        int insertionIndex = 0;
        while (insertionIndex < groups.size() &&
               normalizedGroupKind(groups[insertionIndex].toObject()) ==
                   QStringLiteral("setup")) {
            ++insertionIndex;
        }
        groups.insert(insertionIndex,
                      standardGroup(QStringLiteral("main"), tr("Main")));
        changed = true;
    }
    if (!kinds.contains(QStringLiteral("cleanup"))) {
        groups.push_back(
            standardGroup(QStringLiteral("cleanup"), tr("Cleanup")));
        changed = true;
    }
    if (!changed) {
        return false;
    }

    auto root = m_root;
    root.insert(QStringLiteral("groups"), groups);
    return commitRoot(std::move(root), tr("Normalize Setup/Main/Cleanup Groups"));
}

bool SequenceDocument::isStandardGroup(const SequenceItemPath& path) const
{
    return path.isGroup() &&
           isStandardGroupKind(normalizedGroupKind(objectAt(path)));
}

QJsonObject SequenceDocument::objectAt(const SequenceItemPath& path) const
{
    const auto groups = m_root.value("groups").toArray();
    if (!path.isValid() || path.groupIndex >= groups.size() ||
        !groups[path.groupIndex].isObject()) {
        return {};
    }

    const auto group = groups[path.groupIndex].toObject();
    return path.stepIndices.isEmpty() ? group : nestedObject(group, path.stepIndices);
}

SequenceItemPath SequenceDocument::findItemPath(
    const QJsonObject& object,
    const SequenceItemPath& preferredPath) const
{
    if (object.isEmpty()) {
        return {};
    }
    if (preferredPath.isValid() && objectAt(preferredPath) == object) {
        return preferredPath;
    }

    QVector<SequenceItemPath> exactMatches;
    QVector<SequenceItemPath> identityMatches;
    const auto identityMatchesObject = [&object](const QJsonObject& candidate) {
        const auto sourceKey = object.value(QStringLiteral("key")).toString();
        const auto candidateKey = candidate.value(QStringLiteral("key")).toString();
        if (!sourceKey.isEmpty() || !candidateKey.isEmpty()) {
            return !sourceKey.isEmpty() && sourceKey == candidateKey;
        }
        return object.value(QStringLiteral("id")).toString() ==
                   candidate.value(QStringLiteral("id")).toString() &&
               object.value(QStringLiteral("kind")).toString(
                   object.value(QStringLiteral("type")).toString()) ==
                   candidate.value(QStringLiteral("kind")).toString(
                       candidate.value(QStringLiteral("type")).toString()) &&
               object.value(QStringLiteral("name")).toString() ==
                   candidate.value(QStringLiteral("name")).toString();
    };
    const std::function<void(const QJsonArray&, const SequenceItemPath&)> collect =
        [&](const QJsonArray& steps, const SequenceItemPath& parentPath) {
            for (int row = 0; row < steps.size(); ++row) {
                const auto candidate = steps.at(row).toObject();
                if (candidate.isEmpty()) {
                    continue;
                }
                auto path = parentPath;
                path.stepIndices.push_back(row);
                if (candidate == object) {
                    exactMatches.push_back(path);
                }
                if (identityMatchesObject(candidate)) {
                    identityMatches.push_back(path);
                }
                collect(candidate.value(QStringLiteral("steps")).toArray(), path);
            }
        };

    const auto groups = m_root.value(QStringLiteral("groups")).toArray();
    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        const auto group = groups.at(groupIndex).toObject();
        SequenceItemPath groupPath{groupIndex, {}};
        if (object == group) {
            exactMatches.push_back(groupPath);
        }
        collect(group.value(QStringLiteral("steps")).toArray(), groupPath);
    }
    if (exactMatches.size() == 1) {
        return exactMatches.first();
    }
    return identityMatches.size() == 1 ? identityMatches.first()
                                       : SequenceItemPath{};
}

bool SequenceDocument::canContainSteps(const SequenceItemPath& path) const
{
    if (path.isGroup()) {
        return true;
    }
    const auto object = objectAt(path);
    return !object.isEmpty() && isCompositeKind(
        object.value("kind").toString(object.value("type").toString()));
}

bool SequenceDocument::insertStep(const SequenceItemPath& parentPath,
                                  int row,
                                  QJsonObject step)
{
    if (!canContainSteps(parentPath)) {
        return false;
    }
    if (step.isEmpty()) {
        const auto id = nextStepId(parentPath);
        step.insert("id", id);
        step.insert("name", tr("New Step %1").arg(id));
        step.insert("kind", "action");
        step.insert("enabled", true);
    } else if (step.value("id").toString().trimmed().isEmpty()) {
        step.insert("id", nextStepId(parentPath));
    }

    return mutateSteps(parentPath, [&](QJsonArray& steps) {
        const int insertionRow = row < 0 ? steps.size() : qBound(0, row, steps.size());
        steps.insert(insertionRow, step);
        return true;
    }, tr("Add Step"));
}

bool SequenceDocument::removeStep(const SequenceItemPath& path)
{
    if (!path.isValid() || path.stepIndices.isEmpty()) {
        return false;
    }

    auto parentPath = path;
    const int row = parentPath.stepIndices.takeLast();
    return mutateSteps(parentPath, [&](QJsonArray& steps) {
        if (row < 0 || row >= steps.size()) {
            return false;
        }
        steps.removeAt(row);
        return true;
    }, tr("Delete Step"));
}

bool SequenceDocument::removeSteps(QVector<SequenceItemPath> paths)
{
    if (paths.isEmpty()) {
        return false;
    }
    std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        if (left.groupIndex != right.groupIndex) {
            return left.groupIndex > right.groupIndex;
        }
        return std::lexicographical_compare(
            right.stepIndices.cbegin(), right.stepIndices.cend(),
            left.stepIndices.cbegin(), left.stepIndices.cend());
    });
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    auto root = m_root;
    for (const auto& path : paths) {
        if (!removeStepFromRoot(root, path)) {
            return false;
        }
    }
    return commitRoot(std::move(root), tr("Delete Selected Steps"));
}

bool SequenceDocument::setStepsEnabled(
    const QVector<SequenceItemPath>& paths,
    bool enabled)
{
    if (paths.isEmpty()) {
        return false;
    }

    auto root = m_root;
    QVector<SequenceItemPath> uniquePaths;
    for (const auto& path : paths) {
        if (uniquePaths.contains(path)) {
            continue;
        }
        uniquePaths.push_back(path);
        if (!mutateStepInRoot(root, path, [enabled](QJsonObject& step) {
                step.insert(QStringLiteral("enabled"), enabled);
                return true;
            })) {
            return false;
        }
    }
    return commitRoot(
        std::move(root), enabled ? tr("Enable Selected Steps")
                                 : tr("Disable Selected Steps"),
        std::move(uniquePaths));
}

bool SequenceDocument::duplicateStep(const SequenceItemPath& path)
{
    return duplicateSteps({path});
}

QVector<QJsonObject> SequenceDocument::copiedSteps(
    QVector<SequenceItemPath> paths) const
{
    std::sort(paths.begin(), paths.end(), pathLess);
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    QVector<SequenceItemPath> roots;
    QVector<QJsonObject> result;
    for (const auto& path : paths) {
        if (!path.isValid() || path.stepIndices.isEmpty()) {
            continue;
        }
        const auto object = objectAtRoot(m_root, path);
        if (object.isEmpty()) {
            continue;
        }
        const bool coveredByParent = std::any_of(
            roots.cbegin(), roots.cend(), [&path](const auto& root) {
                return root.groupIndex == path.groupIndex &&
                       pathStartsWith(path.stepIndices, root.stepIndices);
        });
        if (!coveredByParent) {
            roots.push_back(path);
            auto clipboardObject = object;
            clipboardObject.insert(
                QString::fromLatin1(clipboardSourceNodePathKey),
                sequenceNodePath(m_root, path));
            result.push_back(std::move(clipboardObject));
        }
    }
    return result;
}

bool SequenceDocument::pasteSteps(
    const SequenceItemPath& parentPath,
    int row,
    const QVector<QJsonObject>& sourceSteps,
    QVector<SequenceItemPath>* pastedPaths)
{
    if (!canContainSteps(parentPath) || sourceSteps.isEmpty()) {
        return false;
    }
    for (const auto& step : sourceSteps) {
        if (step.isEmpty()) {
            return false;
        }
    }

    const auto firstId = nextStepId(parentPath);
    bool numericId = false;
    int nextNumber = firstId.toInt(&numericId);
    if (!numericId) {
        return false;
    }
    const int idWidth = firstId.size();
    const auto destinationParentNodePath = sequenceNodePath(m_root, parentPath);
    QVector<QJsonObject> copies;
    copies.reserve(sourceSteps.size());
    for (const auto& source : sourceSteps) {
        auto copy = source;
        stripResourceRegionMarkers(copy);
        const auto originalName = copy.value(QStringLiteral("name")).toString(
            copy.value(QStringLiteral("id")).toString());
        const auto oldRootPath = copy.take(
            QString::fromLatin1(clipboardSourceNodePathKey)).toString();
        const auto newRootId = QStringLiteral("%1").arg(
            nextNumber++, idWidth, 10, QLatin1Char('0'));
        copy.remove(QStringLiteral("key"));
        assignCopiedRootId(copy,
                           oldRootPath,
                           destinationParentNodePath,
                           newRootId);
        copy.insert(QStringLiteral("name"), tr("%1 Copy").arg(originalName));
        copies.push_back(std::move(copy));
    }

    const int existingCount = objectAt(parentPath)
                                  .value(QStringLiteral("steps"))
                                  .toArray()
                                  .size();
    const int insertionRow = row < 0 ? existingCount
                                     : qBound(0, row, existingCount);
    const bool changed = mutateSteps(
        parentPath,
        [insertionRow, &copies](QJsonArray& steps) {
            for (int index = 0; index < copies.size(); ++index) {
                steps.insert(insertionRow + index, copies[index]);
            }
            return true;
        },
        copies.size() == 1 ? tr("Paste Step") : tr("Paste Selected Steps"));
    if (!changed) {
        return false;
    }

    if (pastedPaths) {
        pastedPaths->clear();
        pastedPaths->reserve(copies.size());
        for (int index = 0; index < copies.size(); ++index) {
            auto path = parentPath;
            path.stepIndices.push_back(insertionRow + index);
            pastedPaths->push_back(std::move(path));
        }
    }
    return true;
}

bool SequenceDocument::duplicateSteps(QVector<SequenceItemPath> paths)
{
    if (paths.isEmpty()) {
        return false;
    }
    std::sort(paths.begin(), paths.end(), pathLess);
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    QVector<SequenceItemPath> roots;
    for (const auto& path : paths) {
        if (!path.isValid() || path.stepIndices.isEmpty() ||
            objectAtRoot(m_root, path).isEmpty()) {
            return false;
        }
        const bool coveredBySelectedParent = std::any_of(
            roots.cbegin(), roots.cend(), [&path](const auto& root) {
                return root.groupIndex == path.groupIndex &&
                       pathStartsWith(path.stepIndices, root.stepIndices);
            });
        if (!coveredBySelectedParent) {
            roots.push_back(path);
        }
    }
    if (roots.isEmpty()) {
        return false;
    }

    struct IdCursor {
        int maximum = 0;
        int width = 2;
    };
    QHash<QString, IdCursor> cursors;
    auto nextId = [&](const SequenceItemPath& parentPath) {
        const auto scope = idScopeKey(parentPath);
        auto cursor = cursors.find(scope);
        if (cursor == cursors.end()) {
            IdCursor created;
            created.width = parentPath.stepIndices.isEmpty() ? 3 : 2;
            QJsonArray siblings;
            if (parentPath.stepIndices.isEmpty()) {
                const auto groups = m_root.value(QStringLiteral("groups")).toArray();
                for (const auto& groupValue : groups) {
                    for (const auto& stepValue :
                         groupValue.toObject().value(QStringLiteral("steps")).toArray()) {
                        siblings.push_back(stepValue);
                    }
                }
            } else {
                siblings = objectAtRoot(m_root, parentPath)
                               .value(QStringLiteral("steps")).toArray();
            }
            for (const auto& siblingValue : siblings) {
                bool numeric = false;
                const auto id = siblingValue.toObject()
                                    .value(QStringLiteral("id")).toString();
                const int number = id.toInt(&numeric);
                if (numeric) {
                    created.maximum = qMax(created.maximum, number);
                    created.width = qMax(created.width, id.size());
                }
            }
            cursor = cursors.insert(scope, created);
        }
        ++cursor->maximum;
        return QStringLiteral("%1").arg(
            cursor->maximum, cursor->width, 10, QLatin1Char('0'));
    };

    struct CopyEntry {
        SequenceItemPath sourcePath;
        QJsonObject object;
    };
    QVector<CopyEntry> copies;
    copies.reserve(roots.size());
    for (const auto& path : roots) {
        auto parentPath = path;
        parentPath.stepIndices.removeLast();
        auto copy = objectAtRoot(m_root, path);
        stripResourceRegionMarkers(copy);
        const auto originalName = copy.value(QStringLiteral("name")).toString(
            copy.value(QStringLiteral("id")).toString());
        const auto oldRootPath = sequenceNodePath(m_root, path);
        const auto newRootId = nextId(parentPath);
        copy.remove(QStringLiteral("key"));
        assignCopiedRootId(copy,
                           oldRootPath,
                           sequenceNodePath(m_root, parentPath),
                           newRootId);
        copy.insert(QStringLiteral("name"), tr("%1 Copy").arg(originalName));
        copies.push_back({path, std::move(copy)});
    }

    std::sort(copies.begin(), copies.end(), [](const auto& left, const auto& right) {
        return pathLess(right.sourcePath, left.sourcePath);
    });
    auto root = m_root;
    for (const auto& copy : copies) {
        if (!insertStepCopy(root, copy.sourcePath, copy.object)) {
            return false;
        }
    }
    return commitRoot(
        std::move(root), roots.size() == 1 ? tr("Duplicate Step")
                                           : tr("Duplicate Selected Steps"));
}

bool SequenceDocument::moveStep(const SequenceItemPath& path, int offset)
{
    if (!path.isValid() || path.stepIndices.isEmpty() || offset == 0) {
        return false;
    }

    auto parentPath = path;
    const int sourceRow = parentPath.stepIndices.takeLast();
    return mutateSteps(parentPath, [&](QJsonArray& steps) {
        const int targetRow = sourceRow + offset;
        if (sourceRow < 0 || sourceRow >= steps.size() ||
            targetRow < 0 || targetRow >= steps.size()) {
            return false;
        }
        const auto value = steps.takeAt(sourceRow);
        steps.insert(targetRow, value);
        return true;
    }, tr("Move Step"));
}

bool SequenceDocument::relocateStep(
    const SequenceItemPath& sourcePath,
    const SequenceItemPath& destinationParent,
    int destinationRow,
    SequenceItemPath* relocatedPath)
{
    QVector<SequenceItemPath> relocatedPaths;
    if (!relocateSteps({sourcePath}, destinationParent, destinationRow,
                       &relocatedPaths) || relocatedPaths.size() != 1) {
        return false;
    }
    if (relocatedPath) {
        *relocatedPath = relocatedPaths.first();
    }
    return true;
}

bool SequenceDocument::relocateSteps(
    QVector<SequenceItemPath> sourcePaths,
    const SequenceItemPath& destinationParent,
    int destinationRow,
    QVector<SequenceItemPath>* relocatedPaths)
{
    SequenceItemPath sourceParent;
    QVector<int> sourceRows;
    if (!normalizeSiblingPaths(std::move(sourcePaths), sourceParent,
                               sourceRows, false) ||
        !destinationParent.isValid() || !canContainSteps(destinationParent)) {
        return false;
    }

    QVector<SequenceItemPath> orderedSourcePaths;
    QVector<QJsonObject> sourceObjects;
    QVector<QString> oldRootPaths;
    QVector<QString> newRootPaths;
    orderedSourcePaths.reserve(sourceRows.size());
    sourceObjects.reserve(sourceRows.size());
    oldRootPaths.reserve(sourceRows.size());
    newRootPaths.reserve(sourceRows.size());

    const auto destinationParentNodePath = sequenceNodePath(
        m_root, destinationParent);
    for (const int sourceRow : std::as_const(sourceRows)) {
        auto sourcePath = sourceParent;
        sourcePath.stepIndices.push_back(sourceRow);
        if (sourcePath.groupIndex == destinationParent.groupIndex &&
            pathStartsWith(destinationParent.stepIndices,
                           sourcePath.stepIndices)) {
            return false;
        }

        auto sourceObject = objectAt(sourcePath);
        if (sourceObject.isEmpty()) {
            return false;
        }
        const auto oldRootPath = sequenceNodePath(m_root, sourcePath);
        const auto sourceId = sourceObject.value(QStringLiteral("id"))
                                  .toString().trimmed();
        const auto sourceKey = sourceObject.value(QStringLiteral("key"))
                                   .toString().trimmed();
        const auto newRootSegment = destinationParent.stepIndices.isEmpty() ||
                sourceKey.isEmpty()
            ? sourceId
            : sourceKey;
        const auto newRootPath = childNodePath(destinationParentNodePath,
                                               newRootSegment);
        const auto oldRootSegment = oldRootPath.section(QLatin1Char('.'), -1);
        sourceObject = rewriteScopedStepReferences(
            sourceObject, oldRootPath, newRootPath).toObject();
        if (!oldRootSegment.isEmpty() && oldRootSegment != newRootSegment) {
            sourceObject = rewriteScopedStepReferences(
                sourceObject, oldRootSegment, newRootSegment).toObject();
        }

        orderedSourcePaths.push_back(std::move(sourcePath));
        sourceObjects.push_back(std::move(sourceObject));
        oldRootPaths.push_back(oldRootPath);
        newRootPaths.push_back(newRootPath);
    }

    auto adjustedDestination = destinationParent;
    int adjustedRow = destinationRow;
    if (sourceParent.groupIndex == destinationParent.groupIndex &&
        pathStartsWith(adjustedDestination.stepIndices,
                       sourceParent.stepIndices)) {
        if (sourceParent == adjustedDestination) {
            if (adjustedRow >= 0) {
                adjustedRow -= static_cast<int>(std::count_if(
                    sourceRows.cbegin(), sourceRows.cend(),
                    [adjustedRow](int sourceRow) {
                        return sourceRow < adjustedRow;
                    }));
            }
        } else {
            const int depth = sourceParent.stepIndices.size();
            const int destinationBranch = adjustedDestination.stepIndices[depth];
            adjustedDestination.stepIndices[depth] -=
                static_cast<int>(std::count_if(
                    sourceRows.cbegin(), sourceRows.cend(),
                    [destinationBranch](int sourceRow) {
                        return sourceRow < destinationBranch;
                    }));
        }
    }

    auto root = m_root;
    auto groups = root.value(QStringLiteral("groups")).toArray();
    if (sourceParent.groupIndex < 0 ||
        sourceParent.groupIndex >= groups.size() ||
        adjustedDestination.groupIndex < 0 ||
        adjustedDestination.groupIndex >= groups.size()) {
        return false;
    }

    auto sourceGroup = groups[sourceParent.groupIndex].toObject();
    if (!mutateNestedSteps(
            sourceGroup, sourceParent.stepIndices, 0,
            [&sourceRows](QJsonArray& steps) {
                for (auto it = sourceRows.crbegin();
                     it != sourceRows.crend(); ++it) {
                    if (*it < 0 || *it >= steps.size()) {
                        return false;
                    }
                    steps.removeAt(*it);
                }
                return true;
            })) {
        return false;
    }
    groups[sourceParent.groupIndex] = sourceGroup;

    auto destinationGroup = groups[adjustedDestination.groupIndex].toObject();
    int insertedRow = -1;
    if (!mutateNestedSteps(
            destinationGroup, adjustedDestination.stepIndices, 0,
            [&](QJsonArray& steps) {
                insertedRow = adjustedRow < 0
                    ? steps.size()
                    : qBound(0, adjustedRow, steps.size());
                for (int index = 0; index < sourceObjects.size(); ++index) {
                    steps.insert(insertedRow + index, sourceObjects[index]);
                }
                return true;
            })) {
        return false;
    }
    groups[adjustedDestination.groupIndex] = destinationGroup;
    root.insert(QStringLiteral("groups"), groups);
    for (int index = 0; index < oldRootPaths.size(); ++index) {
        root = rewriteScopedStepReferences(
            root, oldRootPaths[index], newRootPaths[index]).toObject();
    }
    if (!commitRoot(std::move(root), sourceObjects.size() == 1
            ? tr("Move Step") : tr("Move Selected Steps"))) {
        return false;
    }

    if (relocatedPaths) {
        relocatedPaths->clear();
        relocatedPaths->reserve(sourceObjects.size());
        for (int index = 0; index < sourceObjects.size(); ++index) {
            auto relocatedPath = adjustedDestination;
            relocatedPath.stepIndices.push_back(insertedRow + index);
            relocatedPaths->push_back(std::move(relocatedPath));
        }
    }
    return true;
}

bool SequenceDocument::canWrapStepsInTestItem(
    const QVector<SequenceItemPath>& paths) const
{
    SequenceItemPath parentPath;
    QVector<int> rows;
    if (!normalizeSiblingPaths(paths, parentPath, rows) ||
        !canContainSteps(parentPath)) {
        return false;
    }
    const auto steps = objectAt(parentPath).value(QStringLiteral("steps")).toArray();
    return !rows.isEmpty() && rows.last() < steps.size();
}

bool SequenceDocument::wrapStepsInTestItem(
    QVector<SequenceItemPath> paths,
    SequenceItemPath* testItemPath)
{
    SequenceItemPath parentPath;
    QVector<int> rows;
    if (!normalizeSiblingPaths(std::move(paths), parentPath, rows) ||
        !canContainSteps(parentPath)) {
        return false;
    }
    const auto sourceSteps = objectAt(parentPath)
                                 .value(QStringLiteral("steps")).toArray();
    if (rows.isEmpty() || rows.last() >= sourceSteps.size()) {
        return false;
    }

    const auto id = nextStepId(parentPath);
    const int firstRow = rows.first();
    QJsonObject testItem;
    testItem.insert(QStringLiteral("id"), id);
    testItem.insert(QStringLiteral("name"), tr("Test Item %1").arg(id));
    testItem.insert(QStringLiteral("kind"), QStringLiteral("testItem"));
    testItem.insert(QStringLiteral("enabled"), true);

    const auto parentNodePath = sequenceNodePath(m_root, parentPath);
    const auto testItemNodePath = childNodePath(parentNodePath, id);
    QVector<std::pair<QString, QString>> referenceRemaps;

    auto root = m_root;
    auto groups = root.value(QStringLiteral("groups")).toArray();
    if (parentPath.groupIndex < 0 || parentPath.groupIndex >= groups.size() ||
        !groups[parentPath.groupIndex].isObject()) {
        return false;
    }
    auto group = groups[parentPath.groupIndex].toObject();
    if (!mutateNestedSteps(
            group, parentPath.stepIndices, 0,
            [&](QJsonArray& steps) {
            QJsonArray children;
            QSet<QString> childIds;
            for (const int row : rows) {
                auto child = steps.at(row).toObject();
                const auto existingId = child.value(QStringLiteral("id"))
                                            .toString().trimmed();
                if (!existingId.isEmpty()) {
                    childIds.insert(existingId);
                }
                children.push_back(child);
            }
            int nextChildId = 1;
            for (int index = 0; index < children.size(); ++index) {
                auto child = children[index].toObject();
                if (child.value(QStringLiteral("id")).toString().trimmed().isEmpty()) {
                    QString childId;
                    do {
                        childId = QStringLiteral("%1").arg(
                            nextChildId++, 2, 10, QLatin1Char('0'));
                    } while (childIds.contains(childId));
                    child.insert(QStringLiteral("id"), childId);
                    childIds.insert(childId);
                    children[index] = child;
                }

                auto sourcePath = parentPath;
                sourcePath.stepIndices.push_back(rows[index]);
                const auto oldNodePath = sequenceNodePath(m_root, sourcePath);
                const auto childKey = child.value(QStringLiteral("key"))
                                          .toString().trimmed();
                const auto childId = child.value(QStringLiteral("id"))
                                         .toString().trimmed();
                const auto newNodePath = childNodePath(
                    testItemNodePath, childKey.isEmpty() ? childId : childKey);
                if (!oldNodePath.isEmpty() && !newNodePath.isEmpty() &&
                    oldNodePath != newNodePath) {
                    referenceRemaps.push_back({oldNodePath, newNodePath});
                }
            }
            for (int index = rows.size() - 1; index >= 0; --index) {
                steps.removeAt(rows[index]);
            }
            auto container = testItem;
            container.insert(QStringLiteral("steps"), children);
            steps.insert(firstRow, container);
            return true;
        })) {
        return false;
    }
    groups[parentPath.groupIndex] = group;
    root.insert(QStringLiteral("groups"), groups);
    for (const auto& [oldNodePath, newNodePath] : referenceRemaps) {
        root = rewriteScopedStepReferences(root, oldNodePath, newNodePath).toObject();
    }

    const bool changed = commitRoot(std::move(root), tr("Wrap Steps in TestItem"));
    if (changed && testItemPath) {
        *testItemPath = parentPath;
        testItemPath->stepIndices.push_back(firstRow);
    }
    return changed;
}

bool SequenceDocument::setItemValue(const SequenceItemPath& path,
                                    const QString& key,
                                    const QJsonValue& value)
{
    if (!path.isValid() || key.isEmpty()) {
        return false;
    }

    auto root = m_root;
    auto groups = root.value("groups").toArray();
    if (path.groupIndex < 0 || path.groupIndex >= groups.size() ||
        !groups[path.groupIndex].isObject()) {
        return false;
    }

    auto group = groups[path.groupIndex].toObject();
    if (path.stepIndices.isEmpty()) {
        group.insert(key, value);
    } else {
        auto parentSteps = path.stepIndices;
        const int row = parentSteps.takeLast();
        if (!mutateNestedSteps(group, parentSteps, 0, [&](QJsonArray& steps) {
                if (row < 0 || row >= steps.size() || !steps[row].isObject()) {
                    return false;
                }
                auto step = steps[row].toObject();
                step.insert(key, value);
                steps[row] = step;
                return true;
            })) {
            return false;
        }
    }

    groups[path.groupIndex] = group;
    root.insert("groups", groups);
    return commitRoot(std::move(root), tr("Edit Property"), {path});
}

bool SequenceDocument::replaceItemObject(const SequenceItemPath& path,
                                         QJsonObject object)
{
    if (!path.isValid() || object.isEmpty()) {
        return false;
    }

    auto root = m_root;
    auto groups = root.value("groups").toArray();
    if (path.groupIndex < 0 || path.groupIndex >= groups.size() ||
        !groups[path.groupIndex].isObject()) {
        return false;
    }

    if (path.stepIndices.isEmpty()) {
        groups[path.groupIndex] = std::move(object);
    } else {
        auto group = groups[path.groupIndex].toObject();
        auto parentSteps = path.stepIndices;
        const int row = parentSteps.takeLast();
        if (!mutateNestedSteps(group, parentSteps, 0, [&](QJsonArray& steps) {
                if (row < 0 || row >= steps.size() || !steps[row].isObject()) {
                    return false;
                }
                steps[row] = object;
                return true;
            })) {
            return false;
        }
        groups[path.groupIndex] = group;
    }

    root.insert("groups", groups);
    return commitRoot(std::move(root), tr("Apply Properties"), {path});
}

QString SequenceDocument::pendingResourceRegionId() const
{
    for (const auto& location : resourceRegionLocations(m_root)) {
        if (location.exitRow < 0) {
            return location.id;
        }
    }
    return {};
}

bool SequenceDocument::placeNextResourceRegionBoundary(
    const SequenceItemPath& path,
    const QString& resourceId,
    bool* placedEntry,
    QString* errorMessage)
{
    if (placedEntry) {
        *placedEntry = false;
    }
    const auto fail = [errorMessage](const QString& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };
    if (!path.isValid() || path.stepIndices.isEmpty()) {
        return fail(tr("Select a Step for LOCK or UNLOCK"));
    }

    auto root = m_root;
    const auto selected = objectAt(path);
    if (selected.isEmpty()) {
        return fail(tr("The selected sequence step no longer exists"));
    }
    const auto selectedStart = selected.value(QStringLiteral("resourceRegionStart"))
                                   .toObject();
    const auto selectedEnd = selected.value(QStringLiteral("resourceRegionEnd"))
                                 .toString();

    int nextNumber = 1;
    const auto locations = resourceRegionLocations(root, &nextNumber);
    QVector<ResourceRegionLocation> pendingLocations;
    for (const auto& location : locations) {
        if (location.exitRow < 0) {
            pendingLocations.push_back(location);
        }
    }
    if (pendingLocations.size() > 1) {
        return fail(tr("The sequence contains more than one unfinished resource region"));
    }
    const bool completesSingleItem = pendingLocations.size() == 1 &&
        selectedEnd.isEmpty() && !selectedStart.isEmpty() &&
        sameResourceRegionParent(path, pendingLocations.first()) &&
        path.stepIndices.last() == pendingLocations.first().entryRow &&
        selectedStart.value(QStringLiteral("id")).toString() ==
            pendingLocations.first().id;
    if ((selected.contains(QStringLiteral("resourceRegionStart")) ||
         selected.contains(QStringLiteral("resourceRegionEnd"))) &&
        !completesSingleItem) {
        return fail(tr("The selected step already contains LOCK or UNLOCK"));
    }

    QJsonObject updated = selected;
    if (pendingLocations.isEmpty()) {
        for (const auto& location : locations) {
            if (pathFallsInsideResourceRegion(path, location)) {
                return fail(tr("Nested or overlapping LOCK/UNLOCK intervals are not supported"));
            }
        }
        const auto regionId = QStringLiteral("resource-region-%1")
                                  .arg(nextNumber, 3, 10, QLatin1Char('0'));
        QJsonArray resources;
        const auto normalizedResource = resourceId.trimmed();
        if (!normalizedResource.isEmpty()) {
            resources.push_back(QJsonObject{
                {QStringLiteral("resourceId"), normalizedResource},
                {QStringLiteral("mode"), QStringLiteral("exclusive")}});
        }
        updated.insert(
            QStringLiteral("resourceRegionStart"),
            QJsonObject{{QStringLiteral("id"), regionId},
                        {QStringLiteral("resources"), resources}});
        if (placedEntry) {
            *placedEntry = true;
        }
    } else {
        const auto& pending = pendingLocations.first();
        if (!sameResourceRegionParent(path, pending)) {
            return fail(tr("UNLOCK must be a sibling of LOCK under the same parent"));
        }
        const int selectedRow = path.stepIndices.last();
        if (selectedRow < pending.entryRow ||
            (selectedRow == pending.entryRow && !completesSingleItem)) {
            return fail(tr("UNLOCK must be placed on LOCK itself or a later sibling step"));
        }
        updated.insert(QStringLiteral("resourceRegionEnd"), pending.id);
    }

    if (!mutateStepInRoot(root, path, [&](QJsonObject& step) {
            step = updated;
            return true;
        })) {
        return fail(tr("The selected sequence step no longer exists"));
    }
    return commitRoot(std::move(root), tr("Place Resource Boundary"));
}

bool SequenceDocument::completePendingResourceRegion(
    const SequenceItemPath& path,
    const QStringList& resourceIds,
    QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };
    if (!path.isValid() || path.stepIndices.isEmpty()) {
        return fail(tr("Select a Step for UNLOCK"));
    }

    QStringList normalizedResources;
    for (const auto& resourceId : resourceIds) {
        const auto normalized = resourceId.trimmed();
        if (!normalized.isEmpty() &&
            !normalizedResources.contains(normalized, Qt::CaseInsensitive)) {
            normalizedResources.push_back(normalized);
        }
    }
    if (normalizedResources.isEmpty()) {
        return fail(tr("Select at least one hardware resource"));
    }

    const auto locations = resourceRegionLocations(m_root);
    QVector<ResourceRegionLocation> pendingLocations;
    for (const auto& location : locations) {
        if (location.exitRow < 0) {
            pendingLocations.push_back(location);
        }
    }
    if (pendingLocations.size() != 1) {
        return fail(pendingLocations.isEmpty()
                        ? tr("Place a LOCK before selecting UNLOCK")
                        : tr("The sequence contains more than one unfinished resource region"));
    }

    const auto& pending = pendingLocations.first();
    if (!sameResourceRegionParent(path, pending)) {
        return fail(tr("UNLOCK must be a sibling of LOCK under the same parent"));
    }
    const int selectedRow = path.stepIndices.last();
    if (selectedRow < pending.entryRow) {
        return fail(tr("UNLOCK must be placed on LOCK itself or a later sibling step"));
    }

    const auto selected = objectAt(path);
    if (selected.isEmpty()) {
        return fail(tr("The selected sequence step no longer exists"));
    }
    const auto selectedStart = selected.value(QStringLiteral("resourceRegionStart"))
                                   .toObject();
    const bool completesSingleItem = selectedRow == pending.entryRow &&
        selectedStart.value(QStringLiteral("id")).toString() == pending.id;
    if ((!selectedStart.isEmpty() && !completesSingleItem) ||
        selected.contains(QStringLiteral("resourceRegionEnd"))) {
        return fail(tr("The selected step already contains LOCK or UNLOCK"));
    }

    QJsonArray resources;
    for (const auto& resourceId : normalizedResources) {
        resources.push_back(QJsonObject{
            {QStringLiteral("resourceId"), resourceId},
            {QStringLiteral("mode"), QStringLiteral("exclusive")}});
    }

    auto root = m_root;
    if (!mutateStepInRoot(root, path, [&](QJsonObject& step) {
            step.insert(QStringLiteral("resourceRegionEnd"), pending.id);
            return true;
        })) {
        return fail(tr("The selected sequence step no longer exists"));
    }
    if (!mutateStepInRoot(
            root,
            resourceRegionPath(pending, pending.entryRow),
            [&](QJsonObject& step) {
                auto start = step.value(QStringLiteral("resourceRegionStart"))
                                 .toObject();
                if (start.value(QStringLiteral("id")).toString() != pending.id) {
                    return false;
                }
                start.insert(QStringLiteral("resources"), resources);
                step.insert(QStringLiteral("resourceRegionStart"), start);
                return true;
            })) {
        return fail(tr("The resource region entry no longer exists"));
    }
    return commitRoot(std::move(root), tr("Complete Resource Region"));
}

QStringList SequenceDocument::resourceRegionResources(const QString& regionId) const
{
    QStringList result;
    for (const auto& location : resourceRegionLocations(m_root)) {
        if (location.id != regionId) {
            continue;
        }
        const auto start = objectAt(resourceRegionPath(location, location.entryRow))
                               .value(QStringLiteral("resourceRegionStart"))
                               .toObject();
        for (const auto& resourceValue :
             start.value(QStringLiteral("resources")).toArray()) {
            const auto resourceId = resourceValue.toObject()
                                        .value(QStringLiteral("resourceId"))
                                        .toString().trimmed();
            if (!resourceId.isEmpty() && !result.contains(resourceId)) {
                result.push_back(resourceId);
            }
        }
        return result;
    }
    return result;
}

bool SequenceDocument::setResourceRegionResources(const QString& regionId,
                                                   const QStringList& resourceIds,
                                                   QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };
    QStringList normalized;
    for (const auto& resourceId : resourceIds) {
        const auto value = resourceId.trimmed();
        if (!value.isEmpty() && !normalized.contains(value, Qt::CaseInsensitive)) {
            normalized.push_back(value);
        }
    }
    if (normalized.isEmpty()) {
        return fail(tr("Select at least one hardware resource"));
    }

    const auto locations = resourceRegionLocations(m_root);
    const auto location = std::find_if(
        locations.cbegin(), locations.cend(), [&](const ResourceRegionLocation& value) {
            return value.id == regionId;
        });
    if (location == locations.cend()) {
        return fail(tr("The resource region no longer exists"));
    }

    QJsonArray resources;
    for (const auto& resourceId : normalized) {
        resources.push_back(QJsonObject{
            {QStringLiteral("resourceId"), resourceId},
            {QStringLiteral("mode"), QStringLiteral("exclusive")}});
    }
    auto root = m_root;
    if (!mutateStepInRoot(root,
                          resourceRegionPath(*location, location->entryRow),
                          [&](QJsonObject& step) {
                              auto start = step.value(
                                  QStringLiteral("resourceRegionStart")).toObject();
                              start.insert(QStringLiteral("resources"), resources);
                              step.insert(QStringLiteral("resourceRegionStart"), start);
                              return true;
                          })) {
        return fail(tr("The resource region no longer exists"));
    }
    if (root == m_root) {
        return true;
    }
    return commitRoot(std::move(root), tr("Set Resource Region Hardware"));
}

bool SequenceDocument::removeResourceRegionEndAt(const SequenceItemPath& path,
                                                  QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };
    if (!path.isValid() || path.stepIndices.isEmpty()) {
        return fail(tr("Select an UNLOCK marker"));
    }
    const auto selected = objectAt(path);
    if (selected.value(QStringLiteral("resourceRegionEnd")).toString().isEmpty()) {
        return fail(tr("The selected step is not an UNLOCK marker"));
    }
    auto root = m_root;
    if (!mutateStepInRoot(root, path, [](QJsonObject& step) {
            step.remove(QStringLiteral("resourceRegionEnd"));
            return true;
        })) {
        return fail(tr("The selected sequence step no longer exists"));
    }
    return commitRoot(std::move(root), tr("Remove Resource Region End"));
}

bool SequenceDocument::clearResourceRegionAt(const SequenceItemPath& path,
                                             QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };
    if (!path.isValid() || path.stepIndices.isEmpty()) {
        return fail(tr("Select a step inside the resource region"));
    }

    const auto selected = objectAt(path);
    QString regionId = selected.value(QStringLiteral("resourceRegionStart"))
                           .toObject().value(QStringLiteral("id")).toString();
    if (regionId.isEmpty()) {
        regionId = selected.value(QStringLiteral("resourceRegionEnd")).toString();
    }
    const auto locations = resourceRegionLocations(m_root);
    auto location = locations.cend();
    if (!regionId.isEmpty()) {
        location = std::find_if(
            locations.cbegin(), locations.cend(), [&](const ResourceRegionLocation& value) {
                return value.id == regionId;
            });
    } else {
        location = std::find_if(
            locations.cbegin(), locations.cend(), [&](const ResourceRegionLocation& value) {
                return pathFallsInsideResourceRegion(path, value);
            });
    }
    if (location == locations.cend()) {
        return fail(tr("The selected step is not inside a resource region"));
    }

    auto root = m_root;
    if (!mutateStepInRoot(root,
                          resourceRegionPath(*location, location->entryRow),
                          [](QJsonObject& step) {
                              step.remove(QStringLiteral("resourceRegionStart"));
                              return true;
                          })) {
        return fail(tr("The resource region entry no longer exists"));
    }
    if (location->exitRow >= 0 &&
        !mutateStepInRoot(root,
                          resourceRegionPath(*location, location->exitRow),
                          [](QJsonObject& step) {
                              step.remove(QStringLiteral("resourceRegionEnd"));
                              return true;
                          })) {
        return fail(tr("The resource region exit no longer exists"));
    }
    return commitRoot(std::move(root), tr("Remove Resource Region"));
}

bool SequenceDocument::mutateSteps(const SequenceItemPath& parentPath,
                                   const StepsMutation& mutation,
                                   const QString& commandText)
{
    auto root = m_root;
    auto groups = root.value("groups").toArray();
    if (!parentPath.isValid() || parentPath.groupIndex >= groups.size() ||
        !groups[parentPath.groupIndex].isObject()) {
        return false;
    }

    auto group = groups[parentPath.groupIndex].toObject();
    if (!mutateNestedSteps(group, parentPath.stepIndices, 0, mutation)) {
        return false;
    }
    groups[parentPath.groupIndex] = group;
    root.insert("groups", groups);
    return commitRoot(std::move(root), commandText);
}

bool SequenceDocument::commitRoot(
    QJsonObject root,
    const QString& commandText,
    QVector<SequenceItemPath> changedItemPaths)
{
    if (root == m_root) {
        return false;
    }
    m_undoStack->push(new SequenceRootCommand(
        this, m_root, std::move(root), commandText,
        std::move(changedItemPaths)));
    return true;
}

void SequenceDocument::applyCommandRoot(
    QJsonObject root,
    const QVector<SequenceItemPath>& changedItemPaths)
{
    m_root = std::move(root);
    m_lastChangedItemPaths = changedItemPaths;
    ++m_revision;
    scheduleValidation();
    emit documentChanged();
}

QString SequenceDocument::nextStepId(const SequenceItemPath& parentPath) const
{
    int maximum = 0;
    int width = parentPath.stepIndices.isEmpty() ? 3 : 2;

    QJsonArray siblings;
    if (parentPath.stepIndices.isEmpty()) {
        const auto groups = m_root.value("groups").toArray();
        for (const auto& groupValue : groups) {
            const auto steps = groupValue.toObject().value("steps").toArray();
            for (const auto& stepValue : steps) {
                bool ok = false;
                const auto id = stepValue.toObject().value("id").toString();
                const int number = id.toInt(&ok);
                if (ok) {
                    maximum = qMax(maximum, number);
                    width = qMax(width, id.size());
                }
            }
        }
    } else {
        siblings = objectAt(parentPath).value("steps").toArray();
        for (const auto& stepValue : siblings) {
            bool ok = false;
            const auto id = stepValue.toObject().value("id").toString();
            const int number = id.toInt(&ok);
            if (ok) {
                maximum = qMax(maximum, number);
                width = qMax(width, id.size());
            }
        }
    }

    return QString("%1").arg(maximum + 1, width, 10, QLatin1Char('0'));
}

void SequenceDocument::acceptRoot(QJsonObject root, QString filePath)
{
    const bool pathChanged = m_filePath != filePath;
    m_undoStack->clear();
    m_root = std::move(root);
    m_lastChangedItemPaths.clear();
    m_filePath = std::move(filePath);
    ++m_revision;
    setModified(false);
    validate();
    if (pathChanged) {
        emit filePathChanged(m_filePath);
    }
    emit documentChanged();
}

void SequenceDocument::setModified(bool modified)
{
    if (m_modified == modified) {
        return;
    }
    m_modified = modified;
    emit modifiedChanged(m_modified);
}

void SequenceDocument::validate()
{
    if (m_validationTimer) {
        m_validationTimer->stop();
    }
    ++m_validationGeneration;
    auto diagnostics = diagnosticsForRoot(m_root);
    if (m_diagnostics == diagnostics) {
        return;
    }
    m_diagnostics = std::move(diagnostics);
    emit diagnosticsChanged();
}

void SequenceDocument::scheduleValidation(int delayMs)
{
    if (!m_validationTimer) {
        validate();
        return;
    }
    ++m_validationGeneration;
    m_validationTimer->start(qMax(0, delayMs));
}

void SequenceDocument::startAsyncValidation()
{
    if (!m_validationPool) {
        validate();
        return;
    }

    const auto root = m_root;
    const auto revision = m_revision;
    const auto generation = m_validationGeneration;
    m_validationPool->clear();
    m_validationPool->start([this, root, revision, generation] {
        auto diagnostics = diagnosticsForRoot(root);
        QMetaObject::invokeMethod(
            this,
            [this, revision, generation,
             diagnostics = std::move(diagnostics)]() mutable {
                applyValidationResult(revision, generation,
                                      std::move(diagnostics));
            },
            Qt::QueuedConnection);
    });
}

void SequenceDocument::applyValidationResult(
    quint64 revision, quint64 generation,
    QVector<UiDiagnostic> diagnostics)
{
    if (revision != m_revision || generation != m_validationGeneration) {
        return;
    }
    if (m_diagnostics == diagnostics) {
        return;
    }
    m_diagnostics = std::move(diagnostics);
    emit diagnosticsChanged();
}

void SequenceDocument::setLoadError(QString path,
                                    QString message,
                                    QString suggestion)
{
    ++m_validationGeneration;
    if (m_validationTimer) {
        m_validationTimer->stop();
    }
    if (m_validationPool) {
        m_validationPool->clear();
    }
    m_diagnostics = {{UiDiagnosticSeverity::Error,
                      std::move(path),
                      std::move(message),
                      std::move(suggestion)}};
    emit diagnosticsChanged();
}

} // namespace PicoATE::Ui
