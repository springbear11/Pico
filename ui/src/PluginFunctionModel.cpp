#include "PluginFunctionModel.h"

#include <QJsonDocument>
#include <QMimeData>

#include <algorithm>

namespace PicoATE::Ui {

PluginFunctionModel::PluginFunctionModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    rebuild();
}

PluginFunctionModel::~PluginFunctionModel() = default;

QModelIndex PluginFunctionModel::index(int row, int column,
                                      const QModelIndex& parentIndex) const
{
    if (column != 0 || row < 0) {
        return {};
    }
    auto* parentItem = itemForIndex(parentIndex);
    if (!parentItem || row >= static_cast<int>(parentItem->children.size())) {
        return {};
    }
    return createIndex(row, column, parentItem->children[row].get());
}

QModelIndex PluginFunctionModel::parent(const QModelIndex& child) const
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
    const auto iterator = std::find_if(
        grandParent->children.cbegin(), grandParent->children.cend(),
        [parentItem](const auto& candidate) { return candidate.get() == parentItem; });
    if (iterator == grandParent->children.cend()) {
        return {};
    }
    return createIndex(static_cast<int>(std::distance(
                           grandParent->children.cbegin(), iterator)),
                       0,
                       parentItem);
}

int PluginFunctionModel::rowCount(const QModelIndex& parentIndex) const
{
    if (parentIndex.column() > 0) {
        return 0;
    }
    const auto* item = itemForIndex(parentIndex);
    return item ? static_cast<int>(item->children.size()) : 0;
}

int PluginFunctionModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant PluginFunctionModel::data(const QModelIndex& modelIndex, int role) const
{
    const auto* item = itemForIndex(modelIndex);
    if (!item || item == m_root.get()) {
        return {};
    }
    if (role == Qt::DisplayRole) return item->text;
    if (role == Qt::ToolTipRole) return item->tooltip;
    if (role == ItemKindRole) return QVariant::fromValue(item->kind);
    if (item->pluginIndex >= 0 && item->pluginIndex < m_plugins.size()) {
        const auto& plugin = m_plugins[item->pluginIndex];
        if (role == CategoryRole) return plugin.category;
        if (role == ModuleIdRole) return plugin.moduleId;
        if (role == DeviceIdRole) return item->deviceId;
        if (role == FunctionIdRole && item->functionIndex >= 0 &&
            item->functionIndex < plugin.functions.size()) {
            return plugin.functions[item->functionIndex].id;
        }
    }
    return {};
}

QVariant PluginFunctionModel::headerData(int section,
                                         Qt::Orientation orientation,
                                         int role) const
{
    return section == 0 && orientation == Qt::Horizontal && role == Qt::DisplayRole
        ? tr("Plugin Functions")
        : QVariant{};
}

Qt::ItemFlags PluginFunctionModel::flags(const QModelIndex& modelIndex) const
{
    if (!modelIndex.isValid()) {
        return Qt::NoItemFlags;
    }
    auto result = QAbstractItemModel::flags(modelIndex);
    const auto* item = itemForIndex(modelIndex);
    if (item && item->kind == ItemKind::Function) {
        if (!item->deviceId.isEmpty()) {
            result |= Qt::ItemIsDragEnabled;
        }
    }
    return result;
}

QStringList PluginFunctionModel::mimeTypes() const
{
    return {QString::fromLatin1(PluginFunctionMimeType)};
}

QMimeData* PluginFunctionModel::mimeData(const QModelIndexList& indexes) const
{
    for (const auto& modelIndex : indexes) {
        const auto* item = itemForIndex(modelIndex);
        if (!item || modelIndex.column() != 0 ||
            item->kind != ItemKind::Function ||
            item->pluginIndex < 0 || item->pluginIndex >= m_plugins.size()) {
            continue;
        }
        const auto& plugin = m_plugins[item->pluginIndex];
        if (item->functionIndex < 0 ||
            item->functionIndex >= plugin.functions.size()) {
            continue;
        }
        auto step = PluginCatalog::createStep(
            plugin, plugin.functions[item->functionIndex], {});
        const auto deviceId = item->deviceId;
        if (deviceId.isEmpty()) {
            continue;
        }
        auto inputs = step.value("inputs").toObject();
        inputs.insert("deviceId", deviceId);
        step.insert("inputs", inputs);
        step.insert("moduleId", "device");
        auto* mime = new QMimeData;
        mime->setData(PluginFunctionMimeType,
                      QJsonDocument(step).toJson(QJsonDocument::Compact));
        return mime;
    }
    return nullptr;
}

Qt::DropActions PluginFunctionModel::supportedDragActions() const
{
    return Qt::CopyAction;
}

void PluginFunctionModel::setPlugins(QVector<PluginManifest> plugins)
{
    beginResetModel();
    m_plugins = std::move(plugins);
    rebuild();
    endResetModel();
}

void PluginFunctionModel::setDeviceBindings(
    QHash<QString, QStringList> devicesByModuleId)
{
    beginResetModel();
    m_devicesByModuleId = std::move(devicesByModuleId);
    rebuild();
    endResetModel();
}

QVector<PluginManifest> PluginFunctionModel::plugins() const
{
    return m_plugins;
}

void PluginFunctionModel::rebuild()
{
    m_root = std::make_unique<Item>();
    m_root->kind = ItemKind::Root;

    QVector<int> order(m_plugins.size());
    for (int index = 0; index < order.size(); ++index) order[index] = index;
    std::sort(order.begin(), order.end(), [this](int left, int right) {
        const auto& a = m_plugins[left];
        const auto& b = m_plugins[right];
        const int category = a.category.compare(b.category, Qt::CaseInsensitive);
        return category == 0
            ? a.name.compare(b.name, Qt::CaseInsensitive) < 0
            : category < 0;
    });

    for (const int pluginIndex : order) {
        const auto& plugin = m_plugins[pluginIndex];
        Item* categoryItem = nullptr;
        for (const auto& child : m_root->children) {
            if (child->text.compare(plugin.category, Qt::CaseInsensitive) == 0) {
                categoryItem = child.get();
                break;
            }
        }
        if (!categoryItem) {
            auto category = std::make_unique<Item>();
            category->kind = ItemKind::Category;
            category->text = plugin.category;
            category->tooltip = tr("%1 device plugins").arg(plugin.category);
            category->parent = m_root.get();
            categoryItem = category.get();
            m_root->children.push_back(std::move(category));
        }

        auto pluginItem = std::make_unique<Item>();
        pluginItem->kind = ItemKind::Plugin;
        pluginItem->text = plugin.name;
        auto deviceIds = m_devicesByModuleId.value(plugin.moduleId);
        deviceIds.removeAll(QString());
        deviceIds.removeDuplicates();
        pluginItem->tooltip = deviceIds.isEmpty()
            ? tr("Bind this plugin to a logical device in Station Editor before dragging functions")
            : tr("Logical devices: %1\n%2").arg(deviceIds.join(", "), plugin.dllPath);
        pluginItem->pluginIndex = pluginIndex;
        pluginItem->parent = categoryItem;
        auto* pluginPointer = pluginItem.get();
        categoryItem->children.push_back(std::move(pluginItem));

        const auto targets = deviceIds.isEmpty() ? QStringList{QString()} : deviceIds;
        for (const auto& deviceId : targets) {
            for (int functionIndex = 0;
                 functionIndex < plugin.functions.size(); ++functionIndex) {
                const auto& function = plugin.functions[functionIndex];
                auto functionItem = std::make_unique<Item>();
                functionItem->kind = ItemKind::Function;
                functionItem->text = deviceIds.size() > 1
                    ? tr("%1 [%2]").arg(function.name, deviceId)
                    : function.name;
                functionItem->tooltip = function.description.isEmpty()
                    ? tr("Function: %1").arg(function.id)
                    : function.description;
                if (deviceId.isEmpty()) {
                    functionItem->tooltip += tr("\nNo logical device binding");
                } else {
                    functionItem->tooltip += tr("\nDevice: %1").arg(deviceId);
                }
                functionItem->pluginIndex = pluginIndex;
                functionItem->functionIndex = functionIndex;
                functionItem->deviceId = deviceId;
                functionItem->parent = pluginPointer;
                pluginPointer->children.push_back(std::move(functionItem));
            }
        }
    }
}

PluginFunctionModel::Item* PluginFunctionModel::itemForIndex(
    const QModelIndex& modelIndex) const
{
    return modelIndex.isValid()
        ? static_cast<Item*>(modelIndex.internalPointer())
        : m_root.get();
}

} // namespace PicoATE::Ui
