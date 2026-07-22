#include "LoginDialog.h"

#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace PicoATE::Ui {

LoginDialog::LoginDialog(QString sequenceRootDirectory, QWidget* parent)
    : QDialog(parent)
    , m_sequenceRootDirectory(std::move(sequenceRootDirectory))
{
    setObjectName(QStringLiteral("loginDialog"));
    setWindowTitle(tr("PicoATE Login"));
    setModal(true);
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("PicoATE"), this);
    auto titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 6);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(12);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->setObjectName(QStringLiteral("loginModeCombo"));
    m_modeCombo->addItem(tr("TEST"), int(UiMode::Test));
    m_modeCombo->addItem(tr("Admin"), int(UiMode::Admin));
    form->addRow(tr("Mode"), m_modeCombo);

    m_sequenceCombo = new QComboBox(this);
    m_sequenceCombo->setObjectName(QStringLiteral("loginSequenceCombo"));
    m_sequenceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sequenceCombo->setMinimumContentsLength(36);
    form->addRow(tr("Sequence"), m_sequenceCombo);

    m_passwordLabel = new QLabel(tr("Admin Password"), this);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setObjectName(QStringLiteral("loginAdminPassword"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    form->addRow(m_passwordLabel, m_passwordEdit);
    layout->addLayout(form);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("loginErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    m_loginButton = new QPushButton(tr("Enter"), this);
    m_loginButton->setObjectName(QStringLiteral("loginButton"));
    m_loginButton->setDefault(true);
    m_loginButton->setMinimumHeight(36);
    layout->addWidget(m_loginButton);

    connect(m_modeCombo, &QComboBox::currentIndexChanged,
            this, &LoginDialog::updateModeUi);
    connect(m_sequenceCombo, &QComboBox::currentIndexChanged,
            this, &LoginDialog::updateStationPath);
    connect(m_loginButton, &QPushButton::clicked,
            this, &LoginDialog::submit);
    connect(m_passwordEdit, &QLineEdit::returnPressed,
            this, &LoginDialog::submit);

    populateSequences();
    updateModeUi();
    updateStationPath();
}

StartupSelection LoginDialog::selection() const
{
    return m_selection;
}

void LoginDialog::setInitialSequencePath(const QString& filePath)
{
    const auto absolutePath = QFileInfo(filePath).absoluteFilePath();
    int index = m_sequenceCombo->findData(absolutePath);
    if (index < 0 && QFileInfo::exists(absolutePath)) {
        m_sequenceCombo->addItem(QFileInfo(absolutePath).fileName(), absolutePath);
        index = m_sequenceCombo->count() - 1;
    }
    if (index >= 0) {
        m_sequenceCombo->setCurrentIndex(index);
    }
}

void LoginDialog::updateModeUi()
{
    const bool admin = UiMode(m_modeCombo->currentData().toInt()) == UiMode::Admin;
    m_passwordLabel->setVisible(admin);
    m_passwordEdit->setVisible(admin);
    if (!admin) {
        m_passwordEdit->clear();
    }
    m_errorLabel->hide();
}

void LoginDialog::updateStationPath()
{
    m_loginButton->setEnabled(m_sequenceCombo->currentIndex() >= 0);
}

void LoginDialog::submit()
{
    const auto mode = UiMode(m_modeCombo->currentData().toInt());
    const auto sequencePath = m_sequenceCombo->currentData().toString();
    const auto stationPath = StartupSupport::stationPathForSequence(sequencePath);
    const auto validation = StartupSupport::validateSelection(
        mode, sequencePath, stationPath, m_passwordEdit->text());
    if (!validation.ok()) {
        showError(validation.errors.join(QStringLiteral("\n")));
        if (mode == UiMode::Admin) {
            m_passwordEdit->selectAll();
            m_passwordEdit->setFocus(Qt::OtherFocusReason);
        }
        return;
    }

    m_selection.mode = mode;
    m_selection.sequencePath = QFileInfo(sequencePath).absoluteFilePath();
    m_selection.stationPath = QFileInfo(stationPath).absoluteFilePath();
    m_selection.scanDialogEnabled = StartupSupport::stationScanDialogEnabled(
        m_selection.stationPath);
    m_selection.snValidationRules =
        StartupSupport::stationSnValidationRules(m_selection.stationPath);
    accept();
}

void LoginDialog::populateSequences()
{
    m_sequenceCombo->clear();
    for (const auto& filePath :
         StartupSupport::discoverSequenceFiles(m_sequenceRootDirectory)) {
        m_sequenceCombo->addItem(QFileInfo(filePath).fileName(), filePath);
    }
    if (m_sequenceCombo->count() == 0) {
        showError(tr("No Sequence JSON was found in %1")
                      .arg(m_sequenceRootDirectory));
    }
}

void LoginDialog::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->show();
}

} // namespace PicoATE::Ui
