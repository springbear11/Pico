#include "RunInformationDialog.h"
#include "UiTextBinding.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <utility>

namespace PicoATE::Ui {
namespace {
constexpr auto PreviousGroup = "RunInformation/Previous";
}

QToolButton* makeRunInformationButton(QAction* action, QWidget* parent, const QString& name)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(name);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setAutoRaise(true);
    button->setFixedSize(28, 28);
    button->setIconSize(QSize(18, 18));
    button->setStyleSheet(QStringLiteral(
        "QToolButton{border:0;border-radius:4px;background:transparent;padding:4px;}"
        "QToolButton:hover{background:#e7eff4;}QToolButton:pressed{background:#d8e5ed;}"));
    return button;
}

RunInformationDialog::RunInformationDialog(const RunInformation& current, bool modelEditable,
                                         ModelSaver saveModel, QWidget* parent)
    : QDialog(parent), m_information(current), m_saveModel(std::move(saveModel))
{
    setObjectName(QStringLiteral("runInformationDialog"));
    setWindowTitle(uiText("Basic Information"));
    setWindowModality(Qt::WindowModal);
    setFixedWidth(440);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 16);
    root->setSpacing(18);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    root->addLayout(form);
    const auto addField = [&](const char* caption, const char* name, const QString& value) {
        auto* edit = new QLineEdit(value, this);
        edit->setObjectName(QString::fromLatin1(name));
        edit->setMinimumHeight(30);
        edit->setPlaceholderText(QStringLiteral("--"));
        edit->setMaxLength(256);
        addUiRow(form, caption, edit);
        return edit;
    };
    auto* station = addField("Station ID", "runInfoStation", computerStationId());
    station->setReadOnly(true);
    bindUiText(station, "toolTip", "Computer name (automatic)");
    m_model = addField("Model", "runInfoModel", current.model);
    m_model->setReadOnly(!modelEditable);
    if (!modelEditable) bindUiText(m_model, "toolTip", "Select a saved Station without pending edits to change Model");
    m_customer = addField("Customer ID", "runInfoCustomer", current.customerId);
    m_order = addField("Order", "runInfoOrder", current.order);
    m_tester = addField("Tester", "runInfoTester", current.tester);
    m_jig = addField("Jig No.", "runInfoJig", current.jigNo);
    auto* commands = new QHBoxLayout;
    auto* previous = new QPushButton(QIcon(QStringLiteral(":/icons/folder-open.svg")), uiText("Load Previous"), this);
    previous->setObjectName(QStringLiteral("runInfoLoadPrevious"));
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(PreviousGroup));
    previous->setEnabled(settings.value(QStringLiteral("schemaVersion")).toInt() == 1);
    connect(previous, &QPushButton::clicked, this, &RunInformationDialog::loadPrevious);
    commands->addWidget(previous);
    commands->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    buttons->setObjectName(QStringLiteral("runInfoButtons"));
    buttons->button(QDialogButtonBox::Ok)->setText(uiText("Apply"));
    buttons->button(QDialogButtonBox::Cancel)->setText(uiText("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &RunInformationDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &RunInformationDialog::reject);
    commands->addWidget(buttons);
    root->addLayout(commands);
}

void RunInformationDialog::loadPrevious()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(PreviousGroup));
    if (settings.value(QStringLiteral("schemaVersion")).toInt() != 1) return;
    m_customer->setText(settings.value(QStringLiteral("customerId")).toString());
    m_order->setText(settings.value(QStringLiteral("order")).toString());
    m_tester->setText(settings.value(QStringLiteral("tester")).toString());
    m_jig->setText(settings.value(QStringLiteral("jigNo")).toString());
}

void RunInformationDialog::accept()
{
    const auto model = m_model->isReadOnly() ? m_information.model : m_model->text().trimmed();
    if (model != m_information.model) {
        QString error;
        if (!m_saveModel || !m_saveModel(model, &error)) {
            QMessageBox::warning(this, uiText("Basic Information"),
                                 uiText("Cannot save Model: %1").arg(error));
            return;
        }
    }
    m_information.stationId = computerStationId();
    m_information.model = model;
    m_information.customerId = m_customer->text().trimmed();
    m_information.order = m_order->text().trimmed();
    m_information.tester = m_tester->text().trimmed();
    m_information.jigNo = m_jig->text().trimmed();
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(PreviousGroup));
    settings.setValue(QStringLiteral("schemaVersion"), 1);
    settings.setValue(QStringLiteral("customerId"), m_information.customerId);
    settings.setValue(QStringLiteral("order"), m_information.order);
    settings.setValue(QStringLiteral("tester"), m_information.tester);
    settings.setValue(QStringLiteral("jigNo"), m_information.jigNo);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        QMessageBox::warning(this, uiText("Basic Information"), uiText("Current information was applied, but the previous configuration could not be remembered."));
    QDialog::accept();
}

} // namespace PicoATE::Ui
