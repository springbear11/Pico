#include "StationSettingsEditor.h"

#include "OnOffControl.h"
#include "StationDocument.h"

#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace PicoATE::Ui {

namespace {

QString objectText(const QJsonObject& object)
{
    return QString::fromUtf8(
        QJsonDocument(object).toJson(QJsonDocument::Indented)).trimmed();
}

bool parseObject(const QString& text, QJsonObject& object, QString& error)
{
    const auto trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        object = {};
        return true;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = parseError.errorString();
        return false;
    }
    if (!document.isObject()) {
        error = QObject::tr("Expected a JSON object");
        return false;
    }
    object = document.object();
    return true;
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

    auto* title = new QLabel(tr("Basic Settings"), this);
    auto titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    title->setFont(titleFont);
    layout->addWidget(title);

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

    m_snLengthSpin = new QSpinBox(this);
    m_snLengthSpin->setObjectName(QStringLiteral("stationSnLengthSpin"));
    m_snLengthSpin->setRange(0, 256);
    m_snLengthSpin->setSpecialValueText(tr("Any"));
    m_snLengthSpin->setSuffix(tr(" chars"));
    m_snLengthSpin->setToolTip(tr("0 means no SN length restriction"));
    form->addRow(tr("SN Length"), m_snLengthSpin);
    layout->addLayout(form);

    auto* metadataLabel = new QLabel(tr("Metadata (JSON)"), this);
    layout->addWidget(metadataLabel);
    m_metadataEdit = new QPlainTextEdit(this);
    m_metadataEdit->setObjectName(QStringLiteral("stationBasicMetadataEdit"));
    m_metadataEdit->setMinimumHeight(110);
    layout->addWidget(m_metadataEdit, 1);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("stationBasicErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    m_applyButton = new QPushButton(tr("Apply"), this);
    m_applyButton->setObjectName(QStringLiteral("applyStationSettingsButton"));
    layout->addWidget(m_applyButton);

    connect(m_applyButton, &QPushButton::clicked,
            this, &StationSettingsEditor::apply);
    if (m_document) {
        connect(m_document, &StationDocument::documentChanged,
                this, &StationSettingsEditor::reload);
    }
    reload();
}

void StationSettingsEditor::setEditable(bool editable)
{
    m_editable = editable;
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
    } else if (path == QStringLiteral("snLength")) {
        field = m_snLengthSpin;
    } else if (path.startsWith(QStringLiteral("metadata"))) {
        field = m_metadataEdit;
    }
    if (!field) {
        return false;
    }
    field->setFocus(Qt::OtherFocusReason);
    return true;
}

void StationSettingsEditor::reload()
{
    const auto root = m_document ? m_document->rootObject() : QJsonObject{};
    const bool valid = !root.isEmpty();
    m_stationIdEdit->setText(root.value(QStringLiteral("stationId")).toString(
        root.value(QStringLiteral("id")).toString()));
    m_stationNameEdit->setText(root.value(QStringLiteral("name")).toString());
    m_stopOnFailureSwitch->setChecked(
        root.value(QStringLiteral("stopOnFailure")).toBool(true));
    m_scanDialogSwitch->setChecked(
        root.value(QStringLiteral("scanDialogEnabled")).toBool(true));
    m_snLengthSpin->setValue(
        qBound(0, root.value(QStringLiteral("snLength")).toInt(0), 256));
    m_metadataEdit->setPlainText(
        objectText(root.value(QStringLiteral("metadata")).toObject()));

    for (auto* field : {static_cast<QWidget*>(m_stationIdEdit),
                        static_cast<QWidget*>(m_stationNameEdit),
                        static_cast<QWidget*>(m_stopOnFailureSwitch),
                        static_cast<QWidget*>(m_scanDialogSwitch),
                        static_cast<QWidget*>(m_snLengthSpin),
                        static_cast<QWidget*>(m_metadataEdit)}) {
        field->setEnabled(m_editable && valid);
    }
    m_applyButton->setEnabled(m_editable && valid);
    m_errorLabel->hide();
}

void StationSettingsEditor::apply()
{
    if (!m_document || m_document->isEmpty()) {
        return;
    }
    if (m_stationIdEdit->text().trimmed().isEmpty()) {
        showError(tr("Station ID cannot be empty"));
        return;
    }
    QJsonObject metadata;
    QString parseError;
    if (!parseObject(m_metadataEdit->toPlainText(), metadata, parseError)) {
        showError(tr("Metadata: %1").arg(parseError));
        return;
    }

    auto root = m_document->rootObject();
    root.insert(QStringLiteral("stationId"), m_stationIdEdit->text().trimmed());
    root.remove(QStringLiteral("id"));
    root.insert(QStringLiteral("name"), m_stationNameEdit->text().trimmed());
    root.insert(QStringLiteral("stopOnFailure"), m_stopOnFailureSwitch->isChecked());
    root.insert(QStringLiteral("scanDialogEnabled"), m_scanDialogSwitch->isChecked());
    root.insert(QStringLiteral("snLength"), m_snLengthSpin->value());
    root.insert(QStringLiteral("metadata"), metadata);
    m_document->replaceRootObject(std::move(root));
    m_errorLabel->hide();
}

void StationSettingsEditor::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->show();
}

} // namespace PicoATE::Ui
