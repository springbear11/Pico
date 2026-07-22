#include "StationSettingsEditor.h"

#include "OnOffControl.h"
#include "StationDocument.h"

#include <QFormLayout>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <initializer_list>

namespace PicoATE::Ui {

namespace {

QString metadataValue(const QJsonObject& metadata,
                      std::initializer_list<const char*> keys)
{
    for (const auto* key : keys) {
        const auto value = metadata.value(QString::fromLatin1(key))
                               .toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

void setMetadataValue(QJsonObject& metadata,
                      const QString& key,
                      const QString& value,
                      std::initializer_list<const char*> aliases)
{
    for (const auto* alias : aliases) {
        metadata.remove(QString::fromLatin1(alias));
    }
    const auto trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        metadata.remove(key);
    } else {
        metadata.insert(key, trimmed);
    }
}

} // namespace

StationSettingsEditor::StationSettingsEditor(StationDocument* document,
                                             QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    setObjectName(QStringLiteral("stationSettingsEditor"));
    setMinimumWidth(210);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 12);
    layout->setSpacing(10);

    m_title = new QLabel(tr("Basic Settings"), this);
    auto titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setVerticalSpacing(10);

    m_stationIdEdit = new QLineEdit(this);
    m_stationIdEdit->setObjectName(QStringLiteral("stationBasicIdEdit"));
    form->addRow(tr("Station ID"), m_stationIdEdit);
    m_stationNameEdit = new QLineEdit(this);
    m_stationNameEdit->setObjectName(QStringLiteral("stationBasicNameEdit"));
    form->addRow(tr("Name"), m_stationNameEdit);

    m_stopOnFailureSwitch = new OnOffSwitch(this);
    m_stopOnFailureSwitch->setObjectName(QStringLiteral("stationStopOnFailureSwitch"));
    m_stopOnFailureSwitch->setAccessibleName(tr("Stop on failure"));
    form->addRow(tr("Stop on Failure"), m_stopOnFailureSwitch);

    m_scanDialogSwitch = new OnOffSwitch(this);
    m_scanDialogSwitch->setObjectName(QStringLiteral("stationScanDialogSwitch"));
    m_scanDialogSwitch->setAccessibleName(tr("Enable scan dialog"));
    form->addRow(tr("Scan Dialog"), m_scanDialogSwitch);

    m_loopTestSwitch = new OnOffSwitch(this);
    m_loopTestSwitch->setObjectName(QStringLiteral("stationLoopTestSwitch"));
    m_loopTestSwitch->setAccessibleName(tr("Enable repeated sequence testing"));
    form->addRow(tr("Loop Test"), m_loopTestSwitch);

    m_loopTestCountSpin = new QSpinBox(this);
    m_loopTestCountSpin->setObjectName(QStringLiteral("stationLoopTestCountSpin"));
    m_loopTestCountSpin->setRange(1, 100000);
    m_loopTestCountSpin->setSuffix(tr(" runs"));
    m_loopTestCountSpin->setToolTip(
        tr("Run the complete sequence this many times after one Run command"));
    form->addRow(tr("Loop Count"), m_loopTestCountSpin);

    m_snLengthSpin = new QSpinBox(this);
    m_snLengthSpin->setObjectName(QStringLiteral("stationSnLengthSpin"));
    m_snLengthSpin->setRange(0, 256);
    m_snLengthSpin->setSpecialValueText(tr("Any"));
    m_snLengthSpin->setSuffix(tr(" chars"));
    m_snLengthSpin->setToolTip(tr("0 means no SN length restriction"));
    form->addRow(tr("SN Length"), m_snLengthSpin);

    m_snPatternEdit = new QLineEdit(this);
    m_snPatternEdit->setObjectName(QStringLiteral("stationSnPatternEdit"));
    m_snPatternEdit->setPlaceholderText(tr("BTSN*, *BTSN*, or *BTSN"));
    m_snPatternEdit->setToolTip(
        tr("Optional wildcard rule. * matches any number of characters."));
    form->addRow(tr("SN Pattern"), m_snPatternEdit);

    m_snAllowedRegexEdit = new QLineEdit(this);
    m_snAllowedRegexEdit->setObjectName(
        QStringLiteral("stationSnAllowedRegexEdit"));
    m_snAllowedRegexEdit->setPlaceholderText(QStringLiteral("^[A-Z0-9]+$"));
    m_snAllowedRegexEdit->setToolTip(
        tr("Optional regular expression applied to the complete SN."));
    form->addRow(tr("Allowed Characters"), m_snAllowedRegexEdit);

    m_txtLogSwitch = new OnOffSwitch(this);
    m_txtLogSwitch->setObjectName(QStringLiteral("stationTxtLogSwitch"));
    m_txtLogSwitch->setAccessibleName(tr("Enable TXT execution log"));
    form->addRow(tr("TXT Log"), m_txtLogSwitch);

    m_csvReportSwitch = new OnOffSwitch(this);
    m_csvReportSwitch->setObjectName(QStringLiteral("stationCsvReportSwitch"));
    m_csvReportSwitch->setAccessibleName(tr("Enable CSV result report"));
    form->addRow(tr("CSV Report"), m_csvReportSwitch);

    m_xlsxReportSwitch = new OnOffSwitch(this);
    m_xlsxReportSwitch->setObjectName(QStringLiteral("stationXlsxReportSwitch"));
    m_xlsxReportSwitch->setAccessibleName(tr("Enable XLSX result report"));
    form->addRow(tr("XLSX Report"), m_xlsxReportSwitch);

    auto* outputRow = new QWidget(this);
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(6);
    m_reportOutputEdit = new QLineEdit(outputRow);
    m_reportOutputEdit->setObjectName(QStringLiteral("stationReportOutputEdit"));
    m_reportOutputEdit->setPlaceholderText(tr("<application>/log"));
    m_browseReportOutputButton = new QPushButton(tr("Browse"), outputRow);
    m_browseReportOutputButton->setObjectName(
        QStringLiteral("stationReportOutputBrowseButton"));
    outputLayout->addWidget(m_reportOutputEdit, 1);
    outputLayout->addWidget(m_browseReportOutputButton);
    form->addRow(tr("Output Folder"), outputRow);

    m_jigNoEdit = new QLineEdit(this);
    m_jigNoEdit->setObjectName(QStringLiteral("stationJigNoEdit"));
    m_jigNoEdit->setPlaceholderText(tr("Fixture or jig identifier"));
    form->addRow(tr("Jig No"), m_jigNoEdit);
    m_orderEdit = new QLineEdit(this);
    m_orderEdit->setObjectName(QStringLiteral("stationOrderEdit"));
    m_orderEdit->setPlaceholderText(tr("Production or work order"));
    form->addRow(tr("Order"), m_orderEdit);
    m_testerEdit = new QLineEdit(this);
    m_testerEdit->setObjectName(QStringLiteral("stationTesterEdit"));
    m_testerEdit->setPlaceholderText(tr("Tester or operator name"));
    form->addRow(tr("Tester"), m_testerEdit);
    layout->addLayout(form);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("stationBasicErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);
    layout->addStretch(1);

    const auto markPending = [this] { markPendingChanges(); };
    connect(m_stationIdEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_stationNameEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_stopOnFailureSwitch, &QAbstractButton::toggled, this, markPending);
    connect(m_scanDialogSwitch, &QAbstractButton::toggled, this, markPending);
    connect(m_loopTestSwitch, &QAbstractButton::toggled, this, [this] {
        m_loopTestCountSpin->setEnabled(
            m_editable && m_loopTestSwitch->isChecked());
        markPendingChanges();
    });
    connect(m_loopTestCountSpin, &QSpinBox::valueChanged, this, markPending);
    connect(m_txtLogSwitch, &QAbstractButton::toggled, this, markPending);
    connect(m_csvReportSwitch, &QAbstractButton::toggled, this, markPending);
    connect(m_xlsxReportSwitch, &QAbstractButton::toggled, this, markPending);
    connect(m_reportOutputEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_browseReportOutputButton, &QPushButton::clicked, this, [this] {
        const auto selected = QFileDialog::getExistingDirectory(
            this,
            tr("Select Report Output Folder"),
            m_reportOutputEdit->text().trimmed());
        if (!selected.isEmpty()) {
            m_reportOutputEdit->setText(selected);
            markPendingChanges();
        }
    });
    connect(m_snLengthSpin, &QSpinBox::valueChanged, this, markPending);
    connect(m_snPatternEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_snAllowedRegexEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_jigNoEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_orderEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_testerEdit, &QLineEdit::textEdited, this, markPending);
    if (m_document) {
        connect(m_document, &StationDocument::documentChanged,
                this, &StationSettingsEditor::reload);
    }
    reload();
}

void StationSettingsEditor::setEditable(bool editable)
{
    m_editable = editable;
    const bool valid = m_document && !m_document->isEmpty();
    for (auto* field : {static_cast<QWidget*>(m_stationIdEdit),
                        static_cast<QWidget*>(m_stationNameEdit),
                        static_cast<QWidget*>(m_stopOnFailureSwitch),
                        static_cast<QWidget*>(m_scanDialogSwitch),
                        static_cast<QWidget*>(m_loopTestSwitch),
                        static_cast<QWidget*>(m_loopTestCountSpin),
                        static_cast<QWidget*>(m_txtLogSwitch),
                        static_cast<QWidget*>(m_csvReportSwitch),
                        static_cast<QWidget*>(m_xlsxReportSwitch),
                        static_cast<QWidget*>(m_reportOutputEdit),
                        static_cast<QWidget*>(m_browseReportOutputButton),
                        static_cast<QWidget*>(m_snLengthSpin),
                        static_cast<QWidget*>(m_snPatternEdit),
                        static_cast<QWidget*>(m_snAllowedRegexEdit),
                        static_cast<QWidget*>(m_jigNoEdit),
                        static_cast<QWidget*>(m_orderEdit),
                        static_cast<QWidget*>(m_testerEdit)}) {
        field->setEnabled(m_editable && valid);
    }
    if (!m_pendingChanges) {
        reload();
    }
}

bool StationSettingsEditor::hasPendingChanges() const
{
    return m_pendingChanges;
}

bool StationSettingsEditor::commitPendingChanges()
{
    if (!m_pendingChanges) {
        return true;
    }
    if (!m_document || m_document->isEmpty()) {
        return false;
    }
    if (m_stationIdEdit->text().trimmed().isEmpty()) {
        showError(tr("Station ID cannot be empty"));
        return false;
    }
    auto root = m_document->rootObject();
    auto metadata = root.value(QStringLiteral("metadata")).toObject();
    setMetadataValue(metadata, QStringLiteral("jigNo"), m_jigNoEdit->text(),
                     {"fixtureId", "fixture"});
    setMetadataValue(metadata, QStringLiteral("order"), m_orderEdit->text(),
                     {"orderNumber"});
    setMetadataValue(metadata, QStringLiteral("tester"), m_testerEdit->text(),
                     {"operator"});
    root.insert(QStringLiteral("stationId"), m_stationIdEdit->text().trimmed());
    root.remove(QStringLiteral("id"));
    root.insert(QStringLiteral("name"), m_stationNameEdit->text().trimmed());
    root.insert(QStringLiteral("stopOnFailure"), m_stopOnFailureSwitch->isChecked());
    root.insert(QStringLiteral("scanDialogEnabled"), m_scanDialogSwitch->isChecked());
    root.insert(QStringLiteral("loopTestEnabled"), m_loopTestSwitch->isChecked());
    root.insert(QStringLiteral("loopTestCount"), m_loopTestCountSpin->value());
    root.insert(QStringLiteral("pluginRegistry"),
                QStringLiteral("plugins/PluginRegistry.json"));
    root.insert(QStringLiteral("txtLogEnabled"), m_txtLogSwitch->isChecked());
    root.insert(QStringLiteral("csvReportEnabled"), m_csvReportSwitch->isChecked());
    root.insert(QStringLiteral("xlsxReportEnabled"), m_xlsxReportSwitch->isChecked());
    const auto outputDirectory = m_reportOutputEdit->text().trimmed();
    if (outputDirectory.isEmpty()) {
        root.remove(QStringLiteral("reportOutputDirectory"));
    } else {
        root.insert(QStringLiteral("reportOutputDirectory"), outputDirectory);
    }
    root.insert(QStringLiteral("snLength"), m_snLengthSpin->value());
    const auto snPattern = m_snPatternEdit->text().trimmed();
    if (snPattern.isEmpty()) {
        root.remove(QStringLiteral("snPattern"));
    } else {
        root.insert(QStringLiteral("snPattern"), snPattern);
    }
    const auto snAllowedRegex = m_snAllowedRegexEdit->text().trimmed();
    if (snAllowedRegex.isEmpty()) {
        root.remove(QStringLiteral("snAllowedRegex"));
    } else {
        root.insert(QStringLiteral("snAllowedRegex"), snAllowedRegex);
    }
    root.insert(QStringLiteral("metadata"), metadata);
    m_document->replaceRootObject(std::move(root));
    m_errorLabel->hide();
    setPendingChanges(false);
    reload();
    return true;
}

void StationSettingsEditor::discardPendingChanges()
{
    setPendingChanges(false);
    reload();
}

bool StationSettingsEditor::focusField(const QString& path)
{
    QWidget* field = nullptr;
    if (path == QStringLiteral("stationId") || path == QStringLiteral("id")) {
        field = m_stationIdEdit;
    } else if (path == QStringLiteral("name")) {
        field = m_stationNameEdit;
    } else if (path == QStringLiteral("stopOnFailure")) {
        field = m_stopOnFailureSwitch;
    } else if (path == QStringLiteral("scanDialogEnabled")) {
        field = m_scanDialogSwitch;
    } else if (path == QStringLiteral("loopTestEnabled")) {
        field = m_loopTestSwitch;
    } else if (path == QStringLiteral("loopTestCount")) {
        field = m_loopTestCountSpin;
    } else if (path == QStringLiteral("txtLogEnabled")) {
        field = m_txtLogSwitch;
    } else if (path == QStringLiteral("csvReportEnabled")) {
        field = m_csvReportSwitch;
    } else if (path == QStringLiteral("xlsxReportEnabled")) {
        field = m_xlsxReportSwitch;
    } else if (path == QStringLiteral("reportOutputDirectory")) {
        field = m_reportOutputEdit;
    } else if (path == QStringLiteral("snLength")) {
        field = m_snLengthSpin;
    } else if (path == QStringLiteral("snPattern")) {
        field = m_snPatternEdit;
    } else if (path == QStringLiteral("snAllowedRegex")) {
        field = m_snAllowedRegexEdit;
    } else if (path.startsWith(QStringLiteral("metadata"))) {
        if (path.contains(QStringLiteral("jigNo")) ||
            path.contains(QStringLiteral("fixture"))) {
            field = m_jigNoEdit;
        } else if (path.contains(QStringLiteral("order"))) {
            field = m_orderEdit;
        } else if (path.contains(QStringLiteral("tester")) ||
                   path.contains(QStringLiteral("operator"))) {
            field = m_testerEdit;
        } else {
            field = m_jigNoEdit;
        }
    }
    if (!field) {
        return false;
    }
    field->setFocus(Qt::OtherFocusReason);
    return true;
}

void StationSettingsEditor::reload()
{
    if (m_pendingChanges) {
        return;
    }
    m_loading = true;
    const auto root = m_document ? m_document->rootObject() : QJsonObject{};
    const bool valid = !root.isEmpty();
    m_stationIdEdit->setText(root.value(QStringLiteral("stationId")).toString(
        root.value(QStringLiteral("id")).toString()));
    m_stationNameEdit->setText(root.value(QStringLiteral("name")).toString());
    m_stopOnFailureSwitch->setChecked(
        root.value(QStringLiteral("stopOnFailure")).toBool(true));
    m_scanDialogSwitch->setChecked(
        root.value(QStringLiteral("scanDialogEnabled")).toBool(true));
    m_loopTestSwitch->setChecked(
        root.value(QStringLiteral("loopTestEnabled")).toBool(false));
    m_loopTestCountSpin->setValue(qBound(
        1, root.value(QStringLiteral("loopTestCount")).toInt(1), 100000));
    m_txtLogSwitch->setChecked(
        root.value(QStringLiteral("txtLogEnabled")).toBool(false));
    m_csvReportSwitch->setChecked(
        root.value(QStringLiteral("csvReportEnabled")).toBool(false));
    m_xlsxReportSwitch->setChecked(
        root.value(QStringLiteral("xlsxReportEnabled")).toBool(false));
    m_reportOutputEdit->setText(
        root.value(QStringLiteral("reportOutputDirectory")).toString());
    m_snLengthSpin->setValue(
        qBound(0, root.value(QStringLiteral("snLength")).toInt(0), 256));
    m_snPatternEdit->setText(
        root.value(QStringLiteral("snPattern")).toString());
    m_snAllowedRegexEdit->setText(
        root.value(QStringLiteral("snAllowedRegex")).toString());
    const auto metadata = root.value(QStringLiteral("metadata")).toObject();
    m_jigNoEdit->setText(metadataValue(
        metadata, {"jigNo", "fixtureId", "fixture"}));
    m_orderEdit->setText(metadataValue(
        metadata, {"order", "orderNumber"}));
    m_testerEdit->setText(metadataValue(
        metadata, {"tester", "operator"}));

    for (auto* field : {static_cast<QWidget*>(m_stationIdEdit),
                        static_cast<QWidget*>(m_stationNameEdit),
                        static_cast<QWidget*>(m_stopOnFailureSwitch),
                        static_cast<QWidget*>(m_scanDialogSwitch),
                        static_cast<QWidget*>(m_loopTestSwitch),
                        static_cast<QWidget*>(m_loopTestCountSpin),
                        static_cast<QWidget*>(m_txtLogSwitch),
                        static_cast<QWidget*>(m_csvReportSwitch),
                        static_cast<QWidget*>(m_xlsxReportSwitch),
                        static_cast<QWidget*>(m_reportOutputEdit),
                        static_cast<QWidget*>(m_browseReportOutputButton),
                        static_cast<QWidget*>(m_snLengthSpin),
                        static_cast<QWidget*>(m_snPatternEdit),
                        static_cast<QWidget*>(m_snAllowedRegexEdit),
                        static_cast<QWidget*>(m_jigNoEdit),
                        static_cast<QWidget*>(m_orderEdit),
                        static_cast<QWidget*>(m_testerEdit)}) {
        field->setEnabled(m_editable && valid);
    }
    m_loopTestCountSpin->setEnabled(
        m_editable && valid && m_loopTestSwitch->isChecked());
    m_errorLabel->hide();
    m_loading = false;
}

void StationSettingsEditor::markPendingChanges()
{
    if (!m_loading && m_editable) {
        setPendingChanges(true);
    }
}

void StationSettingsEditor::setPendingChanges(bool pending)
{
    if (m_pendingChanges == pending) {
        return;
    }
    m_pendingChanges = pending;
    if (m_title) {
        m_title->setText(m_pendingChanges
                             ? tr("Basic Settings *")
                             : tr("Basic Settings"));
    }
    emit pendingChangesChanged(m_pendingChanges);
}

void StationSettingsEditor::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->show();
}

} // namespace PicoATE::Ui
