#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QLabel;
class QEvent;
class QPushButton;
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

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum Column {
        NameColumn,
        TypeColumn,
        ScopeColumn,
        SharedValueColumn,
        Uut1Column
    };

    static constexpr int DefaultUutColumnCount = 4;
    static constexpr int MaximumUutColumnCount = 64;

    void appendVariable(const QJsonObject& variable = {},
                        bool selectNewRow = true);
    void appendUutColumn();
    void removeSelectedVariables();
    void selectVariableRow(int row);
    void restoreSelectedVariableRow();
    int selectedVariableRow() const;
    void updateRemoveButton();
    void updateRowAvailability(int row);
    bool buildVariables(QJsonArray& result, QString& errorMessage) const;
    int descriptionColumn() const;
    int tableColumnCount() const;

    QTableWidget* m_table = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_addUutButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    int m_uutColumnCount = DefaultUutColumnCount;
    int m_selectedVariableRow = -1;
};

} // namespace PicoATE::Ui
