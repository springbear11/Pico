#include "SequenceTreeModel.h"
#include "ApplicationDiagnostics.h"
#include "PluginFunctionModel.h"

#include <QColor>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <algorithm>
#include <functional>
#include <utility>

namespace PicoATE::Ui {

namespace {

constexpr auto sequencePathMimeType = "application/x-picoate-sequence-item-path";

QJsonObject pathToJson(const SequenceItemPath& path)
{
    QJsonArray steps;
    for (const int index : path.stepIndices) {
        steps.push_back(index);
    }
    return QJsonObject{{"group", path.groupIndex}, {"steps", steps}};
}

SequenceItemPath pathFromJson(const QJsonObject& object)
{
    SequenceItemPath path;
    if (!object.value("group").isDouble() || !object.value("steps").isArray()) {
        return path;
    }
    path.groupIndex = object.value("group").toInt(-1);
    for (const auto& value : object.value("steps").toArray()) {
        if (!value.isDouble()) {
            return {};
        }
        path.stepIndices.push_back(value.toInt(-1));
    }
    return path;
}

QByteArray encodePaths(const QVector<SequenceItemPath>& paths)
{
    QJsonArray items;
    for (const auto& path : paths) {
        items.push_back(pathToJson(path));
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("items"), items}})
        .toJson(QJsonDocument::Compact);
}

QVector<SequenceItemPath> decodePaths(const QByteArray& bytes)
{
    QVector<SequenceItemPath> paths;
    const auto document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()) {
        return paths;
    }
    const auto object = document.object();
    const auto items = object.value(QStringLiteral("items"));
    if (!items.isArray()) {
        const auto path = pathFromJson(object);
        if (path.isValid()) {
            paths.push_back(path);
        }
        return paths;
    }
    for (const auto& value : items.toArray()) {
        if (!value.isObject()) {
            return {};
        }
        const auto path = pathFromJson(value.toObject());
        if (!path.isValid() || paths.contains(path)) {
            return {};
        }
        paths.push_back(path);
    }
    return paths;
}

QString itemKind(const QJsonObject& object, const QString& fallback)
{
    return object.value("kind").toString(
        object.value("type").toString(fallback));
}

bool itemEnabled(const QJsonObject& object)
{
    return !object.contains("enabled") || object.value("enabled").toBool(true);
}

QString stepId(const QJsonObject& object)
{
    return object.value("id").toString();
}

QString stepKey(const QJsonObject& object)
{
    return object.value("key").toString();
}

QString childNodePath(const QString& parentNodePath, const QJsonObject& object)
{
    const auto id = stepId(object);
    const auto key = stepKey(object);
    const auto segment = parentNodePath.isEmpty() || key.isEmpty() ? id : key;
    return parentNodePath.isEmpty() ? segment : QString("%1.%2").arg(parentNodePath, segment);
}

QString childLocalPath(const QString& parentLocalPath, const QJsonObject& object)
{
    const auto id = stepId(object);
    return parentLocalPath.isEmpty() ? id : QString("%1/%2").arg(parentLocalPath, id);
}

QJsonValue valueByPath(const QJsonObject& object, const QStringList& segments)
{
    QJsonValue current(object);
    for (const auto& segment : segments) {
        if (!current.isObject()) {
            return {};
        }
        const auto currentObject = current.toObject();
        auto iterator = currentObject.constBegin();
        for (; iterator != currentObject.constEnd(); ++iterator) {
            if (iterator.key().compare(segment, Qt::CaseInsensitive) == 0) {
                break;
            }
        }
        if (iterator == currentObject.constEnd()) {
            return {};
        }
        current = iterator.value();
    }
    return current;
}

QJsonValue findValue(const QJsonObject& object, const QString& fieldPath)
{
    const auto segments = fieldPath.split('.', Qt::SkipEmptyParts);
    if (segments.size() > 1) {
        return valueByPath(object, segments);
    }
    const auto field = segments.value(0);
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (iterator.key().compare(field, Qt::CaseInsensitive) == 0) {
            return iterator.value();
        }
    }
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (iterator.key().compare(QStringLiteral("steps"),
                                   Qt::CaseInsensitive) == 0 ||
            !iterator.value().isObject()) {
            continue;
        }
        const auto nested = findValue(iterator.value().toObject(), field);
        if (!nested.isUndefined()) {
            return nested;
        }
    }
    return {};
}

QString displayJsonValue(const QJsonValue& value)
{
    if (value.isUndefined() || value.isNull()) return {};
    if (value.isString()) return value.toString();
    if (value.isBool()) return value.toBool() ? QStringLiteral("true")
                                               : QStringLiteral("false");
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 15);
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject())
                                     .toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray())
                                     .toJson(QJsonDocument::Compact));
    }
    return {};
}

} // namespace

SequenceTreeModel::SequenceTreeModel(SequenceDocument* document, QObject* parent)
    : QAbstractItemModel(parent)
    , m_document(document)
{
    Q_ASSERT(m_document);
    connect(m_document, &SequenceDocument::documentChanged,
            this, &SequenceTreeModel::refreshFromDocument);
    rebuild();
}

SequenceTreeModel::~SequenceTreeModel() = default;

QModelIndex SequenceTreeModel::index(int row,
                                    int column,
                                    const QModelIndex& parentIndex) const
{
    if (row < 0 || column < 0 || column >= ColumnCount) {
        return {};
    }
    auto* parentItem = itemForIndex(parentIndex);
    if (!parentItem || row >= parentItem->children.size()) {
        return {};
    }
    return createIndex(row, column, parentItem->children[row].get());
}

QModelIndex SequenceTreeModel::parent(const QModelIndex& child) const
{
    auto* item = itemForIndex(child);
    if (!item || !item->parent || item->parent == m_root.get()) {
        return {};
    }

    auto* parentItem = item->parent;
    auto* grandParent = parentItem->parent;
    if (!grandParent) {
        return {};
    }
    for (int row = 0; row < grandParent->children.size(); ++row) {
        if (grandParent->children[row].get() == parentItem) {
            return createIndex(row, 0, parentItem);
        }
    }
    return {};
}

int SequenceTreeModel::rowCount(const QModelIndex& parentIndex) const
{
    if (parentIndex.column() > 0) {
        return 0;
    }
    const auto* item = itemForIndex(parentIndex);
    return item ? item->children.size() : 0;
}

int SequenceTreeModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

QVariant SequenceTreeModel::data(const QModelIndex& modelIndex, int role) const
{
    auto* item = itemForIndex(modelIndex);
    if (!item || item == m_root.get()) {
        return {};
    }

    if (role == ItemTypeRole) {
        return QVariant::fromValue(item->type);
    }
    if (role == JsonPathRole) {
        return item->path.jsonPath();
    }
    if (role == EffectiveEnabledRole) {
        return item->effectiveEnabled;
    }
    if (role == DisabledByAncestorRole) {
        return item->disabledByAncestor;
    }
    if (role == ResourceRegionIdRole) {
        return item->resourceRegionId;
    }
    if (role == ResourceMarkerRole) {
        return static_cast<int>(item->resourceMarker);
    }
    if (role == ResourceBoundaryEligibleRole) {
        return item->type == ItemType::Step &&
               (item->resourceRegionId.isEmpty() ||
                item->resourceMarker != Item::ResourceMarker::None);
    }
    if (role == Qt::CheckStateRole && modelIndex.column() == EnabledColumn) {
        if (item->type == ItemType::Group) {
            return {};
        }
        if (item->disabledByAncestor) {
            return Qt::Unchecked;
        }
        return itemEnabled(item->object) ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::CheckStateRole && modelIndex.column() == BreakpointColumn &&
        item->type == ItemType::Step && !item->nodePath.isEmpty()) {
        return m_breakpointNodePaths.contains(item->nodePath)
            ? Qt::Checked
            : Qt::Unchecked;
    }
    if (role == Qt::FontRole &&
        (item->nodePath == m_currentDebugNodePath || !item->effectiveEnabled)) {
        QFont font;
        font.setBold(item->nodePath == m_currentDebugNodePath);
        font.setStrikeOut(item->type == ItemType::Step &&
                          !item->effectiveEnabled);
        font.setItalic(item->disabledByAncestor);
        return font;
    }
    if (role == Qt::ForegroundRole && !item->effectiveEnabled) {
        return QColor(QStringLiteral("#737d87"));
    }
    if (role == Qt::BackgroundRole && !item->effectiveEnabled) {
        return QColor(QStringLiteral("#eef1f3"));
    }
    if (role == Qt::BackgroundRole && modelIndex.column() == InspectionColumn &&
        !m_inspectionField.isEmpty()) {
        const auto value = displayJsonValue(
            findValue(item->object, m_inspectionField));
        if (!value.isEmpty()) {
            return m_inspectionColors.value(value,
                                            QColor(QStringLiteral("#dceeff")));
        }
    }
    if (role == Qt::BackgroundRole && !item->resourceRegionId.isEmpty()) {
        return QColor(QStringLiteral("#dceeff"));
    }
    if (role == Qt::ToolTipRole) {
        if (modelIndex.column() == InspectionColumn &&
            !m_inspectionField.isEmpty()) {
            const auto value = displayJsonValue(
                findValue(item->object, m_inspectionField));
            return value.isEmpty()
                ? tr("Field '%1' is not set on this item").arg(m_inspectionField)
                : tr("%1 = %2").arg(m_inspectionField, value);
        }
        if (modelIndex.column() == ResourceRegionColumn &&
            !item->resourceRegionId.isEmpty()) {
            if (item->resourceMarker == Item::ResourceMarker::SingleItem) {
                const bool isTestItem = item->object.value(QStringLiteral("kind"))
                                            .toString()
                                            .compare(QStringLiteral("testItem"),
                                                     Qt::CaseInsensitive) == 0;
                if (isTestItem) {
                    return tr("TESTITEM LOCK: hold %1 through all retries and release it only after the final result")
                        .arg(item->resourceRegionId);
                }
                return tr("ITEM LOCK: acquire %1 before this item and release it after completion")
                    .arg(item->resourceRegionId);
            }
            if (item->resourceMarker == Item::ResourceMarker::Entry) {
                return tr("LOCK: acquire %1 before this step")
                    .arg(item->resourceRegionId);
            }
            if (item->resourceMarker == Item::ResourceMarker::Exit) {
                return tr("UNLOCK: release %1 after this step")
                    .arg(item->resourceRegionId);
            }
            return tr("Resource %1 remains locked on this step")
                .arg(item->resourceRegionId);
        }
        if (item->disabledByAncestor) {
            return tr("Inactive because a parent TestItem or Loop is disabled");
        }
        if (!item->effectiveEnabled) {
            return item->type == ItemType::Group
                ? tr("This group is disabled in the sequence JSON")
                : tr("This step is disabled and will not run");
        }
        if (item->type == ItemType::Group) {
            return tr("Drop steps here; drag rows to reorder the group");
        }
        if (m_document && m_document->canContainSteps(item->path)) {
            return tr("Drop steps here to add them inside this %1")
                .arg(itemKind(item->object, QStringLiteral("container")));
        }
        return tr("Drag this step to reorder it or move it into a Group/TestItem");
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }

    switch (modelIndex.column()) {
    case NameColumn:
        return item->object.value("name").toString(
            item->object.value("id").toString(
                item->type == ItemType::Group ? tr("Group") : tr("Step")));
    case KindColumn:
        return itemKind(item->object,
                        item->type == ItemType::Group ? QStringLiteral("custom")
                                                     : QStringLiteral("noop"));
    case IdColumn: {
        const auto id = item->object.value("id").toString();
        const auto key = item->object.value("key").toString();
        return key.isEmpty() ? id : QString("%1 / %2").arg(id, key);
    }
    case ResourceRegionColumn:
        if (item->resourceMarker == Item::ResourceMarker::SingleItem) {
            return tr("LOCK/UNLOCK");
        }
        if (item->resourceMarker == Item::ResourceMarker::Entry) {
            return tr("LOCK");
        }
        if (item->resourceMarker == Item::ResourceMarker::Exit) {
            return tr("UNLOCK");
        }
        return {};
    case BreakpointColumn:
        if (item->type != ItemType::Step) {
            return {};
        }
        if (item->nodePath == m_currentDebugNodePath &&
            m_breakpointNodePaths.contains(item->nodePath)) {
            return tr("Hit");
        }
        if (item->nodePath == m_currentDebugNodePath) {
            return tr("Current");
        }
        return m_breakpointNodePaths.contains(item->nodePath) ? tr("On") : QVariant{};
    case EnabledColumn:
        return {};
    case InspectionColumn:
        return m_inspectionField.isEmpty()
            ? QVariant{}
            : displayJsonValue(findValue(item->object, m_inspectionField));
    default:
        return {};
    }
}

bool SequenceTreeModel::setData(const QModelIndex& modelIndex,
                                const QVariant& value,
                                int role)
{
    auto* item = itemForIndex(modelIndex);
    if (!m_document || !item || role != Qt::CheckStateRole) {
        return false;
    }
    if (modelIndex.column() == BreakpointColumn && item->type == ItemType::Step &&
        !item->nodePath.isEmpty()) {
        const bool enabled = value.toInt() == Qt::Checked;
        const bool alreadyEnabled = m_breakpointNodePaths.contains(item->nodePath);
        if (enabled == alreadyEnabled) {
            return true;
        }
        if (enabled) {
            m_breakpointNodePaths.insert(item->nodePath);
        } else {
            m_breakpointNodePaths.remove(item->nodePath);
        }
        emit dataChanged(modelIndex, modelIndex,
                         {Qt::DisplayRole, Qt::CheckStateRole, Qt::ToolTipRole});
        emit breakpointsChanged();
        return true;
    }
    if (modelIndex.column() != EnabledColumn) {
        return false;
    }
    if (item->type != ItemType::Step || item->disabledByAncestor) {
        return false;
    }
    return m_document->setItemValue(
        item->path, "enabled", value.toInt() == Qt::Checked);
}

Qt::ItemFlags SequenceTreeModel::flags(const QModelIndex& modelIndex) const
{
    if (!modelIndex.isValid()) {
        return Qt::NoItemFlags;
    }

    auto result = QAbstractItemModel::flags(modelIndex);
    const auto path = pathForIndex(modelIndex);
    if (itemType(modelIndex) == ItemType::Step) {
        result |= Qt::ItemIsDragEnabled;
    }
    if (m_document && m_document->canContainSteps(path)) {
        result |= Qt::ItemIsDropEnabled;
    }
    if (modelIndex.column() == BreakpointColumn &&
        itemType(modelIndex) == ItemType::Step) {
        result |= Qt::ItemIsUserCheckable;
    }
    auto* item = itemForIndex(modelIndex);
    if (modelIndex.column() == EnabledColumn && item &&
        item->disabledByAncestor) {
        result &= ~Qt::ItemIsEnabled;
    }
    if (modelIndex.column() == EnabledColumn && item &&
        item->type == ItemType::Step && !item->disabledByAncestor) {
        result |= Qt::ItemIsUserCheckable;
    }
    return result;
}

QVariant SequenceTreeModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case NameColumn:
        return tr("Name");
    case KindColumn:
        return tr("Kind");
    case IdColumn:
        return tr("ID / Key");
    case ResourceRegionColumn:
        return {};
    case BreakpointColumn:
        return tr("BP");
    case EnabledColumn:
        return tr("Enabled");
    case InspectionColumn:
        return m_inspectionField.isEmpty()
            ? tr("Key")
            : tr("Key: %1").arg(m_inspectionField);
    default:
        return {};
    }
}

int SequenceTreeModel::setInspectionField(QString fieldPath)
{
    fieldPath = fieldPath.trimmed();
    if (m_inspectionField == fieldPath) {
        return m_inspectionMatchCount;
    }
    m_inspectionField = std::move(fieldPath);
    rebuildInspectionColors();
    emit headerDataChanged(Qt::Horizontal, InspectionColumn, InspectionColumn);
    std::function<void(const QModelIndex&)> refresh =
        [this, &refresh](const QModelIndex& parentIndex) {
            for (int row = 0; row < rowCount(parentIndex); ++row) {
                const auto inspection = index(row, InspectionColumn, parentIndex);
                emit dataChanged(inspection, inspection,
                                 {Qt::DisplayRole, Qt::BackgroundRole,
                                  Qt::ToolTipRole});
                refresh(index(row, NameColumn, parentIndex));
            }
        };
    refresh({});
    return m_inspectionMatchCount;
}

QString SequenceTreeModel::inspectionField() const
{
    return m_inspectionField;
}

int SequenceTreeModel::inspectionMatchCount() const
{
    return m_inspectionMatchCount;
}

void SequenceTreeModel::rebuildInspectionColors()
{
    m_inspectionColors.clear();
    m_inspectionMatchCount = 0;
    if (!m_root || m_inspectionField.isEmpty()) {
        return;
    }

    static const QVector<QColor> palette = {
        QColor(QStringLiteral("#dceeff")),
        QColor(QStringLiteral("#dff4e5")),
        QColor(QStringLiteral("#fff0c7")),
        QColor(QStringLiteral("#eadffc")),
        QColor(QStringLiteral("#ffdfe0")),
        QColor(QStringLiteral("#d9f2f2")),
        QColor(QStringLiteral("#f6e2ce")),
        QColor(QStringLiteral("#e4e8f7")),
    };
    std::function<void(const Item&)> collect = [&](const Item& item) {
        if (item.type == ItemType::Step) {
            const auto value = displayJsonValue(
                findValue(item.object, m_inspectionField));
            if (!value.isEmpty()) {
                ++m_inspectionMatchCount;
                if (!m_inspectionColors.contains(value)) {
                    m_inspectionColors.insert(
                        value, palette[m_inspectionColors.size() % palette.size()]);
                }
            }
        }
        for (const auto& child : item.children) {
            collect(*child);
        }
    };
    collect(*m_root);
}

QStringList SequenceTreeModel::mimeTypes() const
{
    return {QString::fromLatin1(sequencePathMimeType),
            QString::fromLatin1(PluginFunctionMimeType)};
}

QMimeData* SequenceTreeModel::mimeData(const QModelIndexList& indexes) const
{
    QVector<SequenceItemPath> paths;
    for (const auto& index : indexes) {
        if (!index.isValid() || index.column() != NameColumn ||
            itemType(index) != ItemType::Step) {
            continue;
        }
        const auto path = pathForIndex(index);
        if (path.isValid() && !paths.contains(path)) {
            paths.push_back(path);
        }
    }
    if (paths.isEmpty()) {
        return nullptr;
    }
    std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        if (left.groupIndex != right.groupIndex) {
            return left.groupIndex < right.groupIndex;
        }
        return std::lexicographical_compare(
            left.stepIndices.cbegin(), left.stepIndices.cend(),
            right.stepIndices.cbegin(), right.stepIndices.cend());
    });
    auto* data = new QMimeData;
    data->setData(sequencePathMimeType, encodePaths(paths));
    return data;
}

bool SequenceTreeModel::dropMimeData(const QMimeData* data,
                                     Qt::DropAction action,
                                     int row,
                                     int column,
                                     const QModelIndex& parentIndex)
{
    if (action == Qt::IgnoreAction) {
        return true;
    }
    if (!data || column > 0) {
        return false;
    }

    const auto destinationParent = pathForIndex(parentIndex);
    if (!m_document || !destinationParent.isValid() ||
        !m_document->canContainSteps(destinationParent)) {
        return false;
    }

    if (action == Qt::CopyAction &&
        data->hasFormat(PluginFunctionMimeType)) {
        const auto document = QJsonDocument::fromJson(
            data->data(PluginFunctionMimeType));
        if (!document.isObject()) {
            return false;
        }
        const int count = m_document->objectAt(destinationParent)
                              .value("steps").toArray().size();
        const int insertionRow = row < 0 ? count : qBound(0, row, count);
        if (!m_document->insertStep(
                destinationParent, insertionRow, document.object())) {
            return false;
        }
        auto insertedPath = destinationParent;
        insertedPath.stepIndices.push_back(insertionRow);
        emit itemInserted(insertedPath);
        return true;
    }

    if (action != Qt::MoveAction ||
        !data->hasFormat(sequencePathMimeType)) {
        return false;
    }

    const auto sourcePaths = decodePaths(data->data(sequencePathMimeType));
    if (sourcePaths.isEmpty()) {
        return false;
    }
    for (const auto& sourcePath : sourcePaths) {
        if (!sourcePath.isValid() || sourcePath.stepIndices.isEmpty()) {
            return false;
        }
    }

    QVector<SequenceItemPath> movedPaths;
    m_deferDocumentRefresh = true;
    const bool moved = m_document->relocateSteps(
        sourcePaths, destinationParent, row, &movedPaths);
    m_deferDocumentRefresh = false;
    const bool refreshWasRequested = std::exchange(
        m_documentRefreshPending, false);
    if (!moved) {
        if (refreshWasRequested) {
            refreshFromDocument();
        }
        return false;
    }
    if (sourcePaths.size() == 1 && movedPaths.size() == 1 &&
        !applyModelMove(sourcePaths.first(), destinationParent,
                        movedPaths.first())) {
        ApplicationDiagnostics::recordAction(
            QStringLiteral("FLOW_MOVE_FALLBACK"),
            QStringLiteral("from=%1 to=%2")
                .arg(sourcePaths.first().jsonPath(),
                     movedPaths.first().jsonPath()));
    }
    refreshFromDocument();
    if (sourcePaths.size() == 1) {
        emit itemMoved(sourcePaths.first(), movedPaths.first());
    } else {
        emit itemsMoved(sourcePaths, movedPaths);
    }
    return true;
}

Qt::DropActions SequenceTreeModel::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

SequenceItemPath SequenceTreeModel::pathForIndex(const QModelIndex& modelIndex) const
{
    auto* item = itemForIndex(modelIndex);
    return item && item != m_root.get() ? item->path : SequenceItemPath{};
}

SequenceTreeModel::ItemType SequenceTreeModel::itemType(
    const QModelIndex& modelIndex) const
{
    auto* item = itemForIndex(modelIndex);
    return item ? item->type : ItemType::Step;
}

bool SequenceTreeModel::canContainSteps(const QModelIndex& modelIndex) const
{
    const auto path = pathForIndex(modelIndex);
    return m_document && path.isValid() && m_document->canContainSteps(path);
}

QModelIndex SequenceTreeModel::indexForPath(const SequenceItemPath& path) const
{
    return m_root ? findIndex(*m_root, path) : QModelIndex{};
}

QModelIndex SequenceTreeModel::indexForNodePath(const QString& nodePath) const
{
    return m_root ? findIndexForNodePath(*m_root, nodePath.trimmed()) : QModelIndex{};
}

QString SequenceTreeModel::nodePathForIndex(const QModelIndex& modelIndex) const
{
    auto* item = itemForIndex(modelIndex);
    return item && item != m_root.get() ? item->nodePath : QString{};
}

QString SequenceTreeModel::localPathForIndex(const QModelIndex& modelIndex) const
{
    auto* item = itemForIndex(modelIndex);
    return item && item != m_root.get() ? item->localPath : QString{};
}

QSet<QString> SequenceTreeModel::breakpointNodePaths() const
{
    return m_breakpointNodePaths;
}

QVector<PicoATE::Core::BreakpointSpec> SequenceTreeModel::breakpointSpecs() const
{
    QVector<PicoATE::Core::BreakpointSpec> specs;
    if (!m_root) {
        return specs;
    }

    std::function<void(const Item&)> collect = [&](const Item& item) {
        if (item.type == ItemType::Step &&
            m_breakpointNodePaths.contains(item.nodePath)) {
            PicoATE::Core::BreakpointSpec spec;
            spec.id = QStringLiteral("ui-bp-%1").arg(specs.size() + 1);
            spec.address = PicoATE::Core::BreakpointAddress::nodePath(item.nodePath);
            spec.enabled = true;
            specs.push_back(spec);
        }
        for (const auto& child : item.children) {
            collect(*child);
        }
    };
    collect(*m_root);
    return specs;
}

void SequenceTreeModel::setBreakpointNodePaths(QSet<QString> nodePaths)
{
    if (m_breakpointNodePaths == nodePaths) {
        return;
    }

    QSet<QString> changedPaths = m_breakpointNodePaths;
    for (const auto& nodePath : nodePaths) {
        if (changedPaths.contains(nodePath)) {
            changedPaths.remove(nodePath);
        } else {
            changedPaths.insert(nodePath);
        }
    }
    m_breakpointNodePaths = std::move(nodePaths);
    for (const auto& nodePath : changedPaths) {
        const auto index = indexForNodePath(nodePath);
        if (!index.isValid()) {
            continue;
        }
        const auto breakpointIndex = index.siblingAtColumn(BreakpointColumn);
        emit dataChanged(breakpointIndex,
                         breakpointIndex,
                         {Qt::DisplayRole, Qt::CheckStateRole, Qt::ToolTipRole});
    }
    emit breakpointsChanged();
}

void SequenceTreeModel::clearBreakpoints()
{
    setBreakpointNodePaths({});
}

void SequenceTreeModel::setCurrentDebugNodePath(const QString& nodePath)
{
    const auto normalized = nodePath.trimmed();
    if (m_currentDebugNodePath == normalized) {
        return;
    }
    const auto previous = m_currentDebugNodePath;
    m_currentDebugNodePath = normalized;
    emitRowChanged(previous);
    emitRowChanged(m_currentDebugNodePath);
}

void SequenceTreeModel::refreshFromDocument()
{
    if (m_deferDocumentRefresh) {
        m_documentRefreshPending = true;
        return;
    }
    ScopedOperationTimer timer(
        QStringLiteral("SequenceTreeModel.refreshFromDocument"), 20);
    const auto changedItemPaths = m_document
        ? m_document->lastChangedItemPaths()
        : QVector<SequenceItemPath>{};
    if (!changedItemPaths.isEmpty() &&
        tryRefreshChangedItems(changedItemPaths)) {
        return;
    }
    if (!structureMatchesDocument()) {
        if (tryApplySingleStructuralChange()) {
            return;
        }
        ApplicationDiagnostics::recordAction(
            QStringLiteral("FLOW_MODEL_RESET"),
            QStringLiteral("reason=structure_changed"));
        rebuild();
        return;
    }

    struct ItemSnapshot {
        Item* item = nullptr;
        QJsonObject object;
        SequenceItemPath path;
        QString nodePath;
        QString localPath;
        QString resourceRegionId;
        bool effectiveEnabled = true;
        bool disabledByAncestor = false;
        Item::ResourceMarker resourceMarker = Item::ResourceMarker::None;
    };

    QVector<ItemSnapshot> snapshots;
    const std::function<void(Item&)> collect = [&](Item& parent) {
        for (auto& child : parent.children) {
            snapshots.push_back({child.get(),
                                 child->object,
                                 child->path,
                                 child->nodePath,
                                 child->localPath,
                                 child->resourceRegionId,
                                 child->effectiveEnabled,
                                 child->disabledByAncestor,
                                 child->resourceMarker});
            collect(*child);
        }
    };
    collect(*m_root);

    const auto previousInspectionColors = m_inspectionColors;
    const int previousInspectionCount = m_inspectionMatchCount;
    updateTreeFromDocument();
    rebuildInspectionColors();

    for (const auto& snapshot : snapshots) {
        const auto* item = snapshot.item;
        if (!item ||
            (snapshot.object == item->object &&
             snapshot.path == item->path &&
             snapshot.nodePath == item->nodePath &&
             snapshot.localPath == item->localPath &&
             snapshot.resourceRegionId == item->resourceRegionId &&
             snapshot.effectiveEnabled == item->effectiveEnabled &&
             snapshot.disabledByAncestor == item->disabledByAncestor &&
             snapshot.resourceMarker == item->resourceMarker)) {
            continue;
        }
        const auto changed = indexForItem(item);
        if (changed.isValid()) {
            emit dataChanged(changed.siblingAtColumn(0),
                             changed.siblingAtColumn(ColumnCount - 1), {});
        }
    }

    if (previousInspectionColors != m_inspectionColors ||
        previousInspectionCount != m_inspectionMatchCount) {
        const std::function<void(const QModelIndex&)> refreshInspection =
            [&](const QModelIndex& parentIndex) {
                for (int row = 0; row < rowCount(parentIndex); ++row) {
                    const auto item = index(row, InspectionColumn, parentIndex);
                    emit dataChanged(item, item,
                                     {Qt::DisplayRole, Qt::BackgroundRole,
                                      Qt::ToolTipRole});
                    refreshInspection(index(row, NameColumn, parentIndex));
                }
            };
        refreshInspection({});
    }
}

bool SequenceTreeModel::tryRefreshChangedItems(
    const QVector<SequenceItemPath>& changedItemPaths)
{
    if (!m_document || !m_root || !m_inspectionField.isEmpty()) {
        return false;
    }

    QVector<SequenceItemPath> roots;
    for (const auto& path : changedItemPaths) {
        if (!path.isValid()) {
            return false;
        }
        bool coveredByAncestor = false;
        for (const auto& existing : roots) {
            if (existing.groupIndex == path.groupIndex &&
                existing.stepIndices.size() <= path.stepIndices.size() &&
                std::equal(existing.stepIndices.cbegin(),
                           existing.stepIndices.cend(),
                           path.stepIndices.cbegin())) {
                coveredByAncestor = true;
                break;
            }
        }
        if (!coveredByAncestor) {
            roots.erase(std::remove_if(
                roots.begin(), roots.end(), [&](const auto& existing) {
                    return existing.groupIndex == path.groupIndex &&
                           path.stepIndices.size() <=
                               existing.stepIndices.size() &&
                           std::equal(path.stepIndices.cbegin(),
                                      path.stepIndices.cend(),
                                      existing.stepIndices.cbegin());
                }), roots.end());
            roots.push_back(path);
        }
    }

    struct RefreshTarget {
        Item* item = nullptr;
        QJsonObject object;
    };
    QVector<RefreshTarget> targets;
    targets.reserve(roots.size());
    for (const auto& path : roots) {
        const auto modelIndex = indexForPath(path);
        if (!modelIndex.isValid()) {
            return false;
        }
        auto* item = itemForIndex(modelIndex);
        const auto object = m_document->objectAt(path);
        if (!item || object.isEmpty() ||
            !stepStructureMatches(*item, object) ||
            item->object.value(QStringLiteral("resourceRegionStart")) !=
                object.value(QStringLiteral("resourceRegionStart")) ||
            item->object.value(QStringLiteral("resourceRegionEnd")) !=
                object.value(QStringLiteral("resourceRegionEnd"))) {
            return false;
        }
        targets.push_back({item, object});
    }

    for (auto& target : targets) {
        updateItemFromObject(*target.item, target.object);
        for (auto* ancestor = target.item->parent;
             ancestor && ancestor != m_root.get();
             ancestor = ancestor->parent) {
            ancestor->object = m_document->objectAt(ancestor->path);
        }
        emitSubtreeChanged(*target.item);
    }
    return true;
}

void SequenceTreeModel::updateItemFromObject(
    Item& item,
    const QJsonObject& object)
{
    item.object = object;
    if (item.type == ItemType::Group) {
        item.nodePath.clear();
        item.localPath.clear();
        item.disabledByAncestor = false;
        item.effectiveEnabled = itemEnabled(item.object);
    } else {
        const auto* parent = item.parent;
        const bool parentEnabled = parent ? parent->effectiveEnabled : true;
        const QString parentNodePath = parent ? parent->nodePath : QString{};
        const QString parentLocalPath = parent ? parent->localPath : QString{};
        item.nodePath = childNodePath(parentNodePath, item.object);
        item.localPath = childLocalPath(parentLocalPath, item.object);
        item.disabledByAncestor = !parentEnabled;
        item.effectiveEnabled = parentEnabled && itemEnabled(item.object);
    }

    const auto steps = item.object.value(QStringLiteral("steps")).toArray();
    for (int index = 0; index < steps.size(); ++index) {
        updateItemFromObject(*item.children[index],
                             steps.at(index).toObject());
    }
}

void SequenceTreeModel::emitSubtreeChanged(Item& item)
{
    const auto changed = indexForItem(&item);
    if (changed.isValid()) {
        emit dataChanged(changed.siblingAtColumn(0),
                         changed.siblingAtColumn(ColumnCount - 1), {});
    }
    for (auto& child : item.children) {
        emitSubtreeChanged(*child);
    }
}

bool SequenceTreeModel::applyModelMove(
    const SequenceItemPath& sourcePath,
    const SequenceItemPath& destinationParent,
    const SequenceItemPath& movedPath)
{
    if (!m_root || sourcePath.stepIndices.isEmpty() ||
        movedPath.stepIndices.isEmpty()) {
        return false;
    }

    const auto sourceIndex = indexForPath(sourcePath);
    const auto destinationParentIndex = indexForPath(destinationParent);
    auto* sourceItem = itemForIndex(sourceIndex);
    auto* destinationParentItem = itemForIndex(destinationParentIndex);
    if (!sourceItem || !sourceItem->parent || !destinationParentItem) {
        return false;
    }

    auto* sourceParentItem = sourceItem->parent;
    const auto sourceParentIndex = indexForItem(sourceParentItem);
    const int sourceRow = sourcePath.stepIndices.last();
    const int destinationRow = movedPath.stepIndices.last();
    if (sourceRow < 0 || sourceRow >= sourceParentItem->children.size() ||
        destinationRow < 0 ||
        destinationRow > destinationParentItem->children.size()) {
        return false;
    }

    const bool sameParent = sourceParentItem == destinationParentItem;
    if (sameParent && sourceRow == destinationRow) {
        return true;
    }
    int qtDestinationRow = destinationRow;
    if (sameParent && destinationRow > sourceRow) {
        ++qtDestinationRow;
    }
    if (!beginMoveRows(sourceParentIndex, sourceRow, sourceRow,
                       destinationParentIndex, qtDestinationRow)) {
        return false;
    }

    auto moving = std::move(sourceParentItem->children[sourceRow]);
    sourceParentItem->children.erase(
        sourceParentItem->children.begin() + sourceRow);
    moving->parent = destinationParentItem;
    destinationParentItem->children.insert(
        destinationParentItem->children.begin() + destinationRow,
        std::move(moving));
    endMoveRows();

    ApplicationDiagnostics::recordAction(
        QStringLiteral("FLOW_ROW_MOVED"),
        QStringLiteral("from=%1 to=%2")
            .arg(sourcePath.jsonPath(), movedPath.jsonPath()));
    return true;
}

bool SequenceTreeModel::tryApplySingleStructuralChange()
{
    if (!m_document || !m_root) {
        return false;
    }
    const auto groups = m_document->rootObject()
                            .value(QStringLiteral("groups")).toArray();
    if (groups.size() != m_root->children.size()) {
        return false;
    }

    StructuralChange change;
    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        if (!groups.at(groupIndex).isObject() ||
            !locateSingleStructuralChange(
                *m_root->children[groupIndex],
                groups.at(groupIndex).toObject()
                    .value(QStringLiteral("steps")).toArray(),
                change)) {
            return false;
        }
    }
    if (change.kind == StructuralChange::Kind::None || !change.parent ||
        change.row < 0) {
        return false;
    }

    const auto parentIndex = indexForItem(change.parent);
    if (!parentIndex.isValid()) {
        return false;
    }

    if (change.kind == StructuralChange::Kind::Insert) {
        Item stagingParent;
        appendSteps(stagingParent,
                    QJsonArray{change.insertedObject},
                    change.parent->path,
                    change.parent->nodePath,
                    change.parent->localPath,
                    change.parent->effectiveEnabled);
        if (stagingParent.children.size() != 1) {
            return false;
        }
        auto inserted = std::move(stagingParent.children.front());
        inserted->parent = change.parent;
        beginInsertRows(parentIndex, change.row, change.row);
        change.parent->children.insert(
            change.parent->children.begin() + change.row,
            std::move(inserted));
        endInsertRows();
        ApplicationDiagnostics::recordAction(
            QStringLiteral("FLOW_ROW_INSERTED"),
            QStringLiteral("row=%1").arg(change.row));
    } else {
        if (change.row >= change.parent->children.size()) {
            return false;
        }
        beginRemoveRows(parentIndex, change.row, change.row);
        change.parent->children.erase(
            change.parent->children.begin() + change.row);
        endRemoveRows();
        ApplicationDiagnostics::recordAction(
            QStringLiteral("FLOW_ROW_REMOVED"),
            QStringLiteral("row=%1").arg(change.row));
    }

    refreshFromDocument();
    return true;
}

bool SequenceTreeModel::locateSingleStructuralChange(
    Item& parent,
    const QJsonArray& newSteps,
    StructuralChange& change) const
{
    for (const auto& value : newSteps) {
        if (!value.isObject()) {
            return false;
        }
    }

    const int oldCount = int(parent.children.size());
    const int newCount = newSteps.size();
    const int delta = newCount - oldCount;
    if (delta == 0) {
        for (int index = 0; index < newCount; ++index) {
            if (!locateSingleStructuralChange(
                    *parent.children[index],
                    newSteps.at(index).toObject()
                        .value(QStringLiteral("steps")).toArray(),
                    change)) {
                return false;
            }
        }
        return true;
    }
    if (delta != 1 && delta != -1) {
        return false;
    }
    if (change.kind != StructuralChange::Kind::None) {
        return false;
    }

    QVector<int> candidates;
    const int candidateCount = delta > 0 ? newCount : oldCount;
    for (int candidate = 0; candidate < candidateCount; ++candidate) {
        bool matches = true;
        if (delta > 0) {
            for (int oldIndex = 0; oldIndex < oldCount; ++oldIndex) {
                const int newIndex = oldIndex < candidate
                    ? oldIndex
                    : oldIndex + 1;
                if (parent.children[oldIndex]->object !=
                    newSteps.at(newIndex).toObject()) {
                    matches = false;
                    break;
                }
            }
        } else {
            for (int newIndex = 0; newIndex < newCount; ++newIndex) {
                const int oldIndex = newIndex < candidate
                    ? newIndex
                    : newIndex + 1;
                if (parent.children[oldIndex]->object !=
                    newSteps.at(newIndex).toObject()) {
                    matches = false;
                    break;
                }
            }
        }
        if (matches) {
            candidates.push_back(candidate);
        }
    }
    if (candidates.size() != 1) {
        return false;
    }

    change.kind = delta > 0 ? StructuralChange::Kind::Insert
                            : StructuralChange::Kind::Remove;
    change.parent = &parent;
    change.row = candidates.first();
    if (delta > 0) {
        change.insertedObject = newSteps.at(change.row).toObject();
    }
    return true;
}

bool SequenceTreeModel::structureMatchesDocument() const
{
    if (!m_document || !m_root) {
        return false;
    }
    const auto groups = m_document->rootObject()
                            .value(QStringLiteral("groups")).toArray();
    if (groups.size() != m_root->children.size()) {
        return false;
    }
    for (int index = 0; index < groups.size(); ++index) {
        if (!groups.at(index).isObject() ||
            m_root->children[index]->type != ItemType::Group ||
            !stepStructureMatches(*m_root->children[index],
                                  groups.at(index).toObject())) {
            return false;
        }
    }
    return true;
}

bool SequenceTreeModel::stepStructureMatches(const Item& item,
                                             const QJsonObject& object) const
{
    const auto steps = object.value(QStringLiteral("steps")).toArray();
    if (steps.size() != item.children.size()) {
        return false;
    }
    for (int index = 0; index < steps.size(); ++index) {
        if (!steps.at(index).isObject() ||
            !stepStructureMatches(*item.children[index],
                                  steps.at(index).toObject())) {
            return false;
        }
    }
    return true;
}

void SequenceTreeModel::updateTreeFromDocument()
{
    const auto groups = m_document->rootObject()
                            .value(QStringLiteral("groups")).toArray();
    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        auto& group = *m_root->children[groupIndex];
        group.type = ItemType::Group;
        group.path = SequenceItemPath{groupIndex, {}};
        group.object = groups.at(groupIndex).toObject();
        group.nodePath.clear();
        group.localPath.clear();
        group.disabledByAncestor = false;
        group.effectiveEnabled = itemEnabled(group.object);
        group.resourceMarker = Item::ResourceMarker::None;
        group.resourceRegionId.clear();
        updateStepsFromDocument(
            group, group.object.value(QStringLiteral("steps")).toArray(),
            group.path, {}, {}, group.effectiveEnabled);
        applyResourceRegions(group);
    }
}

void SequenceTreeModel::updateStepsFromDocument(
    Item& parent,
    const QJsonArray& steps,
    const SequenceItemPath& parentPath,
    const QString& parentNodePath,
    const QString& parentLocalPath,
    bool parentEffectiveEnabled)
{
    for (int index = 0; index < steps.size(); ++index) {
        auto& child = *parent.children[index];
        child.type = ItemType::Step;
        child.path = parentPath;
        child.path.stepIndices.push_back(index);
        child.object = steps.at(index).toObject();
        child.nodePath = childNodePath(parentNodePath, child.object);
        child.localPath = childLocalPath(parentLocalPath, child.object);
        child.disabledByAncestor = !parentEffectiveEnabled;
        child.effectiveEnabled = parentEffectiveEnabled && itemEnabled(child.object);
        child.resourceMarker = Item::ResourceMarker::None;
        child.resourceRegionId.clear();
        updateStepsFromDocument(
            child, child.object.value(QStringLiteral("steps")).toArray(),
            child.path, child.nodePath, child.localPath,
            child.effectiveEnabled);
    }
}

void SequenceTreeModel::applyResourceRegions(Item& parent)
{
    const auto markRegion = [&](Item& item,
                                const QString& regionId,
                                const auto& markRef) -> void {
        item.resourceRegionId = regionId;
        for (auto& child : item.children) {
            markRef(*child, regionId, markRef);
        }
    };
    const std::function<void(Item&)> markSiblingRegions =
        [&](Item& regionParent) {
            for (int entryRow = 0;
                 entryRow < regionParent.children.size(); ++entryRow) {
                auto& entry = regionParent.children[entryRow];
                const auto start = entry->object
                                       .value(QStringLiteral("resourceRegionStart"))
                                       .toObject();
                const auto regionId = start.value(QStringLiteral("id"))
                                          .toString();
                if (regionId.isEmpty()) {
                    continue;
                }

                entry->resourceMarker = Item::ResourceMarker::Entry;
                entry->resourceRegionId = regionId;
                int exitRow = entry->object
                                      .value(QStringLiteral("resourceRegionEnd"))
                                      .toString() == regionId
                    ? entryRow
                    : -1;
                for (int candidate = entryRow + 1;
                     exitRow < 0 && candidate < regionParent.children.size();
                     ++candidate) {
                    if (regionParent.children[candidate]->object
                            .value(QStringLiteral("resourceRegionEnd"))
                            .toString() == regionId) {
                        exitRow = candidate;
                        break;
                    }
                }
                if (exitRow < 0) {
                    continue;
                }

                markRegion(*entry, regionId, markRegion);
                if (exitRow == entryRow) {
                    entry->resourceMarker = Item::ResourceMarker::SingleItem;
                    continue;
                }
                for (int row = entryRow + 1; row < exitRow; ++row) {
                    markRegion(*regionParent.children[row], regionId, markRegion);
                }
                auto& exit = regionParent.children[exitRow];
                markRegion(*exit, regionId, markRegion);
                exit->resourceMarker = Item::ResourceMarker::Exit;
            }
            for (auto& child : regionParent.children) {
                markSiblingRegions(*child);
            }
        };
    markSiblingRegions(parent);
}

void SequenceTreeModel::rebuild()
{
    beginResetModel();
    m_root = std::make_unique<Item>();
    m_root->parent = nullptr;

    if (m_document) {
        const auto groups = m_document->rootObject().value("groups").toArray();
        for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            if (!groups[groupIndex].isObject()) {
                continue;
            }

            auto group = std::make_unique<Item>();
            group->type = ItemType::Group;
            group->path.groupIndex = groupIndex;
            group->object = groups[groupIndex].toObject();
            group->effectiveEnabled = itemEnabled(group->object);
            group->parent = m_root.get();
            appendSteps(*group,
                        group->object.value("steps").toArray(),
                        group->path,
                        {},
                        {},
                        group->effectiveEnabled);
            applyResourceRegions(*group);
            m_root->children.push_back(std::move(group));
        }
    }
    rebuildInspectionColors();
    endResetModel();
}

void SequenceTreeModel::appendSteps(Item& parent,
                                    const QJsonArray& steps,
                                    const SequenceItemPath& parentPath,
                                    const QString& parentNodePath,
                                    const QString& parentLocalPath,
                                    bool parentEffectiveEnabled)
{
    for (int index = 0; index < steps.size(); ++index) {
        if (!steps[index].isObject()) {
            continue;
        }

        auto child = std::make_unique<Item>();
        child->type = ItemType::Step;
        child->path = parentPath;
        child->path.stepIndices.push_back(index);
        child->object = steps[index].toObject();
        child->nodePath = childNodePath(parentNodePath, child->object);
        child->localPath = childLocalPath(parentLocalPath, child->object);
        child->disabledByAncestor = !parentEffectiveEnabled;
        child->effectiveEnabled = parentEffectiveEnabled &&
                                  itemEnabled(child->object);
        child->parent = &parent;
        appendSteps(*child,
                    child->object.value("steps").toArray(),
                    child->path,
                    child->nodePath,
                    child->localPath,
                    child->effectiveEnabled);
        parent.children.push_back(std::move(child));
    }
}

SequenceTreeModel::Item* SequenceTreeModel::itemForIndex(
    const QModelIndex& modelIndex) const
{
    if (!modelIndex.isValid()) {
        return m_root.get();
    }
    return static_cast<Item*>(modelIndex.internalPointer());
}

QModelIndex SequenceTreeModel::indexForItem(const Item* item) const
{
    if (!item || item == m_root.get() || !item->parent) {
        return {};
    }
    const auto* parentItem = item->parent;
    for (int row = 0; row < parentItem->children.size(); ++row) {
        if (parentItem->children[row].get() == item) {
            return createIndex(row, NameColumn, const_cast<Item*>(item));
        }
    }
    return {};
}

QModelIndex SequenceTreeModel::findIndex(const Item& parentItem,
                                        const SequenceItemPath& path) const
{
    for (int row = 0; row < parentItem.children.size(); ++row) {
        const auto* child = parentItem.children[row].get();
        if (child->path == path) {
            return createIndex(row, 0, const_cast<Item*>(child));
        }
        const auto nested = findIndex(*child, path);
        if (nested.isValid()) {
            return nested;
        }
    }
    return {};
}

QModelIndex SequenceTreeModel::findIndexForNodePath(
    const Item& parentItem,
    const QString& nodePath) const
{
    if (nodePath.isEmpty()) {
        return {};
    }
    for (int row = 0; row < parentItem.children.size(); ++row) {
        const auto* child = parentItem.children[row].get();
        if (child->nodePath == nodePath) {
            return createIndex(row, 0, const_cast<Item*>(child));
        }
        const auto nested = findIndexForNodePath(*child, nodePath);
        if (nested.isValid()) {
            return nested;
        }
    }
    return {};
}

void SequenceTreeModel::emitRowChanged(const QString& nodePath)
{
    const auto index = indexForNodePath(nodePath);
    if (!index.isValid()) {
        return;
    }
    emit dataChanged(index.siblingAtColumn(0),
                     index.siblingAtColumn(ColumnCount - 1),
                     {Qt::DisplayRole, Qt::FontRole});
}

} // namespace PicoATE::Ui
