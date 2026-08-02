#include "SequenceVariablesDialog.h"

#include <QComboBox>
#include <QBrush>
#include <QColor>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace PicoATE::Ui {
namespace {

QString displayValue(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    return {};
}

QComboBox* comboFor(QTableWidget* table, int row, int column)
{
    return qobject_cast<QComboBox*>(table->cellWidget(row, column));
}

QJsonValue typedValue(const QString& text,
                      const QString& type,
                      bool& ok)
{
    const auto trimmed = text.trimmed();
    ok = !trimmed.isEmpty();
    if (!ok) {
        return QJsonValue::Null;
    }
    if (type == QStringLiteral("string")) {
        return text;
    }
    if (type == QStringLiteral("integer")) {
        bool converted = false;
        const auto value = trimmed.toLongLong(&converted, 10);
        ok = converted;
        return converted ? QJsonValue::fromVariant(value) : QJsonValue::Null;
    }
    if (type == QStringLiteral("hex")) {
        static const QRegularExpression pattern(
            QStringLiteral("^(?:0[xX])?[0-9A-Fa-f]+$"));
        ok = pattern.match(trimmed).hasMatch();
        if (!ok) {
            return QJsonValue::Null;
        }
        auto digits = trimmed;
        if (digits.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            digits.remove(0, 2);
        }
        bool converted = false;
        digits.toULongLong(&converted, 16);
        ok = converted;
        return converted
            ? QJsonValue(QStringLiteral("0x%1").arg(digits.toUpper()))
            : QJsonValue::Null;
    }
    if (type == QStringLiteral("double")) {
        bool converted = false;
        const auto value = trimmed.toDouble(&converted);
        ok = converted;
        return converted ? QJsonValue(value) : QJsonValue::Null;
    }
    if (type == QStringLiteral("bool")) {
        if (trimmed.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
            return true;
        }
        if (trimmed.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
            return false;
        }
        ok = false;
    }
    return QJsonValue::Null;
}

} // namespace

SequenceVariablesDialog::SequenceVariablesDialog(QJsonArray variables,
                                                 QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("sequenceVariablesDialog"));
    setWindowTitle(tr("Sequence Variables"));
    resize(1120, 520);
    setMinimumSize(900, 420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 14);
    layout->setSpacing(10);

    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(6);
    auto* addButton = new QPushButton(
        style()->standardIcon(QStyle::SP_FileIcon), tr("Add Variable"), this);
    addButton->setObjectName(QStringLiteral("addSequenceVariableButton"));
    auto* removeButton = new QPushButton(
        style()->standardIcon(QStyle::SP_TrashIcon), tr("Remove"), this);
    removeButton->setObjectName(QStringLiteral("removeSequenceVariableButton"));
    toolbar->addWidget(addButton);
    toolbar->addWidget(removeButton);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("sequenceVariablesTable"));
    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels(
        {tr("Name"), tr("Type"), tr("Scope"), tr("Shared Value"),
         tr("UUT1"), tr("UUT2"), tr("UUT3"), tr("UUT4"),
         tr("Description")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(34);
    auto* header = m_table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(true);
    m_table->setColumnWidth(NameColumn, 145);
    m_table->setColumnWidth(TypeColumn, 100);
    m_table->setColumnWidth(ScopeColumn, 105);
    for (int column = SharedValueColumn; column <= Uut4Column; ++column) {
        m_table->setColumnWidth(column, 105);
    }
    layout->addWidget(m_table, 1);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("sequenceVariablesErrorLabel"));
    m_errorLabel->setStyleSheet(QStringLiteral("color: #b42318; font-weight: 600;"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("sequenceVariablesButtons"));
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(buttons, &QDialogButtonBox::accepted,
            this, &SequenceVariablesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &SequenceVariablesDialog::reject);
    layout->addWidget(buttons);

    connect(addButton, &QPushButton::clicked,
            this, [this] { appendVariable(); });
    connect(removeButton, &QPushButton::clicked,
            this, &SequenceVariablesDialog::removeSelectedVariables);
    connect(m_table, &QTableWidget::itemChanged, this, [this] {
        m_errorLabel->hide();
    });

    for (const auto& value : variables) {
        if (value.isObject()) {
            appendVariable(value.toObject());
        }
    }
}

QJsonArray SequenceVariablesDialog::variables() const
{
    QJsonArray result;
    QString ignored;
    buildVariables(result, ignored);
    return result;
}

void SequenceVariablesDialog::accept()
{
    QJsonArray result;
    QString errorMessage;
    if (!buildVariables(result, errorMessage)) {
        m_errorLabel->setText(errorMessage);
        m_errorLabel->show();
        return;
    }
    QDialog::accept();
}

void SequenceVariablesDialog::appendVariable(const QJsonObject& variable)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto* name = new QTableWidgetItem(variable.value(QStringLiteral("name")).toString());
    if (name->text().isEmpty()) {
        int suffix = row + 1;
        name->setText(QStringLiteral("VARIABLE_%1").arg(suffix));
    }
    m_table->setItem(row, NameColumn, name);

    auto* type = new QComboBox(m_table);
    type->setObjectName(QStringLiteral("sequenceVariableType"));
    type->addItem(tr("String"), QStringLiteral("string"));
    type->addItem(tr("Integer"), QStringLiteral("integer"));
    type->addItem(tr("Hex"), QStringLiteral("hex"));
    type->addItem(tr("Double"), QStringLiteral("double"));
    type->addItem(tr("Boolean"), QStringLiteral("bool"));
    const auto requestedType = variable.value(QStringLiteral("type"))
                                   .toString(QStringLiteral("string"));
    type->setCurrentIndex(qMax(0, type->findData(requestedType)));
    m_table->setCellWidget(row, TypeColumn, type);

    auto* scope = new QComboBox(m_table);
    scope->setObjectName(QStringLiteral("sequenceVariableScope"));
    scope->addItem(tr("Shared"), QStringLiteral("shared"));
    scope->addItem(tr("Per UUT"), QStringLiteral("perUut"));
    const auto requestedScope = variable.value(QStringLiteral("scope"))
                                    .toString(QStringLiteral("shared"));
    scope->setCurrentIndex(qMax(0, scope->findData(requestedScope)));
    m_table->setCellWidget(row, ScopeColumn, scope);

    m_table->setItem(row, SharedValueColumn,
                     new QTableWidgetItem(displayValue(variable.value(QStringLiteral("value")))));
    const auto values = variable.value(QStringLiteral("values")).toArray();
    for (int index = 0; index < 4; ++index) {
        m_table->setItem(row, Uut1Column + index,
                         new QTableWidgetItem(index < values.size()
                                                  ? displayValue(values[index])
                                                  : QString{}));
    }
    m_table->setItem(row, DescriptionColumn,
                     new QTableWidgetItem(variable.value(QStringLiteral("description")).toString()));

    connect(scope, &QComboBox::currentIndexChanged,
            this, [this, row] { updateRowAvailability(row); });
    connect(type, &QComboBox::currentIndexChanged,
            this, [this] { m_errorLabel->hide(); });
    updateRowAvailability(row);
    m_table->setCurrentCell(row, NameColumn);
}

void SequenceVariablesDialog::removeSelectedVariables()
{
    QSet<int> rows;
    for (const auto& index : m_table->selectionModel()->selectedRows()) {
        rows.insert(index.row());
    }
    QList<int> ordered = rows.values();
    std::sort(ordered.begin(), ordered.end(), std::greater<int>());
    for (int row : ordered) {
        m_table->removeRow(row);
    }
    m_errorLabel->hide();
}

void SequenceVariablesDialog::updateRowAvailability(int row)
{
    const auto* scope = comboFor(m_table, row, ScopeColumn);
    const bool perUut = scope && scope->currentData().toString() == QStringLiteral("perUut");
    for (int column = SharedValueColumn; column <= Uut4Column; ++column) {
        auto* item = m_table->item(row, column);
        if (!item) {
            continue;
        }
        const bool editable = perUut ? column >= Uut1Column
                                     : column == SharedValueColumn;
        auto flags = item->flags();
        if (editable) {
            flags |= Qt::ItemIsEditable;
            item->setBackground(QBrush{});
        } else {
            flags &= ~Qt::ItemIsEditable;
            item->setBackground(QColor(QStringLiteral("#edf0f2")));
        }
        item->setFlags(flags);
    }
}

bool SequenceVariablesDialog::buildVariables(QJsonArray& result,
                                             QString& errorMessage) const
{
    static const QRegularExpression namePattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    static const QSet<QString> reserved = {
        QStringLiteral("uut"), QStringLiteral("loop"),
        QStringLiteral("frame"), QStringLiteral("attempt"),
        QStringLiteral("sn"), QStringLiteral("serialnumber")};
    QSet<QString> names;

    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto name = m_table->item(row, NameColumn)->text().trimmed();
        if (!namePattern.match(name).hasMatch()) {
            errorMessage = tr("Row %1: Name must start with a letter or underscore and contain only letters, numbers, and underscores.")
                               .arg(row + 1);
            return false;
        }
        if (reserved.contains(name.toLower())) {
            errorMessage = tr("Row %1: '%2' is reserved by the runtime.")
                               .arg(row + 1).arg(name);
            return false;
        }
        if (names.contains(name)) {
            errorMessage = tr("Row %1: Variable name '%2' is duplicated.")
                               .arg(row + 1).arg(name);
            return false;
        }
        names.insert(name);

        const auto* typeCombo = comboFor(m_table, row, TypeColumn);
        const auto* scopeCombo = comboFor(m_table, row, ScopeColumn);
        const auto type = typeCombo->currentData().toString();
        const auto scope = scopeCombo->currentData().toString();
        QJsonObject variable;
        variable.insert(QStringLiteral("name"), name);
        variable.insert(QStringLiteral("type"), type);
        variable.insert(QStringLiteral("scope"), scope);
        const auto description = m_table->item(row, DescriptionColumn)->text().trimmed();
        if (!description.isEmpty()) {
            variable.insert(QStringLiteral("description"), description);
        }

        if (scope == QStringLiteral("shared")) {
            bool ok = false;
            const auto value = typedValue(
                m_table->item(row, SharedValueColumn)->text(), type, ok);
            if (!ok) {
                errorMessage = tr("Row %1: Shared Value is missing or does not match type %2.")
                                   .arg(row + 1).arg(type);
                return false;
            }
            variable.insert(QStringLiteral("value"), value);
        } else {
            QJsonArray values;
            bool hasValue = false;
            for (int index = 0; index < 4; ++index) {
                const auto text = m_table->item(row, Uut1Column + index)->text();
                if (text.trimmed().isEmpty()) {
                    values.push_back(QJsonValue::Null);
                    continue;
                }
                bool ok = false;
                const auto value = typedValue(text, type, ok);
                if (!ok) {
                    errorMessage = tr("Row %1: UUT%2 value does not match type %3.")
                                       .arg(row + 1).arg(index + 1).arg(type);
                    return false;
                }
                hasValue = true;
                values.push_back(value);
            }
            if (!hasValue) {
                errorMessage = tr("Row %1: Enter at least one Per UUT value.")
                                   .arg(row + 1);
                return false;
            }
            while (!values.isEmpty() && values.last().isNull()) {
                values.removeLast();
            }
            variable.insert(QStringLiteral("values"), values);
        }
        result.push_back(variable);
    }
    return true;
}

} // namespace PicoATE::Ui
