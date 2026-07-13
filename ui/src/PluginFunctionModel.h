#pragma once

#include "PluginCatalog.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QJsonObject>

#include <memory>
#include <vector>

namespace PicoATE::Ui {

inline constexpr auto PluginFunctionMimeType =
    "application/x-picoate-plugin-function";

class PluginFunctionModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Role {
        ItemKindRole = Qt::UserRole + 1,
        CategoryRole,
        ModuleIdRole,
        FunctionIdRole,
        DeviceIdRole
    };

    enum class ItemKind {
        Root,
        Section,
        Category,
        Plugin,
        Function
    };
    Q_ENUM(ItemKind)

    explicit PluginFunctionModel(QObject* parent = nullptr);
    ~PluginFunctionModel() override;

    QModelIndex index(int row, int column,
                      const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override;

    void setPlugins(QVector<PluginManifest> plugins);
    void setDeviceBindings(QHash<QString, QStringList> devicesByModuleId);
    QVector<PluginManifest> plugins() const;
    QJsonObject stepTemplate(const QModelIndex& index) const;

private:
    struct Item {
        ItemKind kind = ItemKind::Root;
        QString text;
        QString tooltip;
        int pluginIndex = -1;
        int functionIndex = -1;
        QString deviceId;
        QJsonObject stepTemplate;
        Item* parent = nullptr;
        std::vector<std::unique_ptr<Item>> children;
    };

    void rebuild();
    Item* itemForIndex(const QModelIndex& index) const;

    QVector<PluginManifest> m_plugins;
    QHash<QString, QStringList> m_devicesByModuleId;
    std::unique_ptr<Item> m_root;
};

} // namespace PicoATE::Ui
