#include "SequenceDocument.h"

#include "PicoATE/Core/SequenceCompiler.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
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
                           QVector<int>& rows)
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
    for (int index = 1; index < rows.size(); ++index) {
        if (rows[index] != rows[index - 1] + 1) {
            return false;
        }
    }
    return true;
}

QString normalizedAbsolutePath(const QString& path)
{
    return path.trimmed().isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

} // namespace

class SequenceRootCommand final : public QUndoCommand
{
public:
    SequenceRootCommand(SequenceDocument* document,
                        QJsonObject before,
                        QJsonObject after,
                        QString text)
        : m_document(document)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
        setText(std::move(text));
    }

    void undo() override
    {
        m_document->applyCommandRoot(m_before);
    }

    void redo() override
    {
        m_document->applyCommandRoot(m_after);
    }

private:
    SequenceDocument* m_document = nullptr;
    QJsonObject m_before;
    QJsonObject m_after;
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
}

SequenceDocument::~SequenceDocument()
{
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

QJsonObject SequenceDocument::rootObject() const
{
    return m_root;
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
                                       const QString& oldRootId,
                                       const QString& newRootId)
{
    if (value.isString()) {
        auto text = value.toString();
        text.replace(QStringLiteral("${step:%1.").arg(oldRootId),
                     QStringLiteral("${step:%1.").arg(newRootId));
        text.replace(QStringLiteral("${step:%1}").arg(oldRootId),
                     QStringLiteral("${step:%1}").arg(newRootId));
        return text;
    }
    if (value.isArray()) {
        auto array = value.toArray();
        for (int index = 0; index < array.size(); ++index) {
            array[index] = rewriteScopedStepReferences(array[index], oldRootId, newRootId);
        }
        return array;
    }
    if (value.isObject()) {
        auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            it.value() = rewriteScopedStepReferences(it.value(), oldRootId, newRootId);
        }
        return object;
    }
    return value;
}

void assignCopiedRootId(QJsonObject& copy, const QString& newRootId)
{
    const auto oldRootId = copy.value(QStringLiteral("id")).toString();
    copy = rewriteScopedStepReferences(copy, oldRootId, newRootId).toObject();
    copy.insert(QStringLiteral("id"), newRootId);
}

bool SequenceDocument::replaceRootObject(QJsonObject root)
{
    if (root.isEmpty()) {
        return false;
    }
    return commitRoot(std::move(root), tr("Update Sequence References"));
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
    return true;
}

void SequenceDocument::clear()
{
    const bool hadPath = !m_filePath.isEmpty();
    m_undoStack->clear();
    m_filePath.clear();
    m_root = {};
    m_diagnostics.clear();
    ++m_revision;
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
                                 : tr("Disable Selected Steps"));
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
            result.push_back(object);
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
    QVector<QJsonObject> copies;
    copies.reserve(sourceSteps.size());
    for (const auto& source : sourceSteps) {
        auto copy = source;
        const auto originalName = copy.value(QStringLiteral("name")).toString(
            copy.value(QStringLiteral("id")).toString());
        assignCopiedRootId(copy,
                           QStringLiteral("%1").arg(
                               nextNumber++, idWidth, 10, QLatin1Char('0')));
        copy.remove(QStringLiteral("key"));
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
        const auto originalName = copy.value(QStringLiteral("name")).toString(
            copy.value(QStringLiteral("id")).toString());
        assignCopiedRootId(copy, nextId(parentPath));
        copy.remove(QStringLiteral("key"));
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
    if (!sourcePath.isValid() || sourcePath.stepIndices.isEmpty() ||
        !destinationParent.isValid() || !canContainSteps(destinationParent)) {
        return false;
    }
    if (sourcePath.groupIndex == destinationParent.groupIndex &&
        pathStartsWith(destinationParent.stepIndices, sourcePath.stepIndices)) {
        return false;
    }
    const auto sourceObject = objectAt(sourcePath);
    if (sourceObject.isEmpty()) {
        return false;
    }

    auto sourceParent = sourcePath;
    const int sourceRow = sourceParent.stepIndices.takeLast();
    auto adjustedDestination = destinationParent;
    int adjustedRow = destinationRow;
    if (sourceParent == destinationParent && adjustedRow > sourceRow) {
        --adjustedRow;
    }
    if (sourcePath.groupIndex == destinationParent.groupIndex &&
        sourceParent.stepIndices.size() < adjustedDestination.stepIndices.size() &&
        pathStartsWith(adjustedDestination.stepIndices, sourceParent.stepIndices)) {
        const int depth = sourceParent.stepIndices.size();
        if (adjustedDestination.stepIndices[depth] > sourceRow) {
            --adjustedDestination.stepIndices[depth];
        }
    }

    auto root = m_root;
    auto groups = root.value(QStringLiteral("groups")).toArray();
    if (sourcePath.groupIndex < 0 || sourcePath.groupIndex >= groups.size() ||
        destinationParent.groupIndex < 0 ||
        destinationParent.groupIndex >= groups.size()) {
        return false;
    }
    auto sourceGroup = groups[sourcePath.groupIndex].toObject();
    if (!mutateNestedSteps(
            sourceGroup, sourceParent.stepIndices, 0,
            [sourceRow](QJsonArray& steps) {
                if (sourceRow < 0 || sourceRow >= steps.size()) {
                    return false;
                }
                steps.removeAt(sourceRow);
                return true;
            })) {
        return false;
    }
    groups[sourcePath.groupIndex] = sourceGroup;

    auto destinationGroup = groups[adjustedDestination.groupIndex].toObject();
    int insertedRow = -1;
    if (!mutateNestedSteps(
            destinationGroup, adjustedDestination.stepIndices, 0,
            [&](QJsonArray& steps) {
                insertedRow = adjustedRow < 0
                    ? steps.size()
                    : qBound(0, adjustedRow, steps.size());
                steps.insert(insertedRow, sourceObject);
                return true;
            })) {
        return false;
    }
    groups[adjustedDestination.groupIndex] = destinationGroup;
    root.insert(QStringLiteral("groups"), groups);
    if (!commitRoot(std::move(root), tr("Move Step"))) {
        return false;
    }
    if (relocatedPath) {
        *relocatedPath = adjustedDestination;
        relocatedPath->stepIndices.push_back(insertedRow);
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

    const bool changed = mutateSteps(
        parentPath,
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
            }
            for (int index = rows.size() - 1; index >= 0; --index) {
                steps.removeAt(rows[index]);
            }
            auto container = testItem;
            container.insert(QStringLiteral("steps"), children);
            steps.insert(firstRow, container);
            return true;
        },
        tr("Wrap Steps in TestItem"));
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
    return commitRoot(std::move(root), tr("Edit Property"));
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
    return commitRoot(std::move(root), tr("Apply Properties"));
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

bool SequenceDocument::commitRoot(QJsonObject root,
                                  const QString& commandText)
{
    if (root == m_root) {
        return false;
    }
    m_undoStack->push(new SequenceRootCommand(
        this, m_root, std::move(root), commandText));
    return true;
}

void SequenceDocument::applyCommandRoot(QJsonObject root)
{
    m_root = std::move(root);
    ++m_revision;
    validate();
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
    m_diagnostics.clear();
    if (!m_root.isEmpty()) {
        PicoATE::Core::SequenceCompiler compiler;
        const auto result = compiler.compileJson(m_root);
        for (const auto& diagnostic : result.errors) {
            m_diagnostics.push_back({UiDiagnosticSeverity::Error,
                                     diagnostic.path,
                                     diagnostic.message,
                                     diagnostic.suggestion});
        }
        for (const auto& diagnostic : result.warnings) {
            m_diagnostics.push_back({UiDiagnosticSeverity::Warning,
                                     diagnostic.path,
                                     diagnostic.message,
                                     diagnostic.suggestion});
        }
    }
    emit diagnosticsChanged();
}

void SequenceDocument::setLoadError(QString path,
                                    QString message,
                                    QString suggestion)
{
    m_diagnostics = {{UiDiagnosticSeverity::Error,
                      std::move(path),
                      std::move(message),
                      std::move(suggestion)}};
    emit diagnosticsChanged();
}

} // namespace PicoATE::Ui
