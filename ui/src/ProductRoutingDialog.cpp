#include "ProductRoutingDialog.h"

#include "FieldDeviceDialog.h"
#include "OnOffControl.h"
#include "StartupSupport.h"

#include "PicoATE/Core/SequenceCompiler.h"
#include "PicoATE/Core/StationConfig.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace PicoATE::Ui {

namespace {

constexpr int LegacySequenceRole = Qt::UserRole + 1;

QIcon routingIcon(const char* name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg")
                     .arg(QString::fromLatin1(name)));
}

QString diagnosticText(const PicoATE::Core::ProductRoutingDiagnostic& diagnostic)
{
    auto text = diagnostic.path.isEmpty()
        ? diagnostic.message
        : QStringLiteral("%1: %2").arg(diagnostic.path, diagnostic.message);
    if (!diagnostic.suggestion.isEmpty()) {
        text += QStringLiteral(" (%1)").arg(diagnostic.suggestion);
    }
    return text;
}

} // namespace

ProductRoutingDialog::ProductRoutingDialog(QString routingPath, QWidget* parent)
    : QDialog(parent)
    , m_routingPath(QFileInfo(std::move(routingPath)).absoluteFilePath())
{
    setObjectName(QStringLiteral("productRoutingDialog"));
    setWindowTitle(tr("Product Routing"));
    setMinimumSize(1040, 520);
    resize(1120, 600);
    buildUi();
    load();
}

void ProductRoutingDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    auto* title = new QLabel(tr("Product routing"), this);
    title->setObjectName(QStringLiteral("productRoutingTitle"));
    root->addWidget(title);

    auto* subtitle = new QLabel(
        tr("Match a scanned SN to one product project containing its Sequence and Station."),
        this);
    subtitle->setObjectName(QStringLiteral("productRoutingSubtitle"));
    root->addWidget(subtitle);

    auto* policyRow = new QHBoxLayout;
    auto* policyLabel = new QLabel(tr("Allow manual project selection in Test"), this);
    policyLabel->setObjectName(QStringLiteral("productRoutingPolicyLabel"));
    m_allowManualSwitch = new OnOffSwitch(this);
    m_allowManualSwitch->setObjectName(
        QStringLiteral("productRoutingAllowManualSwitch"));
    m_allowManualSwitch->setAccessibleName(
        tr("Allow manual project selection in Test mode"));
    policyRow->addWidget(policyLabel);
    policyRow->addWidget(m_allowManualSwitch);
    policyRow->addStretch(1);
    root->addLayout(policyRow);

    auto* routeTools = new QHBoxLayout;
    auto* routesLabel = new QLabel(tr("Routes"), this);
    routesLabel->setObjectName(QStringLiteral("productRoutingRoutesLabel"));
    routeTools->addWidget(routesLabel);
    routeTools->addStretch(1);

    auto* addButton = new QToolButton(this);
    addButton->setObjectName(QStringLiteral("productRoutingAddButton"));
    addButton->setIcon(routingIcon("list-plus"));
    addButton->setToolTip(tr("Add route"));
    addButton->setAccessibleName(tr("Add route"));
    m_duplicateButton = new QToolButton(this);
    m_duplicateButton->setObjectName(
        QStringLiteral("productRoutingDuplicateButton"));
    m_duplicateButton->setIcon(routingIcon("copy-plus"));
    m_duplicateButton->setToolTip(tr("Duplicate selected route"));
    m_duplicateButton->setAccessibleName(tr("Duplicate selected route"));
    m_removeButton = new QToolButton(this);
    m_removeButton->setObjectName(QStringLiteral("productRoutingRemoveButton"));
    m_removeButton->setIcon(routingIcon("trash-2"));
    m_removeButton->setToolTip(tr("Delete selected route"));
    m_removeButton->setAccessibleName(tr("Delete selected route"));
    for (auto* button : {addButton, m_duplicateButton, m_removeButton}) {
        button->setAutoRaise(true);
        button->setIconSize(QSize(19, 19));
        button->setFixedSize(34, 32);
        routeTools->addWidget(button);
    }
    root->addLayout(routeTools);

    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setObjectName(QStringLiteral("productRoutingTable"));
    m_table->setHorizontalHeaderLabels({
        tr("Enabled"), tr("Product / Route"), tr("SN Pattern"), tr("Project"),
        tr("Device Status"), tr("Devices"),
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked |
                             QAbstractItemView::EditKeyPressed |
                             QAbstractItemView::SelectedClicked);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(42);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(
        EnabledColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(
        NameColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(
        PatternColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(
        ProjectColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(
        DeviceStatusColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(
        DevicesColumn, QHeaderView::ResizeToContents);
    m_table->setItemDelegateForColumn(
        EnabledColumn, new OnOffItemDelegate(m_table));
    root->addWidget(m_table, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("productRoutingStatus"));
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_saveButton = buttons->button(QDialogButtonBox::Save);
    m_saveButton->setObjectName(QStringLiteral("productRoutingSaveButton"));
    root->addWidget(buttons);

    connect(addButton, &QToolButton::clicked,
            this, &ProductRoutingDialog::addRoute);
    connect(m_duplicateButton, &QToolButton::clicked,
            this, &ProductRoutingDialog::duplicateSelectedRoute);
    connect(m_removeButton, &QToolButton::clicked,
            this, &ProductRoutingDialog::removeSelectedRoute);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &ProductRoutingDialog::updateButtons);
    connect(m_saveButton, &QPushButton::clicked,
            this, &ProductRoutingDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setStyleSheet(QStringLiteral(R"css(
        QDialog#productRoutingDialog { background: #f4f6f7; color: #20262b; }
        QLabel#productRoutingTitle { font-size: 18px; font-weight: 700; }
        QLabel#productRoutingSubtitle { color: #68737b; }
        QLabel#productRoutingPolicyLabel,
        QLabel#productRoutingRoutesLabel { font-weight: 650; }
        QTableWidget#productRoutingTable {
            background: #ffffff;
            alternate-background-color: #f7f8f9;
            border: 1px solid #d7dde1;
            border-radius: 5px;
            gridline-color: #e2e6e9;
            selection-background-color: #dcecf6;
            selection-color: #20262b;
        }
        QTableWidget#productRoutingTable QHeaderView::section {
            background: #eef1f3;
            border: 0;
            border-right: 1px solid #d7dde1;
            border-bottom: 1px solid #d7dde1;
            padding: 7px 9px;
            font-weight: 650;
        }
        QLabel#productRoutingStatus { color: #68737b; min-height: 20px; }
        QToolButton { border-radius: 4px; padding: 5px; }
        QToolButton:hover { background: #dcecf6; }
    )css"));
}

void ProductRoutingDialog::load()
{
    if (!QFileInfo::exists(m_routingPath)) {
        PicoATE::Core::ProductRoutingConfig config;
        config.sourcePath = m_routingPath;
        config.projectRootPath = StartupSupport::productProjectRootPathForRoot(
            QFileInfo(m_routingPath).absolutePath());
        populate(config);
        m_statusLabel->setText(
            tr("ProductRouting.json does not exist yet. Save to create it."));
        return;
    }

    const auto loaded = PicoATE::Core::loadProductRoutingFile(m_routingPath);
    populate(loaded.config);
    if (loaded.ok()) {
        m_statusLabel->setText(
            tr("Loaded %1 route(s).").arg(loaded.config.routes.size()));
        return;
    }

    QStringList errors;
    for (const auto& error : loaded.errors) {
        errors.push_back(diagnosticText(error));
    }
    m_statusLabel->setText(errors.join(QStringLiteral("\n")));
    m_statusLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
}

void ProductRoutingDialog::populate(
    const PicoATE::Core::ProductRoutingConfig& config)
{
    m_allowManualSwitch->setChecked(config.allowManualInTest);
    m_projectRootPath = config.projectRootPath.trimmed().isEmpty()
        ? StartupSupport::productProjectRootPathForRoot(
              QFileInfo(m_routingPath).absolutePath())
        : QFileInfo(config.projectRootPath).absoluteFilePath();
    m_projects = PicoATE::Core::discoverProductProjects(m_projectRootPath);
    m_table->setRowCount(0);
    for (const auto& route : config.routes) {
        appendRoute(route);
    }
    if (m_table->rowCount() > 0) {
        m_table->selectRow(0);
    }
    updateButtons();
}

void ProductRoutingDialog::addRoute()
{
    PicoATE::Core::ProductRoute route;
    route.name = tr("Route %1").arg(m_table->rowCount() + 1);
    route.enabled = true;
    if (!m_projects.isEmpty()) {
        route.projectPath = m_projects.first().directoryPath;
    }
    appendRoute(route);
    m_table->selectRow(m_table->rowCount() - 1);
    m_table->editItem(m_table->item(m_table->rowCount() - 1, PatternColumn));
}

void ProductRoutingDialog::duplicateSelectedRoute()
{
    const int row = selectedRouteRow();
    if (row < 0) {
        return;
    }
    PicoATE::Core::ProductRoute route;
    route.enabled = m_table->item(row, EnabledColumn)->checkState() == Qt::Checked;
    route.name = m_table->item(row, NameColumn)->text().trimmed() + tr(" Copy");
    route.pattern = m_table->item(row, PatternColumn)->text().trimmed();
    if (auto* project = qobject_cast<QComboBox*>(
            m_table->cellWidget(row, ProjectColumn))) {
        route.projectPath = project->currentData().toString();
        route.sequencePath = project->currentData(LegacySequenceRole).toString();
    }
    appendRoute(route);
    m_table->selectRow(m_table->rowCount() - 1);
    m_table->editItem(m_table->item(m_table->rowCount() - 1, PatternColumn));
}

void ProductRoutingDialog::removeSelectedRoute()
{
    const int row = selectedRouteRow();
    if (row < 0) {
        return;
    }
    m_table->removeRow(row);
    if (m_table->rowCount() > 0) {
        m_table->selectRow(qMin(row, m_table->rowCount() - 1));
    }
    updateButtons();
}

void ProductRoutingDialog::appendRoute(const PicoATE::Core::ProductRoute& route)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto* enabled = new QTableWidgetItem;
    enabled->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                      Qt::ItemIsUserCheckable);
    enabled->setCheckState(route.enabled ? Qt::Checked : Qt::Unchecked);
    m_table->setItem(row, EnabledColumn, enabled);

    auto* name = new QTableWidgetItem(route.name);
    name->setToolTip(tr("A readable product or route name"));
    m_table->setItem(row, NameColumn, name);

    auto* pattern = new QTableWidgetItem(route.pattern);
    pattern->setToolTip(tr("Wildcard examples: BTSN* or *C1234567*"));
    m_table->setItem(row, PatternColumn, pattern);

    auto* project = new QComboBox(m_table);
    project->setObjectName(QStringLiteral("productRoutingProjectCombo"));
    project->addItem(tr("Select Product Project"), QString{});
    for (const auto& candidate : m_projects) {
        const auto label = candidate.ok()
            ? candidate.name
            : tr("%1 (Needs attention)").arg(candidate.name);
        project->addItem(label, candidate.directoryPath);
        const int index = project->count() - 1;
        if (!candidate.ok()) {
            QStringList details;
            for (const auto& error : candidate.errors) {
                details.push_back(error.message);
            }
            project->setItemData(index, details.join(QStringLiteral("\n")),
                                 Qt::ToolTipRole);
        }
    }
    const auto configuredProject = route.projectPath.trimmed().isEmpty()
        ? QString{}
        : QFileInfo(route.projectPath).absoluteFilePath();
    int projectIndex = project->findData(configuredProject);
    if (!configuredProject.isEmpty() && projectIndex < 0) {
        const auto inspected = PicoATE::Core::inspectProductProject(
            configuredProject);
        project->addItem(inspected.name.isEmpty()
                             ? QFileInfo(configuredProject).fileName()
                             : inspected.name,
                         configuredProject);
        projectIndex = project->count() - 1;
    } else if (!route.sequencePath.trimmed().isEmpty()) {
        const auto legacyPath = QFileInfo(route.sequencePath).absoluteFilePath();
        project->addItem(
            tr("Legacy: %1").arg(QFileInfo(legacyPath).fileName()), QString{});
        projectIndex = project->count() - 1;
        project->setItemData(projectIndex, legacyPath, LegacySequenceRole);
        project->setItemData(
            projectIndex,
            tr("Legacy flat route. Select a product project to migrate it."),
            Qt::ToolTipRole);
    }
    project->setCurrentIndex(qMax(0, projectIndex));
    m_table->setCellWidget(row, ProjectColumn, project);

    auto* deviceStatus = new QTableWidgetItem;
    deviceStatus->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    m_table->setItem(row, DeviceStatusColumn, deviceStatus);

    auto* devices = new QPushButton(tr("Configure"), m_table);
    devices->setObjectName(QStringLiteral("productRoutingDevicesButton"));
    devices->setIcon(routingIcon("cable"));
    devices->setToolTip(tr("Configure the devices in this project's Station"));
    m_table->setCellWidget(row, DevicesColumn, devices);
    connect(project, &QComboBox::currentIndexChanged, this,
            [this, project] {
                const int row = m_table->indexAt(
                    project->mapTo(m_table->viewport(), project->rect().center()))
                                    .row();
                updateProjectRow(row);
            });
    connect(devices, &QPushButton::clicked, this, [this, devices] {
        const int row = m_table->indexAt(
            devices->mapTo(m_table->viewport(), devices->rect().center()))
                            .row();
        openProjectDevices(row);
    });
    updateProjectRow(row);
}

void ProductRoutingDialog::updateProjectRow(int row)
{
    if (row < 0 || row >= m_table->rowCount()) {
        return;
    }
    auto* projectCombo = qobject_cast<QComboBox*>(
        m_table->cellWidget(row, ProjectColumn));
    auto* status = m_table->item(row, DeviceStatusColumn);
    auto* devices = qobject_cast<QPushButton*>(
        m_table->cellWidget(row, DevicesColumn));
    if (!projectCombo || !status || !devices) {
        return;
    }

    PicoATE::Core::ProductProject project;
    const auto projectPath = projectCombo->currentData().toString();
    const auto legacySequence = projectCombo->currentData(
        LegacySequenceRole).toString();
    if (!projectPath.isEmpty()) {
        project = PicoATE::Core::inspectProductProject(projectPath);
    } else if (!legacySequence.isEmpty()) {
        project.name = QFileInfo(legacySequence).absoluteDir().dirName();
        project.directoryPath = QFileInfo(legacySequence).absolutePath();
        project.sequencePath = QFileInfo(legacySequence).absoluteFilePath();
        const auto stationPath = QFileInfo(legacySequence).absoluteDir().filePath(
            QStringLiteral("StationSystem.json"));
        if (QFileInfo(stationPath).isFile()) {
            project.stationPath = QFileInfo(stationPath).absoluteFilePath();
        } else {
            project.errors.push_back({
                QStringLiteral("project.station"),
                tr("Missing StationSystem.json"),
                {}});
        }
    }

    if (projectPath.isEmpty() && legacySequence.isEmpty()) {
        status->setText(tr("Not selected"));
        status->setForeground(QColor(QStringLiteral("#68737b")));
        status->setToolTip({});
        devices->setEnabled(false);
        return;
    }

    QStringList details;
    for (const auto& error : project.errors) {
        details.push_back(error.message);
    }
    if (project.ok()) {
        status->setText(tr("Ready"));
        status->setForeground(QColor(QStringLiteral("#177245")));
        status->setToolTip(
            tr("Sequence: %1\nStation: %2")
                .arg(QFileInfo(project.sequencePath).fileName(),
                     QFileInfo(project.stationPath).fileName()));
    } else {
        status->setText(tr("Needs attention"));
        status->setForeground(QColor(QStringLiteral("#b42318")));
        status->setToolTip(details.join(QStringLiteral("\n")));
    }
    devices->setEnabled(QFileInfo(project.stationPath).isFile());
}

void ProductRoutingDialog::openProjectDevices(int row)
{
    if (row < 0 || row >= m_table->rowCount()) {
        return;
    }
    auto* projectCombo = qobject_cast<QComboBox*>(
        m_table->cellWidget(row, ProjectColumn));
    if (!projectCombo) {
        return;
    }

    QString stationPath;
    const auto projectPath = projectCombo->currentData().toString();
    const auto legacySequence = projectCombo->currentData(
        LegacySequenceRole).toString();
    if (!projectPath.isEmpty()) {
        stationPath = PicoATE::Core::inspectProductProject(projectPath)
                          .stationPath;
    } else if (!legacySequence.isEmpty()) {
        stationPath = QFileInfo(legacySequence).absoluteDir().filePath(
            QStringLiteral("StationSystem.json"));
    }
    if (!QFileInfo(stationPath).isFile()) {
        QMessageBox::warning(
            this, tr("Product Devices"),
            tr("This product project has no StationSystem.json."));
        updateProjectRow(row);
        return;
    }

    FieldDeviceDialog dialog(stationPath, this);
    connect(&dialog, &FieldDeviceDialog::stationSaved, this, [this, row] {
        m_statusLabel->setStyleSheet({});
        m_statusLabel->setText(
            tr("Device configuration saved. Unsaved route edits remain in this dialog."));
        updateProjectRow(row);
    });
    dialog.exec();
}

void ProductRoutingDialog::save()
{
    PicoATE::Core::ProductRoutingConfig config;
    QStringList errors;
    if (!collectAndValidate(&config, &errors)) {
        m_statusLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
        m_statusLabel->setText(errors.join(QStringLiteral("\n")));
        return;
    }

    QDir().mkpath(QFileInfo(m_routingPath).absolutePath());
    QSaveFile file(m_routingPath);
    const auto data = QJsonDocument(
        PicoATE::Core::productRoutingToJson(config, m_routingPath))
                          .toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
        !file.commit()) {
        m_statusLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
        m_statusLabel->setText(
            tr("Cannot save ProductRouting.json: %1").arg(file.errorString()));
        return;
    }

    emit routingSaved();
    accept();
}

bool ProductRoutingDialog::collectAndValidate(
    PicoATE::Core::ProductRoutingConfig* config,
    QStringList* errors)
{
    clearValidationState();
    config->allowManualInTest = m_allowManualSwitch->isChecked();
    config->sourcePath = m_routingPath;
    config->projectRootPath = m_projectRootPath;
    config->routes.clear();

    for (int row = 0; row < m_table->rowCount(); ++row) {
        PicoATE::Core::ProductRoute route;
        route.enabled = m_table->item(row, EnabledColumn)->checkState() == Qt::Checked;
        route.name = m_table->item(row, NameColumn)->text().trimmed();
        route.pattern = m_table->item(row, PatternColumn)->text().trimmed();
        if (auto* project = qobject_cast<QComboBox*>(
                m_table->cellWidget(row, ProjectColumn))) {
            route.projectPath = project->currentData().toString().trimmed();
            route.sequencePath = project->currentData(
                LegacySequenceRole).toString().trimmed();
        }
        if (route.name.isEmpty()) {
            const auto message = tr("Route %1 needs a name.").arg(row + 1);
            errors->push_back(message);
            markCellInvalid(row, NameColumn, message);
        }
        config->routes.push_back(std::move(route));
    }

    const auto serialized = PicoATE::Core::productRoutingToJson(
        *config, m_routingPath);
    const auto parsed = PicoATE::Core::parseProductRoutingJson(
        serialized, m_routingPath);
    for (const auto& diagnostic : parsed.errors) {
        errors->push_back(diagnosticText(diagnostic));
        const QRegularExpression routePath(
            QStringLiteral("^routes\\[(\\d+)\\]\\.(name|pattern|project|sequence)$"));
        const auto match = routePath.match(diagnostic.path);
        if (!match.hasMatch()) {
            continue;
        }
        const int row = match.captured(1).toInt();
        const auto field = match.captured(2);
        markCellInvalid(
            row,
            field == QStringLiteral("name") ? NameColumn
                : field == QStringLiteral("pattern") ? PatternColumn
                                                       : ProjectColumn,
            diagnostic.message);
    }
    if (!errors->isEmpty()) {
        return false;
    }

    QSet<QString> validatedProjects;
    for (int row = 0; row < config->routes.size(); ++row) {
        const auto& route = config->routes.at(row);
        const auto targetKey = !route.projectPath.isEmpty()
            ? route.projectPath : route.sequencePath;
        if (!route.enabled || validatedProjects.contains(targetKey)) {
            continue;
        }
        validatedProjects.insert(targetKey);
        QString sequencePath = route.sequencePath;
        if (!route.projectPath.isEmpty()) {
            const auto project = PicoATE::Core::inspectProductProject(
                route.projectPath);
            if (!project.ok()) {
                QStringList details;
                for (const auto& diagnostic : project.errors) {
                    details.push_back(diagnostic.message);
                }
                const auto message = tr("Route %1: %2")
                    .arg(row + 1)
                    .arg(details.join(QStringLiteral("; ")));
                errors->push_back(message);
                markCellInvalid(row, ProjectColumn, message);
                continue;
            }
            const auto station = PicoATE::Core::loadStationConfigFile(
                project.stationPath);
            if (!station.ok()) {
                const auto& diagnostic = station.errors.first();
                const auto detail = diagnostic.path.isEmpty()
                    ? diagnostic.message
                    : QStringLiteral("%1: %2")
                          .arg(diagnostic.path, diagnostic.message);
                const auto message = tr("Route %1: Station is invalid: %2")
                    .arg(row + 1)
                    .arg(detail);
                errors->push_back(message);
                markCellInvalid(row, ProjectColumn, message);
                continue;
            }
            sequencePath = project.sequencePath;
        }
        const auto error = validateSequence(sequencePath);
        if (!error.isEmpty()) {
            const auto message = tr("Route %1: %2").arg(row + 1).arg(error);
            errors->push_back(message);
            markCellInvalid(row, ProjectColumn, message);
        }
    }
    return errors->isEmpty();
}

QString ProductRoutingDialog::validateSequence(const QString& sequencePath) const
{
    QFile file(sequencePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return tr("Sequence cannot be read: %1").arg(sequencePath);
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return tr("Sequence is not valid JSON: %1").arg(parseError.errorString());
    }
    PicoATE::Core::SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    if (compiled.ok()) {
        return {};
    }
    const auto& error = compiled.errors.first();
    return error.path.isEmpty()
        ? error.message
        : QStringLiteral("%1: %2").arg(error.path, error.message);
}

void ProductRoutingDialog::clearValidationState()
{
    m_statusLabel->setStyleSheet({});
    for (int row = 0; row < m_table->rowCount(); ++row) {
        for (int column : {NameColumn, PatternColumn}) {
            auto* item = m_table->item(row, column);
            item->setBackground(QBrush{});
            item->setToolTip(column == PatternColumn
                ? tr("Wildcard examples: BTSN* or *C1234567*")
                : tr("A readable product or route name"));
        }
        if (auto* project = m_table->cellWidget(row, ProjectColumn)) {
            project->setStyleSheet({});
        }
    }
}

void ProductRoutingDialog::markCellInvalid(int row,
                                           int column,
                                           const QString& message)
{
    if (row < 0 || row >= m_table->rowCount()) {
        return;
    }
    if (column == ProjectColumn) {
        if (auto* project = m_table->cellWidget(row, column)) {
            project->setStyleSheet(
                QStringLiteral("QComboBox { border: 1px solid #d92d20; }"));
            project->setToolTip(message);
        }
        return;
    }
    if (auto* item = m_table->item(row, column)) {
        item->setBackground(QColor(QStringLiteral("#fee4e2")));
        item->setToolTip(message);
    }
}

int ProductRoutingDialog::selectedRouteRow() const
{
    if (!m_table || !m_table->selectionModel()) {
        return -1;
    }
    const auto rows = m_table->selectionModel()->selectedRows();
    return rows.size() == 1 ? rows.constFirst().row() : -1;
}

void ProductRoutingDialog::updateButtons()
{
    const bool selected = selectedRouteRow() >= 0;
    m_duplicateButton->setEnabled(selected);
    m_removeButton->setEnabled(selected);
}

} // namespace PicoATE::Ui
