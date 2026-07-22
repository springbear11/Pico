#pragma once

#include "SequenceDocument.h"
#include "PicoATE/Core/ExecutionDebug.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QHash>
#include <QPointer>
#include <QSet>

#include <memory>
#include <vector>

class QMimeData;

namespace PicoATE::Ui {

class SequenceTreeModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column {
        NameColumn,
        KindColumn,
        IdColumn,
        BreakpointColumn,
        EnabledColumn,
        InspectionColumn,
        ColumnCount
    };

    enum Role {
        ItemTypeRole = Qt::UserRole + 1,
        JsonPathRole,
        EffectiveEnabledRole,
        DisabledByAncestorRole
    };

    enum class ItemType {
        Group,
        Step
    };
    Q_ENUM(ItemType)

    explicit SequenceTreeModel(SequenceDocument* document,
                               QObject* parent = nullptr);
    ~SequenceTreeModel() override;

    QModelIndex index(int row,
                      int column,
                      const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index,
                 const QVariant& value,
                 int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data,
                      Qt::DropAction action,
                      int row,
                      int column,
                      const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;

    SequenceItemPath pathForIndex(const QModelIndex& index) const;
    ItemType itemType(const QModelIndex& index) const;
    QModelIndex indexForPath(const SequenceItemPath& path) const;
    QModelIndex indexForNodePath(const QString& nodePath) const;
    QString nodePathForIndex(const QModelIndex& index) const;
    QString localPathForIndex(const QModelIndex& index) const;
    QSet<QString> breakpointNodePaths() const;
    QVector<PicoATE::Core::BreakpointSpec> breakpointSpecs() const;
    void setBreakpointNodePaths(QSet<QString> nodePaths);
    void clearBreakpoints();
    void setCurrentDebugNodePath(const QString& nodePath);
    int setInspectionField(QString fieldPath);
    QString inspectionField() const;
    int inspectionMatchCount() const;

signals:
    void itemMoved(const PicoATE::Ui::SequenceItemPath& from,
                   const PicoATE::Ui::SequenceItemPath& to);
    void itemInserted(const PicoATE::Ui::SequenceItemPath& path);
    void breakpointsChanged();

private:
    struct Item {
        ItemType type = ItemType::Step;
        SequenceItemPath path;
        QJsonObject object;
        QString nodePath;
        QString localPath;
        bool effectiveEnabled = true;
        bool disabledByAncestor = false;
        Item* parent = nullptr;
        std::vector<std::unique_ptr<Item>> children;
    };

    void rebuild();
    void appendSteps(Item& parent,
                     const QJsonArray& steps,
                     const SequenceItemPath& parentPath,
                     const QString& parentNodePath = {},
                     const QString& parentLocalPath = {},
                     bool parentEffectiveEnabled = true);
    Item* itemForIndex(const QModelIndex& index) const;
    QModelIndex findIndex(const Item& parent,
                          const SequenceItemPath& path) const;
    QModelIndex findIndexForNodePath(const Item& parent,
                                     const QString& nodePath) const;
    void emitRowChanged(const QString& nodePath);
    void rebuildInspectionColors();

    QPointer<SequenceDocument> m_document;
    std::unique_ptr<Item> m_root;
    QSet<QString> m_breakpointNodePaths;
    QString m_currentDebugNodePath;
    QString m_inspectionField;
    QHash<QString, QColor> m_inspectionColors;
    int m_inspectionMatchCount = 0;
};

} // namespace PicoATE::Ui
