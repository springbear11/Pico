#include "StationSettingsEditor.h"

#include "LoadingSpinner.h"
#include "OnOffControl.h"
#include "StationDocument.h"

#include <QFormLayout>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
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
    setMinimumWidth(160);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 12);
    layout->setSpacing(10);

    m_title = new QLabel(tr("Basic Settings"), this);
    auto titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    m_form = new QFormLayout;
    auto* form = m_form;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setVerticalSpacing(10);

    m_stationIdEdit = new QLineEdit(this);
    m_stationIdEdit->setObjectName(QStringLiteral("stationBasicIdEdit"));
    form->addRow(tr("Station ID"), m_stationIdEdit);
    m_stationModelEdit = new QLineEdit(this);
    m_stationModelEdit->setObjectName(QStringLiteral("stationModelEdit"));
    form->addRow(tr("Model"), m_stationModelEdit);
    m_customerIdEdit = new QLineEdit(this);
    m_customerIdEdit->setObjectName(QStringLiteral("stationCustomerIdEdit"));
    form->addRow(tr("Customer ID"), m_customerIdEdit);

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

    m_snLengthEdit = new QLineEdit(this);
    m_snLengthEdit->setObjectName(QStringLiteral("stationSnLengthEdit"));
    m_snLengthEdit->setValidator(new QIntValidator(1, 256, m_snLengthEdit));
    m_snLengthEdit->setMaxLength(3);
    m_snLengthEdit->setPlaceholderText(tr("Any"));
    m_snLengthEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_snLengthEdit->setToolTip(tr("Exact SN length. Leave empty for Any."));
    form->addRow(tr("SN Length"), m_snLengthEdit);

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

    m_uutCountEdit = new QLineEdit(this);
    m_uutCountEdit->setObjectName(QStringLiteral("stationUutCountEdit"));
    m_uutCountEdit->setValidator(new QIntValidator(1, 64, m_uutCountEdit));
    m_uutCountEdit->setMaxLength(2);
    m_uutCountEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_uutCountEdit->setToolTip(
        tr("Number of UUT SNs collected before one batch starts"));
    form->addRow(tr("UUT Count"), m_uutCountEdit);

    m_loopTestCountEdit = new QLineEdit(this);
    m_loopTestCountEdit->setObjectName(QStringLiteral("stationLoopTestCountEdit"));
    m_loopTestCountEdit->setValidator(
        new QIntValidator(1, 100000, m_loopTestCountEdit));
    m_loopTestCountEdit->setMaxLength(6);
    m_loopTestCountEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_loopTestCountEdit->setToolTip(
        tr("Run the complete sequence this many times after one Run command"));
    form->addRow(tr("Loop Count"), m_loopTestCountEdit);
    serviceAdminStartupAnimation();

    m_loopTestSwitch = new OnOffSwitch(this);
    m_loopTestSwitch->setObjectName(QStringLiteral("stationLoopTestSwitch"));
    m_loopTestSwitch->setAccessibleName(tr("Enable repeated sequence testing"));
    form->addRow(tr("Loop Test"), m_loopTestSwitch);

    m_scanDialogSwitch = new OnOffSwitch(this);
    m_scanDialogSwitch->setObjectName(QStringLiteral("stationScanDialogSwitch"));
    m_scanDialogSwitch->setAccessibleName(tr("Enable scan dialog"));
    form->addRow(tr("Scan Dialog"), m_scanDialogSwitch);

    m_stopOnFailureSwitch = new OnOffSwitch(this);
    m_stopOnFailureSwitch->setObjectName(QStringLiteral("stationStopOnFailureSwitch"));
    m_stopOnFailureSwitch->setAccessibleName(tr("Stop on failure by default"));
    m_stopOnFailureSwitch->setToolTip(
        tr("Default for inherited onFail, onError, and onTimeout policies. Explicit Step policies are preserved."));
    form->addRow(tr("Default Stop on Failure"), m_stopOnFailureSwitch);

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

    m_pdfReportSwitch = new OnOffSwitch(this);
    m_pdfReportSwitch->setObjectName(QStringLiteral("stationPdfReportSwitch"));
    m_pdfReportSwitch->setAccessibleName(tr("Enable PDF result report"));
    form->addRow(tr("PDF Report"), m_pdfReportSwitch);

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
    layout->addLayout(form);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("stationBasicErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);
    layout->addStretch(1);
    serviceAdminStartupAnimation();

    const auto markPending = [this] { markPendingChanges(); };
    connect(m_stationIdEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_stationModelEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_customerIdEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_stopOnFailureSwitch, &QAbstractButton::toggled, this, markPending);
    connect(m_scanDialogSwitch, &QAbstractButton::toggled, this, markPending);
    connect(m_loopTestSwitch, &QAbstractButton::toggled, this, [this] {
        m_loopTestCountEdit->setEnabled(
            m_editable && m_loopTestSwitch->isChecked());
        markPendingChanges();
    });
    connect(m_loopTestCountEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_txtLogSwitch, &QAbstractButton::toggled, this, markPending);
    connect(m_csvReportSwitch, &QAbstractButton::toggled, this, markPending);
    connect(m_xlsxReportSwitch, &QAbstractButton::toggled, this, markPending);
    connect(m_pdfReportSwitch, &QAbstractButton::toggled, this, markPending);
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
    connect(m_snLengthEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_snPatternEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_snAllowedRegexEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_uutCountEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_jigNoEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_orderEdit, &QLineEdit::textEdited, this, markPending);
    connect(m_testerEdit, &QLineEdit::textEdited, this, markPending);
    if (m_document) {
        connect(m_document, &StationDocument::documentChanged,
                this, &StationSettingsEditor::reload);
    }
    reload();
}

void StationSettingsEditor::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    applyResponsiveFormLayout();
}

void StationSettingsEditor::applyResponsiveFormLayout()
{
    if (!m_form) {
        return;
    }

    const bool compact = width() < 245;
    if (m_compactFormLayout == compact) {
        return;
    }

    m_compactFormLayout = compact;
    m_form->setRowWrapPolicy(compact ? QFormLayout::WrapAllRows
                                     : QFormLayout::WrapLongRows);
    m_form->invalidate();
    updateGeometry();
}

void StationSettingsEditor::setEditable(bool editable)
{
    m_editable = editable;
    const bool valid = m_document && !m_document->isEmpty();
    for (auto* field : {static_cast<QWidget*>(m_stationIdEdit),
                        static_cast<QWidget*>(m_stationModelEdit),
                        static_cast<QWidget*>(m_customerIdEdit),
                        static_cast<QWidget*>(m_stopOnFailureSwitch),
                        static_cast<QWidget*>(m_scanDialogSwitch),
                        static_cast<QWidget*>(m_loopTestSwitch),
                        static_cast<QWidget*>(m_loopTestCountEdit),
                        static_cast<QWidget*>(m_uutCountEdit),
                        static_cast<QWidget*>(m_txtLogSwitch),
                        static_cast<QWidget*>(m_csvReportSwitch),
                        static_cast<QWidget*>(m_xlsxReportSwitch),
                        static_cast<QWidget*>(m_reportOutputEdit),
                        static_cast<QWidget*>(m_browseReportOutputButton),
                        static_cast<QWidget*>(m_snLengthEdit),
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
    const auto snLengthText = m_snLengthEdit->text().trimmed();
    if (!snLengthText.isEmpty() && !m_snLengthEdit->hasAcceptableInput()) {
        showError(tr("SN Length must be an integer from 1 to 256, or empty for Any"));
        m_snLengthEdit->setFocus();
        m_snLengthEdit->selectAll();
        return false;
    }
    const auto loopCountText = m_loopTestCountEdit->text().trimmed();
    if (m_loopTestSwitch->isChecked() &&
        !m_loopTestCountEdit->hasAcceptableInput()) {
        showError(tr("Loop Count must be an integer from 1 to 100000"));
        m_loopTestCountEdit->setFocus();
        m_loopTestCountEdit->selectAll();
        return false;
    }
    if (!m_uutCountEdit->hasAcceptableInput()) {
        showError(tr("UUT Count must be an integer from 1 to 64"));
        m_uutCountEdit->setFocus();
        m_uutCountEdit->selectAll();
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
    root.insert(QStringLiteral("model"), m_stationModelEdit->text().trimmed());
    root.insert(QStringLiteral("customerId"), m_customerIdEdit->text().trimmed());
    root.remove(QStringLiteral("name"));
    root.insert(QStringLiteral("stopOnFailure"), m_stopOnFailureSwitch->isChecked());
    root.insert(QStringLiteral("scanDialogEnabled"), m_scanDialogSwitch->isChecked());
    root.insert(QStringLiteral("loopTestEnabled"), m_loopTestSwitch->isChecked());
    root.insert(QStringLiteral("loopTestCount"),
                m_loopTestCountEdit->hasAcceptableInput()
                    ? loopCountText.toInt()
                    : 1);
    root.insert(QStringLiteral("uutCount"), m_uutCountEdit->text().toInt());
    root.insert(QStringLiteral("pluginRegistry"),
                QStringLiteral("plugins/PluginRegistry.json"));
    root.insert(QStringLiteral("txtLogEnabled"), m_txtLogSwitch->isChecked());
    root.insert(QStringLiteral("csvReportEnabled"), m_csvReportSwitch->isChecked());
    root.insert(QStringLiteral("xlsxReportEnabled"), m_xlsxReportSwitch->isChecked());
    root.insert(QStringLiteral("pdfReportEnabled"), m_pdfReportSwitch->isChecked());
    const auto outputDirectory = m_reportOutputEdit->text().trimmed();
    if (outputDirectory.isEmpty()) {
        root.remove(QStringLiteral("reportOutputDirectory"));
    } else {
        root.insert(QStringLiteral("reportOutputDirectory"), outputDirectory);
    }
    root.insert(QStringLiteral("snLength"),
                snLengthText.isEmpty() ? 0 : snLengthText.toInt());
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
    } else if (path == QStringLiteral("model") ||
               path == QStringLiteral("name")) {
        field = m_stationModelEdit;
    } else if (path == QStringLiteral("customerId")) {
        field = m_customerIdEdit;
    } else if (path == QStringLiteral("stopOnFailure")) {
        field = m_stopOnFailureSwitch;
    } else if (path == QStringLiteral("scanDialogEnabled")) {
        field = m_scanDialogSwitch;
    } else if (path == QStringLiteral("loopTestEnabled")) {
        field = m_loopTestSwitch;
    } else if (path == QStringLiteral("loopTestCount")) {
        field = m_loopTestCountEdit;
    } else if (path == QStringLiteral("uutCount")) {
        field = m_uutCountEdit;
    } else if (path == QStringLiteral("txtLogEnabled")) {
        field = m_txtLogSwitch;
    } else if (path == QStringLiteral("csvReportEnabled")) {
        field = m_csvReportSwitch;
    } else if (path == QStringLiteral("xlsxReportEnabled")) {
        field = m_xlsxReportSwitch;
    } else if (path == QStringLiteral("pdfReportEnabled")) {
        field = m_pdfReportSwitch;
    } else if (path == QStringLiteral("reportOutputDirectory")) {
        field = m_reportOutputEdit;
    } else if (path == QStringLiteral("snLength")) {
        field = m_snLengthEdit;
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
    m_stationModelEdit->setText(root.value(QStringLiteral("model")).toString(
        root.value(QStringLiteral("name")).toString()));
    m_customerIdEdit->setText(
        root.value(QStringLiteral("customerId")).toString());
    m_stopOnFailureSwitch->setChecked(
        root.value(QStringLiteral("stopOnFailure")).toBool(true));
    m_scanDialogSwitch->setChecked(
        root.value(QStringLiteral("scanDialogEnabled")).toBool(true));
    m_loopTestSwitch->setChecked(
        root.value(QStringLiteral("loopTestEnabled")).toBool(false));
    m_loopTestCountEdit->setText(QString::number(qBound(
        1, root.value(QStringLiteral("loopTestCount")).toInt(1), 100000)));
    m_uutCountEdit->setText(QString::number(qBound(
        1, root.value(QStringLiteral("uutCount")).toInt(1), 64)));
    m_txtLogSwitch->setChecked(
        root.value(QStringLiteral("txtLogEnabled")).toBool(false));
    m_csvReportSwitch->setChecked(
        root.value(QStringLiteral("csvReportEnabled")).toBool(false));
    m_xlsxReportSwitch->setChecked(
        root.value(QStringLiteral("xlsxReportEnabled")).toBool(false));
    m_pdfReportSwitch->setChecked(
        root.value(QStringLiteral("pdfReportEnabled")).toBool(false));
    m_reportOutputEdit->setText(
        root.value(QStringLiteral("reportOutputDirectory")).toString());
    const int snLength = qBound(
        0, root.value(QStringLiteral("snLength")).toInt(0), 256);
    m_snLengthEdit->setText(snLength > 0 ? QString::number(snLength)
                                        : QString{});
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
                        static_cast<QWidget*>(m_stationModelEdit),
                        static_cast<QWidget*>(m_customerIdEdit),
                        static_cast<QWidget*>(m_stopOnFailureSwitch),
                        static_cast<QWidget*>(m_scanDialogSwitch),
                        static_cast<QWidget*>(m_loopTestSwitch),
                        static_cast<QWidget*>(m_loopTestCountEdit),
                        static_cast<QWidget*>(m_uutCountEdit),
                        static_cast<QWidget*>(m_txtLogSwitch),
                        static_cast<QWidget*>(m_csvReportSwitch),
                        static_cast<QWidget*>(m_xlsxReportSwitch),
                        static_cast<QWidget*>(m_pdfReportSwitch),
                        static_cast<QWidget*>(m_reportOutputEdit),
                        static_cast<QWidget*>(m_browseReportOutputButton),
                        static_cast<QWidget*>(m_snLengthEdit),
                        static_cast<QWidget*>(m_snPatternEdit),
                        static_cast<QWidget*>(m_snAllowedRegexEdit),
                        static_cast<QWidget*>(m_jigNoEdit),
                        static_cast<QWidget*>(m_orderEdit),
                        static_cast<QWidget*>(m_testerEdit)}) {
        field->setEnabled(m_editable && valid);
    }
    m_loopTestCountEdit->setEnabled(
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
