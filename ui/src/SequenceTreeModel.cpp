#include "SequenceTreeModel.h"
#include "PluginFunctionModel.h"

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

QByteArray encodePath(const SequenceItemPath& path)
{
    QJsonArray steps;
    for (const int index : path.stepIndices) {
        steps.push_back(index);
    }
    return QJsonDocument(QJsonObject{{"group", path.groupIndex},
                                     {"steps", steps}})
        .toJson(QJsonDocument::Compact);
}

SequenceItemPath decodePath(const QByteArray& bytes)
{
    SequenceItemPath path;
    const auto document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()) {
        return path;
    }
    const auto object = document.object();
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

} // namespace

SequenceTreeModel::SequenceTreeModel(SequenceDocument* document, QObject* parent)
    : QAbstractItemModel(parent)
    , m_document(document)
{
    Q_ASSERT(m_document);
    connect(m_document, &SequenceDocument::documentChanged,
            this, &SequenceTreeModel::rebuild);
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
    if (role == Qt::CheckStateRole && modelIndex.column() == EnabledColumn) {
        return itemEnabled(item->object) ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::CheckStateRole && modelIndex.column() == BreakpointColumn &&
        item->type == ItemType::Step && !item->nodePath.isEmpty()) {
        return m_breakpointNodePaths.contains(item->nodePath)
            ? Qt::Checked
            : Qt::Unchecked;
    }
    if (role == Qt::FontRole && item->nodePath == m_currentDebugNodePath) {
        QFont font;
        font.setBold(true);
        return font;
    }
    if (role == Qt::ToolTipRole) {
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
    if (modelIndex.column() == EnabledColumn) {
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
    case BreakpointColumn:
        return tr("BP");
    case EnabledColumn:
        return tr("Enabled");
    default:
        return {};
    }
}

QStringList SequenceTreeModel::mimeTypes() const
{
    return {QString::fromLatin1(sequencePathMimeType),
            QString::fromLatin1(PluginFunctionMimeType)};
}

QMimeData* SequenceTreeModel::mimeData(const QModelIndexList& indexes) const
{
    for (const auto& index : indexes) {
        if (!index.isValid() || index.column() != NameColumn ||
            itemType(index) != ItemType::Step) {
            continue;
        }
        auto* data = new QMimeData;
        data->setData(sequencePathMimeType, encodePath(pathForIndex(index)));
        return data;
    }
    return nullptr;
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

    const auto sourcePath = decodePath(data->data(sequencePathMimeType));
    if (!sourcePath.isValid() || sourcePath.stepIndices.isEmpty()) {
        return false;
    }
    SequenceItemPath movedPath;
    if (!m_document->relocateStep(
            sourcePath, destinationParent, row, &movedPath)) {
        return false;
    }
    emit itemMoved(sourcePath, movedPath);
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
    beginResetModel();
    m_breakpointNodePaths = std::move(nodePaths);
    endResetModel();
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
            group->parent = m_root.get();
            appendSteps(*group,
                        group->object.value("steps").toArray(),
                        group->path);
            m_root->children.push_back(std::move(group));
        }
    }
    endResetModel();
}

void SequenceTreeModel::appendSteps(Item& parent,
                                    const QJsonArray& steps,
                                    const SequenceItemPath& parentPath,
                                    const QString& parentNodePath,
                                    const QString& parentLocalPath)
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
        child->parent = &parent;
        appendSteps(*child,
                    child->object.value("steps").toArray(),
                    child->path,
                    child->nodePath,
                    child->localPath);
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
