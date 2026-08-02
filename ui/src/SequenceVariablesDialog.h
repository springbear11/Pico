#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QLabel;
class QTableWidget;

namespace PicoATE::Ui {

class SequenceVariablesDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit SequenceVariablesDialog(QJsonArray variables,
                                     QWidget* parent = nullptr);

    QJsonArray variables() const;

    void accept() override;

private:
    enum Column {
        NameColumn,
        TypeColumn,
        ScopeColumn,
        SharedValueColumn,
        Uut1Column,
        Uut2Column,
        Uut3Column,
        Uut4Column,
        DescriptionColumn,
        ColumnCount
    };

    void appendVariable(const QJsonObject& variable = {});
    void removeSelectedVariables();
    void updateRowAvailability(int row);
    bool buildVariables(QJsonArray& result, QString& errorMessage) const;

    QTableWidget* m_table = nullptr;
    QLabel* m_errorLabel = nullptr;
};

} // namespace PicoATE::Ui
