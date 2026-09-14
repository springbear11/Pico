#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

class QPushButton;

namespace PicoATE::Ui {

class ExecutionViewModel;
class ScanDialog;
class UutOverviewModel;

// Presentation state only; never changes a run request, SN variable or UUT result.
class UutNavigationStatus final : public QObject
{
public:
    UutNavigationStatus(UutOverviewModel* model, ScanDialog* scanner,
                        ExecutionViewModel* execution, QObject* parent);
    void bind(QPushButton* button, int row);

private:
    void scanProgressChanged(const QStringList& serials, const QVector<bool>& enabled);
    void refresh();
    void refreshRange(int firstRow, int lastRow);

    UutOverviewModel* m_model;
    ExecutionViewModel* m_execution;
    QVector<QPointer<QPushButton>> m_buttons;
    QStringList m_scanSerials;
    QVector<bool> m_scanEnabled;
    bool m_scanPreview = false;
};

} // namespace PicoATE::Ui
