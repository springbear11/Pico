#pragma once

#include "PicoATE/Core/ProductRouting.h"

#include <QDialog>
#include <QStringList>

class QLabel;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace PicoATE::Ui {

class OnOffSwitch;

class ProductRoutingDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ProductRoutingDialog(QString routingPath,
                                  QWidget* parent = nullptr);

signals:
    void routingSaved();

private:
    enum Column {
        EnabledColumn,
        NameColumn,
        PatternColumn,
        ProjectColumn,
        DeviceStatusColumn,
        DevicesColumn,
        ColumnCount,
    };

    void buildUi();
    void load();
    void populate(const PicoATE::Core::ProductRoutingConfig& config);
    void addRoute();
    void duplicateSelectedRoute();
    void removeSelectedRoute();
    void appendRoute(const PicoATE::Core::ProductRoute& route);
    void updateProjectRow(int row);
    void openProjectDevices(int row);
    void save();
    bool collectAndValidate(PicoATE::Core::ProductRoutingConfig* config,
                            QStringList* errors);
    QString validateSequence(const QString& sequencePath) const;
    void clearValidationState();
    void markCellInvalid(int row, int column, const QString& message);
    int selectedRouteRow() const;
    void updateButtons();

    QString m_routingPath;
    QString m_projectRootPath;
    QVector<PicoATE::Core::ProductProject> m_projects;
    OnOffSwitch* m_allowManualSwitch = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolButton* m_duplicateButton = nullptr;
    QToolButton* m_removeButton = nullptr;
    QPushButton* m_saveButton = nullptr;
};

} // namespace PicoATE::Ui
