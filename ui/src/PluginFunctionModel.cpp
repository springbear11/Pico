#include "PluginFunctionModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>

#include <algorithm>
#include <utility>

namespace PicoATE::Ui {

namespace {

QJsonObject basicStep(const QString& name,
                      const QString& kind,
                      QJsonObject extra = {})
{
    QJsonObject step{{QStringLiteral("name"), name},
                     {QStringLiteral("kind"), kind},
                     {QStringLiteral("enabled"), true}};
    for (auto iterator = extra.constBegin(); iterator != extra.constEnd(); ++iterator) {
        step.insert(iterator.key(), iterator.value());
    }
    return step;
}

} // namespace

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
    if (!item->stepTemplate.isEmpty()) {
        if (role == CategoryRole) return QStringLiteral("Basic");
        if (role == FunctionIdRole) {
            return item->stepTemplate.value(QStringLiteral("kind")).toString();
        }
    }
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
        ? tr("Function Palette")
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
        if (!item->stepTemplate.isEmpty() || item->pluginIndex >= 0) {
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
            item->kind != ItemKind::Function) {
            continue;
        }
        if (item->requiresDeviceSelection) {
            return nullptr;
        }
        if (item->stepTemplate.isEmpty() && item->pluginIndex < 0) {
            continue;
        }
        const auto step = stepTemplate(modelIndex);
        if (step.isEmpty()) {
            continue;
        }
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
    bool selectedStillExists = false;
    for (const auto& deviceIds : std::as_const(m_devicesByModuleId)) {
        selectedStillExists = selectedStillExists ||
            deviceIds.contains(m_selectedDeviceId, Qt::CaseInsensitive);
    }
    if (!selectedStillExists) {
        m_selectedDeviceId.clear();
    }
    rebuild();
    endResetModel();
}

void PluginFunctionModel::setSelectedDeviceId(const QString& deviceId)
{
    const auto normalized = deviceId.trimmed();
    if (m_selectedDeviceId == normalized) {
        return;
    }
    beginResetModel();
    m_selectedDeviceId = normalized;
    rebuild();
    endResetModel();
}

QVector<PluginManifest> PluginFunctionModel::plugins() const
{
    return m_plugins;
}

QString PluginFunctionModel::selectedDeviceId() const
{
    return m_selectedDeviceId;
}

bool PluginFunctionModel::requiresDeviceSelection(
    const QModelIndex& modelIndex) const
{
    const auto* item = itemForIndex(modelIndex);
    return item && item->kind == ItemKind::Function &&
           item->requiresDeviceSelection;
}

QJsonObject PluginFunctionModel::stepTemplate(const QModelIndex& modelIndex) const
{
    const auto* item = itemForIndex(modelIndex);
    if (!item || item->kind != ItemKind::Function) {
        return {};
    }
    if (!item->stepTemplate.isEmpty()) {
        return item->stepTemplate;
    }
    if (item->pluginIndex < 0 || item->pluginIndex >= m_plugins.size()) {
        return {};
    }
    const auto& plugin = m_plugins[item->pluginIndex];
    if (item->functionIndex < 0 || item->functionIndex >= plugin.functions.size()) {
        return {};
    }
    auto step = PluginCatalog::createStep(
        plugin, plugin.functions[item->functionIndex], {});
    if (!item->deviceId.isEmpty()) {
        const auto functionId = plugin.functions[item->functionIndex].id;
        const bool stationManagedConnection =
            functionId.compare(QStringLiteral("open"), Qt::CaseInsensitive) == 0 ||
            functionId.compare(QStringLiteral("connect"), Qt::CaseInsensitive) == 0 ||
            functionId.compare(QStringLiteral("connectCan"), Qt::CaseInsensitive) == 0;
        auto inputs = stationManagedConnection
            ? QJsonObject{}
            : step.value(QStringLiteral("inputs")).toObject();
        inputs.insert(QStringLiteral("deviceId"), item->deviceId);
        step.insert(QStringLiteral("inputs"), inputs);
        step.insert(QStringLiteral("moduleId"), QStringLiteral("device"));
    }
    return step;
}

void PluginFunctionModel::rebuild()
{
    m_root = std::make_unique<Item>();
    m_root->kind = ItemKind::Root;

    auto basicSection = std::make_unique<Item>();
    basicSection->kind = ItemKind::Section;
    basicSection->text = tr("Basic Functions");
    basicSection->tooltip = tr("Built-in flow control and result evaluation steps");
    basicSection->parent = m_root.get();
    auto* basicSectionPointer = basicSection.get();
    m_root->children.push_back(std::move(basicSection));

    const QVector<QPair<QString, QJsonObject>> basicFunctions = {
        {tr("Wait"), basicStep(tr("Wait"), QStringLiteral("wait"),
                               {{QStringLiteral("ms"), 1000}})},
        {tr("MessageBox"), basicStep(
             tr("MessageBox"), QStringLiteral("operatorPrompt"),
             {{QStringLiteral("prompt"),
               QJsonObject{{QStringLiteral("mode"), QStringLiteral("confirm")},
                           {QStringLiteral("title"), QStringLiteral("Message")},
                           {QStringLiteral("message"), QStringLiteral("Complete the requested action, then click OK.")},
                           {QStringLiteral("confirmText"), QStringLiteral("OK")},
                           {QStringLiteral("timeoutMs"), 60000}}}})},
        {tr("Limit Check"), basicStep(
             tr("Limit Check"), QStringLiteral("limit"),
             {{QStringLiteral("inputs"), QJsonObject{{QStringLiteral("actual"), QString()}}},
              {QStringLiteral("parameters"),
               QJsonObject{{QStringLiteral("comparison"), QStringLiteral("between")},
                           {QStringLiteral("expected"), 0.0},
                           {QStringLiteral("tolerance"), 0.0},
                           {QStringLiteral("inclusive"), true},
                           {QStringLiteral("measurementName"), tr("Measurement")}}}})},
        {tr("Test Item"), basicStep(tr("Test Item"), QStringLiteral("testItem"),
                                    {{QStringLiteral("steps"), QJsonArray{}}})},
        {tr("For Loop"), basicStep(
             tr("For Loop"), QStringLiteral("loop"),
             {{QStringLiteral("loop"),
               QJsonObject{{QStringLiteral("type"), QStringLiteral("for")},
                           {QStringLiteral("variable"), QStringLiteral("i")},
                           {QStringLiteral("from"), 0},
                           {QStringLiteral("to"), 0},
                           {QStringLiteral("step"), 1}}},
              {QStringLiteral("steps"), QJsonArray{}}})},
        {tr("While Loop"), basicStep(
             tr("While Loop"), QStringLiteral("loop"),
             {{QStringLiteral("loop"),
               QJsonObject{{QStringLiteral("type"), QStringLiteral("while")},
                           {QStringLiteral("intervalMs"), 200},
                           {QStringLiteral("maxIterations"), 100},
                           {QStringLiteral("timeoutMs"), 60000},
                           {QStringLiteral("iterationErrorPolicy"), QStringLiteral("abortLoop")}}},
              {QStringLiteral("steps"), QJsonArray{}}})},
        {tr("Break If"), basicStep(
             tr("Break If"), QStringLiteral("break"),
             {{QStringLiteral("inputs"), QJsonObject{{QStringLiteral("actual"), QString()}}},
              {QStringLiteral("parameters"),
               QJsonObject{{QStringLiteral("comparison"), QStringLiteral("isTrue")}}}})},
         {tr("Counter"), basicStep(
              tr("Counter"), QStringLiteral("counter"),
              {{QStringLiteral("parameters"),
                QJsonObject{{QStringLiteral("mode"), QStringLiteral("consecutive")},
                            {QStringLiteral("start"), 0},
                            {QStringLiteral("increment"), 1}}}})},
        {tr("Aggregate"), basicStep(
             tr("Aggregate"), QStringLiteral("aggregate"),
             {{QStringLiteral("inputs"), QJsonObject{{QStringLiteral("value"), QString()}}}})},
        {tr("Barrier"), basicStep(
             tr("Barrier"), QStringLiteral("barrier"),
             {{QStringLiteral("barrier"),
               QJsonObject{{QStringLiteral("cohortId"), QStringLiteral("default")},
                           {QStringLiteral("expectedUutCount"), -1},
                           {QStringLiteral("arrivalPolicy"), QStringLiteral("WaitAll")},
                           {QStringLiteral("releasePolicy"), QStringLiteral("Lockstep")},
                           {QStringLiteral("failurePolicy"), QStringLiteral("FailBarrier")},
                           {QStringLiteral("timeoutPolicy"), QStringLiteral("FailArrivedAndWaiting")},
                           {QStringLiteral("arrivalTimeoutMs"), 60000},
                           {QStringLiteral("releaseTimeoutMs"), 5000},
                           {QStringLiteral("releaseHeldResourcesOnWait"), true}}}})},
        {tr("No Operation"), basicStep(tr("No Operation"), QStringLiteral("noop"))}
    };
    for (const auto& definition : basicFunctions) {
        auto function = std::make_unique<Item>();
        function->kind = ItemKind::Function;
        function->text = definition.first;
        function->tooltip = tr("Drag to the sequence to add a %1 step")
                                .arg(definition.first);
        function->stepTemplate = definition.second;
        function->parent = basicSectionPointer;
        basicSectionPointer->children.push_back(std::move(function));
    }

    const auto parserManifest = builtInDataParserManifest();
    auto parserCategory = std::make_unique<Item>();
    parserCategory->kind = ItemKind::Category;
    parserCategory->text = tr("Data Parsing");
    parserCategory->tooltip = tr(
        "Decode binary payloads, Modbus registers, and structured text");
    parserCategory->parent = basicSectionPointer;
    auto* parserCategoryPointer = parserCategory.get();
    basicSectionPointer->children.push_back(std::move(parserCategory));
    for (const auto& definition : parserManifest.functions) {
        auto function = std::make_unique<Item>();
        function->kind = ItemKind::Function;
        function->text = definition.name;
        function->tooltip = definition.description;
        function->stepTemplate = PluginCatalog::createStep(
            parserManifest, definition, {});
        function->parent = parserCategoryPointer;
        parserCategoryPointer->children.push_back(std::move(function));
    }

    const auto valueToolsManifest = builtInValueToolsManifest();
    auto valueToolsCategory = std::make_unique<Item>();
    valueToolsCategory->kind = ItemKind::Category;
    valueToolsCategory->text = tr("Value Tools");
    valueToolsCategory->tooltip = tr(
        "Calculate statistics, arithmetic results, and number representations");
    valueToolsCategory->parent = basicSectionPointer;
    auto* valueToolsCategoryPointer = valueToolsCategory.get();
    basicSectionPointer->children.push_back(std::move(valueToolsCategory));
    for (const auto& definition : valueToolsManifest.functions) {
        auto function = std::make_unique<Item>();
        function->kind = ItemKind::Function;
        function->text = definition.name;
        function->tooltip = definition.description;
        function->stepTemplate = PluginCatalog::createStep(
            valueToolsManifest, definition, {});
        function->parent = valueToolsCategoryPointer;
        valueToolsCategoryPointer->children.push_back(std::move(function));
    }

    auto pluginSection = std::make_unique<Item>();
    pluginSection->kind = ItemKind::Section;
    pluginSection->text = tr("Plugin Functions");
    pluginSection->tooltip = tr("Functions provided by scanned device plugins");
    pluginSection->parent = m_root.get();
    auto* pluginSectionPointer = pluginSection.get();
    m_root->children.push_back(std::move(pluginSection));

    QString selectedModuleId;
    for (auto iterator = m_devicesByModuleId.constBegin();
         iterator != m_devicesByModuleId.constEnd(); ++iterator) {
        if (iterator.value().contains(m_selectedDeviceId,
                                      Qt::CaseInsensitive)) {
            selectedModuleId = iterator.key();
            break;
        }
    }
    QString selectedCategoryKey;
    if (!selectedModuleId.isEmpty()) {
        for (const auto& plugin : std::as_const(m_plugins)) {
            if (plugin.moduleId.compare(selectedModuleId,
                                        Qt::CaseInsensitive) == 0) {
                selectedCategoryKey = plugin.category.trimmed().toLower();
                break;
            }
        }
    }

    if (m_plugins.isEmpty()) {
        auto placeholder = std::make_unique<Item>();
        placeholder->kind = ItemKind::Plugin;
        placeholder->text = tr("No scanned plugins");
        placeholder->tooltip = tr("Use Scan Plugins to load plugin functions");
        placeholder->parent = pluginSectionPointer;
        pluginSectionPointer->children.push_back(std::move(placeholder));
        return;
    }

    QHash<QString, Item*> categories;
    QHash<QString, QHash<QString, Item*>> functionsByCategory;
    for (int pluginIndex = 0; pluginIndex < m_plugins.size(); ++pluginIndex) {
        const auto& plugin = m_plugins[pluginIndex];
        const auto categoryName = plugin.category.trimmed().isEmpty()
            ? tr("Other") : plugin.category.trimmed();
        const auto categoryKey = categoryName.toLower();
        if (!m_selectedDeviceId.isEmpty() &&
            categoryKey != selectedCategoryKey) {
            continue;
        }
        bool pluginHasBoundDevice = false;
        for (auto iterator = m_devicesByModuleId.constBegin();
             iterator != m_devicesByModuleId.constEnd(); ++iterator) {
            if (iterator.key().compare(plugin.moduleId,
                                       Qt::CaseInsensitive) == 0 &&
                !iterator.value().isEmpty()) {
                pluginHasBoundDevice = true;
                break;
            }
        }
        auto* categoryPointer = categories.value(categoryKey);
        if (!categoryPointer) {
            auto category = std::make_unique<Item>();
            category->kind = ItemKind::Category;
            category->text = categoryName;
            category->tooltip = tr("Scanned %1 plugin functions").arg(categoryName);
            category->parent = pluginSectionPointer;
            categoryPointer = category.get();
            pluginSectionPointer->children.push_back(std::move(category));
            categories.insert(categoryKey, categoryPointer);
        }

        const bool selectedDeviceUsesPlugin = !m_selectedDeviceId.isEmpty() &&
            selectedModuleId.compare(plugin.moduleId, Qt::CaseInsensitive) == 0;
        for (int functionIndex = 0;
             functionIndex < plugin.functions.size(); ++functionIndex) {
            const auto& function = plugin.functions[functionIndex];
            const auto functionKey = function.id.trimmed().toLower();
            auto* existing = functionsByCategory[categoryKey].value(functionKey);
            if (existing) {
                existing->requiresDeviceSelection =
                    existing->requiresDeviceSelection ||
                    (m_selectedDeviceId.isEmpty() && pluginHasBoundDevice);
                const bool existingMatchesTarget = !existing->deviceId.isEmpty();
                if (!selectedDeviceUsesPlugin || existingMatchesTarget) {
                    continue;
                }
                existing->text = function.name;
                existing->tooltip = function.description;
                existing->pluginIndex = pluginIndex;
                existing->functionIndex = functionIndex;
                existing->deviceId = m_selectedDeviceId;
                existing->requiresDeviceSelection = false;
                existing->tooltip += tr("\nTarget: %1\nDriver module: %2")
                                         .arg(m_selectedDeviceId, plugin.moduleId);
                continue;
            }

            auto functionItem = std::make_unique<Item>();
            functionItem->kind = ItemKind::Function;
            functionItem->text = function.name;
            functionItem->tooltip = function.description.isEmpty()
                ? tr("Function: %1").arg(function.id)
                : function.description;
            functionItem->tooltip += selectedDeviceUsesPlugin
                ? tr("\nTarget: %1\nModule: %2")
                      .arg(m_selectedDeviceId, plugin.moduleId)
                : tr("\nDirect plugin call\nModule: %1").arg(plugin.moduleId);
            functionItem->pluginIndex = pluginIndex;
            functionItem->functionIndex = functionIndex;
            functionItem->deviceId = selectedDeviceUsesPlugin
                ? m_selectedDeviceId : QString{};
            functionItem->requiresDeviceSelection =
                m_selectedDeviceId.isEmpty() && pluginHasBoundDevice;
            if (functionItem->requiresDeviceSelection) {
                functionItem->tooltip +=
                    tr("\nSelect a target device above before dragging this function");
            }
            functionItem->parent = categoryPointer;
            auto* functionPointer = functionItem.get();
            categoryPointer->children.push_back(std::move(functionItem));
            functionsByCategory[categoryKey].insert(functionKey, functionPointer);
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
