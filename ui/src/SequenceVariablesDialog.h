#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QLabel;
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

    void appendVariable(const QJsonObject& variable = {});
    void appendUutColumn();
    void removeSelectedVariables();
    void updateRowAvailability(int row);
    bool buildVariables(QJsonArray& result, QString& errorMessage) const;
    int descriptionColumn() const;
    int tableColumnCount() const;

    QTableWidget* m_table = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_addUutButton = nullptr;
    int m_uutColumnCount = DefaultUutColumnCount;
};

} // namespace PicoATE::Ui
