#include <QtTest/QtTest>

#include "AdminStartupSplash.h"
#include "ExecutionViewModel.h"
#include "FieldDeviceDialog.h"
#include "FlowTargetSelector.h"
#include "InputWheelGuard.h"
#include "IntegrityPage.h"
#include "LoginDialog.h"
#include "LoadingSpinner.h"
#include "MainWindow.h"
#include "MultiUutOverviewWidget.h"
#include "OperatorPromptPresenter.h"
#include "PromptCountdownWidget.h"
#include "ParserActualDelegate.h"
#include "ProjectResourcePaths.h"
#include "ReportExporter.h"
#include "RunArtifactWriter.h"
#include "ProductionWindow.h"
#include "ProductRoutingDialog.h"
#include "ProductRoutingScanSupport.h"
#include "ProportionalHeaderView.h"
#include "PluginCatalog.h"
#include "PluginFunctionModel.h"
#include "PicoATEStyle.h"
#include "RunnerModels.h"
#include "ScanDialog.h"
#include "SequenceDocument.h"
#include "SequenceEditorTreeView.h"
#include "SequenceTreeModel.h"
#include "SequenceVariablesDialog.h"
#include "StepPropertyEditor.h"
#include "StationDeviceModel.h"
#include "StationDocument.h"
#include "StationPropertyEditor.h"
#include "StationSettingsEditor.h"
#include "UiLanguage.h"
#include "UiTextBinding.h"
#include "ElidedInfoLabel.h"
#include "UutSlotConfigurationDialog.h"
#include "PicoATE/Core/ExecutionReportJson.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAction>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QPixmap>
#include <QPainter>
#include <QSettings>
#include <QScreen>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QStackedWidget>
#include <QStyleOption>
#include <QStandardItemModel>
#include <QSplitter>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QTreeView>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <array>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

using namespace PicoATE::Ui;

namespace {

QModelIndex sequenceGroupByKind(SequenceTreeModel* model,
                                const QString& kind)
{
    if (!model) {
        return {};
    }
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto group = model->index(row, SequenceTreeModel::NameColumn);
        if (group.siblingAtColumn(SequenceTreeModel::KindColumn)
                .data().toString().compare(kind, Qt::CaseInsensitive) == 0) {
            return group;
        }
    }
    return {};
}

QModelIndex expressionIndex(const QAbstractItemModel* model,
                            const QString& expression,
                            const QModelIndex& parent = {})
{
    if (!model) {
        return {};
    }
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const auto index = model->index(row, 0, parent);
        if (index.data(Qt::UserRole).toString() == expression) {
            return index;
        }
        const auto nested = expressionIndex(model, expression, index);
        if (nested.isValid()) {
            return nested;
        }
    }
    return {};
}

QStringList modelIndexPath(QModelIndex index)
{
    QStringList path;
    while (index.isValid()) {
        path.prepend(index.data().toString());
        index = index.parent();
    }
    return path;
}

bool chooseExpression(QToolButton* picker,
                      const QString& expression,
                      QStringList* selectedPath = nullptr,
                      QStringList* initialPath = nullptr,
                      QStringList* initialRootPath = nullptr,
                      bool* backEnabled = nullptr,
                      QStringList* rootAfterBack = nullptr,
                      bool* columnsStayedFixed = nullptr)
{
    if (!picker || !picker->isEnabled()) {
        return false;
    }
    bool selected = false;
    QTimer::singleShot(0, picker, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != QStringLiteral("expressionPickerDialog")) {
            return;
        }
        const std::array<QListView*, 3> columns = {
            dialog->findChild<QListView*>(QStringLiteral("expressionPickerColumn0")),
            dialog->findChild<QListView*>(QStringLiteral("expressionPickerColumn1")),
            dialog->findChild<QListView*>(QStringLiteral("expressionPickerColumn2"))};
        auto* insert = dialog->findChild<QPushButton*>(
            QStringLiteral("expressionPickerInsertButton"));
        const bool hasColumns = std::all_of(
            columns.cbegin(), columns.cend(), [](const QListView* view) {
                return view != nullptr;
            });
        const auto index = hasColumns
            ? expressionIndex(columns.front()->model(), expression)
            : QModelIndex{};
        if (!hasColumns || !insert || !index.isValid()) {
            dialog->reject();
            return;
        }
        const auto inactiveColumnsAreBlank = [&columns] {
            return std::all_of(
                columns.cbegin(), columns.cend(), [](const QListView* column) {
                    return column->isEnabled() ||
                        column->model()->rowCount(column->rootIndex()) == 0;
                });
        };
        if (!inactiveColumnsAreBlank()) {
            dialog->reject();
            return;
        }
        const std::array<QRect, 3> initialColumnGeometry = {
            columns[0]->geometry(), columns[1]->geometry(), columns[2]->geometry()};

        if (initialPath) {
            initialPath->clear();
            for (auto column = columns.crbegin(); column != columns.crend(); ++column) {
                if ((*column)->currentIndex().isValid()) {
                    *initialPath = modelIndexPath((*column)->currentIndex());
                    break;
                }
            }
        }
        if (initialRootPath) {
            *initialRootPath = modelIndexPath(columns.front()->rootIndex());
        }
        auto* back = dialog->findChild<QToolButton*>(
            QStringLiteral("expressionPickerBackButton"));
        if (backEnabled) {
            *backEnabled = back && back->isEnabled();
        }
        if (rootAfterBack) {
            if (back && back->isEnabled()) {
                back->click();
            }
            *rootAfterBack = modelIndexPath(columns.front()->rootIndex());
        }
        if (selectedPath) {
            *selectedPath = modelIndexPath(index);
        }

        const auto isBelowRoot = [](QModelIndex candidate,
                                    const QModelIndex& root) {
            while (candidate.isValid() && candidate != root) {
                candidate = candidate.parent();
            }
            return candidate == root;
        };
        while (back && back->isEnabled() &&
               !isBelowRoot(index, columns.front()->rootIndex())) {
            back->click();
        }

        bool indexSelected = false;
        for (int attempt = 0; attempt < 16 && !indexSelected; ++attempt) {
            for (auto* column : columns) {
                if (column->isEnabled() &&
                    index.parent() == column->rootIndex()) {
                    column->setCurrentIndex(index);
                    indexSelected = true;
                    break;
                }
            }
            if (indexSelected) {
                break;
            }

            for (auto ancestor = index.parent(); ancestor.isValid();
                 ancestor = ancestor.parent()) {
                bool advanced = false;
                for (auto* column : columns) {
                    if (column->isEnabled() &&
                        ancestor.parent() == column->rootIndex()) {
                        column->setCurrentIndex(ancestor);
                        advanced = true;
                        break;
                    }
                }
                if (advanced) {
                    break;
                }
            }
            if (!inactiveColumnsAreBlank()) {
                dialog->reject();
                return;
            }
        }
        if (columnsStayedFixed) {
            *columnsStayedFixed = indexSelected &&
                columns[0]->geometry() == initialColumnGeometry[0] &&
                columns[1]->geometry() == initialColumnGeometry[1] &&
                columns[2]->geometry() == initialColumnGeometry[2] &&
                columns[0]->geometry().right() < columns[1]->geometry().left() &&
                columns[1]->geometry().right() < columns[2]->geometry().left();
        }
        if (!insert->isEnabled()) {
            dialog->reject();
            return;
        }
        selected = true;
        insert->click();
    });
    picker->click();
    return selected;
}

PicoATE::Core::ExecutionReport pdfReportFixture(int detailCount = 12)
{
    using namespace PicoATE::Core;
    ExecutionReport report;
    report.planId = QStringLiteral("pdf-plan");
    report.sequenceId = QStringLiteral("pdf-sequence");
    report.state = ExecutionState::Completed;
    report.completed = true;
    report.metadata.model = QStringLiteral("PICO-800V");
    report.metadata.customerId = QStringLiteral("CUSTOMER-01");
    report.metadata.sequenceName = QStringLiteral("charging_station_sequence.json");
    report.metadata.serialNumber = QStringLiteral("BTSN2608130001");
    report.metadata.stationId = QStringLiteral("STATION-01");
    report.metadata.jigNo = QStringLiteral("JIG-07");
    report.metadata.order = QStringLiteral("ORDER-20260813");
    report.metadata.tester = QStringLiteral("Tester-01");
    report.metadata.startedAt = QDateTime(QDate(2026, 8, 13), QTime(19, 53, 10));
    report.metadata.finishedAt = report.metadata.startedAt.addMSecs(84382);
    report.metadata.durationMs = 84382;

    UutReport uut;
    uut.uutId = QStringLiteral("UUT-1");
    uut.completed = true;
    uut.outcome = NodeOutcome::Passed;
    QVector<StepReport> generatedSteps;
    for (int index = 0; index < detailCount; ++index) {
        MeasurementResult measurement;
        measurement.name = QStringLiteral("Voltage");
        measurement.value = index == 1
            ? QVariant(QStringLiteral(
                  "RAW=00 01 02 03 04 05 06 07 08 09; PARSED=BTSN2608130001"))
            : QVariant(800.0 + index / 100.0);
        measurement.unit = QStringLiteral("V");
        measurement.hasLowerLimit = index != 1;
        measurement.lowerLimit = 795.0;
        measurement.hasUpperLimit = index != 1;
        measurement.upperLimit = 805.0;
        measurement.status = MeasurementStatus::Passed;

        StepReport step;
        step.stepId = QStringLiteral("step-%1").arg(index + 1);
        step.displayName = index == 0
            ? QStringLiteral(
                  "Read and decode charging controller register payload from CAN channel")
            : QStringLiteral("Stable Sample %1").arg(index, 2, 10, QLatin1Char('0'));
        step.kind = ExecNodeKind::Action;
        step.state = ActivationState::Passed;
        step.outcome = NodeOutcome::Passed;
        step.durationMs = 1000 + index;
        step.measurements = {measurement};
        generatedSteps.push_back(std::move(step));
    }
    if (generatedSteps.size() >= 3) {
        StepReport nested;
        nested.stepId = QStringLiteral("nested-checks");
        nested.displayName = QStringLiteral("CAN Readback Verification");
        nested.kind = ExecNodeKind::TestItem;
        nested.state = ActivationState::Passed;
        nested.outcome = NodeOutcome::Passed;
        nested.resultRecording = false;
        nested.children = {generatedSteps.takeFirst(), generatedSteps.takeFirst()};

        StepReport parent;
        parent.stepId = QStringLiteral("communication-checks");
        parent.displayName = QStringLiteral("Charging Communication Checks");
        parent.kind = ExecNodeKind::TestItem;
        parent.state = ActivationState::Passed;
        parent.outcome = NodeOutcome::Passed;
        parent.resultRecording = false;
        parent.children = {std::move(nested), generatedSteps.takeFirst()};
        uut.steps.push_back(std::move(parent));
    }
    uut.steps += std::move(generatedSteps);
    report.uuts = {std::move(uut)};
    return report;
}

} // namespace

class MainWindowLifecycleTests final : public QObject
{
    Q_OBJECT

private slots:
    void titleBarLanguageButtonPreservesNativeWindow();
    void integrityPageOnlyAppearsForDailyAdmin();
    void integrityPageApprovesSelectedFilesAndKeepsReadableHashes();
    void productionIntegrityBlocksUntilBaselineIsApproved();
    void adminDisabledSlotsRemainVisible();
    void localizedConfigurationKeepsData();
    void smallScreenRunInfoRemainsReadable();
    void stationCapacityLimitsToolbar_data();
    void stationCapacityLimitsToolbar();
    void scannerYieldsToModalWindows();
    void stationSavePromptSuspendsScanner();
    void maximizedRunInformationKeepsFullTextHeight();
    void uutSlotsSuspendScanner_data();
    void uutSlotsSuspendScanner();
    void initTestCase();
    void cleanupTestCase();
    void languageSwitchPreservesAdminDraftAndSelection();
    void languageSwitchPreservesRunningProductionAndScanner();
    void languageSwitchRefreshesModelsWithoutChangingReports();
    void languageSwitchPreservesPromptInputAndResponse();
    void languageSwitchPreservesSlotChoices();
    void picoStyleDrawsFilledCheckedIndicators();
    void parserActualDelegateHighlightsSelectedTokens();
    void inputWheelGuardPreventsAccidentalValueChanges();
    void responsiveLayoutKeepsSmallScreenPanelsUsable();
    void closeAfterEditedRun_data();
    void closeAfterEditedRun();
    void stationEditorFeedsCompileSnapshot_data();
    void stationEditorFeedsCompileSnapshot();
    void stationConnectionActionUpdatesStatus();
    void stationDeviceApplyPreservesDllPathWhenModelIsUnchanged();
    void stationPropertyEditorUsesTypedIdsAndFilteredDrivers();
    void stationPropertyEditorKeepsCanChannelOptionsIndependent();
    void stationPropertyEditorPreservesCanIdentityWhenEditingFirstGroup();
    void stationNewCanKeepsTableEnabledStateWhenDraftIsSaved();
    void stationDeviceSlotsMoveReferencesBeforeOrderedDeletion();
    void compileFailureFocusesDiagnosticAndExplainsDisabledRun();
    void stationCtrlSaveCommitsDraftAndClearsWindowMarker();
    void switchingStationDevicesCanDiscardCurrentDraft();
    void disabledReferencedDeviceDiagnosticPersistsAcrossEditors();
    void runActionSyncsTreeBreakpointsAndStopsAtBreakpoint();
    void runTestBreakpointGutterControlsExecutionBreakpoints();
    void runPopulatesRuntimeTimeline();
    void multiUutOverviewShowsRetryAndRecentSteps();
    void multiUutOverviewShowsDelayedCleanupOverlay();
    void adminMultiUutRunShowsOverviewAndNavigatesToDetails();
    void persistsLayoutAndRecentFiles();
    void invalidOrOffscreenGeometryFallsBackToPrimaryScreen();
    void loginDialogDiscoversSequenceAndValidatesAdminPassword();
    void loginDialogOffersNewProjectTemplateWhenProjectsAreEmpty();
    void newProjectTemplateSavesSequenceAndStationTogether();
    void loginDialogAppliesRoutingPolicyAndRemembersLoadMode();
    void productRoutingDialogEditsAndAtomicallySavesRoutes();
    void productRoutingDialogDeletesSelectedRouteInsteadOfCurrentRoute();
    void productRoutingDialogAllowsPotentialOverlapAndRejectsBrokenSequence();
    void adminStartupSplashCentersLogoAndRunsSpinner();
    void adminStartupInitializationShowsBusyOverlay();
    void stationScanDialogTogglePersists();
    void pdfReportExportsAndArchivesWithStationPolicy();
    void fieldDeviceDialogAppliesCurrentDeviceAndSavesAll();
    void productionFieldDeviceDialogDefersScannerUntilClosed();
    void scanDialogAcceptsRepeatedBarcodeAndHasNoWindowButtons();
    void scanDialogCollectsCarouselBatchAndSupportsReplacement();
    void scanDialogSkipsDisabledSlotWithoutCollapsingBatchPositions();
    void scanDialogExpandsValidatedBatchAndRejectsCurrentSlot();
    void adminStartsOnProductionDashboardAndOpensScannerOnDemand();
    void adminScannerRunsFourExplicitUuts();
    void productionWindowPreloadsFlowAndRunsWithoutScanner();
    void productionScannerRunsFourExplicitUutsAndCountsYield();
    void productionUutControlsConfigureRuntimeSlots();
    void productionWindowRoutesScannedSnBeforeCompiling();
    void adminWindowRoutesScannedSnBeforeCompiling();
    void productionStoppedRunCountsAsFailure();
    void productionLoopTestCountsAndArchivesEveryIteration();
    void productionWindowShowsSkippedStepsAndCleanupAfterFailure();
    void projectImagePathsStayInsideCurrentProject();
    void pluginPropertyEditorValidatesRequiredAndRangeAndSavesInputs();
    void pluginPropertyEditorPreservesLegacyActionData();
    void pluginPropertyEditorAcceptsRevertedInvalidDraftAsNoOp();
    void pluginPropertyEditorInsertsPreviousStepOutputExpression();
    void pluginPropertyEditorSwitchesConditionalInputs();
    void logicalDeviceOpenKeepsStationParametersOutOfStepInputs();
    void pluginPropertyEditorPrefersTypedControlsAndPreservesAdvancedJson();
    void flowEditorAddsAndLocksStandardSequenceGroups();
    void resourceRegionGutterTogglesBoundariesAndSelectsHardware();
    void sequenceVariablesToolbarEditsPerUutValuesAndFeedsFxMenu();
    void sequenceVariablesDeleteUsesExplicitStableSelection();
    void ctrlSaveCommitsCurrentStepDraftWithoutPrompt();
    void deletingStepDiscardsItsInvalidPropertyDraft();
    void switchingStepsKeepsDraftWithoutPrompt();
    void leavingFlowPromptsOnceAndCanKeepDraft();
    void stepEditorRelocatesDraftAfterSequenceStructureChanges();
    void limitPropertyEditorSwitchesComparisonFieldsAndRemovesStaleValues();
    void resultSoFarIsAvailableInFxWithoutChangingItsToken();
    void wrapsSelectedStepsInTestItemFromToolbar();
    void copiesAndPastesSelectedItemsFromToolbar();
    void flowBlankClickClearsSelection();
    void compileSilentlySavesCurrentDraft();
    void functionPalettePreviewsParametersAndShowsDragHandles();
    void flowTargetSelectorHandlesChannelsAndMoreDevices();
    void flowFieldInspectionAppearsImmediatelyAndFillsPanel();
    void proportionalHeaderDistributesAvailableWidthByWeight();
    void flowEnableTogglePreservesTreePosition();
    void flowToolbarExpandsAndCollapsesAtPhaseFirstLevel();
    void flowDropTargetPrefersTestItemInterior();
    void operatorPromptDialogCannotBeDismissedByKeyboardOrWindowControls();
    void promptCountdownTracksRequestAndModes();
    void promptCountdownSurvivesRehostingAndRebuilds();
    void sharedPromptHasCompactCountdown();
    void compactPromptPanelsPreserveStateAndBounds();
    void singleUutNavigationStaysVisible_data();
    void singleUutNavigationStaysVisible();
    void operatorPromptDialogValidatesInputMode();
    void operatorPromptDialogReusesKeyForJudgment();
    void operatorPromptsUseTheirMatchingOverviewCards();
    void operatorPromptsReturnToCardsAfterOverviewNavigation();
    void oncePerBatchOperatorPromptCoversAllOverviewCards();
    void messageBoxPropertyEditorSwitchesConfirmationMode();
    void messageBoxPropertyEditorConfiguresJudgmentMode();
    void messageBoxPropertyEditorConfiguresInputMode();
    void messageBoxPropertyEditorInsertsRuntimeValues();
    void parserPropertyEditorCreatesNamedOutputsForFx();
    void parserPropertyEditorSwitchesRegisterModes();
    void parserPropertyEditorPreservesExplicitEmptyEndMarker();
    void whileLoopPropertyEditorUsesTypedFields();
    void valueToolsPropertyEditorUsesExpressionList();
    void periodicActionPropertyEditorUsesTypedPolicyFields();
    void stepExecutionScopeEditorPersistsSharedMode();
    void barrierPropertyEditorUsesMinimalConfiguration();
    void stepFailurePolicyEditorUsesThreeOutcomeCombos();

private:
    QTemporaryDir m_settingsDirectory;
    QTemporaryDir m_integrityDirectory;
};

void MainWindowLifecycleTests::initTestCase()
{
    qApp->setStyle(new PicoATEStyle);
    applyPicoATEApplicationTheme(*qApp);
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("PicoATE.Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("PicoATEUiWindowTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,
                       QSettings::UserScope,
                       m_settingsDirectory.path());
    QSettings().clear();
    QVERIFY(m_integrityDirectory.isValid());
    for (const auto& name : RuntimeIntegrity::fileNames()) {
        QFile file(m_integrityDirectory.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("UI test integrity fixture");
    }
    QVERIFY(RuntimeIntegrity::authorize(RuntimeIntegrity::check(m_integrityDirectory.path()),
        RuntimeIntegrity::fileNames(), AdminAccess::Supervisor,
        QString::number(StartupSupport::dailyAdminPassword()), "UI test fixture").isEmpty());
    qApp->setProperty("integrityTestRoot", m_integrityDirectory.path());
}

void MainWindowLifecycleTests::cleanupTestCase()
{
    QSettings().clear();
    qApp->setProperty("integrityTestRoot", QVariant{});
}

void MainWindowLifecycleTests::integrityPageOnlyAppearsForDailyAdmin()
{
    QTemporaryDir loginRoot;
    LoginDialog login(loginRoot.path());
    auto* admin = login.findChild<QToolButton*>("loginAdminModeButton");
    auto* password = login.findChild<QLineEdit*>("loginAdminPassword");
    auto* submit = login.findChild<QPushButton*>("loginButton");
    QVERIFY(admin && password && submit);
    admin->click();
    password->setText(QString::number(StartupSupport::dailyAdminPassword()));
    submit->click();
    QTRY_COMPARE(login.result(), int(QDialog::Accepted));
    QCOMPARE(login.selection().adminAccess, AdminAccess::Supervisor);
    auto window = createMainWindow();
    auto* tabs = window->findChild<QTabWidget*>("workspaceTabs");
    QVERIFY(tabs);
    const int originalCount = tabs->count();
    window->setAdminAccess(AdminAccess::Standard);
    QCOMPARE(tabs->count(), originalCount);
    QVERIFY(!window->findChild<IntegrityPage*>());
    window->setAdminAccess(login.selection().adminAccess);
    QCOMPARE(tabs->count(), originalCount + 1);
    auto* page = window->findChild<IntegrityPage*>();
    QVERIFY(page);
    QCOMPARE(tabs->indexOf(page), tabs->count() - 1);
    window->setAdminAccess(AdminAccess::Standard);
    QCOMPARE(tabs->count(), originalCount);
    QVERIFY(!window->findChild<IntegrityPage*>());
    StartupSelection selection;
    auto production = createProductionWindow(selection);
    QVERIFY(!production->findChild<IntegrityPage*>());
}

void MainWindowLifecycleTests::integrityPageApprovesSelectedFilesAndKeepsReadableHashes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const auto& name : RuntimeIntegrity::fileNames()) {
        QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("original runtime");
    }
    const auto password = QString::number(StartupSupport::dailyAdminPassword());
    QVERIFY(RuntimeIntegrity::authorize(RuntimeIntegrity::check(dir.path()), RuntimeIntegrity::fileNames(),
        AdminAccess::Supervisor, password, "Initial test baseline").isEmpty());
    auto& language = UiLanguage::instance();
    const bool wasChinese = language.isChinese();
    const auto restore = qScopeGuard([&] { language.setChinese(wasChinese, false); });
    QVERIFY(language.setChinese(false, false));
    IntegrityPage page(dir.path(), AdminAccess::Supervisor);
    page.resize(1100, 620);
    page.show();
    QTRY_VERIFY(page.report().passed());
    auto* table = page.findChild<QTableWidget*>("integrityFilesTable");
    auto* approve = page.findChild<QPushButton*>("integrityApproveButton");
    QVERIFY(table && approve);
    QCOMPARE(table->item(0, 2)->text().remove('\n').size(), 64);
    const auto screenshots = qEnvironmentVariable("PICOATE_INTEGRITY_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
        QTest::qWait(50);
        QVERIFY(page.grab().save(QDir(screenshots).filePath("integrity-en-1100.png")));
    }
    QFile ui(dir.filePath("PicoATE.UI.exe"));
    QVERIFY(ui.open(QIODevice::WriteOnly));
    ui.write("changed runtime");
    ui.close();
    page.refresh();
    QTRY_VERIFY(!page.busy());
    QCOMPARE(page.report().files[0].status, IntegrityStatus::Modified);
    QCOMPARE(table->item(0, 2)->foreground().color(), QColor("#a43838"));
    QVERIFY(language.setChinese(true, false));
    page.resize(850, 560);
    table->selectRow(0);
    QVERIFY(approve->isEnabled());
    page.setRunActive(true);
    QVERIFY(!approve->isEnabled());
    page.setRunActive(false);
    if (!screenshots.isEmpty()) {
        QTest::qWait(50);
        QVERIFY(page.grab().save(QDir(screenshots).filePath("integrity-zh-850.png")));
    }
    bool fixedRejected = false;
    QTimer::singleShot(40, &page, [&] {
        auto* dialog = page.findChild<QDialog*>("integrityApprovalDialog");
        if (!dialog) return;
        auto* input = dialog->findChild<QLineEdit*>("integrityApprovalPassword");
        auto* reason = dialog->findChild<QLineEdit*>("integrityApprovalReason");
        auto* confirm = dialog->findChild<QPushButton*>("integrityConfirmApproval");
        if (!input || !reason || !confirm) { dialog->reject(); return; }
        input->setText("300693");
        reason->setText("Approved UI update");
        confirm->click();
        fixedRejected = dialog->isVisible() && input->text().isEmpty();
        input->setText(password);
        confirm->click();
    });
    approve->click();
    QVERIFY(fixedRejected);
    QTRY_VERIFY(!page.busy());
    QVERIFY(page.report().passed());
    QCOMPARE(page.report().baseline.value("history").toArray().last().toObject()
        .value("changes").toArray().size(), 1);
}

void MainWindowLifecycleTests::productionIntegrityBlocksUntilBaselineIsApproved()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const auto& name : RuntimeIntegrity::fileNames()) {
        QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("production integrity fixture");
    }
    const auto previousRoot = qApp->property("integrityTestRoot");
    const auto restoreRoot = qScopeGuard([&] { qApp->setProperty("integrityTestRoot", previousRoot); });
    qApp->setProperty("integrityTestRoot", dir.path());
    const bool wasChinese = UiLanguage::instance().isChinese();
    const auto restoreLanguage = qScopeGuard([&] { UiLanguage::instance().setChinese(wasChinese, false); });
    QVERIFY(UiLanguage::instance().setChinese(false, false));
    StartupSelection selection;
    selection.scanDialogEnabled = false;
    selection.sequencePath = dir.filePath("sequence.json");
    selection.stationPath = dir.filePath("StationSystem.json");
    QFile sequence(selection.sequencePath);
    QVERIFY(sequence.open(QIODevice::WriteOnly));
    sequence.write(R"({"id":"guard","name":"Guard","groups":[{"id":"main","kind":"main","steps":[{"id":"done","kind":"noop"}]}]})");
    sequence.close();
    QFile station(selection.stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    auto stationObject = StartupSupport::newProjectStationTemplate();
    stationObject.remove("pluginRegistry");
    for (const auto* field : {"scanDialogEnabled", "txtLogEnabled", "csvReportEnabled", "xlsxReportEnabled", "pdfReportEnabled"}) {
        stationObject.insert(field, false);
    }
    station.write(QJsonDocument(stationObject).toJson());
    station.close();
    auto window = createProductionWindow(selection);
    window->show();
    auto* model = window->findChild<ExecutionViewModel*>();
    auto* start = window->findChild<QAction*>("productionStartAction");
    QVERIFY(model && start);
    QTRY_COMPARE(model->state(), UiRunState::Ready);
    bool errorShown = false;
    QTimer dismiss;
    dismiss.setInterval(10);
    connect(&dismiss, &QTimer::timeout, window.get(), [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            errorShown = box->text().contains("integrity", Qt::CaseInsensitive);
            box->accept();
        }
    });
    dismiss.start();
    start->trigger();
    QTRY_COMPARE(model->state(), UiRunState::Failed);
    QTRY_VERIFY(errorShown);
    dismiss.stop();
    QVERIFY(model->report().uuts.isEmpty());
    const auto total = window->findChild<QLabel*>("productionTotalCount");
    QVERIFY(total);
    QCOMPARE(total->text(), QString("TOTAL 0"));
    QVERIFY(RuntimeIntegrity::authorize(RuntimeIntegrity::check(dir.path()), RuntimeIntegrity::fileNames(),
        AdminAccess::Supervisor, QString::number(StartupSupport::dailyAdminPassword()), "Approved test runtime").isEmpty());
    start->trigger();
    QTRY_COMPARE(model->state(), UiRunState::Completed);
    QCOMPARE(total->text(), QString("TOTAL 1"));
}

void MainWindowLifecycleTests::titleBarLanguageButtonPreservesNativeWindow()
{
    auto& language = UiLanguage::instance();
    QVERIFY(language.setChinese(false, false));
    const auto restoreLanguage = qScopeGuard([&] {
        language.setChinese(false, false);
        QSettings().remove(QStringLiteral("ui/language"));
    });
    QMainWindow window;
    auto* edit = new QLineEdit(&window);
    window.setCentralWidget(edit);
    const auto flags = window.windowFlags();
    auto* toolbar = window.addToolBar(QStringLiteral("Controls"));
    auto* button = makeLanguageButton(toolbar);
    toolbar->addWidget(button);
    window.resize(900, 540);
    window.show();
    window.activateWindow();
    QTRY_VERIFY(button->isVisible());
    QVERIFY(!button->isWindow());
    QCOMPARE(window.windowFlags(), flags);
    QCOMPARE(button->size(), QSize(40, 36));
    QVERIFY(!button->icon().isNull());
    auto* menu = button->menu();
    QVERIFY(menu);
    QCOMPARE(menu->actions().size(), 2);
    auto* chinese = window.findChild<QAction*>(QStringLiteral("uiLanguage_zh_CN"));
    auto* english = window.findChild<QAction*>(QStringLiteral("uiLanguage_en"));
    QVERIFY(chinese && english);
    QVERIFY(english->isChecked());
    edit->setText(QStringLiteral("DRAFT-1234"));
    edit->setCursorPosition(5);
    const auto frame = window.frameGeometry();
    chinese->trigger();
    QVERIFY(language.isChinese());
    QVERIFY(chinese->isChecked());
    QVERIFY(!english->isChecked());
    QCOMPARE(uiText("READY"), QString::fromUtf8("待开始"));
    QCOMPARE(edit->text(), QStringLiteral("DRAFT-1234"));
    QCOMPARE(edit->cursorPosition(), 5);
    QCOMPARE(window.frameGeometry(), frame);
    menu->popup(button->mapToGlobal(QPoint(0, button->height())));
    QTRY_VERIFY(menu->isVisible());
    QTest::keyClick(menu, Qt::Key_Escape);
    QTRY_VERIFY(!menu->isVisible());
    QVERIFY(language.isChinese());

    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&window, &deactivate);
    QCoreApplication::sendEvent(button, &deactivate);
    QVERIFY(button->isVisible());
    QDialog other;
    other.show();
    other.activateWindow();
    QTest::qWait(80);
    QVERIFY(button->isVisible());
    other.close();
    window.showMaximized();
    QTRY_VERIFY(window.isMaximized());
    QVERIFY(button->isVisible());
    QVERIFY(window.rect().contains(button->mapTo(&window, button->rect().center())));
    english->trigger();
    QVERIFY(!language.isChinese());
    QVERIFY(english->isChecked());
    QCOMPARE(edit->text(), QStringLiteral("DRAFT-1234"));
    window.showNormal();
    QVERIFY(button->isVisible());
}

void MainWindowLifecycleTests::adminDisabledSlotsRemainVisible()
{
    QSettings().clear();
    QTemporaryDir directory;
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile file(stationPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"stationId":"slot-preview","uutCount":4,"scanDialogEnabled":false,"devices":[]})");
    file.close();
    MainWindow window;
    QVERIFY(window.openSequenceFile(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR) +
                                    QStringLiteral("/examples/simple_sequence.json")));
    QVERIFY(window.openStationFile(stationPath));
    window.show();
    auto* compile = window.findChild<QAction*>(QStringLiteral("compileAction"));
    auto* slotAction = window.findChild<QAction*>(QStringLiteral("adminUutSlotsAction"));
    auto* count = window.findChild<QSpinBox*>(QStringLiteral("uutCountSpinBox"));
    QVERIFY(count);
    count->setValue(4);
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* overview = window.findChild<MultiUutOverviewWidget*>();
    QVERIFY(compile && slotAction && viewModel && overview);
    compile->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 5000);
    bool applied = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = QApplication::activeModalWidget();
        if (!dialog) return;
        auto* first = dialog->findChild<QPushButton*>(QStringLiteral("uutSlotToggle1"));
        auto* third = dialog->findChild<QPushButton*>(QStringLiteral("uutSlotToggle3"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!first || !third || !buttons) return;
        first->setChecked(false);
        third->setChecked(false);
        applied = true;
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    slotAction->trigger();
    QVERIFY(applied);
    QTRY_COMPARE(overview->model()->rowCount(), 4);
    for (int slot : {1, 3}) {
        const auto entry = overview->model()->entryAt(slot - 1);
        QVERIFY(entry && !entry->enabled);
        QCOMPARE(entry->uutId, QStringLiteral("UUT-%1").arg(slot));
        QCOMPARE(entry->state, UutOverviewState::Disabled);
        auto* card = window.findChild<QAbstractButton*>(QStringLiteral("uutOverviewCard_%1").arg(slot));
        auto* button = window.findChild<QPushButton*>(QStringLiteral("adminUutButton_%1").arg(slot));
        QVERIFY(card && button);
        QVERIFY(!card->isHidden() && !card->isEnabled());
        QTRY_VERIFY(!button->isHidden() && !button->isEnabled());
    }
    auto* run = window.findChild<QAction*>(QStringLiteral("runAction"));
    QVERIFY(run && run->isEnabled());
    run->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed, 5000);
    QCOMPARE(viewModel->report().uuts.size(), 2);
    QCOMPARE(viewModel->report().uuts.at(0).uutId, QStringLiteral("UUT-2"));
    QCOMPARE(viewModel->report().uuts.at(1).uutId, QStringLiteral("UUT-4"));
    QCOMPARE(overview->model()->rowCount(), 4);
    QCOMPARE(overview->model()->entryAt(0)->state, UutOverviewState::Disabled);
    QCOMPARE(overview->model()->entryAt(2)->state, UutOverviewState::Disabled);
    const auto screenshots = qEnvironmentVariable("PICOATE_LANGUAGE_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
        window.resize(1366, 768);
        QTest::qWait(80);
        QVERIFY(window.grab().save(QDir(screenshots).filePath(QStringLiteral("admin-disabled-slots.png"))));
    }
}

void MainWindowLifecycleTests::localizedConfigurationKeepsData()
{
    auto& language = UiLanguage::instance();
    QVERIFY(language.setChinese(false, false));
    const auto restore = qScopeGuard([&] { language.setChinese(false, false); });
    StationDocument document;
    QVERIFY(document.initializeNew(QJsonObject{
        {QStringLiteral("stationId"), QStringLiteral("BENCH")},
        {QStringLiteral("model"), QStringLiteral("M2")},
        {QStringLiteral("devices"), QJsonArray{}}
    }));
    StationSettingsEditor settings(&document);
    auto* model = settings.findChild<QLineEdit*>(QStringLiteral("stationModelEdit"));
    auto* snLength = settings.findChild<QLineEdit*>(QStringLiteral("stationSnLengthEdit"));
    QVERIFY(model && snLength);
    model->setText(QStringLiteral("DRAFT-C123"));
    model->setCursorPosition(4);
    QVERIFY(QMetaObject::invokeMethod(model, "textEdited", Q_ARG(QString, model->text())));
    QVERIFY(settings.hasPendingChanges());
    const auto snapshot = document.snapshot();
    auto* form = settings.findChild<QFormLayout*>();
    QVERIFY(form);

    PluginFunctionModel palette;
    PluginManifest plugin;
    plugin.moduleId = QStringLiteral("plugin.example");
    plugin.category = QStringLiteral("CAN");
    PluginFunctionDefinition function;
    function.id = QStringLiteral("send");
    function.name = QStringLiteral("Send Frame");
    function.description = QStringLiteral("Send a CAN frame to the selected device.");
    plugin.functions.push_back(function);
    palette.setPlugins({plugin});
    const auto basic = palette.index(0, 0);
    const auto wait = palette.index(0, 0, basic);
    const auto waitTemplate = palette.stepTemplate(wait);
    const auto pluginIndex = palette.index(0, 0, palette.index(0, 0, palette.index(1, 0)));
    const auto pluginTip = palette.data(pluginIndex, Qt::ToolTipRole);
    QVERIFY(pluginTip.toString().contains(function.description));

    QVERIFY(language.setChinese(true, false));
    auto* label = qobject_cast<QLabel*>(form->labelForField(snLength));
    QVERIFY(label);
    QCOMPARE(label->text(), QString::fromUtf8("SN 长度"));
    QCOMPARE(model->text(), QStringLiteral("DRAFT-C123"));
    QCOMPARE(model->cursorPosition(), 4);
    QCOMPARE(document.snapshot().json, snapshot.json);
    QVERIFY(settings.hasPendingChanges());
    QVERIFY(palette.data(wait, Qt::ToolTipRole).toString().contains(QString::fromUtf8("等待")));
    QCOMPARE(palette.stepTemplate(wait), waitTemplate);
    QCOMPARE(palette.data(pluginIndex, Qt::ToolTipRole), pluginTip);
    for (int row = 0; row < palette.rowCount(basic); ++row) {
        const auto item = palette.index(row, 0, basic);
        if (palette.rowCount(item) == 0) {
            QVERIFY(!palette.data(item, Qt::ToolTipRole).toString().contains("Drag to"));
        } else {
            for (int child = 0; child < palette.rowCount(item); ++child) {
                const auto tip = palette.data(palette.index(child, 0, item), Qt::ToolTipRole).toString();
                QVERIFY2(tip.contains(QRegularExpression(QStringLiteral("[\\x{4e00}-\\x{9fff}]"))), qPrintable(tip));
            }
        }
    }
    QTemporaryDir directory;
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(QJsonDocument(document.rootObject()).toJson());
    station.close();
    FieldDeviceDialog devices(stationPath);
    QCOMPARE(devices.windowTitle(), QString::fromUtf8("设备资源配置"));
    QCOMPARE(devices.findChild<QPushButton*>(QStringLiteral("fieldSaveAllButton"))->text(),
             QString::fromUtf8("全部保存"));
    ProductRoutingDialog routing(directory.filePath(QStringLiteral("ProductRouting.json")));
    auto* table = routing.findChild<QTableWidget*>();
    QVERIFY(table);
    QCOMPARE(table->horizontalHeaderItem(2)->text(), QString::fromUtf8("SN 规则"));
    QCOMPARE(routing.findChild<QPushButton*>(QStringLiteral("productRoutingSaveButton"))->text(),
             uiText("Save"));
    const auto screenshots = qEnvironmentVariable("PICOATE_LANGUAGE_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
        settings.resize(340, 900);
        settings.show();
        QTest::qWait(30);
        QVERIFY(settings.grab().save(QDir(screenshots).filePath(QStringLiteral("station-basic-zh.png"))));
        routing.show();
        QTest::qWait(30);
        QVERIFY(routing.grab().save(QDir(screenshots).filePath(QStringLiteral("product-routing-zh.png"))));
        routing.hide();
        devices.show();
        QTest::qWait(30);
        QVERIFY(devices.grab().save(QDir(screenshots).filePath(QStringLiteral("field-devices-zh.png"))));
    }
    QVERIFY(language.setChinese(false, false));
    QCOMPARE(label->text(), QStringLiteral("SN Length"));
    QCOMPARE(palette.stepTemplate(wait), waitTemplate);
    QCOMPARE(document.snapshot().json, snapshot.json);
}

void MainWindowLifecycleTests::smallScreenRunInfoRemainsReadable()
{
    QSettings().clear();
    auto& language = UiLanguage::instance();
    const auto restore = qScopeGuard([&] { language.setChinese(false, false); });
    QTemporaryDir directory;
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile file(stationPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"stationId":"ATE-07","model":"M2","customerId":"C1234567","scanDialogEnabled":false,"devices":[]})");
    file.close();
    const auto sequencePath = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR) +
                              QStringLiteral("/examples/simple_sequence.json");
    for (bool admin : {true, false}) {
        std::unique_ptr<QMainWindow> window;
        if (admin) {
            auto instance = std::make_unique<MainWindow>();
            QVERIFY(instance->openSequenceFile(sequencePath));
            QVERIFY(instance->openStationFile(stationPath));
            instance->showRunPage();
            window = std::move(instance);
        } else {
            StartupSelection selection;
            selection.mode = UiMode::Test;
            selection.sequencePath = sequencePath;
            selection.stationPath = stationPath;
            selection.scanDialogEnabled = false;
            window = std::make_unique<ProductionWindow>(selection);
        }
        window->resize(1100, 680);
        window->show();
        QTest::qWait(200);
        const QString prefix = admin ? QStringLiteral("admin") : QStringLiteral("production");
        auto* sn = window->findChild<QLabel*>(prefix + QStringLiteral("SerialLabel"));
        auto* status = window->findChild<QLabel*>(prefix + QStringLiteral("OverallResult"));
        auto* stationLabel = window->findChild<QLabel*>(prefix + QStringLiteral("StationLabel"));
        auto* elapsed = window->findChild<QLabel*>(prefix + QStringLiteral("ElapsedLabel"));
        auto* button = window->findChild<QToolButton*>(QStringLiteral("uiLanguageButton"));
        QVERIFY(sn && status && elapsed && button && stationLabel);
        for (bool chinese : {false, true}) {
            QVERIFY(language.setChinese(chinese, false));
            for (const int length : {22, 24, 32}) {
                const QString serial = QString(length - 4, QLatin1Char('8')) + QStringLiteral("LAST");
                sn->setText(serial);
                QTest::qWait(40);
                QCOMPARE(sn->text(), serial);
                auto* displayed = dynamic_cast<ElidedInfoLabel*>(sn);
                QVERIFY(displayed);
                if (length > 24 || sn->fontMetrics().horizontalAdvance(serial) >
                                      sn->contentsRect().width() - 2) {
                    QVERIFY(displayed->displayText().endsWith(QChar(0x2026)));
                    QVERIFY(displayed->displayText().size() <= 25);
                } else {
                    QCOMPARE(displayed->displayText(), serial);
                }
                QHelpEvent hover(QEvent::ToolTip, QPoint(4, 4), sn->mapToGlobal(QPoint(4, 4)));
                QCoreApplication::sendEvent(sn, &hover);
                QCOMPARE(QToolTip::text(), serial);
                QToolTip::hideText();
                QVERIFY(sn->isVisible());
                QVERIFY(sn->height() >= sn->fontMetrics().height());
                const auto valueLeft = sn->mapTo(window.get(), QPoint()).x();
                QCOMPARE(valueLeft, stationLabel->mapTo(window.get(), QPoint()).x());
                for (const auto& field : {QStringLiteral("ModelLabel"), QStringLiteral("CustomerIdLabel")}) {
                    auto* other = window->findChild<QLabel*>(prefix + field);
                    QVERIFY(other);
                    QCOMPARE(valueLeft, other->mapTo(window.get(), QPoint()).x());
                }
                auto* form = window->findChild<QFormLayout*>(prefix + QStringLiteral("RunInfoForm"));
                QVERIFY(form);
                auto* caption = form->labelForField(sn);
                QVERIFY(caption);
                int labelRow = -1;
                int valueRow = -1;
                QFormLayout::ItemRole labelRole;
                QFormLayout::ItemRole valueRole;
                form->getWidgetPosition(caption, &labelRow, &labelRole);
                form->getWidgetPosition(sn, &valueRow, &valueRole);
                QCOMPARE(labelRow, valueRow);
                QCOMPARE(labelRole, QFormLayout::LabelRole);
                QCOMPARE(valueRole, QFormLayout::FieldRole);
                QCOMPARE(form->rowWrapPolicy(), QFormLayout::DontWrapRows);
                const int snBottom = sn->mapTo(window.get(), sn->rect().bottomLeft()).y();
                QVERIFY(snBottom < status->mapTo(window.get(), QPoint()).y());
                QVERIFY(snBottom < stationLabel->mapTo(window.get(), QPoint()).y());
                QVERIFY(status->geometry().bottom() <= elapsed->geometry().top());
            }
            const auto screenshots = qEnvironmentVariable("PICOATE_LANGUAGE_SCREENSHOTS");
            if (!screenshots.isEmpty()) {
                QVERIFY(window->grab().save(QDir(screenshots).filePath(
                    prefix + (chinese ? QStringLiteral("-small-zh.png") : QStringLiteral("-small-en.png")))));
            }
        }
        QVERIFY(button->isVisible());
        QVERIFY(window->rect().contains(button->mapTo(window.get(), button->rect().center())));
        QVERIFY(button->mapTo(window.get(), QPoint()).x() > window->width() - 100);
        QVERIFY(window->close());
    }
}

void MainWindowLifecycleTests::stationCapacityLimitsToolbar_data()
{
    QTest::addColumn<bool>("admin");
    QTest::addColumn<int>("maximum");
    QTest::newRow("admin-four") << true << 4;
    QTest::newRow("test-four") << false << 4;
    QTest::newRow("admin-single") << true << 1;
    QTest::newRow("test-single") << false << 1;
}

void MainWindowLifecycleTests::stationCapacityLimitsToolbar()
{
    QFETCH(bool, admin);
    QFETCH(int, maximum);
    QSettings().clear();
    QSettings().setValue(QStringLiteral("MainWindow/UutCount"), 63);
    QTemporaryDir directory;
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile file(stationPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{
        {"stationId", "CAPACITY"}, {"uutCount", maximum},
        {"scanDialogEnabled", false}, {"devices", QJsonArray{}}
    }).toJson());
    file.close();
    const auto sequencePath = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR) +
                              QStringLiteral("/examples/simple_sequence.json");
    for (int opening = 0; opening < 2; ++opening) {
        std::unique_ptr<QMainWindow> window;
        if (admin) {
            auto instance = std::make_unique<MainWindow>();
            QVERIFY(instance->openSequenceFile(sequencePath));
            QVERIFY(instance->openStationFile(stationPath));
            window = std::move(instance);
        } else {
            StartupSelection selection;
            selection.mode = UiMode::Test;
            selection.sequencePath = sequencePath;
            selection.stationPath = stationPath;
            selection.scanDialogEnabled = false;
            window = std::make_unique<ProductionWindow>(selection);
        }
        window->resize(1280, 800);
        window->show();
        auto* count = window->findChild<QSpinBox*>(admin
            ? QStringLiteral("uutCountSpinBox") : QStringLiteral("productionUutCountSpinBox"));
        auto* slotAction = window->findChild<QAction*>(admin
            ? QStringLiteral("adminUutSlotsAction") : QStringLiteral("productionUutSlotsAction"));
        auto* scan = window->findChild<ScanDialog*>();
        QVERIFY(count && slotAction && scan);
        QCOMPARE(count->maximum(), maximum);
        QCOMPARE(count->value(), maximum);
        QTRY_COMPARE(!count->isHidden(), maximum > 1);
        QCOMPARE(slotAction->isVisible(), maximum > 1);
        count->setValue(64);
        QCOMPARE(count->value(), maximum);
        if (maximum > 1) {
            count->setValue(2);
            QCOMPARE(count->maximum(), maximum);
            QCOMPARE(scan->slotCount(), 2);
            count->setValue(1);
            QVERIFY(!count->isHidden());
            QVERIFY(!slotAction->isVisible());
            count->setValue(2);
        }
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("uutCount").toInt(), maximum);
        file.close();
        if (admin) {
            auto* document = window->findChild<StationDocument*>();
            QVERIFY(document);
            QVERIFY(document->setRootValue(QStringLiteral("model"), QStringLiteral("M2")));
            QCOMPARE(count->value(), maximum > 1 ? 2 : 1);
            if (opening == 1) {
                if (maximum > 1) QVERIFY(document->setRootValue(QStringLiteral("uutCount"), 1));
                QCOMPARE(count->maximum(), 1);
                QCOMPARE(count->value(), 1);
                QTRY_VERIFY(count->isHidden());
                QVERIFY(!slotAction->isVisible());
                QVERIFY(document->setRootValue(QStringLiteral("uutCount"), 3));
                QCOMPARE(count->maximum(), 3);
                QCOMPARE(count->value(), 3);
                QTRY_VERIFY(!count->isHidden());
            }
            document->undoStack()->setClean();
        }
        QVERIFY(window->close());
    }
    PicoATE::Core::ProductBatchRouteResolution routed;
    routed.uutCount = maximum;
    routed.route.stationPath = stationPath;
    QCOMPARE(routedUutCount(routed, stationPath, 2), qMin(2, maximum));
    QCOMPARE(routedUutCount(routed, directory.filePath("other.json"), 1), maximum);
    QCOMPARE(routedUutCount(routed, stationPath, 64), maximum);
}

void MainWindowLifecycleTests::scannerYieldsToModalWindows()
{
    QMainWindow owner;
    owner.resize(900, 650);
    owner.show();
    ScanDialog scanner(&owner);
    scanner.setSlotCount(2);
    scanner.showForNextScan();
    auto* edit = scanner.findChild<QLineEdit*>(QStringLiteral("barcodeEdit"));
    QVERIFY(edit);
    QVERIFY(!(scanner.windowFlags() & Qt::WindowStaysOnTopHint));
    edit->setText(QStringLiteral("SN0001"));
    QVERIFY(QMetaObject::invokeMethod(&scanner, "submitBarcode"));
    edit->setText(QStringLiteral("DRAFT22"));
    edit->setSelection(1, 3);
    const auto barcodes = scanner.barcodes();

    QDialog outer(&owner);
    outer.setWindowModality(Qt::WindowModal);
    outer.show();
    QTRY_VERIFY(!scanner.isVisible());
    QVERIFY(scanner.isScanRequested());
    QDialog inner(&outer);
    inner.setWindowModality(Qt::WindowModal);
    inner.show();
    inner.accept();
    QTest::qWait(30);
    QVERIFY(!scanner.isVisible());
    QCOMPARE(scanner.barcodes(), barcodes);
    QCOMPARE(edit->text(), QStringLiteral("DRAFT22"));
    scanner.showForNextScan();
    QVERIFY(!scanner.isVisible());
    QCOMPARE(edit->selectedText(), QStringLiteral("RAF"));
    outer.accept();
    QTRY_VERIFY(scanner.isVisible());
    QCOMPARE(edit->text(), QStringLiteral("DRAFT22"));
    QCOMPARE(edit->selectedText(), QStringLiteral("RAF"));
    QCOMPARE(scanner.barcodes(), barcodes);

    auto* destroyedDialog = new QDialog(&owner);
    destroyedDialog->setWindowModality(Qt::ApplicationModal);
    destroyedDialog->show();
    QTRY_VERIFY(!scanner.isVisible());
    delete destroyedDialog;
    QTRY_VERIFY(scanner.isVisible());

    QFileDialog fileDialog(&owner);
    fileDialog.setWindowModality(Qt::WindowModal);
    const auto fileTitle = QStringLiteral("PicoATE native dialog test %1")
        .arg(QCoreApplication::applicationPid());
    fileDialog.setWindowTitle(fileTitle);
    fileDialog.setDirectory(QDir::tempPath());
    fileDialog.open();
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        QTRY_VERIFY_WITH_TIMEOUT(IsWindowVisible(FindWindowW(nullptr,
            reinterpret_cast<LPCWSTR>(fileTitle.utf16()))), 5000);
    }
#endif
    QTRY_VERIFY(!scanner.isVisible());
    fileDialog.reject();
    QTRY_VERIFY(scanner.isVisible());
    QCOMPARE(edit->text(), QStringLiteral("DRAFT22"));

    QEvent blocked(QEvent::WindowBlocked);
    QCoreApplication::sendEvent(owner.windowHandle(), &blocked);
    QTRY_VERIFY(!scanner.isVisible());
    QEvent unblocked(QEvent::WindowUnblocked);
    QCoreApplication::sendEvent(owner.windowHandle(), &unblocked);
    QTRY_VERIFY(scanner.isVisible());

    outer.show();
    QTRY_VERIFY(!scanner.isVisible());
    scanner.cancelCurrentScan();
    outer.accept();
    QTest::qWait(60);
    QVERIFY(!scanner.isVisible());
    QVERIFY(!scanner.isScanRequested());
    outer.show();
    outer.accept();
    QTest::qWait(60);
    QVERIFY(!scanner.isVisible());

    outer.show();
    scanner.showForNextScan();
    QVERIFY(!scanner.isVisible());
    outer.accept();
    QTRY_VERIFY(scanner.isVisible());
    scanner.hide();
}

void MainWindowLifecycleTests::stationSavePromptSuspendsScanner()
{
    QSettings().clear();
    QTemporaryDir directory;
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile file(stationPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"stationId":"MODAL-SAVE","uutCount":2,"scanDialogEnabled":true,"devices":[]})");
    file.close();
    MainWindow window;
    QVERIFY(window.openSequenceFile(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR) +
                                    QStringLiteral("/examples/simple_sequence.json")));
    QVERIFY(window.openStationFile(stationPath));
    window.show();
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    window.findChild<QAction*>(QStringLiteral("compileAction"))->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 5000);
    window.findChild<QAction*>(QStringLiteral("adminScanAction"))->trigger();
    auto* scanner = window.findChild<ScanDialog*>();
    auto* edit = scanner->findChild<QLineEdit*>(QStringLiteral("barcodeEdit"));
    QTRY_VERIFY(scanner->isVisible());
    edit->setText(QStringLiteral("SN0001"));
    QVERIFY(QMetaObject::invokeMethod(scanner, "submitBarcode"));
    edit->setText(QStringLiteral("UNSAVED-SN"));
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    tabs->setCurrentIndex(2);
    auto* model = window.findChild<QLineEdit*>(QStringLiteral("stationModelEdit"));
    QVERIFY(model);
    model->setText(QStringLiteral("NEW-MODEL"));
    QVERIFY(QMetaObject::invokeMethod(model, "textEdited", Q_ARG(QString, model->text())));
    auto* save = window.findChild<QAction*>(QStringLiteral("saveStationAction"));
    QVERIFY(save && save->isEnabled());
    for (bool accept : {false, true}) {
        bool observed = false;
        bool hidden = false;
        QTimer answer;
        answer.setInterval(10);
        connect(&answer, &QTimer::timeout, &window, [&] {
            auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!prompt) return;
            observed = true;
            hidden = !scanner->isVisible();
            answer.stop();
            prompt->button(accept ? QMessageBox::Save : QMessageBox::Cancel)->click();
        });
        answer.start();
        save->trigger();
        answer.stop();
        QVERIFY(observed);
        QVERIFY(hidden);
        QTRY_VERIFY(scanner->isVisible());
        QCOMPARE(edit->text(), QStringLiteral("UNSAVED-SN"));
        QCOMPARE(scanner->barcodes().first(), QStringLiteral("SN0001"));
    }
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("model").toString(),
             QStringLiteral("NEW-MODEL"));
    file.close();
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::maximizedRunInformationKeepsFullTextHeight()
{
    QSettings().clear();
    auto& language = UiLanguage::instance();
    const auto restoreLanguage = qScopeGuard([&] { language.setChinese(false, false); });
    QSettings().setValue(QStringLiteral("MainWindow/ResponsiveLayoutMode"), 1);
    QTemporaryDir directory;
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile file(stationPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"stationId":"HEIGHT","uutCount":1,"scanDialogEnabled":false,"devices":[]})");
    file.close();
    const auto sequencePath = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR) +
                              QStringLiteral("/examples/simple_sequence.json");
    for (bool admin : {true, false}) {
        std::unique_ptr<QMainWindow> window;
        if (admin) {
            auto instance = std::make_unique<MainWindow>();
            QVERIFY(instance->openSequenceFile(sequencePath));
            QVERIFY(instance->openStationFile(stationPath));
            instance->showRunPage();
            window = std::move(instance);
        } else {
            StartupSelection selection;
            selection.mode = UiMode::Test;
            selection.sequencePath = sequencePath;
            selection.stationPath = stationPath;
            selection.scanDialogEnabled = false;
            window = std::make_unique<ProductionWindow>(selection);
        }
        window->show();
        auto* sidebar = window->findChild<QScrollArea*>(admin
            ? QStringLiteral("adminRunSidebar") : QStringLiteral("productionSidebar"));
        QVERIFY(sidebar && sidebar->widget());
        for (int state = 0; state < 4; ++state) {
            QVERIFY(language.setChinese(state != 0, false));
            if (state == 0) window->resize(1500, 800);
            if (state == 1) window->showMaximized();
            if (state == 2) window->showFullScreen();
            if (state == 3) { window->showNormal(); window->resize(1000, 650); }
            QTest::qWait(160);
            const auto labels = sidebar->findChildren<QLabel*>();
            int checked = 0;
            for (auto* label : labels) {
                if (!label->property("runInfoValue").toBool()) continue;
                ++checked;
                QVERIFY2(label->contentsRect().height() >= label->fontMetrics().height(),
                         qPrintable(label->objectName()));
                QVERIFY(!label->wordWrap());
            }
            QCOMPARE(checked, 7);
            QVERIFY(sidebar->widget()->height() >= sidebar->widget()->layout()->minimumSize().height());
            const auto screenshots = qEnvironmentVariable("PICOATE_LANGUAGE_SCREENSHOTS");
            if (!screenshots.isEmpty()) {
                QVERIFY(window->grab().save(QDir(screenshots).filePath(
                    QStringLiteral("%1-height-%2.png").arg(admin ? "admin" : "test").arg(state))));
            }
        }
        QVERIFY(window->close());
    }
}

void MainWindowLifecycleTests::uutSlotsSuspendScanner_data()
{
    QTest::addColumn<bool>("admin");
    QTest::addColumn<bool>("accept");
    QTest::addColumn<bool>("scannerVisible");
    QTest::newRow("admin-apply") << true << true << true;
    QTest::newRow("admin-cancel") << true << false << true;
    QTest::newRow("admin-hidden") << true << false << false;
    QTest::newRow("test-apply") << false << true << true;
    QTest::newRow("test-cancel") << false << false << true;
    QTest::newRow("test-hidden") << false << false << false;
}

void MainWindowLifecycleTests::uutSlotsSuspendScanner()
{
    QFETCH(bool, admin);
    QFETCH(bool, accept);
    QFETCH(bool, scannerVisible);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                       + QStringLiteral("/examples/simple_sequence.json"), sequencePath));
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({"stationId":"SLOT-SCAN","uutCount":4,"scanDialogEnabled":true,"devices":[]})");
    station.close();
    std::unique_ptr<QMainWindow> window;
    if (admin) {
        auto main = std::make_unique<MainWindow>();
        QVERIFY(main->openSequenceFile(sequencePath));
        QVERIFY(main->openStationFile(stationPath));
        window = std::move(main);
    } else {
        StartupSelection selection;
        selection.mode = UiMode::Test;
        selection.sequencePath = sequencePath;
        selection.stationPath = stationPath;
        selection.scanDialogEnabled = true;
        window = std::make_unique<ProductionWindow>(selection);
    }
    window->show();
    auto* viewModel = window->findChild<ExecutionViewModel*>();
    auto* scan = window->findChild<ScanDialog*>();
    auto* count = window->findChild<QSpinBox*>(admin ? QStringLiteral("uutCountSpinBox")
                                       : QStringLiteral("productionUutCountSpinBox"));
    auto* slotAction = window->findChild<QAction*>(admin ? QStringLiteral("adminUutSlotsAction")
                                       : QStringLiteral("productionUutSlotsAction"));
    QVERIFY(viewModel && scan && count && slotAction);
    count->setValue(4);
    if (admin) viewModel->compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 5000);
    QTest::qWait(80);
    scan->setSlotCount(4);
    scan->showForNextScan();
    auto* edit = scan->findChild<QLineEdit*>(QStringLiteral("barcodeEdit"));
    QVERIFY(edit);
    edit->setText(QStringLiteral("SN-0001"));
    QVERIFY(QMetaObject::invokeMethod(scan, "submitBarcode"));
    QCOMPARE(scan->barcodes().at(0), QStringLiteral("SN-0001"));
    edit->setText(QStringLiteral("SN-DRAFT"));
    if (!scannerVisible) scan->hide();
    bool opened = false;
    bool stayedHidden = false;
    QTimer::singleShot(0, window.get(), [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        opened = dialog->objectName() == QStringLiteral("uutSlotConfigurationDialog");
        stayedHidden = !scan->isVisible();
        // A late ready notification must not surface the scanner over Slots.
        if (!admin && scannerVisible) viewModel->stateChanged(UiRunState::Ready);
        QTimer::singleShot(50, dialog, [&, dialog] {
            stayedHidden = stayedHidden && !scan->isVisible();
            if (accept) {
                auto* third = dialog->findChild<QPushButton*>(QStringLiteral("uutSlotToggle3"));
                if (third) third->click();
                dialog->accept();
            } else {
                dialog->reject();
            }
        });
    });
    slotAction->trigger();
    QVERIFY(opened);
    QVERIFY(stayedHidden);
    QCOMPARE(scan->isVisible(), scannerVisible);
    QCOMPARE(scan->barcodes().at(0), QStringLiteral("SN-0001"));
    QCOMPARE(scan->slotEnabledStates(), (QVector<bool>{true, true, !accept, true}));
    if (!accept) QCOMPARE(edit->text(), QStringLiteral("SN-DRAFT"));
    if (scannerVisible) QTRY_VERIFY(edit->hasFocus());
    scan->hide();
    QVERIFY(window->close());
}

void MainWindowLifecycleTests::languageSwitchPreservesAdminDraftAndSelection()
{
    auto& language = UiLanguage::instance();
    QVERIFY(language.setChinese(false, false));
    const auto restore = qScopeGuard([&] {
        language.setChinese(false, false);
        QSettings().remove(QStringLiteral("ui/language"));
    });
    MainWindow window;
    QVERIFY(window.openSequenceFile(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                                    + QStringLiteral("/examples/simple_sequence.json")));
    window.resize(1366, 768);
    window.show();
    auto* button = window.findChild<QToolButton*>(QStringLiteral("uiLanguageButton"));
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* document = window.findChild<SequenceDocument*>();
    auto* model = window.findChild<SequenceTreeModel*>();
    auto* editor = window.findChild<StepPropertyEditor*>();
    auto* compile = window.findChild<QAction*>(QStringLiteral("compileAction"));
    QVERIFY(button && tabs && tree && document && model && editor && compile);
    tabs->setCurrentIndex(1);
    const auto parent = model->index(1, 0);
    const QPersistentModelIndex step = model->index(0, 0, parent);
    tree->expand(parent);
    tree->setCurrentIndex(step);
    auto* name = editor->findChild<QLineEdit*>(QStringLiteral("propertyNameEdit"));
    QVERIFY(name);
    name->setText(QStringLiteral("Waiting"));
    name->setCursorPosition(3);
    QVERIFY(editor->hasPendingChanges());
    const auto before = document->snapshot();
    QSignalSpy changed(document, &SequenceDocument::documentChanged);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    button->menu()->actions().at(language.isChinese() ? 1 : 0)->trigger();
    QTRY_COMPARE(tabs->tabText(0), QString::fromUtf8("运行界面"));
    QCOMPARE(tabs->tabText(1), QString::fromUtf8("流程配置"));
    QCOMPARE(tabs->tabText(2), QString::fromUtf8("工站配置"));
    QCOMPARE(compile->text(), QString::fromUtf8("编译"));
    QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(tree->currentIndex(), QModelIndex(step));
    QVERIFY(tree->isExpanded(parent));
    QCOMPARE(name->text(), QStringLiteral("Waiting"));
    QCOMPARE(name->cursorPosition(), 3);
    QVERIFY(editor->hasPendingChanges());
    QCOMPARE(document->snapshot().json, before.json);
    QCOMPARE(document->revision(), before.revision);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(QSettings().value(QStringLiteral("ui/language")).toString(), QStringLiteral("zh_CN"));
    QVERIFY(language.setChinese(false, false));
    language.restorePreference();
    QVERIFY(language.isChinese());
    const auto screenshotRoot = qEnvironmentVariable("PICOATE_LANGUAGE_SCREENSHOTS");
    if (!screenshotRoot.isEmpty()) {
        QVERIFY(window.grab().save(QDir(screenshotRoot).filePath(QStringLiteral("admin-flow-zh.png"))));
    }
    button->menu()->actions().at(language.isChinese() ? 1 : 0)->trigger();
    QTRY_COMPARE(tabs->tabText(0), QStringLiteral("Run Test"));
    QCOMPARE(compile->text(), QStringLiteral("Compile"));
    QCOMPARE(name->text(), QStringLiteral("Waiting"));
    QCOMPARE(document->snapshot().json, before.json);
    QCOMPARE(reset.count(), 0);
}

void MainWindowLifecycleTests::languageSwitchPreservesRunningProductionAndScanner()
{
    auto& language = UiLanguage::instance();
    QVERIFY(language.setChinese(false, false));
    const auto restore = qScopeGuard([&] {
        language.setChinese(false, false);
        QSettings().remove(QStringLiteral("ui/language"));
    });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("language_sequence.json"));
    QFile sequence(sequencePath);
    QVERIFY(sequence.open(QIODevice::WriteOnly));
    sequence.write(R"({"id":"language","name":"Language","version":"1","groups":[
        {"id":"setup","kind":"setup","steps":[]},
        {"id":"main","kind":"main","steps":[{"id":"wait","name":"Waiting","kind":"wait","ms":1500}]},
        {"id":"cleanup","kind":"cleanup","steps":[]}]})");
    sequence.close();
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({"stationId":"LANG-STATION","uutCount":2,"model":"M2","scanDialogEnabled":false,"devices":[]})");
    station.close();
    StartupSelection selection;
    selection.mode = UiMode::Test;
    selection.sequencePath = sequencePath;
    selection.stationPath = stationPath;
    selection.scanDialogEnabled = false;
    ProductionWindow window(selection);
    window.resize(1366, 768);
    window.show();
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* button = window.findChild<QToolButton*>(QStringLiteral("uiLanguageButton"));
    auto* count = window.findChild<QSpinBox*>(QStringLiteral("productionUutCountSpinBox"));
    auto* start = window.findChild<QAction*>(QStringLiteral("productionStartAction"));
    auto* overall = window.findChild<QLabel*>(QStringLiteral("productionOverallResult"));
    auto* scan = window.findChild<ScanDialog*>();
    QVERIFY(viewModel && button && count && start && overall && scan);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 5000);
    count->setValue(2);
    auto* edit = scan->findChild<QLineEdit*>();
    QVERIFY(edit);
    edit->setText(QStringLiteral("BTSN00001234"));
    edit->setSelection(4, 3);
    button->menu()->actions().at(language.isChinese() ? 1 : 0)->trigger();
    QTRY_COMPARE(start->text(), QString::fromUtf8("开始测试"));
    QCOMPARE(edit->text(), QStringLiteral("BTSN00001234"));
    QCOMPARE(edit->selectedText(), QStringLiteral("000"));
    QVERIFY(!scan->isVisible());
    QCOMPARE(uiText("address"), QStringLiteral("address"));
    QCOMPARE(uiText("canId"), QStringLiteral("canId"));
    QCOMPARE(uiText("frame"), QStringLiteral("frame"));
    start->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Running, 3000);
    button->menu()->actions().at(language.isChinese() ? 1 : 0)->trigger();
    QTRY_COMPARE(overall->text(), QStringLiteral("RUNNING"));
    QCOMPARE(viewModel->state(), UiRunState::Running);
    button->menu()->actions().at(language.isChinese() ? 1 : 0)->trigger();
    QTRY_COMPARE(overall->text(), QString::fromUtf8("运行中"));
    const auto screenshotRoot = qEnvironmentVariable("PICOATE_LANGUAGE_SCREENSHOTS");
    if (!screenshotRoot.isEmpty()) {
        QTest::qWait(60);
        QVERIFY(window.grab().save(QDir(screenshotRoot).filePath(QStringLiteral("test-overview-zh.png"))));
    }
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Completed, 5000);
    QTRY_COMPARE(overall->text(), QString::fromUtf8("成功"));
    const auto report = PicoATE::Core::serializeExecutionReport(viewModel->report());
    QCOMPARE(viewModel->report().uuts.size(), 2);
    button->menu()->actions().at(language.isChinese() ? 1 : 0)->trigger();
    QTRY_COMPARE(overall->text(), QStringLiteral("PASS"));
    QCOMPARE(PicoATE::Core::serializeExecutionReport(viewModel->report()), report);
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::languageSwitchRefreshesModelsWithoutChangingReports()
{
    using namespace PicoATE::Core;
    auto& language = UiLanguage::instance();
    QVERIFY(language.setChinese(false, false));
    const auto restore = qScopeGuard([&] { language.setChinese(false, false); });
    DiagnosticModel diagnostics;
    DeviceStatusModel devices;
    AttemptModel attempts;
    MeasurementModel measurements;
    HistoryModel history;
    const QVector<QAbstractItemModel*> tables = {&diagnostics, &devices, &attempts, &measurements, &history};
    const QStringList english = {QStringLiteral("Severity"), QStringLiteral("Device"),
        QStringLiteral("Attempt"), QStringLiteral("Measurement"), QStringLiteral("Saved")};
    const QStringList chinese = {QString::fromUtf8("级别"), QString::fromUtf8("设备"),
        QString::fromUtf8("执行次数"), QString::fromUtf8("测量项"), QString::fromUtf8("保存时间")};
    for (int i = 0; i < tables.size(); ++i) {
        QCOMPARE(tables[i]->headerData(0, Qt::Horizontal).toString(), english[i]);
    }
    StepReport step;
    step.stepId = QStringLiteral("canId");
    step.nodePath = QStringLiteral("main.canId");
    step.displayName = QStringLiteral("Waiting");
    step.phase = ExecutionPhase::Main;
    step.state = ActivationState::Passed;
    step.outcome = NodeOutcome::Passed;
    UutReport uut;
    uut.uutId = QStringLiteral("UUT-1");
    uut.steps = {step};
    ExecutionReport report;
    report.uuts = {uut};
    report.completed = true;
    report.state = ExecutionState::Completed;
    UutStepModel model;
    model.setReport(report);
    const QPersistentModelIndex index = model.indexForStep(uut.uutId, step.nodePath);
    QVERIFY(index.isValid());
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QTemporaryDir directory;
    const auto englishCsv = directory.filePath(QStringLiteral("en.csv"));
    const auto chineseCsv = directory.filePath(QStringLiteral("zh.csv"));
    QVERIFY(ReportExporter::saveCsv(englishCsv, report).success);
    QVERIFY(language.setChinese(true, false));
    for (int i = 0; i < tables.size(); ++i) {
        QCOMPARE(tables[i]->headerData(0, Qt::Horizontal).toString(), chinese[i]);
    }
    QCOMPARE(index.data().toString(), QStringLiteral("Waiting"));
    QCOMPARE(QModelIndex(index).siblingAtColumn(UutStepModel::OutcomeColumn).data().toString(), QString::fromUtf8("成功"));
    QCOMPARE(reset.count(), 0);
    QVERIFY(changed.count() > 0);
    QVERIFY(ReportExporter::saveCsv(chineseCsv, report).success);
    QFile before(englishCsv), after(chineseCsv);
    QVERIFY(before.open(QIODevice::ReadOnly) && after.open(QIODevice::ReadOnly));
    QCOMPARE(before.readAll(), after.readAll());
    QVERIFY(language.setChinese(false, false));
    for (int i = 0; i < tables.size(); ++i) {
        QCOMPARE(tables[i]->headerData(0, Qt::Horizontal).toString(), english[i]);
    }
    QCOMPARE(QModelIndex(index).siblingAtColumn(UutStepModel::OutcomeColumn).data().toString(), QStringLiteral("Passed"));
    QCOMPARE(reset.count(), 0);
}

void MainWindowLifecycleTests::languageSwitchPreservesPromptInputAndResponse()
{
    using namespace PicoATE::Core;
    auto& language = UiLanguage::instance();
    QVERIFY(language.setChinese(false, false));
    const auto restore = qScopeGuard([&] { language.setChinese(false, false); });
    MultiUutOverviewWidget overview;
    UutOverviewModel model;
    RunRequest::UutInput uut;
    uut.uutId = QStringLiteral("UUT-1");
    model.resetForRun({}, {uut});
    overview.setModel(&model);
    overview.resize(940, 640);
    overview.show();
    RuntimeEvent prompt;
    prompt.kind = RuntimeEventKind::OperatorPromptRequested;
    prompt.uutId = uut.uutId;
    prompt.details = {
        {QStringLiteral("promptInstanceId"), QStringLiteral("language-input")},
        {QStringLiteral("mode"), QStringLiteral("input")},
        {QStringLiteral("inputType"), QStringLiteral("number")},
        {QStringLiteral("message"), QStringLiteral("Waiting")},
        {QStringLiteral("executionScope"), QStringLiteral("OncePerBatch")}
    };
    QVERIFY(overview.presentOperatorPrompt(prompt, {}));
    auto* overlay = overview.findChild<QWidget*>(QStringLiteral("multiUutOverviewPromptOverlay"));
    QVERIFY(overlay);
    auto* input = overlay->findChild<QLineEdit*>(QStringLiteral("uutOverviewPromptInput"));
    auto* submit = overlay->findChild<QPushButton*>(QStringLiteral("uutOverviewPromptConfirmButton"));
    auto* error = overlay->findChild<QLabel*>(QStringLiteral("uutOverviewPromptInputError"));
    auto* context = overlay->findChild<QLabel*>(QStringLiteral("uutOverviewPromptContext"));
    QVERIFY(input && submit && error && context);
    input->setText(QStringLiteral("invalid"));
    submit->click();
    QVERIFY(!error->isHidden());
    QVERIFY(language.setChinese(true, false));
    QCoreApplication::processEvents();
    QTRY_COMPARE(submit->text(), QString::fromUtf8("提交"));
    QCOMPARE(error->text(), uiText("Enter a valid number."));
    QCOMPARE(context->text(), uiText("ALL %1 UUTs  |  ONCE PER BATCH").arg(1));
    QCOMPARE(overlay->findChild<QLabel*>(QStringLiteral("uutOverviewPromptMessage"))->text(),
             QStringLiteral("Waiting"));
    input->setText(QStringLiteral("12.345"));
    input->setSelection(3, 2);
    QVERIFY(language.setChinese(false, false));
    QCoreApplication::processEvents();
    QTRY_COMPARE(submit->text(), QStringLiteral("Submit"));
    QCOMPARE(input->text(), QStringLiteral("12.345"));
    QCOMPARE(input->selectedText(), QStringLiteral("34"));
    QCOMPARE(context->text(), QStringLiteral("ALL 1 UUTs  |  ONCE PER BATCH"));
    QSignalSpy response(&overview, &MultiUutOverviewWidget::operatorPromptResponseRequested);
    submit->click();
    QCOMPARE(response.count(), 1);
    const auto arguments = response.takeFirst();
    QCOMPARE(arguments[0].toString(), QStringLiteral("language-input"));
    QCOMPARE(qvariant_cast<OperatorPromptResponse>(arguments[1]), OperatorPromptResponse::Submitted);
    QCOMPARE(arguments[2].toMap().value(QStringLiteral("value")).toDouble(), 12.345);
    QVERIFY(overview.setOperatorPromptResponsePending(QStringLiteral("language-input"), true));
    QVERIFY(language.setChinese(true, false));
    QTRY_COMPARE(overlay->findChild<QLabel*>(QStringLiteral("uutOverviewPromptStatus"))->text(),
                 uiText("Recording operator response..."));
    QVERIFY(!submit->isEnabled());
    QVERIFY(overview.hasOperatorPrompt(QStringLiteral("language-input")));
    overview.closeOperatorPrompt(QStringLiteral("language-input"));
}

void MainWindowLifecycleTests::languageSwitchPreservesSlotChoices()
{
    auto& language = UiLanguage::instance();
    QVERIFY(language.setChinese(false, false));
    const auto restore = qScopeGuard([&] { language.setChinese(false, false); });
    bool verified = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto closeOnFailure = qScopeGuard([dialog] { dialog->reject(); });
        auto* third = dialog->findChild<QPushButton*>(QStringLiteral("uutSlotToggle3"));
        auto* summary = dialog->findChild<QLabel*>(QStringLiteral("uutSlotConfigurationSummary"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("uutSlotConfigurationButtons"));
        QVERIFY(third && summary && buttons);
        third->click();
        QVERIFY(!third->isChecked());
        QVERIFY(language.setChinese(true, false));
        QCOMPARE(dialog->windowTitle(), QString::fromUtf8("工位设置"));
        QCOMPARE(summary->text(), uiText("%1 / %2 active").arg(3).arg(4));
        QCOMPARE(buttons->button(QDialogButtonBox::Ok)->text(), QString::fromUtf8("应用"));
        QVERIFY(language.setChinese(false, false));
        QCOMPARE(summary->text(), QStringLiteral("3 / 4 active"));
        QVERIFY(!third->isChecked());
        verified = true;
        closeOnFailure.dismiss();
        dialog->accept();
    });
    const auto states = showUutSlotConfigurationDialog(nullptr, 4, {true, true, true, true});
    QVERIFY(verified && states.has_value());
    QCOMPARE(*states, QVector<bool>({true, true, false, true}));
}

void MainWindowLifecycleTests::wrapsSelectedStepsInTestItemFromToolbar()
{
    MainWindow window;
    const auto path = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/test_item_sequence.json");
    QVERIFY(window.openSequenceFile(path));

    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    auto* document = window.findChild<SequenceDocument*>();
    auto* action = window.findChild<QAction*>(QStringLiteral("wrapTestItemAction"));
    QVERIFY(tree);
    QVERIFY(model);
    QVERIFY(document);
    QVERIFY(action);
    QCOMPARE(tree->selectionMode(), QAbstractItemView::ExtendedSelection);

    const auto group = sequenceGroupByKind(model, QStringLiteral("main"));
    QVERIFY(group.isValid());
    const auto first = model->index(0, SequenceTreeModel::NameColumn, group);
    const auto second = model->index(1, SequenceTreeModel::NameColumn, group);
    tree->setCurrentIndex(first);
    tree->selectionModel()->select(
        first, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    tree->selectionModel()->select(
        second, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QVERIFY(action->isEnabled());

    action->trigger();
    const auto refreshedGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    QCOMPARE(model->rowCount(refreshedGroup), 1);
    const auto wrapped = model->index(0, SequenceTreeModel::NameColumn,
                                      refreshedGroup);
    QCOMPARE(wrapped.siblingAtColumn(SequenceTreeModel::KindColumn)
                 .data().toString(),
             QStringLiteral("testItem"));
    QCOMPARE(model->rowCount(wrapped), 2);
    QCOMPARE(tree->currentIndex().siblingAtColumn(
                 SequenceTreeModel::NameColumn),
             wrapped);

    document->undoStack()->undo();
    QVERIFY(document->isModified());
}

void MainWindowLifecycleTests::copiesAndPastesSelectedItemsFromToolbar()
{
    MainWindow window;
    const auto path = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/test_item_sequence.json");
    QVERIFY(window.openSequenceFile(path));
    window.show();
    QTest::qWait(20);

    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    auto* document = window.findChild<SequenceDocument*>();
    auto* copyAction = window.findChild<QAction*>(QStringLiteral("copyStepAction"));
    auto* pasteAction = window.findChild<QAction*>(QStringLiteral("pasteStepAction"));
    QVERIFY(tree);
    QVERIFY(model);
    QVERIFY(document);
    QVERIFY(copyAction);
    QVERIFY(pasteAction);

    const auto group = sequenceGroupByKind(model, QStringLiteral("main"));
    QVERIFY(group.isValid());
    const auto mainPath = model->pathForIndex(group);
    const auto firstTestItem = model->index(0, SequenceTreeModel::NameColumn, group);
    const auto firstChild = model->index(
        0, SequenceTreeModel::NameColumn, firstTestItem);
    const auto secondItem = model->index(1, SequenceTreeModel::NameColumn, group);
    QVERIFY(firstTestItem.isValid());
    QVERIFY(firstChild.isValid());
    QVERIFY(secondItem.isValid());

    tree->setCurrentIndex(firstTestItem);
    tree->selectionModel()->select(
        firstTestItem,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    tree->selectionModel()->select(
        firstChild, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    tree->selectionModel()->select(
        secondItem, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QVERIFY(copyAction->isEnabled());
    QVERIFY(!pasteAction->isEnabled());

    tree->setFocus();
    QTest::keyClick(tree, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(model->rowCount(group), 2);
    QVERIFY(pasteAction->isEnabled());
    QTest::keyClick(tree, Qt::Key_V, Qt::ControlModifier);
    const auto refreshedGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    QCOMPARE(model->rowCount(refreshedGroup), 4);
    QCOMPARE(document->undoStack()->undoText(),
             QStringLiteral("Paste Selected Steps"));
    QCOMPARE(document->objectAt(SequenceItemPath{mainPath.groupIndex, {1}})
             .value(QStringLiteral("id")).toString(),
             QStringLiteral("001"));
    QCOMPARE(document->objectAt(SequenceItemPath{mainPath.groupIndex, {2}})
             .value(QStringLiteral("id")).toString(),
             QStringLiteral("002"));
    document->undoStack()->undo();
    QCOMPARE(model->rowCount(sequenceGroupByKind(model, QStringLiteral("main"))), 2);
}

void MainWindowLifecycleTests::flowBlankClickClearsSelection()
{
    MainWindow window;
    const auto path = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/simple_sequence.json");
    QVERIFY(window.openSequenceFile(path));
    window.resize(1100, 720);
    window.show();
    QTest::qWait(30);

    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    auto* editor = window.findChild<StepPropertyEditor*>();
    QVERIFY(tree && model && editor);
    const auto step = model->index(
        0, SequenceTreeModel::NameColumn,
        model->index(0, SequenceTreeModel::NameColumn));
    tree->setCurrentIndex(step);
    tree->selectionModel()->select(
        step, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QVERIFY(editor->currentPath().isValid());

    const QPoint blankPoint(tree->viewport()->width() / 2,
                            tree->viewport()->height() - 3);
    QVERIFY(!tree->indexAt(blankPoint).isValid());
    QTest::mouseClick(tree->viewport(), Qt::LeftButton,
                      Qt::NoModifier, blankPoint);

    QVERIFY(!tree->currentIndex().isValid());
    QVERIFY(tree->selectionModel()->selectedRows().isEmpty());
    QVERIFY(!editor->currentPath().isValid());
}

void MainWindowLifecycleTests::compileSilentlySavesCurrentDraft()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(
        QStringLiteral("compile_save.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    auto* editor = window.findChild<StepPropertyEditor*>();
    auto* document = window.findChild<SequenceDocument*>();
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* compileAction = window.findChild<QAction*>(
        QStringLiteral("compileAction"));
    QVERIFY(tree && model && editor && document && viewModel && compileAction);

    const auto step = model->index(
        0, SequenceTreeModel::NameColumn,
        model->index(0, SequenceTreeModel::NameColumn));
    tree->setCurrentIndex(step);
    auto* name = editor->findChild<QLineEdit*>(
        QStringLiteral("propertyNameEdit"));
    QVERIFY(name);
    name->setText(QStringLiteral("Saved by Compile"));
    QVERIFY(editor->hasPendingChanges());

    bool confirmationShown = false;
    QTimer::singleShot(0, [&confirmationShown] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* messageBox = qobject_cast<QMessageBox*>(widget)) {
                confirmationShown = true;
                messageBox->accept();
            }
        }
    });
    compileAction->trigger();
    QVERIFY(!confirmationShown);
    QVERIFY(!editor->hasPendingChanges());
    QVERIFY(!document->isModified());
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);

    QFile saved(sequencePath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto root = QJsonDocument::fromJson(saved.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("groups")).toArray().first().toObject()
                 .value(QStringLiteral("steps")).toArray().first().toObject()
                 .value(QStringLiteral("name")).toString(),
             QStringLiteral("Saved by Compile"));
}

void MainWindowLifecycleTests::functionPalettePreviewsParametersAndShowsDragHandles()
{
    MainWindow window;
    const auto path = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/test_item_sequence.json");
    QVERIFY(window.openSequenceFile(path));

    auto* palette = window.findChild<QTreeView*>(
        QStringLiteral("pluginFunctionView"));
    auto* sequence = window.findChild<QTreeView*>(
        QStringLiteral("sequenceTreeView"));
    auto* functionModel = window.findChild<PluginFunctionModel*>();
    auto* sequenceModel = window.findChild<SequenceTreeModel*>();
    auto* editor = window.findChild<StepPropertyEditor*>();
    QVERIFY(palette && sequence && functionModel && sequenceModel && editor);
    QCOMPARE(palette->itemDelegateForColumn(0)->objectName(),
             QStringLiteral("dragHandleDelegate"));
    QCOMPARE(sequence->itemDelegateForColumn(SequenceTreeModel::NameColumn)
                 ->objectName(),
             QStringLiteral("dragHandleDelegate"));

    const auto basicSection = functionModel->index(0, 0);
    const auto limitFunction = functionModel->index(1, 0, basicSection);
    QVERIFY(limitFunction.flags() & Qt::ItemIsDragEnabled);
    palette->selectionModel()->setCurrentIndex(
        limitFunction,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

    auto* title = editor->findChild<QLabel*>(
        QStringLiteral("propertyEditorTitle"));
    auto* comparison = editor->findChild<QComboBox*>(
        QStringLiteral("propertyLimitComparisonCombo"));
    auto* enabledCheck = editor->findChild<QCheckBox*>(
        QStringLiteral("propertyEnabledCheck"));
    auto* apply = editor->findChild<QPushButton*>(
        QStringLiteral("applyPropertiesButton"));
    QVERIFY(title && comparison && enabledCheck);
    QVERIFY(!apply);
    QCOMPARE(title->text(), QStringLiteral("Function Preview"));
    QCOMPARE(comparison->currentData().toString(),
             QStringLiteral("betweenTolerance"));
    QVERIFY(!comparison->isEnabled());
    QVERIFY(!editor->currentPath().isValid());

    const auto group = sequenceGroupByKind(sequenceModel, QStringLiteral("main"));
    QVERIFY(group.isValid());
    editor->setCurrentItem(sequenceModel->pathForIndex(group));
    QVERIFY(enabledCheck->isHidden());

    const auto step = sequenceModel->index(
        0, SequenceTreeModel::NameColumn, group);
    editor->setCurrentItem(sequenceModel->pathForIndex(step));
    QCOMPARE(title->text(), QStringLiteral("Properties"));
    QVERIFY(editor->currentPath().isValid());
    QVERIFY(!enabledCheck->isHidden());
}

void MainWindowLifecycleTests::flowEnableTogglePreservesTreePosition()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(
        QStringLiteral("long_sequence.json"));

    QJsonArray nestedSteps;
    for (int index = 0; index < 4; ++index) {
        nestedSteps.push_back(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("%1").arg(index + 1, 2, 10, QLatin1Char('0'))},
            {QStringLiteral("name"), QStringLiteral("Nested %1").arg(index + 1)},
            {QStringLiteral("kind"), QStringLiteral("noop")},
            {QStringLiteral("enabled"), true}});
    }

    QJsonArray steps;
    steps.push_back(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("001")},
        {QStringLiteral("name"), QStringLiteral("Collapsible Item")},
        {QStringLiteral("kind"), QStringLiteral("testItem")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("steps"), nestedSteps}});
    for (int index = 1; index <= 50; ++index) {
        steps.push_back(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("%1").arg(index + 1, 3, 10, QLatin1Char('0'))},
            {QStringLiteral("name"), QStringLiteral("Step %1").arg(index)},
            {QStringLiteral("kind"), QStringLiteral("noop")},
            {QStringLiteral("enabled"), true}});
    }

    const QJsonObject root{
        {QStringLiteral("id"), QStringLiteral("scroll-sequence")},
        {QStringLiteral("name"), QStringLiteral("Scroll Sequence")},
        {QStringLiteral("groups"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("setup")},
                        {QStringLiteral("name"), QStringLiteral("Setup")},
                        {QStringLiteral("kind"), QStringLiteral("setup")},
                        {QStringLiteral("steps"), QJsonArray{}}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("main")},
                        {QStringLiteral("name"), QStringLiteral("Main")},
                        {QStringLiteral("kind"), QStringLiteral("main")},
                        {QStringLiteral("steps"), steps}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("cleanup")},
                        {QStringLiteral("name"), QStringLiteral("Cleanup")},
                        {QStringLiteral("kind"), QStringLiteral("cleanup")},
                        {QStringLiteral("steps"), QJsonArray{}}}}}};
    QFile file(sequencePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const auto bytes = QJsonDocument(root).toJson();
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    window.resize(1100, 650);
    window.show();
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    QVERIFY(tabs);
    tabs->setCurrentIndex(1);
    QTest::qWait(30);

    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    QVERIFY(tree && model);
    const auto group = sequenceGroupByKind(model, QStringLiteral("main"));
    QVERIFY(group.isValid());
    auto testItem = model->index(0, SequenceTreeModel::NameColumn, group);
    tree->collapse(testItem);

    auto target = model->index(42, SequenceTreeModel::NameColumn, group);
    QVERIFY(target.isValid());
    const auto targetPath = model->pathForIndex(target);
    tree->setCurrentIndex(target);
    tree->scrollTo(target, QAbstractItemView::PositionAtCenter);
    QCoreApplication::processEvents();
    const int scrollBefore = tree->verticalScrollBar()->value();
    QVERIFY(scrollBefore > 0);

    QVERIFY(model->setData(
        target.siblingAtColumn(SequenceTreeModel::EnabledColumn),
        Qt::Unchecked,
        Qt::CheckStateRole));
    QCoreApplication::processEvents();

    target = model->indexForPath(targetPath);
    testItem = model->index(0, SequenceTreeModel::NameColumn,
                            sequenceGroupByKind(model, QStringLiteral("main")));
    QVERIFY(target.isValid());
    QCOMPARE(model->pathForIndex(tree->currentIndex()), targetPath);
    QVERIFY(qAbs(tree->verticalScrollBar()->value() - scrollBefore) <= 1);
    QVERIFY(!tree->isExpanded(testItem));
    QVERIFY(tree->visualRect(target).intersects(tree->viewport()->rect()));
}

void MainWindowLifecycleTests::flowToolbarExpandsAndCollapsesAtPhaseFirstLevel()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(
        QStringLiteral("nested-flow.json"));
    QFile sequence(sequencePath);
    QVERIFY(sequence.open(QIODevice::WriteOnly));
    sequence.write(R"({
      "id":"nested-flow","name":"Nested Flow","groups":[
        {"id":"setup","kind":"setup","steps":[
          {"id":"001","kind":"testItem","name":"Setup Item","steps":[
            {"id":"01","kind":"noop","name":"Setup Child"}
          ]}
        ]},
        {"id":"main","kind":"main","steps":[
          {"id":"002","kind":"testItem","name":"Main Item","steps":[
            {"id":"01","kind":"testItem","name":"Nested Item","steps":[
              {"id":"01","kind":"noop","name":"Nested Child"}
            ]},
            {"id":"02","kind":"noop","name":"Main Child"}
          ]},
          {"id":"003","kind":"noop","name":"Main Leaf"}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[
          {"id":"004","kind":"testItem","name":"Cleanup Item","steps":[
            {"id":"01","kind":"noop","name":"Cleanup Child"}
          ]}
        ]}
      ]
    })");
    sequence.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    window.show();
    QTest::qWait(20);

    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    auto* expand = window.findChild<QAction*>(
        QStringLiteral("expandSequencePhasesAction"));
    auto* collapse = window.findChild<QAction*>(
        QStringLiteral("collapseSequencePhasesAction"));
    QVERIFY(tree && model && expand && collapse);
    QVERIFY(!expand->icon().isNull());
    QVERIFY(!collapse->icon().isNull());

    expand->trigger();
    for (const auto& kind : {QStringLiteral("setup"),
                             QStringLiteral("main"),
                             QStringLiteral("cleanup")}) {
        const auto phase = sequenceGroupByKind(model, kind);
        QVERIFY(phase.isValid());
        QVERIFY(tree->isExpanded(phase));
        const auto firstChild = model->index(
            0, SequenceTreeModel::NameColumn, phase);
        QVERIFY(firstChild.isValid());
        if (model->rowCount(firstChild) > 0) {
            QVERIFY(tree->isExpanded(firstChild));
        }
    }

    const auto main = sequenceGroupByKind(model, QStringLiteral("main"));
    const auto mainItem = model->index(
        0, SequenceTreeModel::NameColumn, main);
    const auto nestedItem = model->index(
        0, SequenceTreeModel::NameColumn, mainItem);
    QVERIFY(tree->isExpanded(nestedItem));

    collapse->trigger();
    for (const auto& kind : {QStringLiteral("setup"),
                             QStringLiteral("main"),
                             QStringLiteral("cleanup")}) {
        const auto phase = sequenceGroupByKind(model, kind);
        QVERIFY(tree->isExpanded(phase));
        const auto firstChild = model->index(
            0, SequenceTreeModel::NameColumn, phase);
        QVERIFY(firstChild.isValid());
        QVERIFY(!tree->isExpanded(firstChild));
        QVERIFY(!tree->isRowHidden(firstChild.row(), phase));
    }
    QVERIFY(!tree->isExpanded(nestedItem));
    QVERIFY(tree->visualRect(mainItem).isValid());
    QVERIFY(tree->visualRect(nestedItem).isEmpty());
}

void MainWindowLifecycleTests::picoStyleDrawsFilledCheckedIndicators()
{
    PicoATEStyle style;
    const auto renderIndicator = [&](QStyle::PrimitiveElement element,
                                     QStyle::State state) {
        QImage image(24, 24, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        QStyleOption option;
        option.rect = image.rect();
        option.state = state;
        style.drawPrimitive(element, &option, &painter);
        return image;
    };

    for (const auto element : {QStyle::PE_IndicatorCheckBox,
                               QStyle::PE_IndicatorItemViewItemCheck}) {
        const auto checked = renderIndicator(
            element, QStyle::State_Enabled | QStyle::State_On);
        const auto unchecked = renderIndicator(
            element, QStyle::State_Enabled | QStyle::State_Off);
        QVERIFY(checked.pixelColor(12, 12).lightness() < 100);
        QVERIFY(unchecked.pixelColor(12, 12).lightness() > 220);
    }
    QCOMPARE(style.pixelMetric(QStyle::PM_IndicatorWidth), 18);
    QCOMPARE(style.pixelMetric(QStyle::PM_IndicatorHeight), 18);
}

void MainWindowLifecycleTests::parserActualDelegateHighlightsSelectedTokens()
{
    QStandardItemModel model(1, 1);
    const auto index = model.index(0, 0);
    model.setData(index, QStringLiteral("Raw: [0,0] | Parsed: 0"));
    model.setData(
        index,
        QVariantMap{
            {QStringLiteral("format"), QStringLiteral("hexBytes")},
            {QStringLiteral("tokens"),
             QVariantList{QStringLiteral("00"), QStringLiteral("00"),
                          QStringLiteral("00"), QStringLiteral("00")}},
            {QStringLiteral("selectedIndices"), QVariantList{2, 3}},
            {QStringLiteral("groupSize"), 2},
            {QStringLiteral("sourceTokenOffset"), 0},
            {QStringLiteral("sourceTokenCount"), 4},
            {QStringLiteral("parsedDisplay"), QStringLiteral("0")}},
        UutStepModel::ParserSelectionDisplayRole);

    ParserActualDelegate delegate;
    QImage image(520, 44, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    QStyleOptionViewItem option;
    option.rect = image.rect();
    option.font = qApp->font();
    option.palette = qApp->palette();
    option.state = QStyle::State_Enabled | QStyle::State_Active;
    delegate.paint(&painter, option, index);
    painter.end();

    const QColor expected(QStringLiteral("#bfe5ff"));
    int highlightedPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) == expected) {
                ++highlightedPixels;
            }
        }
    }
    QVERIFY2(highlightedPixels > 20,
             "The selected register bytes were not painted with the highlight color");
}

void MainWindowLifecycleTests::inputWheelGuardPreventsAccidentalValueChanges()
{
    InputWheelGuard guard;
    qApp->installEventFilter(&guard);

    QScrollArea area;
    area.setAttribute(Qt::WA_DontShowOnScreen);
    area.setWidgetResizable(true);
    area.resize(280, 140);
    auto* content = new QWidget;
    content->setMinimumHeight(700);
    auto* layout = new QVBoxLayout(content);
    auto* combo = new QComboBox(content);
    combo->addItems({QStringLiteral("A"), QStringLiteral("B"),
                     QStringLiteral("C")});
    combo->setCurrentIndex(1);
    auto* spin = new QSpinBox(content);
    spin->setRange(0, 10);
    spin->setValue(5);
    layout->addWidget(combo);
    layout->addWidget(spin);
    layout->addStretch(1);
    area.setWidget(content);
    area.show();
    QCoreApplication::processEvents();

    auto sendWheel = [](QWidget* target) {
        const QPointF localPosition(target->rect().center());
        const QPointF globalPosition(
            target->mapToGlobal(localPosition.toPoint()));
        QWheelEvent event(localPosition,
                          globalPosition,
                          {},
                          QPoint(0, -120),
                          Qt::NoButton,
                          Qt::NoModifier,
                          Qt::NoScrollPhase,
                          false);
        QCoreApplication::sendEvent(target, &event);
    };

    auto* scrollBar = area.verticalScrollBar();
    QVERIFY(scrollBar->maximum() > 0);
    scrollBar->setValue(0);
    sendWheel(combo);
    QCOMPARE(combo->currentIndex(), 1);
    QVERIFY(scrollBar->value() > 0);

    scrollBar->setValue(0);
    sendWheel(spin);
    QCOMPARE(spin->value(), 5);
    QVERIFY(scrollBar->value() > 0);

    qApp->removeEventFilter(&guard);
}

void MainWindowLifecycleTests::responsiveLayoutKeepsSmallScreenPanelsUsable()
{
    QSettings().clear();
    MainWindow window;
    window.resize(1024, 640);
    window.show();
    QTest::qWait(30);

    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    auto* flowSplitter = window.findChild<QSplitter*>(
        QStringLiteral("sequenceWorkSplitter"));
    auto* stationSplitter = window.findChild<QSplitter*>(
        QStringLiteral("stationWorkSplitter"));
    auto* propertyEditor = window.findChild<StepPropertyEditor*>();
    auto* stationEditor = window.findChild<StationPropertyEditor*>();
    QVERIFY(tabs);
    QVERIFY(flowSplitter);
    QVERIFY(stationSplitter);
    QVERIFY(propertyEditor);
    QVERIFY(stationEditor);

    tabs->setCurrentIndex(1);
    QCoreApplication::processEvents();
    const auto flowSizes = flowSplitter->sizes();
    QCOMPARE(flowSizes.size(), 3);
    QVERIFY(flowSizes.at(0) >= 180);
    QVERIFY(flowSizes.at(1) >= 280);
    QVERIFY(flowSizes.at(2) >= 240);
    QCOMPARE(propertyEditor->minimumWidth(), 240);
    QCOMPARE(stationEditor->minimumWidth(), 230);

    tabs->setCurrentIndex(2);
    QCoreApplication::processEvents();
    const auto stationSizes = stationSplitter->sizes();
    QCOMPARE(stationSizes.size(), 3);
    QVERIFY(stationSizes.at(0) >= 180);
    QVERIFY(stationSizes.at(1) >= 300);
    QVERIFY(stationSizes.at(2) >= 230);
    QVERIFY(window.close());
    QSettings().clear();
}

void MainWindowLifecycleTests::flowDropTargetPrefersTestItemInterior()
{
    SequenceDocument document;
    QVERIFY(document.load(
        QDir(QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR))
            .filePath(QStringLiteral("examples/test_item_sequence.json"))));

    SequenceTreeModel model(&document);
    SequenceEditorTreeView tree;
    tree.setModel(&model);
    tree.resize(900, 500);
    tree.show();
    tree.expandAll();
    QTest::qWait(20);

    const auto group = sequenceGroupByKind(&model, QStringLiteral("main"));
    QVERIFY(group.isValid());
    const auto testItem = model.index(0, SequenceTreeModel::NameColumn, group);
    QVERIFY(testItem.isValid());
    QVERIFY(model.canContainSteps(testItem));
    const auto rect = tree.visualRect(testItem);
    QVERIFY(rect.isValid());

    const auto inside = tree.dropPreviewAt(rect.center());
    QCOMPARE(inside.placement,
             SequenceEditorTreeView::DropPlacement::Into);
    QCOMPARE(inside.parentIndex, testItem);
    QCOMPARE(inside.row, model.rowCount(testItem));

    const auto after = tree.dropPreviewAt(
        QPoint(rect.center().x(), rect.bottom()));
    QCOMPARE(after.placement,
             SequenceEditorTreeView::DropPlacement::After);
    QCOMPARE(after.parentIndex, group);
    QCOMPARE(after.row, testItem.row() + 1);
}

void MainWindowLifecycleTests::proportionalHeaderDistributesAvailableWidthByWeight()
{
    QTableView view;
    view.setAttribute(Qt::WA_DontShowOnScreen);
    QStandardItemModel model(1, 3, &view);
    view.setModel(&model);
    auto* header = new ProportionalHeaderView(&view);
    view.setHorizontalHeader(header);
    header->setSectionWeights({2, 1, 1});
    view.resize(900, 240);
    view.show();
    QTest::qWait(20);

    const int first = header->sectionSize(0);
    const int second = header->sectionSize(1);
    const int third = header->sectionSize(2);
    QVERIFY(first > second);
    QVERIFY(qAbs(second - third) <= 2);
    QVERIFY(qAbs(first - second * 2) <= 4);

    const int reducedFirst = qMax(header->minimumSectionSize(), first - 80);
    header->resizeSection(0, reducedFirst);
    QTest::qWait(20);
    QCOMPARE(header->sectionSize(0), reducedFirst);
    QCOMPARE(header->sectionSize(1), second);
    QVERIFY(header->sectionSize(2) >= third + first - reducedFirst - 2);
    QVERIFY(qAbs(header->length() - header->viewport()->width()) <= 2);

    view.resize(1200, 240);
    QTest::qWait(20);
    QVERIFY(qAbs(header->sectionSize(1) - header->sectionSize(2)) <= 2);
    QVERIFY(qAbs(header->sectionSize(0) - header->sectionSize(1) * 2) <= 4);
}

void MainWindowLifecycleTests::pluginPropertyEditorValidatesRequiredAndRangeAndSavesInputs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("plugin_sequence.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"({
      "id": "plugin-editor-test",
      "name": "Plugin Editor Test",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "001",
          "name": "Send CAN",
          "kind": "action",
          "moduleId": "plugin.can.gcan",
          "function": "write",
          "inputs": {}
        }]
      }]
    })");
    sequenceFile.close();

    const QByteArray description = R"json({
      "name": "GCAN USB-CAN",
      "category": "CAN",
      "functions": [{
        "id": "write",
        "name": "Send CAN Frame",
        "inputs": [
          {"key": "id", "name": "CAN ID", "type": "string", "required": true,
           "default": "0x123"},
          {"key": "filterId", "name": "Filter ID (0x000-0x7FF)",
           "type": "string", "required": false, "default": "0x123"},
          {"key": "filterMask", "name": "Filter Mask (0=Any, Std Exact=0x7FF)",
           "type": "string", "required": false, "default": "0x7FF"},
          {"key": "data", "name": "Frame Data", "type": "hex-bytes", "required": true},
          {"key": "extended", "name": "Extended Frame", "type": "boolean",
           "required": false, "default": false},
          {"key": "timeoutMs", "name": "Timeout", "type": "integer", "required": false,
           "minimum": 1, "maximum": 60000, "unit": "ms"}
        ],
        "outputs": []
      }]
    })json";
    const auto plugin = PluginCatalog::parseDescription(
        description,
        directory.filePath(QStringLiteral("PicoATE.CAN.GCAN.dll")),
        1);
    QVERIFY(plugin.ok());

    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    editor.setPluginRegistry({plugin.manifest});
    SequenceItemPath path;
    path.groupIndex = 0;
    path.stepIndices = {0};
    editor.setCurrentItem(path);
    editor.show();
    QTest::qWait(20);

    auto* parameterGroup = editor.findChild<QGroupBox*>(
        QStringLiteral("pluginInputsGroup"));
    auto* function = editor.findChild<QComboBox*>(
        QStringLiteral("propertyFunctionEdit"));
    auto* id = editor.findChild<QLineEdit*>(QStringLiteral("pluginInput_id"));
    auto* filterId = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_filterId"));
    auto* filterMask = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_filterMask"));
    auto* data = editor.findChild<QLineEdit*>(QStringLiteral("pluginInput_data"));
    auto* extended = editor.findChild<QAbstractButton*>(
        QStringLiteral("pluginInput_extended"));
    auto* timeout = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_timeoutMs"));
    auto* error = editor.findChild<QLabel*>(QStringLiteral("propertyErrorLabel"));
    QVERIFY(parameterGroup && !parameterGroup->isHidden());
    QVERIFY(function);
    QVERIFY(!function->isEditable());
    QCOMPARE(function->currentData().toString(), QStringLiteral("write"));
    QVERIFY(id && filterId && filterMask);
    QVERIFY(data);
    QVERIFY(extended);
    QVERIFY(timeout);
    QVERIFY(error);
    QCOMPARE(id->placeholderText(), QStringLiteral("0x000-0x7FF"));
    QCOMPARE(filterId->placeholderText(), QStringLiteral("0x000-0x7FF"));
    QCOMPARE(filterMask->placeholderText(),
             QStringLiteral("0x000=Any; 0x7FF=Exact"));
    QStringList inputLabels;
    for (const auto* label : parameterGroup->findChildren<QLabel*>()) {
        inputLabels.push_back(label->text());
    }
    QVERIFY(inputLabels.contains(QStringLiteral("Filter ID")));
    QVERIFY(inputLabels.contains(QStringLiteral("Filter Mask")));
    QVERIFY(std::none_of(inputLabels.cbegin(), inputLabels.cend(),
                         [](const QString& label) {
                             return label.contains(QStringLiteral("Std Exact")) ||
                                    label.contains(QStringLiteral("0x000-0x7FF"));
                         }));

    data->setText(QStringLiteral(" "));
    QVERIFY(editor.hasPendingChanges());
    QVERIFY(!editor.commitPendingChanges());
    QVERIFY(error->isVisible());
    QVERIFY(error->text().contains(QStringLiteral("required"), Qt::CaseInsensitive));

    data->setText(QStringLiteral("01 02 03 04"));
    timeout->setText(QStringLiteral("70000"));
    QVERIFY(!editor.commitPendingChanges());
    QVERIFY(error->text().contains(QStringLiteral("range"), Qt::CaseInsensitive));

    timeout->setText(QStringLiteral("1500"));
    id->setText(QStringLiteral("0x800"));
    extended->setChecked(false);
    QVERIFY(!editor.commitPendingChanges());
    QVERIFY(error->text().contains(QStringLiteral("0x000~0x7FF")));
    QVERIFY(id->property("validationError").toBool());
    QTRY_VERIFY(id->styleSheet().contains(QStringLiteral("#d92d20")));
    QCOMPARE(id->placeholderText(), QStringLiteral("0x000-0x7FF"));

    extended->setChecked(true);
    QCOMPARE(id->placeholderText(),
             QStringLiteral("0x00000000-0x1FFFFFFF"));
    QCOMPARE(filterId->placeholderText(),
             QStringLiteral("0x00000000-0x1FFFFFFF"));
    QCOMPARE(filterMask->placeholderText(),
             QStringLiteral("0x00000000=Any; 0x1FFFFFFF=Exact"));
    QVERIFY(editor.commitPendingChanges());
    QVERIFY(!id->property("validationError").toBool());
    QVERIFY(id->styleSheet().isEmpty());
    QVERIFY(!error->isVisible());
    const auto inputs = document.objectAt(path).value(QStringLiteral("inputs")).toObject();
    QCOMPARE(inputs.value(QStringLiteral("data")).toString(),
             QStringLiteral("01 02 03 04"));
    QCOMPARE(inputs.value(QStringLiteral("id")).toString(), QStringLiteral("0x800"));
    QVERIFY(inputs.value(QStringLiteral("extended")).toBool());
    QCOMPARE(inputs.value(QStringLiteral("timeoutMs")).toInt(), 1500);
}

void MainWindowLifecycleTests::pluginPropertyEditorPreservesLegacyActionData()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("legacy-action.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"json({
      "id":"legacy-action","name":"Legacy Action","groups":[{
        "id":"main","type":"main","steps":[{
          "id":"001","name":"Legacy","type":"mockAction",
          "moduleId":"legacy.module","function":"execute",
          "inputs":{"deviceId":"CAN1.CH1","canId":"0x123"},
          "parameters":{"vendorOption":17}
        }]
      }]
    })json");
    sequenceFile.close();

    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    const SequenceItemPath stepPath{0, {0}};
    editor.setCurrentItem(stepPath);

    auto* name = editor.findChild<QLineEdit*>(QStringLiteral("propertyNameEdit"));
    QVERIFY(name);
    name->setText(QStringLiteral("Edited Legacy Action"));
    QVERIFY(editor.commitPendingChanges());

    const auto updated = document.objectAt(stepPath);
    QCOMPARE(updated.value(QStringLiteral("kind")).toString(),
             QStringLiteral("action"));
    QVERIFY(!updated.contains(QStringLiteral("type")));
    QCOMPARE(updated.value(QStringLiteral("moduleId")).toString(),
             QStringLiteral("legacy.module"));
    QCOMPARE(updated.value(QStringLiteral("function")).toString(),
             QStringLiteral("execute"));
    const auto inputs = updated.value(QStringLiteral("inputs")).toObject();
    QCOMPARE(inputs.value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN1.CH1"));
    QCOMPARE(inputs.value(QStringLiteral("canId")).toString(),
             QStringLiteral("0x123"));
    QCOMPARE(updated.value(QStringLiteral("parameters")).toObject()
                 .value(QStringLiteral("vendorOption")).toInt(), 17);
}

void MainWindowLifecycleTests::pluginPropertyEditorAcceptsRevertedInvalidDraftAsNoOp()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("reverted_draft.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"({
      "id": "reverted-draft-test",
      "name": "Reverted Draft Test",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "001",
          "name": "Send CAN",
          "kind": "action",
          "moduleId": "plugin.can.gcan",
          "function": "write",
          "inputs": {"id": "0x123"}
        }]
      }]
    })");
    sequenceFile.close();

    const auto plugin = PluginCatalog::parseDescription(
        R"json({
          "name": "GCAN USB-CAN",
          "category": "CAN",
          "functions": [{
            "id": "write",
            "name": "Send CAN Frame",
            "inputs": [{
              "key": "id", "name": "CAN ID", "type": "string",
              "required": true, "default": "0x123"
            }],
            "outputs": []
          }]
        })json",
        directory.filePath(QStringLiteral("PicoATE.CAN.GCAN.dll")),
        1);
    QVERIFY(plugin.ok());

    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    editor.setPluginRegistry({plugin.manifest});
    SequenceItemPath path;
    path.groupIndex = 0;
    path.stepIndices = {0};
    editor.setCurrentItem(path);
    editor.show();
    QTest::qWait(20);

    auto* id = editor.findChild<QLineEdit*>(QStringLiteral("pluginInput_id"));
    auto* error = editor.findChild<QLabel*>(QStringLiteral("propertyErrorLabel"));
    QVERIFY(id);
    QVERIFY(error);

    id->setText(QStringLiteral("0x124"));
    QVERIFY(editor.commitPendingChanges());
    const auto committedObject = document.objectAt(path);
    const bool initiallyModified = document.isModified();
    const int initialUndoCount = document.undoStack()->count();

    id->setText(QStringLiteral("0x999"));
    QVERIFY(!editor.commitPendingChanges());
    QVERIFY(error->isVisible());
    QVERIFY(id->property("validationError").toBool());

    id->setText(QStringLiteral("0x124"));
    QVERIFY(editor.hasPendingChanges());
    QVERIFY(editor.commitPendingChanges());
    QVERIFY(!editor.hasPendingChanges());
    QVERIFY(!error->isVisible());
    QVERIFY(!id->property("validationError").toBool());
    QCOMPARE(document.isModified(), initiallyModified);
    QCOMPARE(document.undoStack()->count(), initialUndoCount);
    QCOMPARE(document.objectAt(path), committedObject);
    QCOMPARE(document.objectAt(path).value("inputs").toObject().value("id").toString(),
             QStringLiteral("0x124"));
}

void MainWindowLifecycleTests::pluginPropertyEditorInsertsPreviousStepOutputExpression()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("expression_picker.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"({
        "id":"picker","name":"Picker","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"001","name":"Acquire CAN","kind":"testItem","steps":[
            {"id":"01","key":"capture","name":"Capture Frames","kind":"testItem","steps":[
              {"id":"01","key":"read","name":"Read Frame","kind":"action",
               "moduleId":"plugin.can.test","function":"read"},
              {"id":"02","key":"send","name":"Send Frame","kind":"action",
               "moduleId":"plugin.can.test","function":"write","inputs":{}}
            ]}
          ]},
          {"id":"002","name":"Check DLC","kind":"limit",
           "inputs":{"actual":""},"parameters":{"comparison":"between","expected":8}}
        ]
      },{
        "id":"setup","kind":"setup","steps":[
          {"id":"open","name":"Open CAN","kind":"action",
           "moduleId":"plugin.can.test","function":"read"}
        ]
      }]
    })");
    sequenceFile.close();

    const auto plugin = PluginCatalog::parseDescription(R"({
      "name":"CAN Test","category":"CAN","functions":[
        {"id":"read","name":"Read","inputs":[],"outputs":[
          {"key":"dlc","name":"Data Length","type":"integer","unit":"byte"}
        ]},
        {"id":"write","name":"Write","inputs":[
          {"key":"timeoutMs","name":"Timeout","type":"integer","required":false}
        ],"outputs":[]}
      ]
    })", directory.filePath(QStringLiteral("PicoATE.CAN.Test.dll")), 1);
    QVERIFY(plugin.ok());

    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    editor.setPluginRegistry({plugin.manifest});
    editor.setCurrentItem(SequenceItemPath{0, {0, 0, 1}});
    editor.show();
    QTest::qWait(20);

    auto* timeout = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_timeoutMs"));
    QVERIFY(timeout);
    auto* picker = timeout->parentWidget()->findChild<QToolButton*>(
        QStringLiteral("expressionPickerButton"));
    QVERIFY(picker);
    const auto outputExpression = QStringLiteral("${step:001.capture.read.outputs.dlc}");
    QStringList selectedPath;
    QStringList initialPath;
    QStringList initialRootPath;
    QStringList rootAfterBack;
    bool backEnabled = false;
    bool columnsStayedFixed = false;
    QVERIFY(chooseExpression(
        picker, outputExpression, &selectedPath, &initialPath,
        &initialRootPath, &backEnabled, &rootAfterBack, &columnsStayedFixed));
    QCOMPARE(initialPath,
             QStringList({QStringLiteral("MAIN"),
                          QStringLiteral("001 - Acquire CAN"),
                          QStringLiteral("capture - Capture Frames"),
                          QStringLiteral("read - Read Frame")}));
    QCOMPARE(initialRootPath,
             QStringList({QStringLiteral("MAIN"),
                          QStringLiteral("001 - Acquire CAN")}));
    QVERIFY(backEnabled);
    QCOMPARE(rootAfterBack, QStringList({QStringLiteral("MAIN")}));
    QVERIFY(columnsStayedFixed);
    QVERIFY(picker->styleSheet().contains(QStringLiteral("border: 1px")));
    QCOMPARE(selectedPath,
             QStringList({QStringLiteral("MAIN"),
                          QStringLiteral("001 - Acquire CAN"),
                          QStringLiteral("capture - Capture Frames"),
                          QStringLiteral("read - Read Frame"),
                          QStringLiteral("Data Length [dlc]")}));
    QCOMPARE(timeout->text(), outputExpression);
    QVERIFY(editor.commitPendingChanges());

    editor.setCurrentItem(SequenceItemPath{0, {1}});
    auto* actual = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyLimitActualEdit"));
    QVERIFY(actual);
    auto* limitPicker = actual->parentWidget()->findChild<QToolButton*>(
        QStringLiteral("expressionPickerButton"));
    QVERIFY(limitPicker);
    QVERIFY(limitPicker->isEnabled());
    QVERIFY(chooseExpression(limitPicker, outputExpression));
    QCOMPARE(actual->text(), outputExpression);
    QVERIFY(editor.commitPendingChanges());
    QCOMPARE(document.objectAt(SequenceItemPath{0, {1}})
                 .value(QStringLiteral("inputs")).toObject()
                 .value(QStringLiteral("actual")).toString(),
             outputExpression);
}

void MainWindowLifecycleTests::pluginPropertyEditorSwitchesConditionalInputs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(
        QStringLiteral("conditional_plugin_inputs.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"json({
      "id":"conditional-inputs","name":"Conditional Inputs","groups":[{
        "id":"main","kind":"main","steps":[{
          "id":"write","name":"Write Registers","kind":"action",
          "moduleId":"plugin.modbus.test","function":"writeMultipleRegisters",
          "inputs":{"dataFormat":"registers","values":"[1,2]",
                    "text":"stale","registerCount":24}
        }]
      }]
    })json");
    sequenceFile.close();

    const auto plugin = PluginCatalog::parseDescription(R"json({
      "name":"Modbus Test","category":"MODBUS","functions":[{
        "id":"writeMultipleRegisters","name":"Write Multiple Registers",
        "inputs":[
          {"key":"dataFormat","name":"Data Format","type":"enum",
           "default":"registers","options":[
             {"label":"Registers","value":"registers"},
             {"label":"ASCII Text","value":"asciiText"},
             {"label":"UTF-8 Text","value":"utf8Text"}]},
          {"key":"values","name":"Values","type":"string","required":true,
           "visibleWhen":{"key":"dataFormat","values":["registers"]}},
          {"key":"text","name":"Text","type":"string","required":true,
           "visibleWhen":{"key":"dataFormat","values":["asciiText","utf8Text"]}},
          {"key":"registerCount","name":"Register Count","type":"integer",
           "required":true,"visibleWhen":{"key":"dataFormat",
                                             "values":["asciiText","utf8Text"]}}
        ],"outputs":[]
      }]
    })json", directory.filePath(QStringLiteral("PicoATE.Modbus.Test.dll")), 1);
    QVERIFY(plugin.ok());

    const SequenceItemPath stepPath{0, {0}};
    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    editor.setPluginRegistry({plugin.manifest});
    editor.setCurrentItem(stepPath);
    editor.show();
    QTest::qWait(20);

    auto* format = editor.findChild<QComboBox*>(
        QStringLiteral("pluginInput_dataFormat"));
    auto* values = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_values"));
    auto* text = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_text"));
    auto* count = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_registerCount"));
    QVERIFY(format && values && text && count);
    QVERIFY(!values->isHidden());
    QVERIFY(text->parentWidget()->isHidden());
    QVERIFY(count->parentWidget()->isHidden());

    const int utf8Index = format->findData(QStringLiteral("utf8Text"));
    QVERIFY(utf8Index >= 0);
    format->setCurrentIndex(utf8Index);
    QVERIFY(values->parentWidget()->isHidden());
    QVERIFY(!text->isHidden());
    QVERIFY(!count->isHidden());
    text->setText(QStringLiteral("${var.serialNumber}"));
    count->setText(QStringLiteral("24"));
    QVERIFY(editor.commitPendingChanges());

    const auto inputs = document.objectAt(stepPath)
                            .value(QStringLiteral("inputs")).toObject();
    QCOMPARE(inputs.value(QStringLiteral("dataFormat")).toString(),
             QStringLiteral("utf8Text"));
    QCOMPARE(inputs.value(QStringLiteral("text")).toString(),
             QStringLiteral("${var.serialNumber}"));
    QCOMPARE(inputs.value(QStringLiteral("registerCount")).toInt(), 24);
    QVERIFY(!inputs.contains(QStringLiteral("values")));
}

void MainWindowLifecycleTests::logicalDeviceOpenKeepsStationParametersOutOfStepInputs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("logical_open.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"({
      "id":"logical-open","name":"Logical Open","groups":[{
        "id":"setup","kind":"setup","steps":[{
          "id":"001","name":"Open CX CAN2","kind":"action",
          "moduleId":"device","function":"open",
          "inputs":{"deviceId":"CX_CAN2","deviceType":99,
                    "channelIndex":0,"bitrate":125000}
        }]
      }]
    })");
    sequenceFile.close();

    const auto plugin = PluginCatalog::parseDescription(R"({
      "name":"CX USB-CAN","category":"CAN","functions":[{
        "id":"open","name":"Open CAN","inputs":[
          {"key":"deviceType","name":"Device Type","type":"integer",
           "required":false,"default":4},
          {"key":"channelIndex","name":"Channel","type":"integer",
           "required":false,"default":0},
          {"key":"bitrate","name":"Bitrate","type":"integer",
           "required":false,"default":500000}
        ],"outputs":[]
      }]
    })", directory.filePath(QStringLiteral("PicoATE.CAN.CX.dll")), 1);
    QVERIFY(plugin.ok());

    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    editor.setPluginRegistry({plugin.manifest});
    editor.setDevicePluginBindings({{QStringLiteral("CX_CAN2"),
                                     QStringLiteral("plugin.can.cx")}});
    editor.setDeviceConfigurations({{
        QStringLiteral("CX_CAN2"),
        QJsonObject{{QStringLiteral("deviceType"), 4},
                    {QStringLiteral("channelIndex"), 1},
                    {QStringLiteral("bitrate"), 250000}}}});
    const SequenceItemPath path{0, {0}};
    editor.setCurrentItem(path);
    editor.show();
    QTest::qWait(20);

    auto* group = editor.findChild<QGroupBox*>(QStringLiteral("pluginInputsGroup"));
    auto* channel = editor.findChild<QLineEdit*>(QStringLiteral("pluginInput_channelIndex"));
    auto* bitrate = editor.findChild<QLineEdit*>(QStringLiteral("pluginInput_bitrate"));
    QVERIFY(group && channel && bitrate);
    QCOMPARE(group->title(), QStringLiteral("Connection Settings - Station Config"));
    QCOMPARE(channel->text(), QStringLiteral("1"));
    QCOMPARE(bitrate->text(), QStringLiteral("250000"));
    QVERIFY(!channel->isEnabled());
    QVERIFY(!bitrate->isEnabled());

    auto* nameEdit = editor.findChild<QLineEdit*>(QStringLiteral("propertyNameEdit"));
    QVERIFY(nameEdit);
    nameEdit->setText(QStringLiteral("Open CX CAN2 Updated"));
    QVERIFY(editor.hasPendingChanges());
    QCOMPARE(document.objectAt(path).value(QStringLiteral("name")).toString(),
             QStringLiteral("Open CX CAN2"));
    QVERIFY(editor.commitPendingChanges());

    const auto updated = document.objectAt(path);
    QCOMPARE(updated.value(QStringLiteral("name")).toString(),
             QStringLiteral("Open CX CAN2 Updated"));
    const auto inputs = updated.value(QStringLiteral("inputs")).toObject();
    QCOMPARE(inputs.size(), 1);
    QCOMPARE(inputs.value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CX_CAN2"));
}

void MainWindowLifecycleTests::pluginPropertyEditorPrefersTypedControlsAndPreservesAdvancedJson()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(
        QStringLiteral("typed_plugin_inputs.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"({
      "id":"typed-inputs","name":"Typed Inputs","groups":[{
        "id":"main","kind":"main","steps":[{
          "id":"001","name":"Configure CAN","kind":"action",
          "moduleId":"device","function":"configure",
          "inputs":{
            "deviceId":"CAN1.CH1","mode":"normal","canFd":true,
            "legacyInput":"keep-input"
          },
          "parameters":{"legacyParameter":"keep-parameter"}
        }]
      }]
    })");
    sequenceFile.close();

    const auto plugin = PluginCatalog::parseDescription(R"({
      "name":"CAN Adapter","category":"CAN","functions":[{
        "id":"configure","name":"Configure CAN","inputs":[
          {"key":"mode","name":"Mode","type":"enum","required":true,
           "options":[
             {"label":"Normal","value":"normal"},
             {"label":"Listen only","value":"listen"}
           ]},
          {"key":"canFd","name":"CAN FD","type":"boolean",
           "required":true,"default":false}
        ],"outputs":[]
      }]
    })", directory.filePath(QStringLiteral("PicoATE.CAN.Adapter.dll")), 1);
    QVERIFY(plugin.ok());

    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    editor.setPluginRegistry({plugin.manifest});
    editor.setDevicePluginBindings({
        {QStringLiteral("CAN1.CH1"), QStringLiteral("plugin.can.adapter")},
        {QStringLiteral("CAN2.CH2"), QStringLiteral("plugin.can.adapter")}});
    const SequenceItemPath path{0, {0}};
    editor.setCurrentItem(path);
    editor.show();
    QTest::qWait(20);

    auto* device = editor.findChild<QComboBox*>(
        QStringLiteral("propertyDeviceIdCombo"));
    auto* mode = editor.findChild<QComboBox*>(QStringLiteral("pluginInput_mode"));
    auto* canFd = editor.findChild<QAbstractButton*>(
        QStringLiteral("pluginInput_canFd"));
    auto* advanced = editor.findChild<QToolButton*>(
        QStringLiteral("propertyAdvancedJsonToggle"));
    auto* advancedContent = editor.findChild<QWidget*>(
        QStringLiteral("propertyAdvancedJsonContent"));
    QVERIFY(device && mode && canFd && advanced && advancedContent);
    QCOMPARE(device->count(), 2);
    QCOMPARE(device->currentData().toString(), QStringLiteral("CAN1.CH1"));
    QCOMPARE(mode->currentData().toString(), QStringLiteral("normal"));
    QVERIFY(canFd->isChecked());
    QVERIFY(!advanced->isHidden());
    QVERIFY(advancedContent->isHidden());
    QVERIFY(advanced->text().contains(QStringLiteral("2 extra")));

    device->setCurrentIndex(device->findData(QStringLiteral("CAN2.CH2")));
    mode = editor.findChild<QComboBox*>(QStringLiteral("pluginInput_mode"));
    canFd = editor.findChild<QAbstractButton*>(QStringLiteral("pluginInput_canFd"));
    QVERIFY(mode && canFd);
    mode->setCurrentIndex(mode->findData(QStringLiteral("listen")));
    canFd->setChecked(false);
    advanced->setChecked(true);
    QVERIFY(!advancedContent->isHidden());
    QVERIFY(editor.commitPendingChanges());

    const auto updated = document.objectAt(path);
    const auto inputs = updated.value(QStringLiteral("inputs")).toObject();
    QCOMPARE(inputs.value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN2.CH2"));
    QCOMPARE(inputs.value(QStringLiteral("mode")).toString(),
             QStringLiteral("listen"));
    QVERIFY(!inputs.value(QStringLiteral("canFd")).toBool(true));
    QCOMPARE(inputs.value(QStringLiteral("legacyInput")).toString(),
             QStringLiteral("keep-input"));
    QCOMPARE(updated.value(QStringLiteral("parameters")).toObject()
                 .value(QStringLiteral("legacyParameter")).toString(),
             QStringLiteral("keep-parameter"));
}

void MainWindowLifecycleTests::flowEditorAddsAndLocksStandardSequenceGroups()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("main-only.json"));
    QFile sequence(sequencePath);
    QVERIFY(sequence.open(QIODevice::WriteOnly));
    sequence.write(R"({
      "id":"main-only","name":"Main Only","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"001","kind":"noop","name":"Existing Step"}
        ]
      }]
    })");
    sequence.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    auto* document = window.findChild<SequenceDocument*>();
    auto* editor = window.findChild<StepPropertyEditor*>();
    QVERIFY(document && editor);
    const auto groups = document->rootObject().value(QStringLiteral("groups"))
                            .toArray();
    QCOMPARE(groups.size(), 3);
    QVERIFY(document->isModified());

    editor->setCurrentItem(SequenceItemPath{0, {}});
    editor->show();
    QTest::qWait(20);
    auto* id = editor->findChild<QLineEdit*>(QStringLiteral("propertyIdEdit"));
    auto* kind = editor->findChild<QComboBox*>(QStringLiteral("propertyKindCombo"));
    auto* enabled = editor->findChild<QCheckBox*>(
        QStringLiteral("propertyEnabledCheck"));
    QVERIFY(id && kind && enabled);
    QVERIFY(id->isHidden());
    QVERIFY(kind->isHidden());
    QVERIFY(enabled->isHidden());

    document->undoStack()->setClean();
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::resourceRegionGutterTogglesBoundariesAndSelectsHardware()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("resource-region.json"));
    QFile sequence(sequencePath);
    QVERIFY(sequence.open(QIODevice::WriteOnly));
    sequence.write(R"({
      "id":"resource-region-ui","name":"Resource Region UI","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"001","kind":"noop","name":"First"},
          {"id":"002","kind":"noop","name":"Second"},
          {"id":"003","kind":"testItem","name":"Third","steps":[
            {"id":"01","kind":"noop","name":"Third Child Start"},
            {"id":"02","kind":"noop","name":"Third Child Body"},
            {"id":"03","kind":"noop","name":"Third Child End"}
          ]}
        ]
      }]
    })");
    sequence.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    window.resize(1200, 760);
    window.show();
    QTest::qWait(20);

    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    auto* document = window.findChild<SequenceDocument*>();
    auto* targetSelector = window.findChild<FlowTargetSelector*>();
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    auto* flowPage = window.findChild<QWidget*>(QStringLiteral("sequenceEditorPage"));
    auto* flowToolbar = window.findChild<QToolBar*>(
        QStringLiteral("sequenceToolbar"));
    auto* stationToolbar = window.findChild<QToolBar*>(
        QStringLiteral("stationToolbar"));
    auto* runnerToolbar = window.findChild<QToolBar*>(
        QStringLiteral("runnerToolbar"));
    QVERIFY(tree && model && document && targetSelector && tabs && flowPage &&
            flowToolbar && stationToolbar && runnerToolbar);
    QCOMPARE(flowToolbar->toolButtonStyle(), Qt::ToolButtonIconOnly);
    QCOMPARE(stationToolbar->toolButtonStyle(), Qt::ToolButtonIconOnly);
    QVERIFY(!flowToolbar->actions().first()->icon().isNull());
    QVERIFY(!stationToolbar->actions().first()->icon().isNull());
    QCOMPARE(runnerToolbar->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    QCOMPARE(runnerToolbar->iconSize(), QSize(20, 20));
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("placeResourceBoundaryAction")));
    tabs->setCurrentWidget(flowPage);
    FlowTargetDevice can;
    can.logicalId = QStringLiteral("CAN1");
    can.deviceType = QStringLiteral("CAN");
    can.driverName = QStringLiteral("GCAN");
    can.moduleId = QStringLiteral("plugin.can.gcan");
    can.targetIds = {QStringLiteral("CAN1.CH1"), QStringLiteral("CAN1.CH2")};
    can.channelNames = {QStringLiteral("CH1"), QStringLiteral("CH2")};
    can.configured = true;
    FlowTargetDevice can2;
    can2.logicalId = QStringLiteral("CAN2");
    can2.deviceType = QStringLiteral("CAN");
    can2.driverName = QStringLiteral("CX");
    can2.moduleId = QStringLiteral("plugin.can.cx");
    can2.targetIds = {QStringLiteral("CAN2.CH1"), QStringLiteral("CAN2.CH2")};
    can2.channelNames = {QStringLiteral("CH1"), QStringLiteral("CH2")};
    can2.configured = true;
    targetSelector->setDevices({can, can2});

    const auto clickBoundary = [tree, model](const QModelIndex& nameIndex) {
        const auto boundary = nameIndex.siblingAtColumn(
            SequenceTreeModel::ResourceRegionColumn);
        QVERIFY(boundary.isValid());
        tree->setCurrentIndex(nameIndex);
        tree->scrollTo(boundary);
        QCoreApplication::processEvents();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                          tree->visualRect(boundary).center());
        QCoreApplication::processEvents();
    };
    const auto clickBoundaryWithHardware = [&clickBoundary](
                                               const QModelIndex& index) {
        bool handled = false;
        int resourceCount = -1;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() !=
                               QStringLiteral("resourceRegionResourceDialog")) {
                return;
            }
            const auto cards = dialog->findChildren<QToolButton*>(
                QStringLiteral("resourceRegionResourceCard"));
            if (cards.isEmpty()) {
                dialog->reject();
                handled = true;
                return;
            }
            resourceCount = cards.size();
            for (auto* card : cards) {
                card->setChecked(true);
            }
            handled = true;
            dialog->accept();
        });
        timer.start(10);
        clickBoundary(index);
        timer.stop();
        QVERIFY(handled);
        QCOMPARE(resourceCount, 2);
    };

    auto mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    QVERIFY(mainGroup.isValid());
    auto entry = model->index(0, SequenceTreeModel::NameColumn, mainGroup);
    clickBoundary(entry);

    const auto singleId = document->pendingResourceRegionId();
    QVERIFY(!singleId.isEmpty());
    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    entry = model->index(0, SequenceTreeModel::NameColumn, mainGroup);
    clickBoundaryWithHardware(entry);
    QVERIFY(document->pendingResourceRegionId().isEmpty());
    const auto singleObject = document->objectAt(SequenceItemPath{1, {0}});
    QCOMPARE(singleObject.value(QStringLiteral("resourceRegionStart"))
                 .toObject().value(QStringLiteral("id")).toString(),
             singleId);
    QCOMPARE(singleObject.value(QStringLiteral("resourceRegionEnd")).toString(),
             singleId);
    QCOMPARE(document->resourceRegionResources(singleId),
             QStringList({QStringLiteral("CAN1"), QStringLiteral("CAN2")}));

    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    auto singleBoundary = model->index(
        0, SequenceTreeModel::ResourceRegionColumn, mainGroup);
    QCOMPARE(singleBoundary.data().toString(), QStringLiteral("LOCK/UNLOCK"));
    QCOMPARE(singleBoundary.data(SequenceTreeModel::ResourceMarkerRole).toInt(), 3);
    const auto resourceLockScreenshot = qEnvironmentVariable(
        "PICOATE_RESOURCE_LOCK_SCREENSHOT");
    if (!resourceLockScreenshot.isEmpty()) {
        QCoreApplication::processEvents();
        QVERIFY2(window.grab().save(resourceLockScreenshot),
                 qPrintable(QStringLiteral("Failed to save resource-lock screenshot: %1")
                                .arg(resourceLockScreenshot)));
    }

    // Clicking a completed single-item lock removes both boundaries.
    clickBoundary(singleBoundary.siblingAtColumn(SequenceTreeModel::NameColumn));
    QVERIFY(document->objectAt(SequenceItemPath{1, {0}})
                .value(QStringLiteral("resourceRegionStart")).toObject().isEmpty());
    QVERIFY(document->objectAt(SequenceItemPath{1, {0}})
                .value(QStringLiteral("resourceRegionEnd")).toString().isEmpty());

    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    entry = model->index(0, SequenceTreeModel::NameColumn, mainGroup);
    clickBoundary(entry);

    const auto pendingId = document->pendingResourceRegionId();
    QVERIFY(!pendingId.isEmpty());
    const auto start = document->objectAt(SequenceItemPath{1, {0}})
                           .value(QStringLiteral("resourceRegionStart"))
                           .toObject();
    QVERIFY(start.value(QStringLiteral("resources")).toArray().isEmpty());

    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    auto exit = model->index(2, SequenceTreeModel::NameColumn, mainGroup);
    clickBoundaryWithHardware(exit);

    QVERIFY(document->pendingResourceRegionId().isEmpty());
    QCOMPARE(document->objectAt(SequenceItemPath{1, {2}})
                 .value(QStringLiteral("resourceRegionEnd")).toString(),
             pendingId);
    QCOMPARE(document->resourceRegionResources(pendingId),
             QStringList({QStringLiteral("CAN1"), QStringLiteral("CAN2")}));

    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    const auto entryBoundary = model->index(
        0, SequenceTreeModel::ResourceRegionColumn, mainGroup);
    const auto middle = model->index(1, SequenceTreeModel::NameColumn, mainGroup);
    const auto exitBoundary = model->index(
        2, SequenceTreeModel::ResourceRegionColumn, mainGroup);
    const auto exitChild = model->index(
        0, SequenceTreeModel::NameColumn,
        model->index(2, SequenceTreeModel::NameColumn, mainGroup));
    QCOMPARE(entryBoundary.data().toString(), QStringLiteral("LOCK"));
    QCOMPARE(exitBoundary.data().toString(), QStringLiteral("UNLOCK"));
    QVERIFY(middle.data(Qt::BackgroundRole).isValid());
    QVERIFY(exitChild.data(Qt::BackgroundRole).isValid());

    // Clicking UNLOCK removes only the end marker and keeps LOCK pending.
    clickBoundary(exitBoundary.siblingAtColumn(SequenceTreeModel::NameColumn));
    QCOMPARE(document->pendingResourceRegionId(), pendingId);
    QVERIFY(document->objectAt(SequenceItemPath{1, {2}})
                .value(QStringLiteral("resourceRegionEnd")).toString().isEmpty());

    // Replacing a removed UNLOCK with the previously selected hardware keeps
    // both boundaries intact.
    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    exit = model->index(2, SequenceTreeModel::NameColumn, mainGroup);
    clickBoundaryWithHardware(exit);
    QVERIFY(document->pendingResourceRegionId().isEmpty());
    QCOMPARE(document->objectAt(SequenceItemPath{1, {0}})
                 .value(QStringLiteral("resourceRegionStart"))
                 .toObject().value(QStringLiteral("id")).toString(),
             pendingId);
    QCOMPARE(document->objectAt(SequenceItemPath{1, {2}})
                 .value(QStringLiteral("resourceRegionEnd")).toString(),
             pendingId);

    // Return to a pending LOCK for the cancellation rollback scenario.
    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    exit = model->index(2, SequenceTreeModel::NameColumn, mainGroup);
    clickBoundary(exit);
    QCOMPARE(document->pendingResourceRegionId(), pendingId);

    // Cancelling hardware selection rolls the pending LOCK and new UNLOCK back.
    bool cancelled = false;
    QTimer cancelTimer;
    QObject::connect(&cancelTimer, &QTimer::timeout, &cancelTimer, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() !=
                           QStringLiteral("resourceRegionResourceDialog")) {
            return;
        }
        cancelled = true;
        dialog->reject();
    });
    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    exit = model->index(2, SequenceTreeModel::NameColumn, mainGroup);
    cancelTimer.start(10);
    clickBoundary(exit);
    cancelTimer.stop();
    QVERIFY(cancelled);
    QVERIFY(document->pendingResourceRegionId().isEmpty());
    QVERIFY(document->objectAt(SequenceItemPath{1, {0}})
                .value(QStringLiteral("resourceRegionStart")).toObject().isEmpty());
    QVERIFY(document->objectAt(SequenceItemPath{1, {2}})
                .value(QStringLiteral("resourceRegionEnd")).toString().isEmpty());

    // A nested interval is allowed between sibling child Steps.
    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    auto testItem = model->index(2, SequenceTreeModel::NameColumn, mainGroup);
    tree->expand(testItem);
    auto childEntry = model->index(0, SequenceTreeModel::NameColumn, testItem);
    clickBoundary(childEntry);
    const auto nestedId = document->pendingResourceRegionId();
    QVERIFY(!nestedId.isEmpty());
    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    testItem = model->index(2, SequenceTreeModel::NameColumn, mainGroup);
    tree->expand(testItem);
    auto childExit = model->index(2, SequenceTreeModel::NameColumn, testItem);
    clickBoundaryWithHardware(childExit);
    QVERIFY(document->pendingResourceRegionId().isEmpty());
    QCOMPARE(document->objectAt(SequenceItemPath{1, {2, 0}})
                 .value(QStringLiteral("resourceRegionStart"))
                 .toObject().value(QStringLiteral("id")).toString(),
             nestedId);
    QCOMPARE(document->objectAt(SequenceItemPath{1, {2, 2}})
                 .value(QStringLiteral("resourceRegionEnd")).toString(),
             nestedId);
    QCOMPARE(document->resourceRegionResources(nestedId),
             QStringList({QStringLiteral("CAN1"), QStringLiteral("CAN2")}));

    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    testItem = model->index(2, SequenceTreeModel::NameColumn, mainGroup);
    tree->expand(testItem);
    childEntry = model->index(0, SequenceTreeModel::NameColumn, testItem);
    const auto childMiddle = model->index(1, SequenceTreeModel::NameColumn, testItem);
    childExit = model->index(2, SequenceTreeModel::NameColumn, testItem);
    QCOMPARE(childEntry.siblingAtColumn(SequenceTreeModel::ResourceRegionColumn)
                 .data().toString(), QStringLiteral("LOCK"));
    QCOMPARE(childExit.siblingAtColumn(SequenceTreeModel::ResourceRegionColumn)
                 .data().toString(), QStringLiteral("UNLOCK"));
    QVERIFY(childMiddle.data(Qt::BackgroundRole).isValid());

    // Clicking the nested LOCK removes the complete nested interval.
    clickBoundary(childEntry);
    QVERIFY(document->pendingResourceRegionId().isEmpty());
    QVERIFY(document->objectAt(SequenceItemPath{1, {2, 0}})
                .value(QStringLiteral("resourceRegionStart")).toObject().isEmpty());
    QVERIFY(document->objectAt(SequenceItemPath{1, {2, 2}})
                .value(QStringLiteral("resourceRegionEnd")).toString().isEmpty());

    // A TestItem can be locked as one item; its complete child subtree is covered.
    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    testItem = model->index(2, SequenceTreeModel::NameColumn, mainGroup);
    clickBoundary(testItem);
    const auto testItemRegionId = document->pendingResourceRegionId();
    QVERIFY(!testItemRegionId.isEmpty());
    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    testItem = model->index(2, SequenceTreeModel::NameColumn, mainGroup);
    clickBoundaryWithHardware(testItem);
    QVERIFY(document->pendingResourceRegionId().isEmpty());
    const auto testItemObject = document->objectAt(SequenceItemPath{1, {2}});
    QCOMPARE(testItemObject.value(QStringLiteral("resourceRegionEnd")).toString(),
             testItemRegionId);

    mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    testItem = model->index(2, SequenceTreeModel::NameColumn, mainGroup);
    tree->expand(testItem);
    const auto testItemBoundary = testItem.siblingAtColumn(
        SequenceTreeModel::ResourceRegionColumn);
    QCOMPARE(testItemBoundary.data().toString(), QStringLiteral("LOCK/UNLOCK"));
    QCOMPARE(testItemBoundary.data(SequenceTreeModel::ResourceMarkerRole).toInt(), 3);
    const auto lockedChild = model->index(1, SequenceTreeModel::NameColumn, testItem);
    QVERIFY(lockedChild.data(Qt::BackgroundRole).isValid());

    clickBoundary(testItem);
    QVERIFY(document->objectAt(SequenceItemPath{1, {2}})
                .value(QStringLiteral("resourceRegionStart")).toObject().isEmpty());
    QVERIFY(document->objectAt(SequenceItemPath{1, {2}})
                .value(QStringLiteral("resourceRegionEnd")).toString().isEmpty());

    QString saveError;
    QVERIFY2(document->save(&saveError), qPrintable(saveError));
    QFile savedSequence(sequencePath);
    QVERIFY(savedSequence.open(QIODevice::ReadOnly));
    const auto savedJson = savedSequence.readAll();
    QVERIFY(!savedJson.contains("resourceRegionStart"));
    QVERIFY(!savedJson.contains("resourceRegionEnd"));

    document->undoStack()->setClean();
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::sequenceVariablesToolbarEditsPerUutValuesAndFeedsFxMenu()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("variables.json"));
    QFile sequence(sequencePath);
    QVERIFY(sequence.open(QIODevice::WriteOnly));
    sequence.write(R"({
      "id":"variables-ui",
      "name":"Variables UI",
      "groups":[{
        "id":"main",
        "kind":"main",
        "steps":[{
          "id":"001",
          "name":"Check ID",
          "kind":"limit",
          "parameters":{"comparison":"equal","actual":0,"expected":1}
        }]
      }]
    })");
    sequence.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    window.show();
    QCoreApplication::processEvents();

    auto* action = window.findChild<QAction*>(
        QStringLiteral("sequenceVariablesAction"));
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    bool handled = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<SequenceVariablesDialog*>(
            QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto* add = dialog->findChild<QPushButton*>(
            QStringLiteral("addSequenceVariableButton"));
        auto* addUut = dialog->findChild<QPushButton*>(
            QStringLiteral("addSequenceUutButton"));
        auto* table = dialog->findChild<QTableWidget*>(
            QStringLiteral("sequenceVariablesTable"));
        QVERIFY(add);
        QVERIFY(addUut);
        QVERIFY(table);
        QCOMPARE(table->columnCount(), 9);
        addUut->click();
        addUut->click();
        QCOMPARE(table->columnCount(), 11);
        QCOMPARE(table->horizontalHeaderItem(8)->text(), QStringLiteral("UUT5"));
        QCOMPARE(table->horizontalHeaderItem(9)->text(), QStringLiteral("UUT6"));
        QCOMPARE(table->horizontalHeaderItem(10)->text(),
                 QStringLiteral("Description"));
        add->click();
        QCOMPARE(table->rowCount(), 1);
        table->item(0, 0)->setText(QStringLiteral("CAN_ID"));
        auto* type = qobject_cast<QComboBox*>(table->cellWidget(0, 1));
        auto* scope = qobject_cast<QComboBox*>(table->cellWidget(0, 2));
        QVERIFY(type);
        QVERIFY(scope);
        QVERIFY(type->width() >= type->sizeHint().width());
        QVERIFY(scope->width() >= scope->sizeHint().width());
        type->setCurrentIndex(type->findData(QStringLiteral("hex")));
        scope->setCurrentIndex(scope->findData(QStringLiteral("perUut")));
        table->item(0, 4)->setText(QStringLiteral("0x101"));
        table->item(0, 5)->setText(QStringLiteral("0x102"));
        table->item(0, 6)->setText(QStringLiteral("0x103"));
        table->item(0, 7)->setText(QStringLiteral("0x104"));
        table->item(0, 8)->setText(QStringLiteral("0x105"));
        table->item(0, 9)->setText(QStringLiteral("0x106"));
        table->item(0, 10)->setText(
            QStringLiteral("CAN identifier by fixture slot"));
        QCoreApplication::processEvents();
        QVERIFY(table->horizontalScrollBar()->maximum() > 0);
        handled = true;
        dialog->accept();
    });
    action->trigger();
    QVERIFY(handled);

    auto* document = window.findChild<SequenceDocument*>();
    QVERIFY(document);
    QCOMPARE(document->sequenceVariables().size(), 1);
    const auto variable = document->sequenceVariables().first().toObject();
    QCOMPARE(variable.value("name").toString(), QString("CAN_ID"));
    QCOMPARE(variable.value("scope").toString(), QString("perUut"));
    QCOMPARE(variable.value("values").toArray().size(), 6);
    QCOMPARE(variable.value("values").toArray().at(5).toString(),
             QStringLiteral("0x106"));
    QVERIFY(document->isModified());

    SequenceVariablesDialog reloaded(document->sequenceVariables());
    auto* reloadedTable = reloaded.findChild<QTableWidget*>(
        QStringLiteral("sequenceVariablesTable"));
    QVERIFY(reloadedTable);
    QCOMPARE(reloadedTable->columnCount(), 11);
    QCOMPARE(reloadedTable->horizontalHeaderItem(9)->text(),
             QStringLiteral("UUT6"));
    QCOMPARE(reloadedTable->item(0, 9)->text(), QStringLiteral("0x106"));

    StepPropertyEditor editor(document);
    editor.setCurrentItem(SequenceItemPath{0, {0}});
    QToolButton* expressionButton = nullptr;
    for (auto* button : editor.findChildren<QToolButton*>(
             QStringLiteral("expressionPickerButton"))) {
        if (chooseExpression(button, QStringLiteral("${var.CAN_ID}"))) {
            expressionButton = button;
            break;
        }
    }
    QVERIFY(expressionButton);
    QVERIFY(expressionButton->isEnabled());
    bool expressionInserted = false;
    for (auto* lineEdit : editor.findChildren<QLineEdit*>()) {
        if (lineEdit->text() == QStringLiteral("${var.CAN_ID}")) {
            expressionInserted = true;
            break;
        }
    }
    QVERIFY(expressionInserted);

    QVERIFY(chooseExpression(expressionButton,
                             QStringLiteral("${var.serialNumber}")));
    bool serialExpressionInserted = false;
    for (auto* lineEdit : editor.findChildren<QLineEdit*>()) {
        if (lineEdit->text() == QStringLiteral("${var.serialNumber}")) {
            serialExpressionInserted = true;
            break;
        }
    }
    QVERIFY(serialExpressionInserted);

    document->undoStack()->undo();
    QVERIFY(document->sequenceVariables().isEmpty());
    document->undoStack()->redo();
    QCOMPARE(document->sequenceVariables().size(), 1);
    document->undoStack()->setClean();
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::sequenceVariablesDeleteUsesExplicitStableSelection()
{
    const auto variables = QJsonDocument::fromJson(R"json({
      "variables":[
        {"name":"CAN_ID","type":"hex","scope":"shared","value":"0x101"},
        {"name":"FILTER_ID","type":"hex","scope":"shared","value":"0x102"}
      ]
    })json").object().value(QStringLiteral("variables")).toArray();
    SequenceVariablesDialog dialog(variables);
    dialog.show();
    QTest::qWait(20);

    auto* table = dialog.findChild<QTableWidget*>(
        QStringLiteral("sequenceVariablesTable"));
    auto* remove = dialog.findChild<QPushButton*>(
        QStringLiteral("removeSequenceVariableButton"));
    QVERIFY(table);
    QVERIFY(remove);
    QCOMPARE(table->selectionMode(), QAbstractItemView::SingleSelection);
    QVERIFY(!remove->isEnabled());

    const auto firstCell = table->visualItemRect(table->item(0, 0)).center();
    QTest::mouseClick(table->viewport(), Qt::LeftButton,
                      Qt::NoModifier, firstCell);
    QVERIFY(remove->isEnabled());
    QCOMPARE(table->selectionModel()->selectedRows().first().row(), 0);

    auto* secondType = qobject_cast<QComboBox*>(table->cellWidget(1, 1));
    QVERIFY(secondType);
    secondType->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    QCOMPARE(table->selectionModel()->selectedRows().first().row(), 0);

    QTimer::singleShot(0, [] {
        auto* message = qobject_cast<QMessageBox*>(
            QApplication::activeModalWidget());
        QVERIFY(message);
        auto* yes = message->button(QMessageBox::Yes);
        QVERIFY(yes);
        yes->click();
    });
    remove->click();
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("FILTER_ID"));
    QVERIFY(!remove->isEnabled());
    QVERIFY(table->selectionModel()->selectedRows().isEmpty());
}

void MainWindowLifecycleTests::ctrlSaveCommitsCurrentStepDraftWithoutPrompt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("draft_save.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    window.show();
    QTest::qWait(20);
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    auto* editor = window.findChild<StepPropertyEditor*>();
    auto* save = window.findChild<QAction*>(QStringLiteral("saveSequenceAction"));
    QVERIFY(tree && model && editor && save);
    const auto step = model->index(0, SequenceTreeModel::NameColumn,
                                   model->index(0, SequenceTreeModel::NameColumn));
    tree->setCurrentIndex(step);
    auto* name = editor->findChild<QLineEdit*>(QStringLiteral("propertyNameEdit"));
    QVERIFY(name);
    const auto path = model->pathForIndex(step);
    const auto oldName = window.findChild<SequenceDocument*>()->objectAt(path)
                             .value(QStringLiteral("name")).toString();
    name->setText(QStringLiteral("Saved through Ctrl+S"));
    QVERIFY(editor->hasPendingChanges());
    QVERIFY(save->isEnabled());
    QCOMPARE(window.findChild<SequenceDocument*>()->objectAt(path)
                 .value(QStringLiteral("name")).toString(), oldName);

    bool confirmationShown = false;
    QTimer::singleShot(0, [&confirmationShown] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* messageBox = qobject_cast<QMessageBox*>(widget)) {
                confirmationShown = true;
                messageBox->reject();
            }
        }
    });
    save->trigger();
    QCoreApplication::processEvents();
    QVERIFY(!confirmationShown);
    QVERIFY(!editor->hasPendingChanges());

    QFile saved(sequencePath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto root = QJsonDocument::fromJson(saved.readAll()).object();
    const auto savedStep = root.value(QStringLiteral("groups")).toArray().at(0)
                               .toObject().value(QStringLiteral("steps")).toArray().at(0)
                               .toObject();
    QCOMPARE(savedStep.value(QStringLiteral("name")).toString(),
             QStringLiteral("Saved through Ctrl+S"));
}

void MainWindowLifecycleTests::deletingStepDiscardsItsInvalidPropertyDraft()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("delete_invalid_draft.json"));
    QFile file(sequencePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"json({
      "id":"delete-invalid-draft","name":"Delete Invalid Draft","groups":[{
        "id":"main","kind":"main","steps":[{
          "id":"discard-me","name":"Discard Me","kind":"action"
        }]
      }]
    })json");
    file.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    window.show();
    QTest::qWait(20);

    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    auto* document = window.findChild<SequenceDocument*>();
    auto* editor = window.findChild<StepPropertyEditor*>();
    auto* remove = window.findChild<QAction*>(QStringLiteral("deleteStepAction"));
    QVERIFY(tree && model && document && editor && remove);

    const auto main = sequenceGroupByKind(model, QStringLiteral("main"));
    const auto step = model->index(0, SequenceTreeModel::NameColumn, main);
    QVERIFY(step.isValid());
    tree->setCurrentIndex(step);
    const auto path = model->pathForIndex(step);
    auto* idEdit = editor->findChild<QLineEdit*>(
        QStringLiteral("propertyIdEdit"));
    QVERIFY(idEdit);
    idEdit->clear();
    QVERIFY(editor->hasPendingChanges());
    QVERIFY(!editor->commitPendingChanges());

    remove->trigger();
    QCoreApplication::processEvents();
    QVERIFY(document->objectAt(path).isEmpty());
    QVERIFY(!editor->hasPendingChanges());
    QCOMPARE(model->rowCount(main), 0);
}

void MainWindowLifecycleTests::switchingStepsKeepsDraftWithoutPrompt()
{
    MainWindow window;
    const auto sequencePath = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/simple_sequence.json");
    QVERIFY(window.openSequenceFile(sequencePath));
    window.show();
    QTest::qWait(20);
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    auto* editor = window.findChild<StepPropertyEditor*>();
    auto* document = window.findChild<SequenceDocument*>();
    QVERIFY(tree && model && editor && document);
    const auto first = model->index(0, SequenceTreeModel::NameColumn,
                                    model->index(0, SequenceTreeModel::NameColumn));
    const auto second = model->index(0, SequenceTreeModel::NameColumn,
                                     model->index(1, SequenceTreeModel::NameColumn));
    QVERIFY(first.isValid() && second.isValid());
    tree->setCurrentIndex(first);
    const auto firstPath = model->pathForIndex(first);
    const auto secondPath = model->pathForIndex(second);
    auto* name = editor->findChild<QLineEdit*>(QStringLiteral("propertyNameEdit"));
    QVERIFY(name);
    name->setText(QStringLiteral("Keep this draft"));
    QVERIFY(editor->hasPendingChanges());

    bool confirmationShown = false;
    QTimer::singleShot(0, [&confirmationShown] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* messageBox = qobject_cast<QMessageBox*>(widget)) {
                confirmationShown = true;
                messageBox->reject();
            }
        }
    });
    tree->setCurrentIndex(second);
    QCoreApplication::processEvents();
    QVERIFY(!confirmationShown);
    QCOMPARE(editor->currentPath(), secondPath);
    QVERIFY(!editor->hasPendingChanges());
    QCOMPARE(document->objectAt(firstPath).value(QStringLiteral("name")).toString(),
             QStringLiteral("Keep this draft"));
    QVERIFY(document->isModified());
    document->undoStack()->setClean();
}

void MainWindowLifecycleTests::leavingFlowPromptsOnceAndCanKeepDraft()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("flow_draft.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    window.show();
    QTest::qWait(20);
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    auto* flowPage = window.findChild<QWidget*>(QStringLiteral("sequenceEditorPage"));
    auto* stationPage = window.findChild<QWidget*>(QStringLiteral("stationEditorPage"));
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    auto* editor = window.findChild<StepPropertyEditor*>();
    auto* document = window.findChild<SequenceDocument*>();
    QVERIFY(tabs && flowPage && stationPage && tree && model && editor && document);
    tabs->setCurrentWidget(flowPage);
    const auto step = model->index(0, SequenceTreeModel::NameColumn,
                                   model->index(0, SequenceTreeModel::NameColumn));
    tree->setCurrentIndex(step);
    auto* name = editor->findChild<QLineEdit*>(QStringLiteral("propertyNameEdit"));
    QVERIFY(name);
    name->setText(QStringLiteral("Flow draft kept in memory"));

    int promptCount = 0;
    QTimer::singleShot(0, [&promptCount] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            auto* messageBox = qobject_cast<QMessageBox*>(widget);
            if (!messageBox) {
                continue;
            }
            ++promptCount;
            for (auto* button : messageBox->buttons()) {
                if (button->text().contains(QStringLiteral("Keep Draft"))) {
                    button->click();
                    return;
                }
            }
        }
    });
    tabs->setCurrentWidget(stationPage);
    QCOMPARE(promptCount, 1);
    QCOMPARE(tabs->currentWidget(), stationPage);
    QVERIFY(!editor->hasPendingChanges());
    QVERIFY(document->isModified());
    document->undoStack()->setClean();
}

void MainWindowLifecycleTests::stepEditorRelocatesDraftAfterSequenceStructureChanges()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("relocate_draft.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"json({
      "id":"relocate","name":"Relocate Draft","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"001","name":"First","kind":"noop"},
          {"id":"002","name":"Second","kind":"noop"}
        ]
      }]
    })json");
    sequenceFile.close();

    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    const SequenceItemPath originalPath{0, {1}};
    editor.setCurrentItem(originalPath);
    auto* name = editor.findChild<QLineEdit*>(QStringLiteral("propertyNameEdit"));
    auto* error = editor.findChild<QLabel*>(QStringLiteral("propertyErrorLabel"));
    QVERIFY(name && error);
    name->setText(QStringLiteral("Second draft after insert"));
    QVERIFY(editor.hasPendingChanges());

    QVERIFY(document.insertStep(SequenceItemPath{0, {}}, 0,
                                QJsonObject{{QStringLiteral("id"), QStringLiteral("000")},
                                            {QStringLiteral("name"), QStringLiteral("Inserted")},
                                            {QStringLiteral("kind"), QStringLiteral("noop")}}));
    QVERIFY(editor.commitPendingChanges());
    QCOMPARE(editor.currentPath(), (SequenceItemPath{0, {2}}));
    QCOMPARE(document.objectAt(SequenceItemPath{0, {2}})
                 .value(QStringLiteral("name")).toString(),
             QStringLiteral("Second draft after insert"));
    QVERIFY(!error->isVisible());
}

void MainWindowLifecycleTests::resultSoFarIsAvailableInFxWithoutChangingItsToken()
{
    auto& language = UiLanguage::instance();
    const bool wasChinese = language.isChinese();
    const auto restore = qScopeGuard([&] { language.setChinese(wasChinese, false); });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("result_gate.json"));
    QFile file(sequencePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"id":"gate","name":"Gate","groups":[{"id":"main","kind":"main","steps":[
      {"id":"probe","kind":"action"},
      {"id":"gate","kind":"limit","inputs":{"actual":""},"parameters":{"comparison":"equal","expected":"PASS"}}
    ]}]})");
    file.close();
    for (const bool chinese : {false, true}) {
        QVERIFY(language.setChinese(chinese, false));
        SequenceDocument document;
        QVERIFY(document.load(sequencePath));
        StepPropertyEditor editor(&document);
        const SequenceItemPath path{0, {1}};
        editor.setCurrentItem(path);
        editor.show();
        auto* actual = editor.findChild<QLineEdit*>(QStringLiteral("propertyLimitActualEdit"));
        QVERIFY(actual);
        auto* picker = actual->parentWidget()->findChild<QToolButton*>(QStringLiteral("expressionPickerButton"));
        QVERIFY(picker);
        QVERIFY(chooseExpression(picker, QStringLiteral("${uut.resultSoFar}")));
        QCOMPARE(actual->text(), QStringLiteral("${uut.resultSoFar}"));
        QVERIFY(editor.commitPendingChanges());
        const auto step = document.objectAt(path);
        QCOMPARE(step.value("inputs").toObject().value("actual").toString(), QStringLiteral("${uut.resultSoFar}"));
        QCOMPARE(step.value("parameters").toObject().value("expected").toString(), QStringLiteral("PASS"));
    }
}

void MainWindowLifecycleTests::limitPropertyEditorSwitchesComparisonFieldsAndRemovesStaleValues()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("limit_editor.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"({
      "id":"limit-editor","name":"Limit Editor","groups":[{
        "id":"main","kind":"main","steps":[{
          "id":"001","name":"Check DLC","kind":"limit",
          "inputs":{"actual":"${step:000.outputs.dlc}"},
          "parameters":{"comparison":"between","expected":8,
                        "tolerance":0,"inclusive":true,
                        "measurementName":"DLC","unit":"byte"}
        }]
      }]
    })");
    sequenceFile.close();

    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    const SequenceItemPath path{0, {0}};
    editor.setCurrentItem(path);
    editor.show();
    QTest::qWait(20);

    auto* comparison = editor.findChild<QComboBox*>(
        QStringLiteral("propertyLimitComparisonCombo"));
    auto* expected = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyLimitExpectedEdit"));
    auto* lower = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyLimitLowerEdit"));
    auto* upper = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyLimitUpperEdit"));
    auto* tolerance = editor.findChild<QDoubleSpinBox*>(
        QStringLiteral("propertyLimitToleranceSpin"));
    QVERIFY(comparison && expected && lower && upper && tolerance);
    auto* expectedField = editor.findChild<QWidget*>(
        QStringLiteral("propertyLimitExpectedField"));
    QVERIFY(expectedField);
    auto* expectedPicker = expectedField->findChild<QToolButton*>(
        QStringLiteral("expressionPickerButton"));
    QVERIFY(expectedPicker);
    QVERIFY(chooseExpression(expectedPicker,
                             QStringLiteral("${var.serialNumber}")));
    QCOMPARE(expected->text(), QStringLiteral("${var.serialNumber}"));
    QCOMPARE(comparison->currentData().toString(),
             QStringLiteral("betweenTolerance"));
    QVERIFY(!expectedField->isHidden());
    QVERIFY(!tolerance->isHidden());
    QVERIFY(lower->isHidden());

    comparison->setCurrentIndex(comparison->findData(QStringLiteral("greaterThan")));
    expected->setText(QStringLiteral("10"));
    QVERIFY(tolerance->isHidden());
    QVERIFY(editor.commitPendingChanges());
    auto parameters = document.objectAt(path).value(QStringLiteral("parameters")).toObject();
    QCOMPARE(parameters.value(QStringLiteral("comparison")).toString(),
             QStringLiteral("greaterThan"));
    QCOMPARE(parameters.value(QStringLiteral("expected")).toInt(), 10);
    QVERIFY(!parameters.contains(QStringLiteral("tolerance")));

    comparison->setCurrentIndex(comparison->findData(QStringLiteral("betweenLimits")));
    lower->setText(QStringLiteral("1.5"));
    upper->setText(QStringLiteral("9.5"));
    QVERIFY(!lower->isHidden());
    QVERIFY(!upper->isHidden());
    QVERIFY(expectedField->isHidden());
    QVERIFY(editor.commitPendingChanges());
    parameters = document.objectAt(path).value(QStringLiteral("parameters")).toObject();
    QCOMPARE(parameters.value(QStringLiteral("comparison")).toString(),
             QStringLiteral("between"));
    QCOMPARE(parameters.value(QStringLiteral("lower")).toDouble(), 1.5);
    QCOMPARE(parameters.value(QStringLiteral("upper")).toDouble(), 9.5);
    QVERIFY(!parameters.contains(QStringLiteral("expected")));

    comparison->setCurrentIndex(comparison->findData(QStringLiteral("equal")));
    expected->setText(QStringLiteral("0.00300"));
    QVERIFY(editor.commitPendingChanges());
    parameters = document.objectAt(path).value(QStringLiteral("parameters")).toObject();
    QCOMPARE(parameters.value(QStringLiteral("expected")).toDouble(), 0.003);
    QCOMPARE(parameters.value(QStringLiteral("decimalPlaces")).toInt(), 5);

    StepPropertyEditor reloadedEditor(&document);
    reloadedEditor.setCurrentItem(path);
    auto* reloadedExpected = reloadedEditor.findChild<QLineEdit*>(
        QStringLiteral("propertyLimitExpectedEdit"));
    QVERIFY(reloadedExpected);
    QCOMPARE(reloadedExpected->text(), QStringLiteral("0.00300"));

    expected->setText(QStringLiteral("1000000000123456789"));
    QVERIFY(editor.commitPendingChanges());
    parameters = document.objectAt(path).value(QStringLiteral("parameters")).toObject();
    QVERIFY(parameters.value(QStringLiteral("expected")).isString());
    QCOMPARE(parameters.value(QStringLiteral("expected")).toString(),
             QStringLiteral("1000000000123456789"));
    QVERIFY(!parameters.contains(QStringLiteral("decimalPlaces")));

    comparison->setCurrentIndex(comparison->findData(QStringLiteral("isTrue")));
    QVERIFY(expectedField->isHidden());
    QVERIFY(lower->isHidden());
    QVERIFY(editor.commitPendingChanges());
    parameters = document.objectAt(path).value(QStringLiteral("parameters")).toObject();
    QCOMPARE(parameters.value(QStringLiteral("comparison")).toString(),
             QStringLiteral("isTrue"));
    QVERIFY(!parameters.contains(QStringLiteral("expected")));
    QVERIFY(!parameters.contains(QStringLiteral("lower")));
    QVERIFY(!parameters.contains(QStringLiteral("upper")));
    QVERIFY(!parameters.contains(QStringLiteral("tolerance")));
    QVERIFY(!parameters.contains(QStringLiteral("decimalPlaces")));
}

void MainWindowLifecycleTests::closeAfterEditedRun_data()
{
    QTest::addColumn<int>("closeChoice");
    QTest::newRow("discard") << int(QMessageBox::Discard);
    QTest::newRow("save") << int(QMessageBox::Save);
}

void MainWindowLifecycleTests::closeAfterEditedRun()
{
    QFETCH(int, closeChoice);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const auto sequencePath = temporaryDirectory.filePath(
        QStringLiteral("edited_sequence.json"));
    const auto sourcePath = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/simple_sequence.json");
    QVERIFY2(QFile::copy(sourcePath, sequencePath), qPrintable(sourcePath));

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    window.show();
    QTest::qWait(20);

    auto* treeView = window.findChild<QTreeView*>(
        QStringLiteral("sequenceTreeView"));
    auto* treeModel = window.findChild<SequenceTreeModel*>();
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    QVERIFY(treeView);
    QVERIFY(treeModel);
    QVERIFY(viewModel);

    const auto group = treeModel->index(0, 0);
    const auto step = treeModel->index(0, 0, group);
    QVERIFY(step.isValid());
    treeView->setCurrentIndex(step);
    const auto enabled = step.siblingAtColumn(SequenceTreeModel::EnabledColumn);
    QVERIFY(treeModel->setData(enabled, Qt::Unchecked, Qt::CheckStateRole));

    viewModel->compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    viewModel->run();
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             3000);

    QTimer::singleShot(0, [closeChoice] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* messageBox = qobject_cast<QMessageBox*>(widget)) {
                messageBox->done(closeChoice);
            }
        }
    });
    QVERIFY(window.close());
    QCoreApplication::processEvents();
}

void MainWindowLifecycleTests::stationEditorFeedsCompileSnapshot_data()
{
    QTest::addColumn<int>("closeChoice");
    QTest::newRow("discard") << int(QMessageBox::Discard);
    QTest::newRow("save") << int(QMessageBox::Save);
}

void MainWindowLifecycleTests::stationEditorFeedsCompileSnapshot()
{
    QFETCH(int, closeChoice);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const auto stationPath = temporaryDirectory.filePath(
        QStringLiteral("station.json"));
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(R"({
        "stationId": "window-station",
        "x-root": true,
        "devices": [{
            "deviceId": "DMM1",
            "deviceType": "DMM",
            "driverId": "original.driver",
            "address": "USB::1",
            "lifetime": "Station",
            "enabled": false,
            "x-device": 42
        }]
    })");
    stationFile.close();

    StationDocument stationDocument;
    QVERIFY(stationDocument.load(stationPath));
    StationPropertyEditor propertyEditor(&stationDocument);
    propertyEditor.setEditable(true);
    propertyEditor.setStationPageVisible(false);
    propertyEditor.setCurrentDevice(0);
    propertyEditor.show();
    QTest::qWait(20);

    auto* addressEdit = propertyEditor.findChild<QLineEdit*>(
        QStringLiteral("deviceAddressEdit"));
    QVERIFY(addressEdit);
    QVERIFY(!propertyEditor.findChild<QPushButton*>(
        QStringLiteral("applyDevicePropertiesButton")));

    addressEdit->setFocus();
    addressEdit->selectAll();
    QTest::keyClicks(addressEdit, QStringLiteral("TCPIP::snapshot"));
    QVERIFY(propertyEditor.hasPendingChanges());
    QVERIFY(propertyEditor.commitPendingChanges());
    QCOMPARE(stationDocument.deviceAt(0).value("resource").toString(),
             QString("TCPIP::snapshot"));
    QCOMPARE(stationDocument.deviceAt(0).value("x-device").toInt(), 42);
    QVERIFY(stationDocument.isModified());

    Q_UNUSED(closeChoice);
}

void MainWindowLifecycleTests::flowTargetSelectorHandlesChannelsAndMoreDevices()
{
    FlowTargetSelector selector;
    QVector<FlowTargetDevice> devices;
    devices.push_back({QStringLiteral("CAN1"), QStringLiteral("CAN"),
                       QStringLiteral("CX"), QStringLiteral("plugin.can.cx"),
                       {QStringLiteral("CAN1.CH1"), QStringLiteral("CAN1.CH2")},
                       {QStringLiteral("CH1"), QStringLiteral("CH2")}, true});
    devices.push_back({QStringLiteral("CAN2"), QStringLiteral("CAN"),
                       QStringLiteral("GCAN"), QStringLiteral("plugin.can.gcan"),
                       {QStringLiteral("CAN2.CH1"), QStringLiteral("CAN2.CH2")},
                       {QStringLiteral("CH1"), QStringLiteral("CH2")}, true});
    devices.push_back({QStringLiteral("DMM1"), QStringLiteral("DMM"),
                       QStringLiteral("Keysight"), QStringLiteral("plugin.dmm"),
                       {QStringLiteral("DMM1")}, {QStringLiteral("DMM1")}, true});
    devices.push_back({QStringLiteral("PSU1"), QStringLiteral("PSU"),
                       QStringLiteral("ITECH"), QStringLiteral("plugin.psu"),
                       {QStringLiteral("PSU1")}, {QStringLiteral("PSU1")}, true});
    devices.push_back({QStringLiteral("SCOPE1"), QStringLiteral("SCOPE"),
                       QStringLiteral("Rigol"), QStringLiteral("plugin.scope"),
                       {QStringLiteral("SCOPE1")}, {QStringLiteral("SCOPE1")}, true});
    devices.push_back({QStringLiteral("SERIAL1"), QStringLiteral("SERIAL"),
                       {}, {}, {QStringLiteral("SERIAL1")},
                       {QStringLiteral("SERIAL1")}, false});

    QSignalSpy targetChanged(&selector, &FlowTargetSelector::targetChanged);
    selector.setDevices(devices);
    QVERIFY(selector.currentDeviceId().isEmpty());
    QVERIFY(selector.currentTargetId().isEmpty());

    const auto shortcutButtons = selector.findChildren<QToolButton*>();
    QCOMPARE(std::count_if(shortcutButtons.cbegin(), shortcutButtons.cend(),
                           [](const QToolButton* button) {
                               return button->property("deviceShortcut").toBool();
                           }), 4);
    auto* more = selector.findChild<QToolButton*>(
        QStringLiteral("flowMoreDevices"));
    QVERIFY(more);
    QVERIFY(!more->isHidden());
    QVERIFY(more->menu());

    QVERIFY(selector.selectTarget(QStringLiteral("CAN2.CH2")));
    QCOMPARE(selector.currentDeviceId(), QStringLiteral("CAN2"));
    QCOMPARE(selector.currentTargetId(), QStringLiteral("CAN2.CH2"));
    const auto channelButtons = selector.findChildren<QToolButton*>();
    const auto checkedChannel = std::find_if(
        channelButtons.cbegin(), channelButtons.cend(),
        [](const QToolButton* button) {
            return button->property("channelButton").toBool() &&
                   button->isChecked();
        });
    QVERIFY(checkedChannel != channelButtons.cend());
    QCOMPARE((*checkedChannel)->text(), QStringLiteral("CH2"));

    const auto can1ButtonIterator = std::find_if(
        shortcutButtons.cbegin(), shortcutButtons.cend(),
        [](const QToolButton* button) {
            return button->property("deviceShortcut").toBool() &&
                   button->text().startsWith(QStringLiteral("CAN1"));
        });
    QVERIFY(can1ButtonIterator != shortcutButtons.cend());
    auto* can1Button = *can1ButtonIterator;
    QVERIFY(can1Button);
    QTest::mouseClick(can1Button, Qt::LeftButton);
    QCOMPARE(selector.currentDeviceId(), QStringLiteral("CAN1"));
    QCOMPARE(selector.currentTargetId(), QStringLiteral("CAN1.CH1"));
    QTest::mouseClick(can1Button, Qt::LeftButton);
    QVERIFY(selector.currentDeviceId().isEmpty());
    QVERIFY(selector.currentTargetId().isEmpty());

    QVERIFY(selector.selectTarget(QStringLiteral("DMM1")));
    QCOMPARE(selector.currentTargetId(), QStringLiteral("DMM1"));
    auto* channels = selector.findChild<QWidget*>(
        QStringLiteral("flowTargetChannels"));
    QVERIFY(channels);
    QVERIFY(channels->isHidden());
    QVERIFY(!selector.selectTarget(QStringLiteral("SERIAL1")));
    QVERIFY(targetChanged.count() >= 2);
}

void MainWindowLifecycleTests::flowFieldInspectionAppearsImmediatelyAndFillsPanel()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("inspect.json"));
    QFile sequence(sequencePath);
    QVERIFY(sequence.open(QIODevice::WriteOnly));
    sequence.write(R"json({
      "id":"inspect","name":"Inspect","groups":[
        {"id":"setup","kind":"setup","steps":[]},
        {"id":"main","kind":"main","steps":[
          {"id":"001","name":"Read CAN","kind":"action",
           "moduleId":"device","function":"read",
           "inputs":{"deviceId":"CAN1.CH1"}}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[]}
      ]
    })json");
    sequence.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    window.show();
    QTest::qWait(30);

    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    auto* flowPage = window.findChild<QWidget*>(QStringLiteral("sequenceEditorPage"));
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("flowFieldSearch"));
    auto* action = window.findChild<QAction*>(QStringLiteral("findFlowFieldAction"));
    QVERIFY(tabs && flowPage && tree && search && action);
    tabs->setCurrentWidget(flowPage);
    QVERIFY(!tree->isColumnHidden(SequenceTreeModel::InspectionColumn));
    auto* model = qobject_cast<SequenceTreeModel*>(tree->model());
    QVERIFY(model);
    const auto mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    QVERIFY(mainGroup.isValid());
    const auto stepIndex = model->index(
        0, SequenceTreeModel::NameColumn, mainGroup);
    tree->setCurrentIndex(stepIndex);
    QCoreApplication::processEvents();

    auto* inspectDevice = window.findChild<QToolButton*>(
        QStringLiteral("inspectField_inputs_deviceId"));
    auto* inspectId = window.findChild<QToolButton*>(
        QStringLiteral("inspectField_id"));
    QVERIFY(inspectDevice && inspectId);
    inspectDevice->click();
    QCoreApplication::processEvents();

    QVERIFY(inspectDevice->isChecked());
    QVERIFY(!inspectId->isChecked());
    const auto keyIndex = model->index(
        0, SequenceTreeModel::InspectionColumn, mainGroup);
    QCOMPARE(keyIndex.data().toString(), QStringLiteral("CAN1.CH1"));
    QCOMPARE(model->headerData(SequenceTreeModel::InspectionColumn,
                               Qt::Horizontal).toString(),
             QStringLiteral("Inspect: Target device"));
    QVERIFY(tree->visualRect(keyIndex).width() > 0);
    QCOMPARE(tree->maximumWidth(), QWIDGETSIZE_MAX);
    QVERIFY(tree->width() >= tree->parentWidget()->width() - 4);

    inspectId->click();
    QCoreApplication::processEvents();
    QVERIFY(!inspectDevice->isChecked());
    QVERIFY(inspectId->isChecked());
    QCOMPARE(keyIndex.data().toString(), QStringLiteral("001"));
    inspectId->click();
    QCoreApplication::processEvents();
    QVERIFY(model->inspectionField().isEmpty());

    action->trigger();
    QVERIFY(search->isVisible());
    QVERIFY(search->width() <= 360);
    QVERIFY(search->width() < tree->width());
    QTest::keyClicks(search, QStringLiteral("CAN1.CH1"));
    QTest::keyPress(search, Qt::Key_Return);
    QCoreApplication::processEvents();
    QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("Read CAN"));

    QTest::keyPress(search, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QVERIFY(!search->isVisible());
    QVERIFY(!tree->isColumnHidden(SequenceTreeModel::InspectionColumn));
}

void MainWindowLifecycleTests::stationDeviceApplyPreservesDllPathWhenModelIsUnchanged()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto deployedDll = directory.filePath(QStringLiteral("deployed.dll"));
    const auto catalogDll = directory.filePath(QStringLiteral("catalog.dll"));
    for (const auto& path : {deployedDll, catalogDll}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("test");
    }

    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(QJsonDocument(QJsonObject{
        {QStringLiteral("stationId"), QStringLiteral("station")},
        {QStringLiteral("devices"), QJsonArray{QJsonObject{
            {QStringLiteral("deviceId"), QStringLiteral("CAN1.CH1")},
            {QStringLiteral("deviceType"), QStringLiteral("CAN")},
            {QStringLiteral("driverId"), QStringLiteral("plugin.can.gcan")},
            {QStringLiteral("pluginPath"), deployedDll},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("timeoutMs"), 30000},
            {QStringLiteral("options"), QJsonObject{
                {QStringLiteral("channelIndex"), 0},
                {QStringLiteral("bitrate"), 500000}}}}}}})
                          .toJson());
    stationFile.close();

    StationDocument document;
    QVERIFY(document.load(stationPath));
    StationPropertyEditor editor(&document);
    PluginManifest plugin;
    plugin.moduleId = QStringLiteral("plugin.can.gcan");
    plugin.name = QStringLiteral("GCAN");
    plugin.category = QStringLiteral("CAN");
    plugin.dllPath = catalogDll;
    PluginFunctionDefinition open;
    open.id = QStringLiteral("open");
    PluginParameterDefinition bitrate;
    bitrate.key = QStringLiteral("bitrate");
    bitrate.name = QStringLiteral("Bitrate");
    bitrate.type = PluginParameterType::Enumeration;
    bitrate.required = true;
    bitrate.options = {
        {QStringLiteral("250 kbit/s"), 250000},
        {QStringLiteral("500 kbit/s"), 500000}};
    open.inputs.push_back(bitrate);
    plugin.functions.push_back(open);
    editor.setPluginRegistry({plugin});
    editor.setCurrentDevice(0);

    auto* timeout = editor.findChild<QSpinBox*>(
        QStringLiteral("deviceTimeoutMsSpin"));
    auto* bitrateCombo = editor.findChild<QComboBox*>(
        QStringLiteral("deviceOption_ch1_bitrate"));
    auto* errorLabel = editor.findChild<QLabel*>(
        QStringLiteral("devicePropertyError"));
    QVERIFY(timeout);
    QVERIFY(!editor.findChild<QPushButton*>(
        QStringLiteral("applyDevicePropertiesButton")));
    QVERIFY(bitrateCombo);
    QVERIFY(!editor.findChild<QPlainTextEdit*>(
        QStringLiteral("deviceOptionsEdit")));
    QVERIFY(errorLabel);

    timeout->setValue(45000);
    QVERIFY(editor.hasPendingChanges());
    QVERIFY(editor.commitPendingChanges());
    QVERIFY(!document.deviceAt(0).contains(QStringLiteral("pluginPath")));
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("timeoutMs")).toInt(),
             45000);
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("options")).toObject()
                 .value(QStringLiteral("bitrate")).toInt(),
             500000);
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("options")).toObject()
                 .value(QStringLiteral("channelIndex")).toInt(),
             0);

    QVERIFY(editor.commitPendingChanges());
    QVERIFY(errorLabel->isHidden());
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN1.CH1"));
}

void MainWindowLifecycleTests::stationPropertyEditorUsesTypedIdsAndFilteredDrivers()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(R"({
        "stationId":"typed-editor",
        "devices":[{
            "deviceId":"GCAN_CAN1",
            "deviceType":"CAN",
            "driverId":"plugin.can.gcan",
            "enabled":false,
            "options":{"channelIndex":0,"bitrate":500000}
        }]
    })");
    stationFile.close();

    StationDocument document;
    QVERIFY(document.load(stationPath));
    StationPropertyEditor editor(&document);
    PluginManifest canPlugin;
    canPlugin.moduleId = QStringLiteral("plugin.can.gcan");
    canPlugin.name = QStringLiteral("GCAN USB-CAN");
    canPlugin.category = QStringLiteral("CAN");
    canPlugin.dllPath = QStringLiteral("PicoATE.CAN.GCAN.dll");
    PluginFunctionDefinition open;
    open.id = QStringLiteral("open");
    PluginParameterDefinition deviceId;
    deviceId.key = QStringLiteral("deviceId");
    deviceId.name = QStringLiteral("Device");
    deviceId.type = PluginParameterType::String;
    PluginParameterDefinition channelIndex;
    channelIndex.key = QStringLiteral("channelIndex");
    channelIndex.name = QStringLiteral("Channel");
    channelIndex.type = PluginParameterType::Integer;
    PluginParameterDefinition bitrate;
    bitrate.key = QStringLiteral("bitrate");
    bitrate.name = QStringLiteral("Bitrate");
    bitrate.type = PluginParameterType::Enumeration;
    bitrate.required = true;
    bitrate.options = {
        {QStringLiteral("250 kbit/s"), 250000},
        {QStringLiteral("500 kbit/s"), 500000}};
    PluginParameterDefinition listenOnly;
    listenOnly.key = QStringLiteral("listenOnly");
    listenOnly.name = QStringLiteral("Listen Only");
    listenOnly.type = PluginParameterType::Boolean;
    listenOnly.defaultValue = false;
    open.inputs = {deviceId, channelIndex, bitrate, listenOnly};
    canPlugin.functions.push_back(open);
    PluginManifest dmmPlugin;
    dmmPlugin.moduleId = QStringLiteral("plugin.dmm.keysight");
    dmmPlugin.name = QStringLiteral("Keysight DMM");
    dmmPlugin.category = QStringLiteral("DMM");
    dmmPlugin.dllPath = QStringLiteral("PicoATE.DMM.Keysight.dll");
    editor.setPluginRegistry({canPlugin, dmmPlugin});
    editor.setCurrentDevice(0);

    auto* deviceIdEdit = editor.findChild<QLineEdit*>(
        QStringLiteral("deviceIdEdit"));
    auto* typeCombo = editor.findChild<QComboBox*>(
        QStringLiteral("deviceTypeCombo"));
    auto* pluginCombo = editor.findChild<QComboBox*>(
        QStringLiteral("devicePluginCombo"));
    auto* address = editor.findChild<QLineEdit*>(
        QStringLiteral("deviceAddressEdit"));
    auto* resourceCombo = editor.findChild<QComboBox*>(
        QStringLiteral("deviceResourceCombo"));
    QVERIFY(deviceIdEdit);
    QVERIFY(typeCombo);
    QVERIFY(pluginCombo);
    QVERIFY(address);
    QVERIFY(resourceCombo);
    QVERIFY(deviceIdEdit->isReadOnly());
    QCOMPARE(deviceIdEdit->text(), QStringLiteral("CAN1 (No active channel)"));
    QCOMPARE(typeCombo->currentData().toString(), QStringLiteral("CAN"));
    QCOMPARE(pluginCombo->currentData().toString(),
             QStringLiteral("plugin.can.gcan"));
    QCOMPARE(pluginCombo->currentText(), QStringLiteral("plugin.can.gcan"));
    QVERIFY(!pluginCombo->currentText().contains(QStringLiteral("Select")));
    QVERIFY(!editor.findChild<QAbstractButton*>(
        QStringLiteral("deviceEnabledSwitch")));
    QVERIFY(pluginCombo->findData(QStringLiteral("plugin.dmm.keysight")) < 0);
    QVERIFY(!address->isHidden());

    auto* channel1 = editor.findChild<QAbstractButton*>(
        QStringLiteral("deviceChannel1Switch"));
    auto* channel2 = editor.findChild<QAbstractButton*>(
        QStringLiteral("deviceChannel2Switch"));
    auto* bitrateCombo = editor.findChild<QComboBox*>(
        QStringLiteral("deviceOption_ch1_bitrate"));
    auto* listenOnlySwitch = editor.findChild<QAbstractButton*>(
        QStringLiteral("deviceOption_ch1_listenOnly"));
    QVERIFY(channel1);
    QVERIFY(channel2);
    QVERIFY(bitrateCombo);
    QVERIFY(listenOnlySwitch);
    QVERIFY(!channel1->isChecked());
    QVERIFY(!channel2->isChecked());
    channel2->setChecked(true);
    QCOMPARE(deviceIdEdit->text(), QStringLiteral("CAN1.CH2"));
    resourceCombo->addItem(QStringLiteral("GCAN-SN-001 | USBCAN-II"),
                           QStringLiteral("GCAN-SN-001"));
    resourceCombo->setCurrentIndex(resourceCombo->count() - 1);

    auto* timeout = editor.findChild<QSpinBox*>(
        QStringLiteral("deviceTimeoutMsSpin"));
    QVERIFY(timeout);
    timeout->setValue(45000);
    QVERIFY(editor.hasPendingChanges());
    QVERIFY(editor.commitPendingChanges());

    QCOMPARE(document.deviceCount(), 2);
    const auto saved = document.deviceAt(0);
    const auto savedChannel2 = document.deviceAt(1);
    QCOMPARE(saved.value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN1.CH1"));
    QCOMPARE(savedChannel2.value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN1.CH2"));
    QVERIFY(!saved.value(QStringLiteral("enabled")).toBool());
    QVERIFY(savedChannel2.value(QStringLiteral("enabled")).toBool());
    QCOMPARE(saved.value(QStringLiteral("deviceType")).toString(),
             QStringLiteral("CAN"));
    QCOMPARE(saved.value(QStringLiteral("driverId")).toString(),
             QStringLiteral("plugin.can.gcan"));
    QCOMPARE(saved.value(QStringLiteral("connectionKind")).toString(),
             QStringLiteral("canSerial"));
    QCOMPARE(saved.value(QStringLiteral("resource")).toString(),
             QStringLiteral("GCAN-SN-001"));
    QCOMPARE(saved.value(QStringLiteral("timeoutMs")).toInt(), 45000);
    QCOMPARE(saved.value(QStringLiteral("options")).toObject()
                 .value(QStringLiteral("bitrate")).toInt(),
             500000);
    QCOMPARE(saved.value(QStringLiteral("options")).toObject()
                 .value(QStringLiteral("channelIndex")).toInt(),
             0);

    typeCombo->setCurrentIndex(typeCombo->findData(QStringLiteral("PSU")));
    QCOMPARE(pluginCombo->currentText(), QStringLiteral("No compatible driver found"));
    QVERIFY(pluginCombo->findData(QStringLiteral("plugin.can.gcan")) < 0);
}

void MainWindowLifecycleTests::stationPropertyEditorKeepsCanChannelOptionsIndependent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(R"({
        "stationId":"channel-options",
        "devices":[
          {"deviceId":"CAN1.CH1","deviceType":"CAN",
           "driverId":"plugin.can.test","enabled":true,
           "options":{"deviceIndex":0,"channelIndex":0,"bitrate":500000}},
          {"deviceId":"CAN1.CH2","deviceType":"CAN",
           "driverId":"plugin.can.test","enabled":true,
           "options":{"deviceIndex":0,"channelIndex":1,"bitrate":1000000}}
        ]
    })");
    stationFile.close();

    PluginManifest plugin;
    plugin.moduleId = QStringLiteral("plugin.can.test");
    plugin.name = QStringLiteral("Two-channel CAN");
    plugin.category = QStringLiteral("CAN");
    plugin.dllPath = QStringLiteral("PicoATE.CAN.Test.dll");
    PluginFunctionDefinition open;
    open.id = QStringLiteral("open");
    PluginParameterDefinition channel;
    channel.key = QStringLiteral("channelIndex");
    channel.name = QStringLiteral("Channel");
    channel.type = PluginParameterType::Integer;
    channel.maximum = 1.0;
    PluginParameterDefinition deviceIndex;
    deviceIndex.key = QStringLiteral("deviceIndex");
    deviceIndex.name = QStringLiteral("Device Index");
    deviceIndex.type = PluginParameterType::Integer;
    deviceIndex.defaultValue = 0;
    PluginParameterDefinition bitrate;
    bitrate.key = QStringLiteral("bitrate");
    bitrate.name = QStringLiteral("Bitrate");
    bitrate.type = PluginParameterType::Enumeration;
    bitrate.options = {
        {QStringLiteral("500 kbit/s"), 500000},
        {QStringLiteral("1 Mbit/s"), 1000000}};
    open.inputs = {deviceIndex, channel, bitrate};
    plugin.functions.push_back(open);

    StationDocument document;
    QVERIFY(document.load(stationPath));
    StationPropertyEditor editor(&document);
    editor.setPluginRegistry({plugin});
    editor.setCurrentDevices({0, 1}, QStringLiteral("CAN1"));
    editor.show();
    QTest::qWait(20);

    auto* channel1Bitrate = editor.findChild<QComboBox*>(
        QStringLiteral("deviceOption_ch1_bitrate"));
    auto* channel2Bitrate = editor.findChild<QComboBox*>(
        QStringLiteral("deviceOption_ch2_bitrate"));
    auto* sharedDeviceIndex = editor.findChild<QSpinBox*>(
        QStringLiteral("deviceOption_deviceIndex"));
    QVERIFY(channel1Bitrate && channel2Bitrate && sharedDeviceIndex);
    QCOMPARE(channel1Bitrate->currentData().toInt(), 500000);
    QCOMPARE(channel2Bitrate->currentData().toInt(), 1000000);
    QCOMPARE(sharedDeviceIndex->value(), 0);

    channel1Bitrate->setCurrentIndex(
        channel1Bitrate->findData(1000000));
    QVERIFY(editor.commitPendingChanges());
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("options")).toObject()
                 .value(QStringLiteral("bitrate")).toInt(),
             1000000);
    QCOMPARE(document.deviceAt(1).value(QStringLiteral("options")).toObject()
                 .value(QStringLiteral("bitrate")).toInt(),
             1000000);
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("options")).toObject()
                 .value(QStringLiteral("channelIndex")).toInt(), 0);
    QCOMPARE(document.deviceAt(1).value(QStringLiteral("options")).toObject()
                 .value(QStringLiteral("channelIndex")).toInt(), 1);
}

void MainWindowLifecycleTests::stationPropertyEditorPreservesCanIdentityWhenEditingFirstGroup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(R"({
        "stationId":"stable-can-identity",
        "devices":[
          {"deviceId":"CAN1.CH1","deviceType":"CAN","driverId":"plugin.can.cx",
           "enabled":true,"options":{"deviceIndex":0,"channelIndex":0,"bitrate":500000}},
          {"deviceId":"CAN1.CH2","deviceType":"CAN","driverId":"plugin.can.cx",
           "enabled":true,"options":{"deviceIndex":0,"channelIndex":1,"bitrate":500000}},
          {"deviceId":"CAN2.CH1","deviceType":"CAN","driverId":"plugin.can.gcan",
           "enabled":true,"options":{"deviceIndex":0,"channelIndex":0,"bitrate":500000}},
          {"deviceId":"CAN2.CH2","deviceType":"CAN","driverId":"plugin.can.gcan",
           "enabled":true,"options":{"deviceIndex":0,"channelIndex":1,"bitrate":500000}}
        ]
    })");
    stationFile.close();

    auto makePlugin = [](const QString& moduleId) {
        PluginManifest plugin;
        plugin.moduleId = moduleId;
        plugin.name = moduleId;
        plugin.category = QStringLiteral("CAN");
        plugin.dllPath = moduleId + QStringLiteral(".dll");
        PluginFunctionDefinition open;
        open.id = QStringLiteral("open");
        PluginParameterDefinition channel;
        channel.key = QStringLiteral("channelIndex");
        channel.name = QStringLiteral("Channel");
        channel.type = PluginParameterType::Integer;
        channel.maximum = 1.0;
        PluginParameterDefinition bitrate;
        bitrate.key = QStringLiteral("bitrate");
        bitrate.name = QStringLiteral("Bitrate");
        bitrate.type = PluginParameterType::Enumeration;
        bitrate.options = {
            {QStringLiteral("250 kbit/s"), 250000},
            {QStringLiteral("500 kbit/s"), 500000}};
        open.inputs = {channel, bitrate};
        plugin.functions.push_back(open);
        return plugin;
    };

    StationDocument document;
    QVERIFY(document.load(stationPath));
    StationPropertyEditor editor(&document);
    editor.setPluginRegistry({makePlugin(QStringLiteral("plugin.can.cx")),
                              makePlugin(QStringLiteral("plugin.can.gcan"))});
    editor.setCurrentDevices({0, 1}, QStringLiteral("CAN1"));

    auto* channel1Bitrate = editor.findChild<QComboBox*>(
        QStringLiteral("deviceOption_ch1_bitrate"));
    QVERIFY(channel1Bitrate);
    channel1Bitrate->setCurrentIndex(channel1Bitrate->findData(250000));
    QVERIFY(editor.commitPendingChanges());

    const QStringList expectedIds = {
        QStringLiteral("CAN1.CH1"), QStringLiteral("CAN1.CH2"),
        QStringLiteral("CAN2.CH1"), QStringLiteral("CAN2.CH2")};
    for (int row = 0; row < expectedIds.size(); ++row) {
        QCOMPARE(document.deviceAt(row).value(QStringLiteral("deviceId")).toString(),
                 expectedIds[row]);
    }
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("driverId")).toString(),
             QStringLiteral("plugin.can.cx"));
    QCOMPARE(document.deviceAt(1).value(QStringLiteral("driverId")).toString(),
             QStringLiteral("plugin.can.cx"));
    QCOMPARE(document.deviceAt(2).value(QStringLiteral("driverId")).toString(),
             QStringLiteral("plugin.can.gcan"));
    QCOMPARE(document.deviceAt(3).value(QStringLiteral("driverId")).toString(),
             QStringLiteral("plugin.can.gcan"));
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("options")).toObject()
                 .value(QStringLiteral("bitrate")).toInt(), 250000);

    auto root = document.rootObject();
    const auto devices = root.value(QStringLiteral("devices")).toArray();
    root.insert(QStringLiteral("devices"), QJsonArray{
        devices[2], devices[3], devices[0], devices[1]});
    QVERIFY(document.replaceRootObject(root));

    StationDeviceModel model(&document);
    QCOMPARE(model.logicalBaseId(model.index(0, 0)), QStringLiteral("CAN2"));
    QCOMPARE(model.data(model.index(0, StationDeviceModel::DriverIdColumn),
                        Qt::EditRole).toString(),
             QStringLiteral("plugin.can.gcan"));
    QCOMPARE(model.logicalBaseId(model.index(1, 0)), QStringLiteral("CAN1"));
    QCOMPARE(model.data(model.index(1, StationDeviceModel::DriverIdColumn),
                        Qt::EditRole).toString(),
             QStringLiteral("plugin.can.cx"));
}

void MainWindowLifecycleTests::stationNewCanKeepsTableEnabledStateWhenDraftIsSaved()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(R"({
      "stationId":"new-can",
      "devices":[{
        "deviceId":"PLUGIN1",
        "deviceType":"PLUGIN",
        "driverId":"",
        "enabled":false,
        "options":{}
      }]
    })");
    stationFile.close();

    PluginManifest plugin;
    plugin.moduleId = QStringLiteral("plugin.can.test");
    plugin.name = QStringLiteral("Two-channel CAN");
    plugin.category = QStringLiteral("CAN");
    PluginFunctionDefinition open;
    open.id = QStringLiteral("open");
    PluginParameterDefinition channel;
    channel.key = QStringLiteral("channelIndex");
    channel.name = QStringLiteral("Channel");
    channel.type = PluginParameterType::Integer;
    channel.maximum = 1.0;
    open.inputs = {channel};
    plugin.functions.push_back(open);

    StationDocument document;
    QVERIFY(document.load(stationPath));
    StationPropertyEditor editor(&document);
    editor.setPluginRegistry({plugin});
    editor.setCurrentDevice(0);
    editor.show();
    QTest::qWait(20);

    auto* type = editor.findChild<QComboBox*>(QStringLiteral("deviceTypeCombo"));
    QVERIFY(type);
    type->setCurrentIndex(type->findData(QStringLiteral("CAN")));
    QVERIFY(editor.hasPendingChanges());

    // This is the center table's Enable checkbox changing the document while
    // the right-side CAN conversion is still an uncommitted draft.
    QVERIFY(document.setDeviceValue(0, QStringLiteral("enabled"), true));
    QVERIFY(editor.commitPendingChanges());
    QCOMPARE(document.deviceCount(), 2);
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN1.CH1"));
    QCOMPARE(document.deviceAt(1).value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN1.CH2"));
    QVERIFY(document.deviceAt(0).value(QStringLiteral("enabled")).toBool());
    QVERIFY(document.deviceAt(1).value(QStringLiteral("enabled")).toBool());
}

void MainWindowLifecycleTests::stationDeviceSlotsMoveReferencesBeforeOrderedDeletion()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("sequence.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"({
        "id":"device-move",
        "name":"Device Move",
        "groups":[{
            "id":"main",
            "name":"Main",
            "kind":"Main",
            "steps":[{
                "id":"001",
                "name":"Read GCAN channel 2",
                "kind":"Action",
                "moduleId":"device",
                "function":"read",
                "inputs":{"deviceId":"GCAN_CAN2"}
            }]
        }]
    })");
    sequenceFile.close();

    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(R"({
        "stationId":"device-move",
        "devices":[
            {
                "deviceId":"GCAN_CAN1",
                "deviceType":"CAN",
                "driverId":"plugin.can.gcan",
                "pluginPath":"PicoATE.CAN.GCAN.dll",
                "enabled":true,
                "options":{"deviceIndex":0,"channelIndex":0}
            },
            {
                "deviceId":"GCAN_CAN2",
                "deviceType":"CAN",
                "driverId":"plugin.can.gcan",
                "pluginPath":"PicoATE.CAN.GCAN.dll",
                "enabled":true,
                "options":{"deviceIndex":0,"channelIndex":1}
            }
        ]
    })");
    stationFile.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    QVERIFY(window.openStationFile(stationPath));
    window.show();
    QTest::qWait(20);

    auto* stationDocument = window.findChild<StationDocument*>();
    auto* sequenceDocument = window.findChild<SequenceDocument*>();
    auto* model = window.findChild<StationDeviceModel*>();
    auto* view = window.findChild<QTreeView*>(
        QStringLiteral("stationDeviceView"));
    QVERIFY(stationDocument);
    QVERIFY(sequenceDocument);
    QVERIFY(model);
    QVERIFY(view);
    QCOMPARE(model->rowCount(), 1);
    const auto canDevice = model->index(0, 0);
    QVERIFY(model->isDeviceGroup(canDevice));
    QVERIFY(!canDevice.parent().isValid());
    QCOMPARE(model->rowCount(canDevice), 0);
    QCOMPARE(canDevice.data().toString(),
             QStringLiteral("CAN1.CH1 / CAN1.CH2"));
    QCOMPARE(model->documentRows(canDevice), QVector<int>({0, 1}));
    PluginManifest canPlugin;
    canPlugin.moduleId = QStringLiteral("plugin.can.gcan");
    canPlugin.name = QStringLiteral("Guangcheng CAN device");
    canPlugin.category = QStringLiteral("CAN");
    canPlugin.dllPath = QStringLiteral("drivers/PicoATE.CAN.GCAN.dll");
    model->setPluginRegistry({canPlugin});
    QCoreApplication::processEvents();
    QCOMPARE(model->index(0, StationDeviceModel::DriverIdColumn)
             .data(Qt::DisplayRole).toString(),
             QStringLiteral("plugin.can.gcan"));
    QCOMPARE(stationDocument->deviceAt(0)
                 .value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN1.CH1"));
    QCOMPARE(stationDocument->deviceAt(1)
                 .value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN1.CH2"));
    const auto groups = sequenceDocument->rootObject()
                            .value(QStringLiteral("groups")).toArray();
    const auto mainGroup = std::find_if(
        groups.cbegin(), groups.cend(), [](const QJsonValue& value) {
            return value.toObject().value(QStringLiteral("kind"))
                       .toString().compare(QStringLiteral("main"),
                                           Qt::CaseInsensitive) == 0;
        });
    QVERIFY(mainGroup != groups.cend());
    const auto step = mainGroup->toObject()
                          .value(QStringLiteral("steps")).toArray()[0].toObject();
    QCOMPARE(step.value(QStringLiteral("inputs")).toObject()
                 .value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN1.CH2"));

    stationDocument->undoStack()->setClean();
    sequenceDocument->undoStack()->setClean();
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::stationCtrlSaveCommitsDraftAndClearsWindowMarker()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                            + QStringLiteral("/examples/simple_sequence.json"),
                        sequencePath));
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(R"({
        "stationId":"station-save",
        "devices":[{
            "deviceId":"DMM1",
            "deviceType":"DMM",
            "driverId":"manual.dmm",
            "pluginPath":"manual.dll",
            "address":"USB::0",
            "enabled":false
        }]
    })");
    stationFile.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    QVERIFY(window.openStationFile(stationPath));
    window.show();
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    auto* stationPage = window.findChild<QWidget*>(QStringLiteral("stationEditorPage"));
    auto* address = window.findChild<QLineEdit*>(QStringLiteral("deviceAddressEdit"));
    auto* editor = window.findChild<StationPropertyEditor*>();
    auto* stationDocument = window.findChild<StationDocument*>();
    auto* save = window.findChild<QAction*>(QStringLiteral("saveSequenceAction"));
    QVERIFY(tabs);
    QVERIFY(stationPage);
    QVERIFY(address);
    QVERIFY(editor);
    QVERIFY(stationDocument);
    QVERIFY(save);
    tabs->setCurrentWidget(stationPage);
    QCOMPARE(save->text(), QStringLiteral("Save Station"));

    address->setFocus();
    address->selectAll();
    QTest::keyClicks(address, QStringLiteral("USB::1"));
    QVERIFY(editor->hasPendingChanges());
    QVERIFY(window.windowTitle().contains(QLatin1Char('*')));
    QVERIFY(save->isEnabled());

    QTimer::singleShot(0, [] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* messageBox = qobject_cast<QMessageBox*>(widget)) {
                if (auto* button = messageBox->button(QMessageBox::Save)) {
                    button->click();
                }
            }
        }
    });
    save->trigger();

    QVERIFY(!editor->hasPendingChanges());
    QVERIFY(!stationDocument->isModified());
    QVERIFY(!window.windowTitle().contains(QLatin1Char('*')));
    QCOMPARE(stationDocument->deviceAt(0).value(QStringLiteral("resource")).toString(),
             QStringLiteral("USB::1"));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Station saved")));
    QVERIFY(!window.findChild<QPushButton*>(
        QStringLiteral("applyDevicePropertiesButton")));
    QVERIFY(!window.findChild<QPushButton*>(
        QStringLiteral("applyStationSettingsButton")));

    QFile saved(stationPath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(saved.readAll()).object()
                 .value(QStringLiteral("devices")).toArray().first().toObject()
                 .value(QStringLiteral("resource")).toString(),
             QStringLiteral("USB::1"));
}

void MainWindowLifecycleTests::switchingStationDevicesCanDiscardCurrentDraft()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(R"({
        "stationId":"station-switch",
        "devices":[
            {"deviceId":"DMM1","deviceType":"DMM","driverId":"manual.dmm",
             "address":"USB::0","enabled":false},
            {"deviceId":"DMM2","deviceType":"DMM","driverId":"manual.dmm",
             "address":"USB::1","enabled":false}
        ]
    })");
    stationFile.close();

    MainWindow window;
    QVERIFY(window.openStationFile(stationPath));
    window.show();
    auto* document = window.findChild<StationDocument*>();
    auto* model = window.findChild<StationDeviceModel*>();
    auto* view = window.findChild<QTreeView*>(QStringLiteral("stationDeviceView"));
    auto* address = window.findChild<QLineEdit*>(QStringLiteral("deviceAddressEdit"));
    auto* editor = window.findChild<StationPropertyEditor*>();
    QVERIFY(document);
    QVERIFY(model);
    QVERIFY(view);
    QVERIFY(address);
    QVERIFY(editor);

    view->setCurrentIndex(model->index(0, 0));
    address->setFocus();
    address->selectAll();
    QTest::keyClicks(address, QStringLiteral("DRAFT"));
    QVERIFY(editor->hasPendingChanges());

    QTimer::singleShot(0, [] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* messageBox = qobject_cast<QMessageBox*>(widget)) {
                if (auto* button = messageBox->button(QMessageBox::Discard)) {
                    button->click();
                }
            }
        }
    });
    view->setCurrentIndex(model->index(1, 0));
    QCOMPARE(editor->currentDeviceRow(), 1);
    QVERIFY(!editor->hasPendingChanges());
    QCOMPARE(document->deviceAt(0).value(QStringLiteral("resource")).toString(),
             QStringLiteral("USB::0"));
    QCOMPARE(address->text(), QStringLiteral("USB::1"));
}

void MainWindowLifecycleTests::disabledReferencedDeviceDiagnosticPersistsAcrossEditors()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("device_sequence.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"({
        "id":"device-sequence",
        "name":"Device Sequence",
        "version":"1.0",
        "groups":[{
            "id":"main",
            "name":"Main",
            "kind":"Main",
            "steps":[{
                "id":"001",
                "name":"Open GCAN CAN1",
                "kind":"action",
                "moduleId":"device",
                "function":"open",
                "inputs":{"deviceId":"GCAN_CAN1"}
            }]
        }]
    })");
    sequenceFile.close();
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(R"({
        "stationId":"diagnostic-station",
        "devices":[{
            "deviceId":"GCAN_CAN1",
            "deviceType":"CAN",
            "driverId":"plugin.can.gcan",
            "pluginPath":"missing-test-plugin.dll",
            "address":"GCAN:0:0",
            "enabled":true
        }]
    })");
    stationFile.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    QVERIFY(window.openStationFile(stationPath));
    window.show();
    auto* stationDocument = window.findChild<StationDocument*>();
    auto* stationDiagnostics = window.findChild<QTableView*>(
        QStringLiteral("stationDiagnosticView"));
    auto* flowDiagnostics = window.findChild<QTableView*>(
        QStringLiteral("sequenceDiagnosticView"));
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    auto* stationPage = window.findChild<QWidget*>(QStringLiteral("stationEditorPage"));
    auto* flowPage = window.findChild<QWidget*>(QStringLiteral("sequenceEditorPage"));
    QVERIFY(stationDocument);
    QVERIFY(stationDiagnostics);
    QVERIFY(flowDiagnostics);
    QVERIFY(tabs);
    QVERIFY(stationPage);
    QVERIFY(flowPage);

    QVERIFY(stationDocument->setDeviceValue(0, QStringLiteral("enabled"), false));
    QCoreApplication::processEvents();
    const auto findDisabledReference = [](QAbstractItemModel* model) {
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto message = model->index(row, DiagnosticModel::MessageColumn)
                                     .data().toString();
            if (message.contains(QStringLiteral("CAN1.CH1")) &&
                message.contains(QStringLiteral("disabled"), Qt::CaseInsensitive)) {
                return row;
            }
        }
        return -1;
    };
    const int stationRow = findDisabledReference(stationDiagnostics->model());
    const int flowRow = findDisabledReference(flowDiagnostics->model());
    QVERIFY(stationRow >= 0);
    QVERIFY(flowRow >= 0);

    tabs->setCurrentWidget(stationPage);
    QVERIFY(findDisabledReference(stationDiagnostics->model()) >= 0);
    tabs->setCurrentWidget(flowPage);
    QVERIFY(findDisabledReference(flowDiagnostics->model()) >= 0);
    const auto flowDiagnostic = flowDiagnostics->model()->index(
        flowRow, DiagnosticModel::MessageColumn);
    flowDiagnostics->scrollTo(flowDiagnostic);
    QCoreApplication::processEvents();
    QTest::mouseClick(flowDiagnostics->viewport(), Qt::LeftButton,
                      Qt::NoModifier,
                      flowDiagnostics->visualRect(flowDiagnostic).center());
    QCOMPARE(tabs->currentWidget(), stationPage);
}

void MainWindowLifecycleTests::compileFailureFocusesDiagnosticAndExplainsDisabledRun()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto firstDll = directory.filePath(QStringLiteral("first.dll"));
    const auto secondDll = directory.filePath(QStringLiteral("second.dll"));
    for (const auto& path : {firstDll, secondDll}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("test");
    }

    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(QJsonDocument(QJsonObject{
        {QStringLiteral("stationId"), QStringLiteral("conflict-station")},
        {QStringLiteral("devices"), QJsonArray{
            QJsonObject{{QStringLiteral("deviceId"), QStringLiteral("CAN1")},
                        {QStringLiteral("deviceType"), QStringLiteral("CAN")},
                        {QStringLiteral("driverId"), QStringLiteral("plugin.can.test")},
                        {QStringLiteral("pluginPath"), firstDll},
                        {QStringLiteral("enabled"), true}},
            QJsonObject{{QStringLiteral("deviceId"), QStringLiteral("CAN2")},
                        {QStringLiteral("deviceType"), QStringLiteral("CAN")},
                        {QStringLiteral("driverId"), QStringLiteral("plugin.can.test")},
                        {QStringLiteral("pluginPath"), secondDll},
                        {QStringLiteral("enabled"), true}}}}})
                          .toJson());
    stationFile.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(
        QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/simple_sequence.json")));
    QVERIFY(window.openStationFile(stationPath));
    window.show();

    auto* compileAction = window.findChild<QAction*>(QStringLiteral("compileAction"));
    auto* runAction = window.findChild<QAction*>(QStringLiteral("runAction"));
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* workspaceTabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    auto* detailsTabs = window.findChild<QTabWidget*>(QStringLiteral("runDetailsTabs"));
    QVERIFY(compileAction);
    QVERIFY(runAction);
    QVERIFY(viewModel);
    QVERIFY(workspaceTabs);
    QVERIFY(detailsTabs);

    compileAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::CompileFailed, 3000);
    QCOMPARE(workspaceTabs->currentIndex(), 0);
    auto* diagnosticView = qobject_cast<QTableView*>(detailsTabs->currentWidget());
    QVERIFY(diagnosticView);
    QVERIFY(diagnosticView->model()->rowCount() > 0);
    QVERIFY(diagnosticView->currentIndex().isValid());
    QVERIFY(detailsTabs->tabText(detailsTabs->currentIndex()).startsWith(
        QStringLiteral("Diagnostics (")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Compile failed"), Qt::CaseInsensitive));
    QVERIFY(!runAction->isEnabled());
    QVERIFY(runAction->toolTip().contains(
        QStringLiteral("compilation failed"), Qt::CaseInsensitive));
}

void MainWindowLifecycleTests::stationConnectionActionUpdatesStatus()
{
    const auto oldAddress = qgetenv("DMM1_RESOURCE");
    qputenv("DMM1_RESOURCE", QByteArray("USB0::TEST::INSTR"));
    const QString projectDir = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR);

    MainWindow window;
    QVERIFY(window.openSequenceFile(
        projectDir + QStringLiteral("/examples/dmm_can_adapter_sequence.json")));
    QVERIFY(window.openStationFile(
        projectDir + QStringLiteral("/examples/stations/basic_station.json")));
    window.show();
    QTest::qWait(20);

    auto* stationModel = window.findChild<StationDeviceModel*>();
    auto* stationView = window.findChild<QTreeView*>(
        QStringLiteral("stationDeviceView"));
    auto* testAction = window.findChild<QAction*>(
        QStringLiteral("testDeviceConnectionAction"));
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    QVERIFY(stationModel);
    QVERIFY(stationView);
    QVERIFY(testAction);
    QVERIFY(viewModel);

    stationView->setCurrentIndex(stationModel->index(0, 0));
    QCoreApplication::processEvents();
    QVERIFY(testAction->isEnabled());
    QSignalSpy finishedSpy(
        viewModel, &ExecutionViewModel::deviceConnectionTestFinished);
    testAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(viewModel->deviceConnectionTestResult().outcome,
             DeviceConnectionTestOutcome::Passed);
    QCOMPARE(stationModel->data(
                 stationModel->index(0, StationDeviceModel::ConnectionColumn)).toString(),
             QString("Passed"));

    stationView->setCurrentIndex(stationModel->index(2, 0));
    QCoreApplication::processEvents();
    QVERIFY(!testAction->isEnabled());
    auto* stationDocument = window.findChild<StationDocument*>();
    auto* sequenceDocument = window.findChild<SequenceDocument*>();
    QVERIFY(stationDocument);
    QVERIFY(sequenceDocument);
    stationDocument->undoStack()->setClean();
    sequenceDocument->undoStack()->setClean();
    QVERIFY(window.close());

    if (oldAddress.isNull()) {
        qunsetenv("DMM1_RESOURCE");
    } else {
        qputenv("DMM1_RESOURCE", oldAddress);
    }
}

void MainWindowLifecycleTests::runActionSyncsTreeBreakpointsAndStopsAtBreakpoint()
{
    const QString projectDir = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR);

    MainWindow window;
    QVERIFY(window.openSequenceFile(
        projectDir + QStringLiteral("/examples/simple_sequence.json")));
    window.show();
    QTest::qWait(20);

    auto* treeView = window.findChild<QTreeView*>(
        QStringLiteral("sequenceTreeView"));
    auto* treeModel = window.findChild<SequenceTreeModel*>();
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* runAction = window.findChild<QAction*>(QStringLiteral("runAction"));
    auto* stepIntoAction = window.findChild<QAction*>(
        QStringLiteral("stepIntoAction"));
    auto* resultView = window.findChild<QTreeView*>(QStringLiteral("resultView"));
    auto* breakpointDelegate = window.findChild<QAbstractItemDelegate*>(
        QStringLiteral("runTestBreakpointDelegate"));
    auto* workspaceTabs = window.findChild<QTabWidget*>(
        QStringLiteral("workspaceTabs"));
    QVERIFY(treeView);
    QVERIFY(treeModel);
    QVERIFY(viewModel);
    QVERIFY(runAction);
    QVERIFY(stepIntoAction);
    QVERIFY(resultView);
    QVERIFY(breakpointDelegate);
    QVERIFY(workspaceTabs);
    workspaceTabs->setCurrentIndex(1);
    QCOMPARE(workspaceTabs->currentIndex(), 1);

    const auto setupGroup = treeModel->index(0, SequenceTreeModel::NameColumn);
    const auto firstStep = treeModel->index(0, SequenceTreeModel::NameColumn, setupGroup);
    QVERIFY(firstStep.isValid());
    QCOMPARE(treeModel->nodePathForIndex(firstStep), QString("open-fixture"));
    QVERIFY(treeView->isColumnHidden(SequenceTreeModel::BreakpointColumn));

    viewModel->compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    QCOMPARE(resultView->model()->rowCount(), 3);
    QVector<int> previewChildCounts;
    QStringList previewPhases;
    for (int row = 0; row < resultView->model()->rowCount(); ++row) {
        const auto phase = resultView->model()->index(row, 0);
        previewPhases.push_back(resultView->model()->data(phase).toString());
        previewChildCounts.push_back(resultView->model()->rowCount(phase));
    }
    QCOMPARE(previewPhases,
             QStringList({QStringLiteral("SETUP"),
                          QStringLiteral("MAIN"),
                          QStringLiteral("CLEANUP")}));
    workspaceTabs->setCurrentIndex(0);
    resultView->expandAll();
    QCoreApplication::processEvents();
    const auto setupPhase = resultView->model()->index(
        0, UutStepModel::NameColumn);
    const auto runTestFirstStep = resultView->model()->index(
        0, UutStepModel::NameColumn, setupPhase);
    const auto breakpointIndex = runTestFirstStep.siblingAtColumn(
        UutStepModel::BreakpointVisualColumn);
    const auto breakpointRect = resultView->visualRect(breakpointIndex);
    QVERIFY(breakpointRect.isValid());
    QTest::mouseClick(resultView->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      QPoint(breakpointRect.left() + 8,
                             breakpointRect.center().y()));
    QCOMPARE(treeModel->breakpointSpecs().size(), 1);
    QCOMPARE(treeModel->breakpointSpecs().first().address.value,
             QString("open-fixture"));

    runAction->trigger();
    QCOMPARE(workspaceTabs->currentIndex(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Paused, 3000);
    QCOMPARE(resultView->model()->rowCount(), 3);
    for (int row = 0; row < resultView->model()->rowCount(); ++row) {
        const auto phase = resultView->model()->index(row, 0);
        QCOMPARE(resultView->model()->data(phase).toString(), previewPhases.at(row));
        QCOMPARE(resultView->model()->rowCount(phase), previewChildCounts.at(row));
    }
    QVERIFY(viewModel->debugSnapshot().has_value());
    QCOMPARE(viewModel->debugSnapshot()->currentNodeId, QString("open-fixture"));
    QTRY_COMPARE_WITH_TIMEOUT(
        breakpointDelegate->property("currentNodePath").toString(),
        QString("open-fixture"),
        1000);
    QTRY_COMPARE_WITH_TIMEOUT(
        treeModel->nodePathForIndex(treeView->currentIndex()),
        QString("open-fixture"),
        1000);

    stepIntoAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Paused, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(
        breakpointDelegate->property("currentNodePath").toString(),
        QString("wait-100ms"),
        1000);

    viewModel->stop();
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             3000);
    QTRY_VERIFY_WITH_TIMEOUT(
        breakpointDelegate->property("currentNodePath").toString().isEmpty(),
        1000);
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::runPopulatesRuntimeTimeline()
{
    const QString projectDir = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR);

    MainWindow window;
    QVERIFY(window.openSequenceFile(
        projectDir + QStringLiteral("/examples/simple_sequence.json")));
    window.show();
    QTest::qWait(20);

    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* timelineView = window.findChild<QTableView*>(
        QStringLiteral("runtimeTimelineView"));
    auto* resultView = window.findChild<QTreeView*>(
        QStringLiteral("resultView"));
    auto* sequenceTreeView = window.findChild<QTreeView*>(
        QStringLiteral("sequenceTreeView"));
    auto* sequenceTreeModel = window.findChild<SequenceTreeModel*>();
    QVERIFY(viewModel);
    QVERIFY(timelineView);
    QVERIFY(resultView);
    QVERIFY(sequenceTreeView);
    QVERIFY(sequenceTreeModel);

    viewModel->compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    viewModel->run();
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             3000);

    auto* timelineProxy = qobject_cast<UutRuntimeTimelineProxyModel*>(
        timelineView->model());
    QVERIFY(timelineProxy);
    auto* model = qobject_cast<RuntimeTimelineModel*>(
        timelineProxy->sourceModel());
    QVERIFY(model);
    QVERIFY(timelineProxy->rowCount() > 0);
    bool sawNodeEvent = false;
    for (int row = 0; row < timelineProxy->rowCount(); ++row) {
        const auto sourceIndex = timelineProxy->mapToSource(
            timelineProxy->index(row, RuntimeTimelineModel::MessageColumn));
        const auto event = model->eventAt(sourceIndex.row());
        if (event &&
            (event->kind == PicoATE::Core::RuntimeEventKind::AttemptStarted ||
             event->kind == PicoATE::Core::RuntimeEventKind::AttemptCompleted)) {
            sawNodeEvent = true;
            break;
        }
    }
    QVERIFY(sawNodeEvent);

    QModelIndex measureEvent;
    for (int row = 0; row < timelineProxy->rowCount(); ++row) {
        const auto proxyIndex = timelineProxy->index(
            row, RuntimeTimelineModel::MessageColumn);
        const auto sourceIndex = timelineProxy->mapToSource(proxyIndex);
        const auto event = model->eventAt(sourceIndex.row());
        if (event && event->nodeDisplayName == QStringLiteral("Measure")) {
            measureEvent = proxyIndex;
            break;
        }
    }
    QVERIFY(measureEvent.isValid());

    auto* details = window.findChild<QTabWidget*>(QStringLiteral("runDetailsTabs"));
    QVERIFY(details);
    QCOMPARE(details->tabText(details->indexOf(timelineView)),
             QStringLiteral("Execution Log"));
    details->setCurrentWidget(timelineView);
    timelineView->scrollTo(measureEvent, QAbstractItemView::PositionAtCenter);
    QTest::qWait(20);
    const auto targetRect = timelineView->visualRect(measureEvent);
    QVERIFY(targetRect.isValid());
    QTest::mouseClick(timelineView->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      targetRect.center());

    QTRY_COMPARE_WITH_TIMEOUT(
        resultView->model()
            ->data(resultView->currentIndex().siblingAtColumn(UutStepModel::NameColumn))
            .toString(),
        QStringLiteral("Measure"),
        1000);
    QTRY_COMPARE_WITH_TIMEOUT(sequenceTreeModel->nodePathForIndex(
                                  sequenceTreeView->currentIndex()),
                              QStringLiteral("measure"),
                              1000);

    const auto resultIndex = resultView->currentIndex();
    resultView->scrollTo(resultIndex, QAbstractItemView::PositionAtCenter);
    QTest::qWait(20);
    const auto resultRect = resultView->visualRect(resultIndex);
    QVERIFY(resultRect.isValid());
    QTest::mouseDClick(resultView->viewport(),
                       Qt::LeftButton,
                       Qt::NoModifier,
                       resultRect.center());
    QTRY_COMPARE_WITH_TIMEOUT(details->currentWidget(),
                              static_cast<QWidget*>(timelineView),
                              1000);
    QTRY_VERIFY_WITH_TIMEOUT(timelineView->currentIndex().isValid(), 1000);
    const auto focusedSourceIndex = timelineProxy->mapToSource(
        timelineView->currentIndex());
    const auto focusedLog = model->eventAt(focusedSourceIndex.row());
    QVERIFY(focusedLog.has_value());
    QCOMPARE(focusedLog->nodeDisplayName, QStringLiteral("Measure"));
    QCOMPARE(focusedLog->kind,
             PicoATE::Core::RuntimeEventKind::AttemptStarted);
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::multiUutOverviewShowsRetryAndRecentSteps()
{
    using namespace PicoATE::Core;

    StepReport first;
    first.stepId = QStringLiteral("first");
    first.nodePath = QStringLiteral("main.first");
    first.displayName = QStringLiteral("Open Device");
    StepReport second;
    second.stepId = QStringLiteral("second");
    second.nodePath = QStringLiteral("main.second");
    second.displayName = QStringLiteral("Configure Device");
    StepReport third;
    third.stepId = QStringLiteral("third");
    third.nodePath = QStringLiteral("main.third");
    third.displayName = QStringLiteral("Read Registers");
    UutReport previewUut;
    previewUut.uutId = QStringLiteral("UUT-1");
    previewUut.steps = {first, second, third};
    ExecutionReport preview;
    preview.uuts = {previewUut};

    RunRequest::UutInput input;
    input.uutId = QStringLiteral("UUT-1");
    input.variables.insert(QStringLiteral("serialNumber"),
                           QStringLiteral("BTSN00000001"));

    UutOverviewModel model;
    model.resetForRun(preview, {input});
    MultiUutOverviewWidget overview;
    overview.resize(860, 420);
    overview.setModel(&model);
    overview.show();
    QTest::qWait(20);

    RuntimeEvent registered;
    registered.kind = RuntimeEventKind::UutRegistered;
    registered.uutId = QStringLiteral("UUT-1");
    const auto completedEvent = [&registered](const QString& nodeId,
                                               const QString& displayName) {
        RuntimeEvent event = registered;
        event.kind = RuntimeEventKind::NodeStateChanged;
        event.nodeId = nodeId;
        event.nodeDisplayName = displayName;
        event.activationState = ActivationState::Passed;
        event.outcome = NodeOutcome::Passed;
        return event;
    };
    RuntimeEvent retry = completedEvent(QStringLiteral("main.third"),
                                        QStringLiteral("Read Registers"));
    retry.kind = RuntimeEventKind::RetryScheduled;
    retry.activationState = ActivationState::Running;
    retry.outcome = NodeOutcome::Failed;
    retry.errorCode = QStringLiteral("TRANSIENT");
    retry.details.insert(QStringLiteral("maxAttempts"), 3);
    retry.details.insert(QStringLiteral("retryAttemptIndex"), 1);
    model.applyRuntimeEvents(
        {registered,
         completedEvent(QStringLiteral("main.first"),
                        QStringLiteral("Open Device")),
         completedEvent(QStringLiteral("main.second"),
                        QStringLiteral("Configure Device")),
         retry});

    auto* card = overview.findChild<QAbstractButton*>(
        QStringLiteral("uutOverviewCard_1"));
    QTRY_VERIFY_WITH_TIMEOUT(card, 500);
    auto* caption = card->findChild<QLabel*>(
        QStringLiteral("uutOverviewStepCaption"));
    auto* retryLabel = card->findChild<QLabel*>(
        QStringLiteral("uutOverviewRetry"));
    auto* errorLabel = card->findChild<QLabel*>(
        QStringLiteral("uutOverviewError"));
    auto* currentState = card->findChild<QLabel*>(
        QStringLiteral("uutOverviewCurrentState"));
    auto* currentStep = card->findChild<QLabel*>(
        QStringLiteral("uutOverviewStep"));
    auto* latestRecent = card->findChild<QLabel*>(
        QStringLiteral("uutOverviewRecentStep_1"));
    auto* earlierRecent = card->findChild<QLabel*>(
        QStringLiteral("uutOverviewRecentStep_2"));
    QVERIFY(caption);
    QVERIFY(retryLabel);
    QVERIFY(errorLabel);
    QVERIFY(currentState);
    QVERIFY(currentStep);
    QVERIFY(latestRecent);
    QVERIFY(earlierRecent);
    QCOMPARE(caption->text(), QStringLiteral("RETRYING CURRENT STEP"));
    QVERIFY(retryLabel->isVisible());
    QCOMPARE(retryLabel->text(), QStringLiteral("ATTEMPT 2 / 3"));
    QCOMPARE(errorLabel->text(), QStringLiteral("TRANSIENT"));
    QCOMPARE(currentState->text(), QStringLiteral("RETRY"));
    QCOMPARE(latestRecent->text(), QStringLiteral("Configure Device"));
    QCOMPARE(earlierRecent->text(), QStringLiteral("Open Device"));

    RuntimeEvent sharedPeriodic;
    sharedPeriodic.kind = RuntimeEventKind::PeriodicTaskStateChanged;
    sharedPeriodic.nodeId = QStringLiteral("shared-heartbeat");
    sharedPeriodic.nodeDisplayName = QStringLiteral("Shared Heartbeat");
    sharedPeriodic.periodicTaskId = QStringLiteral("shared-heartbeat:batch");
    sharedPeriodic.periodicTaskShared = true;
    sharedPeriodic.periodicTaskState = PeriodicTaskState::Waiting;
    sharedPeriodic.periodicIntervalMs = 5000;
    sharedPeriodic.periodicNextDueAtUtc =
        QDateTime::currentDateTimeUtc().addSecs(5);

    RuntimeEvent uutPeriodic = sharedPeriodic;
    uutPeriodic.uutId = QStringLiteral("UUT-1");
    uutPeriodic.nodeId = QStringLiteral("uut-heartbeat");
    uutPeriodic.nodeDisplayName = QStringLiteral("UUT Heartbeat");
    uutPeriodic.periodicTaskId = QStringLiteral("uut-heartbeat:UUT-1");
    uutPeriodic.periodicTaskShared = false;
    uutPeriodic.periodicTaskState = PeriodicTaskState::Running;
    uutPeriodic.periodicInvocationIndex = 2;
    uutPeriodic.periodicCounter = 2;
    uutPeriodic.periodicNextDueAtUtc = {};
    model.applyRuntimeEvents({sharedPeriodic, uutPeriodic});

    auto* sharedPeriodicPanel = overview.findChild<QWidget*>(
        QStringLiteral("sharedPeriodicTaskPanel"));
    auto* sharedPeriodicState = overview.findChild<QLabel*>(
        QStringLiteral("sharedPeriodicTaskState"));
    auto* sharedPeriodicResult = overview.findChild<QLabel*>(
        QStringLiteral("sharedPeriodicTaskResult"));
    auto* sharedPeriodicCountdown = overview.findChild<QLabel*>(
        QStringLiteral("sharedPeriodicTaskCountdown"));
    auto* uutPeriodicPanel = card->findChild<QWidget*>(
        QStringLiteral("uutOverviewPeriodicPanel"));
    auto* uutPeriodicState = card->findChild<QLabel*>(
        QStringLiteral("uutOverviewPeriodicState"));
    auto* uutPeriodicCountdown = card->findChild<QLabel*>(
        QStringLiteral("uutOverviewPeriodicCountdown"));
    QVERIFY(sharedPeriodicPanel);
    QVERIFY(sharedPeriodicState);
    QVERIFY(sharedPeriodicResult);
    QVERIFY(sharedPeriodicCountdown);
    QVERIFY(uutPeriodicPanel);
    QVERIFY(uutPeriodicState);
    QVERIFY(uutPeriodicCountdown);
    QTRY_VERIFY(sharedPeriodicPanel->isVisible());
    QTRY_VERIFY(uutPeriodicPanel->isVisible());
    QCOMPARE(sharedPeriodicState->text(), QStringLiteral("WAITING"));
    QVERIFY(sharedPeriodicCountdown->text().startsWith(
        QStringLiteral("NEXT IN ")));
    QVERIFY(!sharedPeriodicResult->isVisible());
    QCOMPARE(uutPeriodicState->text(), QStringLiteral("RUNNING"));
    QCOMPARE(uutPeriodicCountdown->text(), QStringLiteral("COUNT 2"));
    QCOMPARE(caption->text(), QStringLiteral("RETRYING CURRENT STEP"));
    QCOMPARE(currentStep->text(), QStringLiteral("Read Registers"));

    RuntimeEvent acquiredResource;
    acquiredResource.kind = RuntimeEventKind::ResourceStateChanged;
    acquiredResource.uutId = QStringLiteral("UUT-1");
    acquiredResource.nodeId = QStringLiteral("main.third");
    acquiredResource.requestId = QStringLiteral("resource-uut-1");
    acquiredResource.resourceLeaseId = QStringLiteral("lease-uut-1");
    acquiredResource.resourceState = ResourceRuntimeState::Acquired;
    acquiredResource.resourceIds = {QStringLiteral("MODBUS1"),
                                    QStringLiteral("CAN1")};
    acquiredResource.timestampUtc = QDateTime::currentDateTimeUtc();

    RuntimeEvent waitingResource = acquiredResource;
    waitingResource.requestId = QStringLiteral("resource-wait-uut-1");
    waitingResource.resourceLeaseId.clear();
    waitingResource.resourceState = ResourceRuntimeState::Waiting;
    waitingResource.resourceIds = {QStringLiteral("DMM1")};
    waitingResource.resourceBlockingUutIds = {QStringLiteral("UUT-2")};
    waitingResource.resourceWaitingSinceUtc =
        QDateTime::currentDateTimeUtc().addMSecs(-2300);

    RuntimeEvent sharedResource = acquiredResource;
    sharedResource.resourceShared = true;
    sharedResource.requestId = QStringLiteral("resource-shared");
    sharedResource.resourceLeaseId = QStringLiteral("lease-shared");
    sharedResource.resourceIds = {QStringLiteral("PSU1")};
    model.applyRuntimeEvents(
        {acquiredResource, waitingResource, sharedResource});

    auto* resourcePanel = card->findChild<QWidget*>(
        QStringLiteral("uutOverviewResourceBadge"));
    auto* resourceUsing = card->findChild<QLabel*>(
        QStringLiteral("uutOverviewResourceUsing"));
    auto* resourceWaiting = card->findChild<QLabel*>(
        QStringLiteral("uutOverviewResourceWaiting"));
    auto* sharedResourcePanel = overview.findChild<QWidget*>(
        QStringLiteral("sharedResourcePanel"));
    auto* sharedResourceUsing = overview.findChild<QLabel*>(
        QStringLiteral("sharedResourceUsing"));
    QVERIFY(resourcePanel);
    QVERIFY(resourceUsing);
    QVERIFY(resourceWaiting);
    QVERIFY(sharedResourcePanel);
    QVERIFY(sharedResourceUsing);
    QTRY_VERIFY(resourcePanel->isVisible());
    QTRY_VERIFY(sharedResourcePanel->isVisible());
    QVERIFY(resourceUsing->text().contains(QStringLiteral("CAN1")));
    QVERIFY(resourceUsing->text().contains(QStringLiteral("+1")));
    QVERIFY(resourceWaiting->text().contains(QStringLiteral("DMM1")));
    QVERIFY(resourcePanel->toolTip().contains(QStringLiteral("MODBUS1")));
    QVERIFY(resourcePanel->toolTip().contains(QStringLiteral("HELD BY: UUT-2")));
    QVERIFY(sharedResourceUsing->text().contains(QStringLiteral("PSU1")));
    QVERIFY(sharedResourceUsing->text().contains(QStringLiteral("EXECUTOR UUT-1")));

    RuntimeEvent releasedResource = acquiredResource;
    releasedResource.resourceState = ResourceRuntimeState::Released;
    RuntimeEvent cancelledResource = waitingResource;
    cancelledResource.resourceState = ResourceRuntimeState::Cancelled;
    RuntimeEvent releasedSharedResource = sharedResource;
    releasedSharedResource.resourceState = ResourceRuntimeState::Released;
    model.applyRuntimeEvents(
        {releasedResource, cancelledResource, releasedSharedResource});
    QTRY_VERIFY(!resourcePanel->isVisible());
    QTRY_VERIFY(!sharedResourcePanel->isVisible());

    RuntimeEvent sharedPeriodicCompleted = sharedPeriodic;
    sharedPeriodicCompleted.periodicInvocationIndex = 1;
    sharedPeriodicCompleted.outcome = NodeOutcome::Passed;
    sharedPeriodicCompleted.periodicNextDueAtUtc =
        QDateTime::currentDateTimeUtc().addSecs(5);
    model.applyRuntimeEvents({sharedPeriodicCompleted});
    QTRY_COMPARE(sharedPeriodicState->text(), QStringLiteral("WAITING"));
    QTRY_COMPARE(sharedPeriodicResult->text(), QStringLiteral("LAST PASS"));
    QVERIFY(sharedPeriodicResult->isVisible());

    RuntimeEvent finalFailedAttempt = retry;
    finalFailedAttempt.kind = RuntimeEventKind::AttemptCompleted;
    finalFailedAttempt.activationState = ActivationState::Failed;
    finalFailedAttempt.details.insert(QStringLiteral("retryAttemptIndex"), 3);
    model.applyRuntimeEvents({finalFailedAttempt});

    RuntimeEvent cleanup = retry;
    cleanup.uutId.clear();
    cleanup.kind = RuntimeEventKind::NodeStateChanged;
    cleanup.nodeId = QStringLiteral("cleanup.close");
    cleanup.nodeDisplayName = QStringLiteral("Close Shared Device");
    cleanup.nodePhase = ExecutionPhase::Cleanup;
    cleanup.activationState = ActivationState::Running;
    cleanup.outcome = NodeOutcome::Unknown;
    cleanup.errorCode.clear();
    model.applyRuntimeEvents({cleanup});
    QTRY_COMPARE(caption->text(), QStringLiteral("CLEANUP STEP"));
    QCOMPARE(currentState->text(), QStringLiteral("RUN"));
    QCOMPARE(currentStep->text(), QStringLiteral("Close Shared Device"));

    model.applyRuntimeEvents({acquiredResource});
    QTRY_VERIFY(resourcePanel->isVisible());

    RuntimeEvent failedUut;
    failedUut.kind = RuntimeEventKind::UutCompleted;
    failedUut.uutId = QStringLiteral("UUT-1");
    failedUut.outcome = NodeOutcome::Failed;
    failedUut.details.insert(QStringLiteral("hasError"), true);
    model.applyRuntimeEvents({failedUut});
    QTRY_COMPARE(caption->text(), QStringLiteral("FAILED STEP"));
    QCOMPARE(currentState->text(), QStringLiteral("FAIL"));
    QCOMPARE(currentStep->text(), QStringLiteral("Read Registers"));
    QTRY_VERIFY(!resourcePanel->isVisible());
}

void MainWindowLifecycleTests::multiUutOverviewShowsDelayedCleanupOverlay()
{
    using namespace PicoATE::Core;

    RunRequest::UutInput first;
    first.uutId = QStringLiteral("UUT-1");
    RunRequest::UutInput second;
    second.uutId = QStringLiteral("UUT-2");
    UutOverviewModel model;
    model.resetForRun({}, {first, second});

    MultiUutOverviewWidget overview;
    overview.resize(1000, 620);
    overview.setModel(&model);
    overview.show();
    QTest::qWait(20);

    auto* overlay = overview.findChild<QWidget*>(
        QStringLiteral("multiUutCleanupOverlay"));
    auto* spinnerWidget = overview.findChild<QWidget*>(
        QStringLiteral("multiUutCleanupSpinner"));
    auto* stepLabel = overview.findChild<QLabel*>(
        QStringLiteral("multiUutCleanupStep"));
    auto* titleLabel = overview.findChild<QLabel*>(
        QStringLiteral("multiUutCleanupTitle"));
    auto* statusLabel = overview.findChild<QLabel*>(
        QStringLiteral("multiUutCleanupStatus"));
    QVERIFY(overlay);
    QVERIFY(spinnerWidget);
    QVERIFY(stepLabel);
    QVERIFY(titleLabel);
    QVERIFY(statusLabel);
    auto* spinner = static_cast<LoadingSpinner*>(spinnerWidget);
    QVERIFY(!overlay->isVisible());
    QVERIFY(!spinner->isRunning());

    RuntimeEvent prompt;
    prompt.kind = RuntimeEventKind::OperatorPromptRequested;
    prompt.uutId = QStringLiteral("UUT-1");
    prompt.details = {
        {QStringLiteral("promptInstanceId"), QStringLiteral("stop-prompt")},
        {QStringLiteral("mode"), QStringLiteral("confirm")},
        {QStringLiteral("title"), QStringLiteral("Operator Check")},
        {QStringLiteral("message"), QStringLiteral("Confirm the product state")},
    };
    QVERIFY(overview.presentOperatorPrompt(prompt, {}));

    overview.beginStopTransition();
    QVERIFY(overlay->isVisible());
    QVERIFY(spinner->isRunning());
    QCOMPARE(titleLabel->text(), QStringLiteral("STOPPING"));
    QCOMPARE(statusLabel->text(),
             QStringLiteral("Stopping active work before cleanup..."));
    overview.resetRuntimeState();
    overview.clearOperatorPrompts();
    QVERIFY(!overlay->isVisible());
    QVERIFY(!spinner->isRunning());

    RuntimeEvent cleaningUp;
    cleaningUp.kind = RuntimeEventKind::SessionStateChanged;
    cleaningUp.executionState = ExecutionState::CleaningUp;
    overview.applyRuntimeEvents({cleaningUp});
    QTest::qWait(180);
    QVERIFY(!overlay->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(overlay->isVisible(), 400);
    QVERIFY(spinner->isRunning());
    QCOMPARE(titleLabel->text(), QStringLiteral("CLEANING UP"));

    RuntimeEvent cleanupStep;
    cleanupStep.kind = RuntimeEventKind::NodeStateChanged;
    cleanupStep.nodeId = QStringLiteral("cleanup.close-shared-modbus");
    cleanupStep.nodeDisplayName = QStringLiteral("Close Shared Modbus");
    cleanupStep.nodePhase = ExecutionPhase::Cleanup;
    cleanupStep.activationState = ActivationState::Running;
    overview.applyRuntimeEvents({cleanupStep});
    QCOMPARE(stepLabel->text(), QStringLiteral("Current: Close Shared Modbus"));

    RuntimeEvent completed;
    completed.kind = RuntimeEventKind::SessionStateChanged;
    completed.executionState = ExecutionState::Completed;
    overview.applyRuntimeEvents({completed});
    QVERIFY(!overlay->isVisible());
    QVERIFY(!spinner->isRunning());
}

void MainWindowLifecycleTests::adminMultiUutRunShowsOverviewAndNavigatesToDetails()
{
    QSettings().clear();
    const QString projectDir = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR);

    MainWindow window;
    QTemporaryDir stationDirectory;
    const auto capacityPath = stationDirectory.filePath(QStringLiteral("StationSystem.json"));
    QFile capacityFile(capacityPath);
    QVERIFY(capacityFile.open(QIODevice::WriteOnly));
    capacityFile.write(R"({"stationId":"OVERVIEW","uutCount":4,"scanDialogEnabled":false,"devices":[]})");
    capacityFile.close();
    QVERIFY(window.openStationFile(capacityPath));
    QVERIFY(window.openSequenceFile(
        projectDir + QStringLiteral("/examples/simple_sequence.json")));
    window.resize(1280, 800);
    window.show();
    QTest::qWait(20);

    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* uutCount = window.findChild<QSpinBox*>(
        QStringLiteral("uutCountSpinBox"));
    auto* runAction = window.findChild<QAction*>(QStringLiteral("runAction"));
    auto* runStack = window.findChild<QStackedWidget*>(
        QStringLiteral("adminRunStack"));
    auto* overviewPage = window.findChild<QWidget*>(
        QStringLiteral("adminRunOverviewPage"));
    auto* detailPage = window.findChild<QWidget*>(
        QStringLiteral("adminRunDetailPage"));
    auto* overviewModel = window.findChild<UutOverviewModel*>();
    auto* stepModel = window.findChild<UutStepModel*>();
    auto* timelineProxy = window.findChild<UutRuntimeTimelineProxyModel*>();
    auto* uutNavigation = window.findChild<QButtonGroup*>(
        QStringLiteral("adminUutNavigationGroup"));
    auto* backButton = window.findChild<QPushButton*>(
        QStringLiteral("adminBackToUutOverview"));
    auto* runSidebar = window.findChild<QWidget*>(
        QStringLiteral("adminRunSidebar"));
    auto* overviewSummary = window.findChild<QWidget*>(
        QStringLiteral("adminOverviewSummary"));
    auto* overviewSummaryState = window.findChild<QLabel*>(
        QStringLiteral("adminOverviewSummaryState"));
    auto* overviewCustomerId = window.findChild<QLabel*>(
        QStringLiteral("adminOverviewCustomerIdValue"));
    auto* overviewJig = window.findChild<QLabel*>(
        QStringLiteral("adminOverviewJigValue"));
    auto* overviewYieldChart = window.findChild<QWidget*>(
        QStringLiteral("adminOverviewYieldChart"));
    auto* serialCaption = window.findChild<QLabel*>(
        QStringLiteral("adminSerialCaption"));
    auto* serialLabel = window.findChild<QLabel*>(
        QStringLiteral("adminSerialLabel"));
    auto* progressPanel = window.findChild<QWidget*>(
        QStringLiteral("adminProgressPanel"));
    auto* overallResult = window.findChild<QLabel*>(
        QStringLiteral("adminOverallResult"));
    auto* resultView = window.findChild<QTreeView*>(
        QStringLiteral("resultView"));
    QVERIFY(viewModel);
    QVERIFY(uutCount);
    QVERIFY(runAction);
    QVERIFY(runStack);
    QVERIFY(overviewPage);
    QVERIFY(detailPage);
    QVERIFY(overviewModel);
    QVERIFY(stepModel);
    QVERIFY(timelineProxy);
    QVERIFY(uutNavigation);
    QVERIFY(backButton);
    QVERIFY(runSidebar);
    QVERIFY(overviewSummary);
    QVERIFY(overviewSummaryState);
    QVERIFY(overviewCustomerId);
    QVERIFY(overviewJig);
    QVERIFY(overviewYieldChart);
    QVERIFY(overviewSummaryState->styleSheet().contains(
        QStringLiteral("border:1px solid")));
    QVERIFY(!window.findChild<QLabel*>(
        QStringLiteral("adminOverviewYieldValue")));
    QVERIFY(serialCaption);
    QVERIFY(serialLabel);
    QVERIFY(progressPanel);
    QVERIFY(overallResult);
    QVERIFY(resultView);
    QVERIFY(!window.findChild<QComboBox*>(QStringLiteral("adminUutSelector")));

    if (uutCount->isHidden()) {
        auto* detailNavigation = window.findChild<QWidget*>(
            QStringLiteral("adminUutDetailNavigation"));
        auto* buttonsHost = window.findChild<QWidget*>(
            QStringLiteral("adminUutButtonsHost"));
        QVERIFY(detailNavigation);
        QVERIFY(buttonsHost);
        QVERIFY(detailNavigation->isHidden());
        QVERIFY(buttonsHost->isHidden());
        QVERIFY(backButton->isHidden());

        // Exercise four UUTs through the hidden control to prove the rollout
        // gates affect presentation only; the model and runtime stay intact.
        uutCount->setValue(4);
        viewModel->compile();
        QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
        QCOMPARE(runStack->currentWidget(), detailPage);
        QCOMPARE(overviewModel->rowCount(), 4);
        QCOMPARE(stepModel->visibleUutId(), QStringLiteral("UUT-1"));
        QVERIFY(!window.findChild<QPushButton*>(
            QStringLiteral("adminUutButton_1")));

        runAction->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(overviewModel->rowCount(), 4, 1000);
        QCOMPARE(runStack->currentWidget(), detailPage);
        QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                                 viewModel->state() == UiRunState::Failed,
                                 5000);
        QCOMPARE(runStack->currentWidget(), detailPage);
        for (int row = 0; row < overviewModel->rowCount(); ++row) {
            const auto entry = overviewModel->entryAt(row);
            QVERIFY(entry.has_value());
            QCOMPARE(entry->uutId, QStringLiteral("UUT-%1").arg(row + 1));
            QVERIFY(entry->state == UutOverviewState::Passed ||
                    entry->state == UutOverviewState::Failed ||
                    entry->state == UutOverviewState::Stopped);
        }
        QVERIFY(window.close());
        return;
    }

    uutCount->setValue(4);
    viewModel->compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    QVERIFY(backButton->isVisible());
    QVERIFY(backButton->isEnabled());
    QVERIFY(backButton->isChecked());
    QCOMPARE(runStack->currentWidget(), overviewPage);
    QVERIFY(runSidebar->isHidden());
    QVERIFY(overviewSummary->isVisible());
    QVERIFY(overviewYieldChart->isVisible());
    QVERIFY(overviewYieldChart->mapTo(overviewSummary, QPoint()).x() >
            overviewSummaryState->mapTo(overviewSummary, QPoint()).x());
    QCOMPARE(overviewModel->rowCount(), 4);
    for (int row = 0; row < overviewModel->rowCount(); ++row) {
        const auto entry = overviewModel->entryAt(row);
        QVERIFY(entry.has_value());
        QCOMPARE(entry->uutId, QStringLiteral("UUT-%1").arg(row + 1));
        QCOMPARE(entry->state, UutOverviewState::Waiting);
        QCOMPARE(entry->completedSteps, 0);
        QVERIFY(entry->totalSteps > 0);
        QCOMPARE(entry->progress, 0);
    }
    auto* previewCard = window.findChild<QAbstractButton*>(
        QStringLiteral("uutOverviewCard_1"));
    QVERIFY(previewCard);
    auto* previewCardState = previewCard->findChild<QLabel*>(
        QStringLiteral("uutOverviewState"));
    QVERIFY(previewCardState);
    QCOMPARE(previewCardState->text(), QStringLiteral("WAITING"));
    runAction->trigger();

    QTRY_COMPARE_WITH_TIMEOUT(overviewModel->rowCount(), 4, 1000);
    QCOMPARE(runStack->currentWidget(), overviewPage);
    QVERIFY(runSidebar->isHidden());
    QVERIFY(overviewSummary->isVisible());
    QVERIFY(serialCaption->isHidden());
    QVERIFY(serialLabel->isHidden());
    QVERIFY(backButton->isVisible());
    QVERIFY(backButton->isCheckable());
    QVERIFY(backButton->isChecked());
    QTRY_VERIFY_WITH_TIMEOUT(
        backButton->property("overviewIndicatorFill").toReal() > 0.99, 500);
    QVERIFY(progressPanel->isHidden());
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             5000);
    QCOMPARE(overallResult->text(), QStringLiteral("COMPLETED"));
    QCOMPARE(overviewSummaryState->text(), QStringLiteral("COMPLETED"));
    QCOMPARE(overviewYieldChart->property("passedCount").toInt() +
                 overviewYieldChart->property("failedCount").toInt(),
             4);
    const int overviewResultPointSize = overallResult->font().pointSize();
    QVERIFY(overviewResultPointSize <= 14);
    QCOMPARE(overviewModel->rowCount(), 4);
    for (int row = 0; row < overviewModel->rowCount(); ++row) {
        const auto entry = overviewModel->entryAt(row);
        QVERIFY(entry.has_value());
        QCOMPARE(entry->uutId, QStringLiteral("UUT-%1").arg(row + 1));
        QVERIFY(entry->state == UutOverviewState::Passed ||
                entry->state == UutOverviewState::Failed ||
                entry->state == UutOverviewState::Stopped);
    }

    auto* secondCard = window.findChild<QAbstractButton*>(
        QStringLiteral("uutOverviewCard_2"));
    QVERIFY(secondCard);
    auto* cardState = secondCard->findChild<QLabel*>(
        QStringLiteral("uutOverviewState"));
    auto* cardSerial = secondCard->findChild<QLabel*>(
        QStringLiteral("uutOverviewSerial"));
    auto* cardStepCaption = secondCard->findChild<QLabel*>(
        QStringLiteral("uutOverviewCaption"));
    auto* cardPercent = secondCard->findChild<QLabel*>(
        QStringLiteral("uutOverviewPercent"));
    auto* cardError = secondCard->findChild<QLabel*>(
        QStringLiteral("uutOverviewError"));
    auto* cardRecent = secondCard->findChild<QLabel*>(
        QStringLiteral("uutOverviewRecentStep_1"));
    QVERIFY(cardState);
    QVERIFY(cardSerial);
    QVERIFY(cardStepCaption);
    QVERIFY(cardPercent);
    QVERIFY(cardError);
    QVERIFY(cardRecent);
    QVERIFY(!cardState->text().isEmpty());
    QVERIFY(cardSerial->text().startsWith(QStringLiteral("SN")));
    QVERIFY(!cardStepCaption->text().isEmpty());
    QVERIFY(cardPercent->text().endsWith(QLatin1Char('%')));
    QVERIFY(!cardError->isHidden());
    QVERIFY(!cardError->text().isEmpty());
    QVERIFY(!cardRecent->isHidden());
    QVERIFY(!cardRecent->text().isEmpty());
    QTest::mouseClick(secondCard, Qt::LeftButton);
    auto* secondButton = window.findChild<QPushButton*>(
        QStringLiteral("adminUutButton_2"));
    auto* thirdButton = window.findChild<QPushButton*>(
        QStringLiteral("adminUutButton_3"));
    auto* firstButton = window.findChild<QPushButton*>(
        QStringLiteral("adminUutButton_1"));
    QVERIFY(firstButton);
    QVERIFY(secondButton);
    QVERIFY(thirdButton);
    QCOMPARE(runStack->currentWidget(), detailPage);
    const auto selectedEntry = overviewModel->entryAt(1);
    QVERIFY(selectedEntry.has_value());
    QCOMPARE(overallResult->text(),
             selectedEntry->state == UutOverviewState::Passed
                 ? QStringLiteral("PASS")
                 : QStringLiteral("FAIL"));
    QVERIFY(overallResult->font().pointSize() > overviewResultPointSize);
    QVERIFY(runSidebar->isVisible());
    QVERIFY(!overviewSummary->isVisible());
    QVERIFY(serialCaption->isVisible());
    QVERIFY(serialLabel->isVisible());
    QVERIFY(progressPanel->isVisible());
    QVERIFY(!backButton->isChecked());
    QTRY_VERIFY_WITH_TIMEOUT(
        backButton->property("overviewIndicatorFill").toReal() < 0.01, 500);
    QVERIFY(backButton->isVisible());
    QVERIFY(backButton->isEnabled());
    QCOMPARE(stepModel->visibleUutId(), QStringLiteral("UUT-2"));
    QCOMPARE(timelineProxy->visibleUutId(), QStringLiteral("UUT-2"));
    QVERIFY(secondButton->isChecked());
    QCOMPARE(secondButton->text(), QStringLiteral("UUT2"));
    QVERIFY(secondButton->font().bold());
    QCOMPARE(secondButton->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    QCOMPARE(firstButton->width(), secondButton->width());
    QCOMPARE(secondButton->width(), thirdButton->width());
    QVERIFY(qAbs(firstButton->mapTo(&window, QPoint(0, 0)).x() -
                 resultView->mapTo(&window, QPoint(0, 0)).x()) <= 2);
    QCOMPARE(stepModel->rowCount(), 3);

    QTest::mouseClick(thirdButton, Qt::LeftButton);
    QCOMPARE(stepModel->visibleUutId(), QStringLiteral("UUT-3"));
    QCOMPARE(timelineProxy->visibleUutId(), QStringLiteral("UUT-3"));
    QVERIFY(thirdButton->isChecked());
    QVERIFY(!secondButton->isChecked());

    QTest::mouseClick(backButton, Qt::LeftButton);
    QCOMPARE(runStack->currentWidget(), overviewPage);
    QVERIFY(runSidebar->isHidden());
    QVERIFY(overviewSummary->isVisible());
    QVERIFY(serialCaption->isHidden());
    QVERIFY(serialLabel->isHidden());
    QVERIFY(progressPanel->isHidden());
    QVERIFY(backButton->isChecked());
    QTRY_VERIFY_WITH_TIMEOUT(
        backButton->property("overviewIndicatorFill").toReal() > 0.99, 500);
    QVERIFY(!secondButton->isChecked());
    QVERIFY(!thirdButton->isChecked());
    QCOMPARE(overallResult->text(), QStringLiteral("COMPLETED"));
    QCOMPARE(overallResult->font().pointSize(), overviewResultPointSize);

    uutCount->setValue(2);
    runAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(overviewModel->rowCount(), 2, 1000);
    QCOMPARE(runStack->currentWidget(), overviewPage);
    auto* cardsHost = window.findChild<QWidget*>(
        QStringLiteral("multiUutOverviewCards"));
    auto* firstPairCard = window.findChild<QAbstractButton*>(
        QStringLiteral("uutOverviewCard_1"));
    auto* secondPairCard = window.findChild<QAbstractButton*>(
        QStringLiteral("uutOverviewCard_2"));
    QVERIFY(cardsHost);
    QVERIFY(firstPairCard);
    QVERIFY(secondPairCard);
    QTRY_VERIFY_WITH_TIMEOUT(firstPairCard->height() > 280, 500);
    QVERIFY(firstPairCard->height() <= 460);
    QCOMPARE(firstPairCard->height(), secondPairCard->height());
    QTRY_VERIFY_WITH_TIMEOUT(firstPairCard->y() > 0, 500);
    QCOMPARE(firstPairCard->y(), secondPairCard->y());
    const int cardCenterY = firstPairCard->geometry().center().y();
    const int hostCenterY = cardsHost->rect().center().y();
    QVERIFY2(qAbs(cardCenterY - hostCenterY) <= 3,
             qPrintable(QStringLiteral("card center %1, host center %2, card %3x%4 at %5,%6")
                            .arg(cardCenterY)
                            .arg(hostCenterY)
                            .arg(firstPairCard->width())
                            .arg(firstPairCard->height())
                            .arg(firstPairCard->x())
                            .arg(firstPairCard->y())));
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             5000);
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::persistsLayoutAndRecentFiles()
{
    QSettings().clear();
    QTemporaryDir files;
    QVERIFY(files.isValid());
    const auto sequencePath = files.filePath(QStringLiteral("recent_sequence.json"));
    const auto stationPath = files.filePath(QStringLiteral("recent_station.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/stations/basic_station.json"),
                       stationPath));

    bool uutCountControlVisible = false;
    {
        MainWindow window;
        QVERIFY(window.openSequenceFile(sequencePath));
        QVERIFY(window.openStationFile(stationPath));
        window.resize(1040, 680);
        window.show();
        QTest::qWait(20);

        auto* workspaceTabs = window.findChild<QTabWidget*>(
            QStringLiteral("workspaceTabs"));
        auto* detailsTabs = window.findChild<QTabWidget*>(
            QStringLiteral("runDetailsTabs"));
        auto* runSplitter = window.findChild<QSplitter*>(
            QStringLiteral("runSplitter"));
        auto* uutCount = window.findChild<QSpinBox*>(
            QStringLiteral("uutCountSpinBox"));
        QVERIFY(workspaceTabs);
        QVERIFY(detailsTabs);
        QVERIFY(runSplitter);
        QVERIFY(uutCount);
        uutCountControlVisible = !uutCount->isHidden();
        workspaceTabs->setCurrentIndex(2);
        detailsTabs->setCurrentIndex(3);
        runSplitter->setSizes({320, 720});
        uutCount->setValue(3);
        auto* stationDocument = window.findChild<StationDocument*>();
        auto* sequenceDocument = window.findChild<SequenceDocument*>();
        QVERIFY(stationDocument);
        QVERIFY(sequenceDocument);
        stationDocument->undoStack()->setClean();
        sequenceDocument->undoStack()->setClean();
        QVERIFY(window.close());
    }

    QSettings saved;
    QCOMPARE(saved.value(QStringLiteral("Recent/Sequences")).toStringList().first(),
             QFileInfo(sequencePath).absoluteFilePath());
    QCOMPARE(saved.value(QStringLiteral("Recent/Stations")).toStringList().first(),
             QFileInfo(stationPath).absoluteFilePath());
    QCOMPARE(saved.value(QStringLiteral("MainWindow/WorkspaceTab")).toInt(), 2);
    QCOMPARE(saved.value(QStringLiteral("MainWindow/RunDetailsTab")).toInt(), 3);
    QVERIFY(!saved.contains(QStringLiteral("MainWindow/UutCount")));

    MainWindow restored;
    restored.show();
    QTest::qWait(20);
    auto* workspaceTabs = restored.findChild<QTabWidget*>(
        QStringLiteral("workspaceTabs"));
    auto* detailsTabs = restored.findChild<QTabWidget*>(
        QStringLiteral("runDetailsTabs"));
    auto* runSplitter = restored.findChild<QSplitter*>(QStringLiteral("runSplitter"));
    auto* recentSequences = restored.findChild<QMenu*>(
        QStringLiteral("recentSequenceMenu"));
    auto* recentStations = restored.findChild<QMenu*>(
        QStringLiteral("recentStationMenu"));
    auto* resetLayout = restored.findChild<QAction*>(
        QStringLiteral("resetLayoutAction"));
    QVERIFY(workspaceTabs);
    QVERIFY(detailsTabs);
    QVERIFY(runSplitter);
    QVERIFY(recentSequences);
    QVERIFY(recentStations);
    QVERIFY(resetLayout);
    QCOMPARE(workspaceTabs->currentIndex(), 2);
    QCOMPARE(detailsTabs->currentIndex(), 3);
    auto* restoredUutCount = restored.findChild<QSpinBox*>(
        QStringLiteral("uutCountSpinBox"));
    QVERIFY(restoredUutCount);
    QVERIFY(restoredUutCount->isHidden());
    QCOMPARE(restoredUutCount->value(), 1);
    QCOMPARE(recentSequences->actions().size(), 1);
    QCOMPARE(recentStations->actions().size(), 1);
    QCOMPARE(recentSequences->actions().first()->toolTip(),
             QFileInfo(sequencePath).absoluteFilePath());
    const auto runSizes = runSplitter->sizes();
    QVERIFY(runSizes.size() == 2);
    QVERIFY(runSizes.at(0) > 0);
    QVERIFY(runSizes.at(1) > 0);

    resetLayout->trigger();
    QCOMPARE(workspaceTabs->currentIndex(), 0);
    QCOMPARE(detailsTabs->currentIndex(), 0);
    QVERIFY(!QSettings().contains(QStringLiteral("MainWindow/WorkspaceTab")));
    QVERIFY(restored.close());
}

void MainWindowLifecycleTests::invalidOrOffscreenGeometryFallsBackToPrimaryScreen()
{
    QSettings settings;
    settings.clear();
    settings.setValue(QStringLiteral("MainWindow/Geometry"), QByteArray("invalid"));

    MainWindow invalidGeometryWindow;
    QVERIFY(QGuiApplication::primaryScreen());
    QVERIFY(QGuiApplication::primaryScreen()->availableGeometry().intersects(
        invalidGeometryWindow.frameGeometry()));
    QCOMPARE(invalidGeometryWindow.size(), QSize(1180, 760));
    QVERIFY(invalidGeometryWindow.close());

    QMainWindow offscreenSource;
    offscreenSource.resize(1000, 700);
    offscreenSource.move(100000, 100000);
    settings.setValue(QStringLiteral("MainWindow/Geometry"),
                      offscreenSource.saveGeometry());

    MainWindow offscreenGeometryWindow;
    QVERIFY(QGuiApplication::primaryScreen()->availableGeometry().intersects(
        offscreenGeometryWindow.frameGeometry()));
    QVERIFY(offscreenGeometryWindow.close());
}

void MainWindowLifecycleTests::loginDialogDiscoversSequenceAndValidatesAdminPassword()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto projectPath = directory.filePath(
        QStringLiteral("projects/ProductA"));
    QVERIFY(QDir().mkpath(projectPath));
    const auto sequencePath = QDir(projectPath).filePath(
        QStringLiteral("product_seq_v1.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    QFile station(QDir(projectPath).filePath(QStringLiteral("StationSystem.json")));
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({
        "stationId":"line-1",
        "name":"Legacy Model",
        "customerId":"OLD-CUSTOMER",
        "scanDialogEnabled":true,
        "devices":[{
            "deviceId":"CAN1",
            "deviceType":"CAN",
            "enabled":true,
            "driverId":"",
            "address":""
        }]
    })");
    station.close();

    LoginDialog dialog(directory.path());
    auto* modeSelector = dialog.findChild<QFrame*>(
        QStringLiteral("loginModeSelector"));
    auto* testMode = dialog.findChild<QToolButton*>(
        QStringLiteral("loginTestModeButton"));
    auto* adminMode = dialog.findChild<QToolButton*>(
        QStringLiteral("loginAdminModeButton"));
    auto* sequences = dialog.findChild<QComboBox*>(
        QStringLiteral("loginSequenceCombo"));
    auto* password = dialog.findChild<QLineEdit*>(
        QStringLiteral("loginAdminPassword"));
    auto* login = dialog.findChild<QPushButton*>(QStringLiteral("loginButton"));
    auto* error = dialog.findChild<QLabel*>(QStringLiteral("loginErrorLabel"));
    auto* brand = dialog.findChild<QLabel*>(QStringLiteral("loginBrand"));
    auto* close = dialog.findChild<QToolButton*>(
        QStringLiteral("loginCloseButton"));
    auto* spinner = dialog.findChild<QWidget*>(
        QStringLiteral("loginLoadingSpinner"));
    QVERIFY(modeSelector);
    QVERIFY(testMode);
    QVERIFY(adminMode);
    QVERIFY(sequences);
    QVERIFY(password);
    QVERIFY(login);
    QVERIFY(error);
    QVERIFY(brand);
    QCOMPARE(brand->accessibleName(), QStringLiteral("SINEXCEL"));
    QVERIFY(!brand->pixmap().isNull());
    QCOMPARE(brand->alignment(), Qt::AlignCenter);
    QVERIFY(close);
    QVERIFY(spinner);
    dialog.show();
    QApplication::processEvents();
    QVERIFY(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(!dialog.findChild<QLabel*>(QStringLiteral("loginStationPath")));
    QVERIFY(testMode->isChecked());
    QVERIFY(!adminMode->isChecked());
    QCOMPARE(testMode->accessibleName(), QStringLiteral("Test mode"));
    QCOMPARE(adminMode->accessibleName(), QStringLiteral("Admin mode"));
    QCOMPARE(sequences->count(), 1);
    QCOMPARE(sequences->currentText(), QStringLiteral("ProductA"));
    QVERIFY(password->isHidden());
    const auto brandIsAboveFields = [&] {
        const int brandBottom = brand->mapTo(
            &dialog, QPoint(0, brand->height())).y();
        const int modeTop = modeSelector->mapTo(&dialog, QPoint()).y();
        return brandBottom < modeTop;
    };
    const int testHeight = dialog.height();
    QVERIFY(brandIsAboveFields());

    login->click();
    QApplication::processEvents();
    QVERIFY(!error->isHidden());
    QVERIFY(dialog.height() > testHeight);
    QVERIFY(brandIsAboveFields());
    QCOMPARE(dialog.result(), 0);

    adminMode->click();
    QApplication::processEvents();
    QVERIFY(adminMode->isChecked());
    QVERIFY(!testMode->isChecked());
    QVERIFY(!password->isHidden());
    QVERIFY(password->hasFocus());
    const int adminHeight = dialog.minimumHeight();
    QCOMPARE(dialog.maximumHeight(), adminHeight);
    QVERIFY(adminHeight > testHeight);
    QVERIFY(brandIsAboveFields());
    password->setText(QStringLiteral("-1"));
    const QPoint adminPosition = dialog.pos();
    login->click();
    QApplication::processEvents();
    QVERIFY(error->isHidden());
    QCOMPARE(dialog.minimumHeight(), adminHeight);
    QCOMPARE(dialog.maximumHeight(), adminHeight);
    QCOMPARE(dialog.pos(), adminPosition);
    QVERIFY(password->text().isEmpty());
    QCOMPARE(password->placeholderText(), QStringLiteral("Admin 密码错误"));
    QCOMPARE(password->echoMode(), QLineEdit::Password);
    QVERIFY(password->property("invalid").toBool());
    QVERIFY(password->hasFocus());
    QVERIFY(brandIsAboveFields());
    QCOMPARE(dialog.result(), 0);

    QTest::mouseClick(password, Qt::LeftButton, Qt::NoModifier,
                      password->rect().center());
    QTest::keyClicks(password, QStringLiteral("7"));
    QCOMPARE(password->text(), QStringLiteral("7"));
    QCOMPARE(password->placeholderText(), QStringLiteral("Admin password"));
    QCOMPARE(password->echoMode(), QLineEdit::Password);
    QVERIFY(!password->property("invalid").toBool());

    password->setText(QStringLiteral("300693"));
    login->click();
    QVERIFY(!login->isEnabled());
    QVERIFY(!spinner->isHidden());
    QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.selection().mode, UiMode::Admin);
    QCOMPARE(dialog.selection().adminAccess, AdminAccess::Standard);
    QCOMPARE(dialog.selection().sequencePath,
             QFileInfo(sequencePath).absoluteFilePath());
    QCOMPARE(dialog.selection().projectName, QStringLiteral("ProductA"));
    QCOMPARE(dialog.selection().stationPath,
             QFileInfo(station.fileName()).absoluteFilePath());
    QVERIFY(dialog.selection().scanDialogEnabled);
}

void MainWindowLifecycleTests::loginDialogOffersNewProjectTemplateWhenProjectsAreEmpty()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    LoginDialog dialog(directory.path());
    auto* sequences = dialog.findChild<QComboBox*>(
        QStringLiteral("loginSequenceCombo"));
    auto* automatic = dialog.findChild<QToolButton*>(
        QStringLiteral("loginAutoBySnButton"));
    auto* admin = dialog.findChild<QToolButton*>(
        QStringLiteral("loginAdminModeButton"));
    auto* password = dialog.findChild<QLineEdit*>(
        QStringLiteral("loginAdminPassword"));
    auto* login = dialog.findChild<QPushButton*>(QStringLiteral("loginButton"));
    QVERIFY(sequences);
    QVERIFY(automatic);
    QVERIFY(admin);
    QVERIFY(password);
    QVERIFY(login);
    QCOMPARE(sequences->count(), 1);
    QCOMPARE(sequences->currentText(), QStringLiteral("New Project Template"));
    QVERIFY(!automatic->isChecked());
    QVERIFY(!automatic->isEnabled());
    QVERIFY(login->isEnabled());

    login->click();
    QCOMPARE(dialog.result(), 0);

    admin->click();
    password->setText(QStringLiteral("300693"));
    login->click();
    QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    const auto selection = dialog.selection();
    QVERIFY(selection.newProjectTemplate);
    QCOMPARE(selection.mode, UiMode::Admin);
    QVERIFY(selection.sequencePath.isEmpty());
    QVERIFY(selection.stationPath.isEmpty());
    QCOMPARE(selection.projectRootPath,
             QFileInfo(directory.filePath(QStringLiteral("projects")))
                 .absoluteFilePath());
    QVERIFY(selection.scanDialogEnabled);
    QCOMPARE(selection.snValidationRules.allowedRegex,
             QStringLiteral("^[A-Z0-9]+$"));
}

void MainWindowLifecycleTests::newProjectTemplateSavesSequenceAndStationTogether()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto projectsRoot = directory.filePath(QStringLiteral("projects"));

    MainWindow window;
    window.initializeNewProjectTemplate(projectsRoot);
    auto* sequence = window.findChild<SequenceDocument*>();
    auto* station = window.findChild<StationDocument*>();
    auto* save = window.findChild<QAction*>(QStringLiteral("saveSequenceAction"));
    auto* newProject = window.findChild<QAction*>(QStringLiteral("newProjectAction"));
    QVERIFY(sequence);
    QVERIFY(station);
    QVERIFY(save);
    QVERIFY(newProject);
    QVERIFY(sequence->filePath().isEmpty());
    QVERIFY(station->filePath().isEmpty());
    QVERIFY(!sequence->isModified());
    QVERIFY(!station->isModified());

    SequenceItemPath mainPath;
    mainPath.groupIndex = 1;
    QVERIFY(sequence->insertStep(mainPath));
    QVERIFY(sequence->isModified());
    QVERIFY(save->isEnabled());

    bool promptHandled = false;
    QTimer::singleShot(0, &window, [&] {
        auto* prompt = qobject_cast<QInputDialog*>(
            QApplication::activeModalWidget());
        if (!prompt) {
            return;
        }
        promptHandled = true;
        prompt->setTextValue(QStringLiteral("TemplateProduct"));
        prompt->accept();
    });
    save->trigger();
    QVERIFY(promptHandled);

    const auto projectPath = QDir(projectsRoot).filePath(
        QStringLiteral("TemplateProduct"));
    const auto sequencePath = QDir(projectPath).filePath(
        QStringLiteral("sequence.json"));
    const auto stationPath = QDir(projectPath).filePath(
        QStringLiteral("StationSystem.json"));
    const auto imagesPath = QDir(projectPath).filePath(
        QStringLiteral("images"));
    const auto registerPath = QDir(projectPath).filePath(
        QStringLiteral("register"));
    QVERIFY(QFileInfo(sequencePath).isFile());
    QVERIFY(QFileInfo(stationPath).isFile());
    QVERIFY(QFileInfo(imagesPath).isDir());
    QVERIFY(QFileInfo(registerPath).isDir());
    QCOMPARE(sequence->filePath(), QFileInfo(sequencePath).absoluteFilePath());
    QCOMPARE(station->filePath(), QFileInfo(stationPath).absoluteFilePath());
    QVERIFY(!sequence->isModified());
    QVERIFY(!station->isModified());
    QCOMPARE(sequence->rootObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("templateproduct-sequence"));
    QCOMPARE(station->rootObject().value(QStringLiteral("stationId")).toString(),
             QStringLiteral("templateproduct-station"));

    const auto projects = PicoATE::Core::discoverProductProjects(projectsRoot);
    QCOMPARE(projects.size(), 1);
    QVERIFY(projects.first().ok());
    QCOMPARE(projects.first().name, QStringLiteral("TemplateProduct"));
}

void MainWindowLifecycleTests::loginDialogAppliesRoutingPolicyAndRemembersLoadMode()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(
        QStringLiteral("routed_product_sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));

    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({"stationId":"routing-station","devices":[]})");
    station.close();

    const auto routingPath = directory.filePath(QStringLiteral("ProductRouting.json"));
    const auto writeRouting = [&](bool allowManual) {
        QFile routing(routingPath);
        if (!routing.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        const QJsonObject root{
            {QStringLiteral("allowManualInTest"), allowManual},
            {QStringLiteral("routes"), QJsonArray{
                QJsonObject{{QStringLiteral("name"), QStringLiteral("Routed product")},
                            {QStringLiteral("pattern"), QStringLiteral("BTSN-*")},
                            {QStringLiteral("sequence"),
                             QFileInfo(sequencePath).fileName()}}
            }}
        };
        routing.write(QJsonDocument(root).toJson());
        return true;
    };
    QVERIFY(writeRouting(true));

    {
        LoginDialog dialog(directory.path());
        auto* autoBySn = dialog.findChild<QToolButton*>(
            QStringLiteral("loginAutoBySnButton"));
        auto* sequenceStack = dialog.findChild<QStackedWidget*>(
            QStringLiteral("loginSequenceStack"));
        auto* modeSelector = dialog.findChild<QWidget*>(
            QStringLiteral("loginModeSelector"));
        QVERIFY(autoBySn);
        QVERIFY(sequenceStack);
        QVERIFY(modeSelector);
        QCOMPARE(autoBySn->text(), QStringLiteral("AUTO BY SN"));
        QCOMPARE(autoBySn->size(), QSize(136, 34));
        QCOMPARE(autoBySn->iconSize(), QSize(23, 15));
        QCOMPARE(autoBySn->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
        QVERIFY(!autoBySn->icon().isNull());
        const auto manualIcon = autoBySn->icon().pixmap(
            autoBySn->iconSize(), QIcon::Normal, QIcon::Off).toImage();
        const auto automaticIcon = autoBySn->icon().pixmap(
            autoBySn->iconSize(), QIcon::Normal, QIcon::On).toImage();
        QVERIFY(manualIcon != automaticIcon);
        QVERIFY(autoBySn->width() < modeSelector->width());
        QVERIFY(autoBySn->height() < modeSelector->height());
        QVERIFY(!dialog.findChild<QToolButton*>(
            QStringLiteral("loginManualSequenceButton")));
        QVERIFY(autoBySn->isChecked());
        QVERIFY(autoBySn->isEnabled());
        QCOMPARE(sequenceStack->currentIndex(), 0);
        autoBySn->click();
        QVERIFY(!autoBySn->isChecked());
        QCOMPARE(sequenceStack->currentIndex(), 1);
    }

    {
        LoginDialog dialog(directory.path());
        auto* autoBySn = dialog.findChild<QToolButton*>(
            QStringLiteral("loginAutoBySnButton"));
        QVERIFY(autoBySn);
        QVERIFY(!autoBySn->isChecked());
    }

    QVERIFY(writeRouting(false));
    LoginDialog dialog(directory.path());
    auto* autoBySn = dialog.findChild<QToolButton*>(
        QStringLiteral("loginAutoBySnButton"));
    auto* admin = dialog.findChild<QToolButton*>(
        QStringLiteral("loginAdminModeButton"));
    auto* password = dialog.findChild<QLineEdit*>(
        QStringLiteral("loginAdminPassword"));
    auto* login = dialog.findChild<QPushButton*>(QStringLiteral("loginButton"));
    QVERIFY(autoBySn);
    QVERIFY(admin);
    QVERIFY(password);
    QVERIFY(login);
    QVERIFY(autoBySn->isChecked());
    QVERIFY(!autoBySn->isEnabled());

    admin->click();
    QVERIFY(!autoBySn->isChecked());
    QVERIFY(autoBySn->isEnabled());
    autoBySn->click();
    QVERIFY(autoBySn->isChecked());
    password->setText(QString::number(StartupSupport::dailyAdminPassword()));
    login->click();
    QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.selection().mode, UiMode::Admin);
    QCOMPARE(dialog.selection().sequenceLoadMode, SequenceLoadMode::AutoBySn);
    QVERIFY(dialog.selection().sequencePath.isEmpty());
    QCOMPARE(dialog.selection().productRoutingPath,
             QFileInfo(routingPath).absoluteFilePath());
    QVERIFY(dialog.selection().scanDialogEnabled);

    LoginDialog restored(directory.path());
    auto* restoredAdmin = restored.findChild<QToolButton*>(
        QStringLiteral("loginAdminModeButton"));
    auto* restoredAuto = restored.findChild<QToolButton*>(
        QStringLiteral("loginAutoBySnButton"));
    restoredAdmin->click();
    QVERIFY(restoredAuto->isChecked());
}

void MainWindowLifecycleTests::productRoutingDialogEditsAndAtomicallySavesRoutes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto projectsRoot = directory.filePath(QStringLiteral("projects"));
    const auto firstProject = QDir(projectsRoot).filePath(QStringLiteral("ProductA"));
    const auto secondProject = QDir(projectsRoot).filePath(QStringLiteral("ProductB"));
    QVERIFY(QDir().mkpath(firstProject));
    QVERIFY(QDir().mkpath(secondProject));
    const auto firstSequence = QDir(firstProject).filePath(
        QStringLiteral("product_a_sequence.json"));
    const auto secondSequence = QDir(secondProject).filePath(
        QStringLiteral("product_b_sequence.json"));
    const auto example = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/simple_sequence.json");
    QVERIFY(QFile::copy(example, firstSequence));
    QVERIFY(QFile::copy(example, secondSequence));
    for (const auto& project : {firstProject, secondProject}) {
        QFile station(QDir(project).filePath(QStringLiteral("StationSystem.json")));
        QVERIFY(station.open(QIODevice::WriteOnly));
        station.write(R"({"stationId":"routing-test","devices":[]})");
    }

    const auto routingPath = directory.filePath(
        QStringLiteral("ProductRouting.json"));
    QFile routing(routingPath);
    QVERIFY(routing.open(QIODevice::WriteOnly));
    routing.write(R"json({
      "allowManualInTest": false,
      "projectRoot": "projects",
      "routes": [{
        "name": "Product A",
        "pattern": "A-*",
        "project": "ProductA",
        "enabled": true
      }]
    })json");
    routing.close();

    ProductRoutingDialog dialog(routingPath);
    auto* allowManual = dialog.findChild<QAbstractButton*>(
        QStringLiteral("productRoutingAllowManualSwitch"));
    auto* table = dialog.findChild<QTableWidget*>(
        QStringLiteral("productRoutingTable"));
    auto* add = dialog.findChild<QToolButton*>(
        QStringLiteral("productRoutingAddButton"));
    auto* save = dialog.findChild<QPushButton*>(
        QStringLiteral("productRoutingSaveButton"));
    QVERIFY(allowManual);
    QVERIFY(table);
    QVERIFY(add);
    QVERIFY(save);
    QVERIFY(!allowManual->isChecked());
    QCOMPARE(table->rowCount(), 1);

    const auto screenshotPath = qEnvironmentVariable(
        "PICOATE_ROUTING_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        dialog.show();
        QTest::qWait(40);
        QVERIFY2(dialog.grab().save(screenshotPath), qPrintable(screenshotPath));
    }

    allowManual->click();
    add->click();
    QCOMPARE(table->rowCount(), 2);
    table->item(1, 1)->setText(QStringLiteral("Product B"));
    table->item(1, 2)->setText(QStringLiteral("B-*"));
    auto* length = qobject_cast<QLineEdit*>(table->cellWidget(1, 3));
    auto* project = qobject_cast<QComboBox*>(table->cellWidget(1, 4));
    QVERIFY(length);
    QVERIFY(project);
    QCOMPARE(length->placeholderText(), QStringLiteral("Any"));
    length->setText(QStringLiteral("10"));
    const int projectIndex = project->findData(
        QFileInfo(secondProject).absoluteFilePath());
    QVERIFY(projectIndex >= 0);
    project->setCurrentIndex(projectIndex);
    auto* devices = qobject_cast<QPushButton*>(table->cellWidget(1, 6));
    QVERIFY(devices);
    QVERIFY(devices->isEnabled());

    QSignalSpy savedSpy(&dialog, &ProductRoutingDialog::routingSaved);
    save->click();
    QCOMPARE(savedSpy.count(), 1);
    QCOMPARE(dialog.result(), int(QDialog::Accepted));

    const auto loaded = PicoATE::Core::loadProductRoutingFile(routingPath);
    QVERIFY2(loaded.ok(), loaded.errors.isEmpty()
                              ? "routing load failed"
                              : qPrintable(loaded.errors.first().message));
    QVERIFY(loaded.config.allowManualInTest);
    QCOMPARE(loaded.config.routes.size(), 2);
    QCOMPARE(loaded.config.routes.at(1).name, QStringLiteral("Product B"));
    QCOMPARE(loaded.config.routes.at(1).pattern, QStringLiteral("B-*"));
    QCOMPARE(loaded.config.routes.at(1).snLength, 10);
    QCOMPARE(loaded.config.routes.at(1).projectPath,
             QFileInfo(secondProject).absoluteFilePath());

    QFile savedFile(routingPath);
    QVERIFY(savedFile.open(QIODevice::ReadOnly));
    const auto savedObject = QJsonDocument::fromJson(savedFile.readAll()).object();
    QCOMPARE(savedObject.value(QStringLiteral("routes")).toArray().at(1)
                 .toObject().value(QStringLiteral("project")).toString(),
             QStringLiteral("ProductB"));
    QCOMPARE(savedObject.value(QStringLiteral("routes")).toArray().at(1)
                 .toObject().value(QStringLiteral("snLength")).toInt(),
             10);
}

void MainWindowLifecycleTests::productRoutingDialogDeletesSelectedRouteInsteadOfCurrentRoute()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto routingPath = directory.filePath(
        QStringLiteral("ProductRouting.json"));
    QFile routing(routingPath);
    QVERIFY(routing.open(QIODevice::WriteOnly));
    routing.write(R"json({
      "allowManualInTest": true,
      "routes": [
        {"name":"BTSN Product","pattern":"BTSN*","enabled":true},
        {"name":"SH Product","pattern":"SH*","enabled":true}
      ]
    })json");
    routing.close();

    ProductRoutingDialog dialog(routingPath);
    auto* table = dialog.findChild<QTableWidget*>(
        QStringLiteral("productRoutingTable"));
    auto* remove = dialog.findChild<QToolButton*>(
        QStringLiteral("productRoutingRemoveButton"));
    QVERIFY(table);
    QVERIFY(remove);
    QCOMPARE(table->rowCount(), 2);

    dialog.show();
    QTest::qWait(20);
    QTest::mouseClick(
        table->viewport(), Qt::LeftButton, Qt::NoModifier,
        table->visualItemRect(table->item(1, 1)).center());
    QCOMPARE(table->selectionModel()->selectedRows().constFirst().row(), 1);

    auto* firstLength = qobject_cast<QLineEdit*>(table->cellWidget(0, 3));
    auto* firstProject = qobject_cast<QComboBox*>(table->cellWidget(0, 4));
    auto* firstDevices = qobject_cast<QPushButton*>(table->cellWidget(0, 6));
    QVERIFY(firstLength);
    QVERIFY(firstProject);
    QVERIFY(firstDevices);
    QTest::mouseMove(firstLength, firstLength->rect().center());
    QCoreApplication::processEvents();
    QCOMPARE(table->selectionModel()->selectedRows().constFirst().row(), 1);
    QTest::mouseMove(firstProject, firstProject->rect().center());
    QTest::mouseMove(firstDevices, firstDevices->rect().center());
    QTest::mouseClick(firstLength, Qt::LeftButton);
    firstLength->setText(QStringLiteral("12"));
    QCoreApplication::processEvents();
    QCOMPARE(table->selectionModel()->selectedRows().constFirst().row(), 1);

    bool confirmationHandled = false;
    QTimer::singleShot(0, &dialog, [&] {
        auto* confirmation = qobject_cast<QMessageBox*>(
            QApplication::activeModalWidget());
        if (!confirmation) {
            return;
        }
        auto* confirmDelete = confirmation->button(QMessageBox::Yes);
        if (!confirmDelete) {
            return;
        }
        confirmationHandled = true;
        confirmDelete->click();
    });
    remove->click();
    QVERIFY(confirmationHandled);
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("BTSN Product"));
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("BTSN*"));
}

void MainWindowLifecycleTests::productRoutingDialogAllowsPotentialOverlapAndRejectsBrokenSequence()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto projectsRoot = directory.filePath(QStringLiteral("projects"));
    const auto validProject = QDir(projectsRoot).filePath(QStringLiteral("Valid"));
    const auto brokenProject = QDir(projectsRoot).filePath(QStringLiteral("Broken"));
    QVERIFY(QDir().mkpath(validProject));
    QVERIFY(QDir().mkpath(brokenProject));
    const auto validSequence = QDir(validProject).filePath(
        QStringLiteral("valid_sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       validSequence));
    const auto brokenSequence = QDir(brokenProject).filePath(
        QStringLiteral("broken_sequence.json"));
    QFile broken(brokenSequence);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("{broken");
    broken.close();
    for (const auto& project : {validProject, brokenProject}) {
        QFile station(QDir(project).filePath(QStringLiteral("StationSystem.json")));
        QVERIFY(station.open(QIODevice::WriteOnly));
        station.write(R"({"stationId":"routing-test","devices":[]})");
    }

    const auto routingPath = directory.filePath(
        QStringLiteral("ProductRouting.json"));
    ProductRoutingDialog dialog(routingPath);
    auto* table = dialog.findChild<QTableWidget*>(
        QStringLiteral("productRoutingTable"));
    auto* add = dialog.findChild<QToolButton*>(
        QStringLiteral("productRoutingAddButton"));
    auto* save = dialog.findChild<QPushButton*>(
        QStringLiteral("productRoutingSaveButton"));
    auto* status = dialog.findChild<QLabel*>(
        QStringLiteral("productRoutingStatus"));
    QVERIFY(table);
    QVERIFY(add);
    QVERIFY(save);
    QVERIFY(status);

    add->click();
    add->click();
    QCOMPARE(table->rowCount(), 2);
    table->item(0, 1)->setText(QStringLiteral("Contains C123"));
    table->item(0, 2)->setText(QStringLiteral("*C1234567890*"));
    table->item(1, 1)->setText(QStringLiteral("Contains BTSN"));
    table->item(1, 2)->setText(QStringLiteral("*BTSN*"));
    for (int row = 0; row < 2; ++row) {
        auto* project = qobject_cast<QComboBox*>(table->cellWidget(row, 4));
        QVERIFY(project);
        const int index = project->findData(
            QFileInfo(validProject).absoluteFilePath());
        QVERIFY(index >= 0);
        project->setCurrentIndex(index);
    }

    QSignalSpy savedSpy(&dialog, &ProductRoutingDialog::routingSaved);
    save->click();
    QCOMPARE(savedSpy.count(), 1);
    QVERIFY(QFileInfo::exists(routingPath));

    QFile savedRouting(routingPath);
    QVERIFY(savedRouting.open(QIODevice::ReadOnly));
    const auto savedContents = savedRouting.readAll();
    savedRouting.close();

    ProductRoutingDialog brokenDialog(routingPath);
    auto* brokenTable = brokenDialog.findChild<QTableWidget*>(
        QStringLiteral("productRoutingTable"));
    auto* brokenSave = brokenDialog.findChild<QPushButton*>(
        QStringLiteral("productRoutingSaveButton"));
    auto* brokenStatus = brokenDialog.findChild<QLabel*>(
        QStringLiteral("productRoutingStatus"));
    QVERIFY(brokenTable);
    QVERIFY(brokenSave);
    QVERIFY(brokenStatus);
    auto* secondProjectCombo = qobject_cast<QComboBox*>(
        brokenTable->cellWidget(1, 4));
    QVERIFY(secondProjectCombo);
    const int brokenIndex = secondProjectCombo->findData(
        QFileInfo(brokenProject).absoluteFilePath());
    QVERIFY(brokenIndex >= 0);
    secondProjectCombo->setCurrentIndex(brokenIndex);
    QSignalSpy brokenSavedSpy(&brokenDialog,
                              &ProductRoutingDialog::routingSaved);
    brokenSave->click();
    QCOMPARE(brokenSavedSpy.count(), 0);
    QVERIFY(brokenStatus->text().contains(QStringLiteral("valid JSON"),
                                          Qt::CaseInsensitive));
    QVERIFY(savedRouting.open(QIODevice::ReadOnly));
    QCOMPARE(savedRouting.readAll(), savedContents);
}

void MainWindowLifecycleTests::adminStartupSplashCentersLogoAndRunsSpinner()
{
    AdminStartupSplash splash;
    splash.show();
    QTest::qWait(20);

    auto* content = splash.findChild<QWidget*>(
        QStringLiteral("adminStartupContent"));
    auto* logo = splash.findChild<QLabel*>(
        QStringLiteral("adminStartupLogo"));
    auto* spinner = dynamic_cast<LoadingSpinner*>(
        splash.findChild<QWidget*>(QStringLiteral("adminStartupSplashSpinner")));
    QVERIFY(content);
    QVERIFY(logo);
    QVERIFY(spinner);
    QVERIFY(splash.windowFlags().testFlag(Qt::FramelessWindowHint));
    QCOMPARE(splash.size(), QSize(420, 230));
    QCOMPARE(logo->accessibleName(), QStringLiteral("SINEXCEL"));
    QCOMPARE(logo->size(), QSize(235, 44));
    QVERIFY(!logo->pixmap().isNull());
    QCOMPARE(logo->alignment(), Qt::AlignCenter);
    QVERIFY(spinner->isRunning());
    QVERIFY(logo->geometry().bottom() < spinner->geometry().top());
    const auto contentCenter = content->mapTo(
        &splash, content->rect().center());
    QVERIFY(qAbs(contentCenter.x() - splash.rect().center().x()) <= 2);
    QVERIFY(qAbs(contentCenter.y() - splash.rect().center().y()) <= 2);

    auto* spinnerTimer = spinner->findChild<QTimer*>();
    QVERIFY(spinnerTimer);
    QSignalSpy animationFrames(spinnerTimer, &QTimer::timeout);
    MainWindow startupWindow;
    QVERIFY2(animationFrames.count() >= 2,
             "The Admin startup splash stopped animating while the main window was built");

    const auto screenshotPath = qEnvironmentVariable(
        "PICOATE_ADMIN_STARTUP_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        QVERIFY2(splash.grab().save(screenshotPath), qPrintable(screenshotPath));
    }

    splash.hide();
    QCoreApplication::processEvents();
    QVERIFY(!spinner->isRunning());
}

void MainWindowLifecycleTests::adminStartupInitializationShowsBusyOverlay()
{
    MainWindow window;
    auto* overlay = window.findChild<QWidget*>(
        QStringLiteral("adminStartupOverlay"));
    auto* spinner = window.findChild<QWidget*>(
        QStringLiteral("adminStartupSpinner"));
    auto* status = window.findChild<QLabel*>(
        QStringLiteral("adminStartupStatus"));
    QVERIFY(overlay);
    QVERIFY(spinner);
    QVERIFY(status);
    QVERIFY(overlay->isHidden());

    QSignalSpy readySpy(&window, &MainWindow::adminWorkspaceReady);
    window.initializeAdminWorkspace();
    QVERIFY(!overlay->isHidden());
    QVERIFY(!spinner->isHidden());
    QVERIFY(status->text().contains(QStringLiteral("Admin")));
    QTRY_VERIFY_WITH_TIMEOUT(overlay->isHidden(), 5000);
    QCOMPARE(readySpy.count(), 1);
}

void MainWindowLifecycleTests::stationScanDialogTogglePersists()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({
        "stationId":"line-1",
        "name":"Legacy Model",
        "customerId":"OLD-CUSTOMER",
        "metadata":{
            "jigNo":"JIG-01",
            "order":"ORDER-01",
            "tester":"Tester A",
            "customField":"preserved"
        },
        "devices":[{
            "deviceId":"DMM1",
            "deviceType":"DMM",
            "enabled":false
        }]
    })");
    station.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    QVERIFY(window.openStationFile(stationPath));
    window.show();
    QTest::qWait(20);
    auto* stopOnFailure = window.findChild<QAbstractButton*>(
        QStringLiteral("stationStopOnFailureSwitch"));
    auto* scanEnabled = window.findChild<QAbstractButton*>(
        QStringLiteral("stationScanDialogSwitch"));
    auto* loopEnabled = window.findChild<QAbstractButton*>(
        QStringLiteral("stationLoopTestSwitch"));
    auto* pdfReport = window.findChild<QAbstractButton*>(
        QStringLiteral("stationPdfReportSwitch"));
    auto* loopCount = window.findChild<QLineEdit*>(
        QStringLiteral("stationLoopTestCountEdit"));
    auto* uutCount = window.findChild<QLineEdit*>(
        QStringLiteral("stationUutCountEdit"));
    auto* snLength = window.findChild<QLineEdit*>(
        QStringLiteral("stationSnLengthEdit"));
    auto* snPattern = window.findChild<QLineEdit*>(
        QStringLiteral("stationSnPatternEdit"));
    auto* snAllowedRegex = window.findChild<QLineEdit*>(
        QStringLiteral("stationSnAllowedRegexEdit"));
    auto* model = window.findChild<QLineEdit*>(
        QStringLiteral("stationModelEdit"));
    auto* customerId = window.findChild<QLineEdit*>(
        QStringLiteral("stationCustomerIdEdit"));
    auto* jigNo = window.findChild<QLineEdit*>(
        QStringLiteral("stationJigNoEdit"));
    auto* order = window.findChild<QLineEdit*>(
        QStringLiteral("stationOrderEdit"));
    auto* tester = window.findChild<QLineEdit*>(
        QStringLiteral("stationTesterEdit"));
    auto* settingsEditor = window.findChild<StationSettingsEditor*>();
    auto* workArea = window.findChild<QSplitter*>(
        QStringLiteral("stationWorkSplitter"));
    auto* deviceView = window.findChild<QTreeView*>(
        QStringLiteral("stationDeviceView"));
    auto* document = window.findChild<StationDocument*>();
    QVERIFY(stopOnFailure);
    QVERIFY(scanEnabled);
    QVERIFY(loopEnabled);
    QVERIFY(pdfReport);
    QVERIFY(loopCount);
    QVERIFY(uutCount);
    QVERIFY(snLength);
    QVERIFY(snPattern);
    QVERIFY(snAllowedRegex);
    QVERIFY(model);
    QVERIFY(customerId);
    QVERIFY(jigNo);
    QVERIFY(order);
    QVERIFY(tester);
    QVERIFY(settingsEditor);
    QVERIFY(!window.findChild<QPushButton*>(
        QStringLiteral("applyStationSettingsButton")));
    QVERIFY(workArea);
    QVERIFY(deviceView);
    QVERIFY(document);
    QCOMPARE(workArea->count(), 3);

    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    QVERIFY(tabs);
    for (int tab = 0; tab < tabs->count(); ++tab) {
        if (tabs->tabText(tab) == QStringLiteral("Station Config")) {
            tabs->setCurrentIndex(tab);
            break;
        }
    }
    window.resize(900, 600);
    QCoreApplication::processEvents();
    const auto stationScreenshot = qEnvironmentVariable(
        "PICOATE_STATION_SCREENSHOT");
    if (!stationScreenshot.isEmpty()) {
        QVERIFY2(window.grab().save(stationScreenshot),
                 qPrintable(stationScreenshot));
    }
    const auto paneSizes = workArea->sizes();
    QCOMPARE(paneSizes.size(), 3);
    QVERIFY(paneSizes[0] >= 180);
    QVERIFY(paneSizes[1] >= 300);
    QVERIFY(paneSizes[2] >= 230);
    const int occupiedWidth = paneSizes[0] + paneSizes[1] + paneSizes[2]
        + workArea->handleWidth() * 2;
    QVERIFY(qAbs(occupiedWidth - workArea->width()) <= 2);

    const auto enabledIndex = deviceView->model()->index(
        0, StationDeviceModel::EnabledColumn);
    QVERIFY(enabledIndex.isValid());
    QCOMPARE(enabledIndex.data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
    QVERIFY(deviceView->model()->setData(
        enabledIndex, Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(document->deviceAt(0).value(QStringLiteral("enabled")).toBool(),
             true);
    QVERIFY(stopOnFailure->isChecked());
    QVERIFY(scanEnabled->isChecked());
    QCOMPARE(snLength->text(), QString{});
    QVERIFY(snLength->validator());
    snLength->setText(QStringLiteral("257"));
    QVERIFY(!snLength->hasAcceptableInput());
    QCOMPARE(loopCount->text(), QStringLiteral("1"));
    QVERIFY(loopCount->validator());
    loopCount->setText(QStringLiteral("100001"));
    QVERIFY(!loopCount->hasAcceptableInput());
    QCOMPARE(uutCount->text(), QStringLiteral("1"));
    QVERIFY(uutCount->validator());
    uutCount->setText(QStringLiteral("65"));
    QVERIFY(!uutCount->hasAcceptableInput());
    QCOMPARE(model->text(), QStringLiteral("Legacy Model"));
    QCOMPARE(customerId->text(), QStringLiteral("OLD-CUSTOMER"));
    QCOMPARE(jigNo->text(), QStringLiteral("JIG-01"));
    QCOMPARE(order->text(), QStringLiteral("ORDER-01"));
    QCOMPARE(tester->text(), QStringLiteral("Tester A"));
    stopOnFailure->setChecked(false);
    scanEnabled->setChecked(false);
    loopEnabled->setChecked(true);
    pdfReport->setChecked(true);
    snLength->setText(QStringLiteral("10"));
    loopCount->setText(QStringLiteral("12"));
    uutCount->setText(QStringLiteral("4"));
    snPattern->setText(QStringLiteral("BTSN*"));
    snAllowedRegex->setText(QStringLiteral("^[A-Z0-9]+$"));
    model->setText(QStringLiteral("PICO-M3"));
    customerId->setText(QStringLiteral("CUSTOMER-03"));
    jigNo->setText(QStringLiteral("JIG-02"));
    order->setText(QStringLiteral("ORDER-02"));
    tester->setText(QStringLiteral("Tester B"));
    QVERIFY(settingsEditor->hasPendingChanges());
    QVERIFY(settingsEditor->commitPendingChanges());
    QCOMPARE(document->rootObject().value(QStringLiteral("stopOnFailure")).toBool(),
             false);
    QCOMPARE(document->rootObject().value(QStringLiteral("scanDialogEnabled")).toBool(),
             false);
    QCOMPARE(document->rootObject().value(QStringLiteral("loopTestEnabled")).toBool(),
             true);
    QCOMPARE(document->rootObject().value(QStringLiteral("loopTestCount")).toInt(),
             12);
    QCOMPARE(document->rootObject().value(QStringLiteral("uutCount")).toInt(), 4);
    QCOMPARE(document->rootObject().value(QStringLiteral("pdfReportEnabled")).toBool(),
             true);
    QCOMPARE(document->rootObject().value(QStringLiteral("snLength")).toInt(), 10);
    QCOMPARE(document->rootObject().value(QStringLiteral("snPattern")).toString(),
             QStringLiteral("BTSN*"));
    QCOMPARE(document->rootObject().value(
                 QStringLiteral("snAllowedRegex")).toString(),
             QStringLiteral("^[A-Z0-9]+$"));
    QCOMPARE(document->rootObject().value(QStringLiteral("model")).toString(),
             QStringLiteral("PICO-M3"));
    QCOMPARE(document->rootObject().value(
                 QStringLiteral("customerId")).toString(),
             QStringLiteral("CUSTOMER-03"));
    QVERIFY(!document->rootObject().contains(QStringLiteral("name")));
    const auto metadata = document->rootObject()
                              .value(QStringLiteral("metadata")).toObject();
    QCOMPARE(metadata.value(QStringLiteral("jigNo")).toString(),
             QStringLiteral("JIG-02"));
    QCOMPARE(metadata.value(QStringLiteral("order")).toString(),
             QStringLiteral("ORDER-02"));
    QCOMPARE(metadata.value(QStringLiteral("tester")).toString(),
             QStringLiteral("Tester B"));
    QCOMPARE(metadata.value(QStringLiteral("customField")).toString(),
             QStringLiteral("preserved"));
    QString errorMessage;
    QVERIFY(document->save(&errorMessage));
    QVERIFY2(!StartupSupport::stationScanDialogEnabled(stationPath),
             qPrintable(errorMessage));
    QCOMPARE(StartupSupport::stationSnLength(stationPath), 10);
    QCOMPARE(StartupSupport::stationUutCount(stationPath), 4);
    const auto rules = StartupSupport::stationSnValidationRules(stationPath);
    QCOMPARE(rules.wildcardPattern, QStringLiteral("BTSN*"));
    QCOMPARE(rules.allowedRegex, QStringLiteral("^[A-Z0-9]+$"));
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::pdfReportExportsAndArchivesWithStationPolicy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto requestedPreview = qEnvironmentVariable(
        "PICOATE_REPORT_PREVIEW_PATH");
    const auto pdfPath = requestedPreview.isEmpty()
        ? directory.filePath(QStringLiteral("report.pdf"))
        : requestedPreview;
    QVERIFY(QDir().mkpath(QFileInfo(pdfPath).absolutePath()));

    const auto report = pdfReportFixture();
    const auto exported = ReportExporter::savePdf(pdfPath, report);
    QVERIFY2(exported.success, qPrintable(exported.errorMessage));
    QFile pdf(pdfPath);
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    const auto bytes = pdf.readAll();
    QVERIFY(bytes.startsWith("%PDF-"));
    QVERIFY(bytes.size() > 10000);

    const auto requestedLongPreview = qEnvironmentVariable(
        "PICOATE_LONG_REPORT_PREVIEW_PATH");
    const auto longPdfPath = requestedLongPreview.isEmpty()
        ? directory.filePath(QStringLiteral("long-report.pdf"))
        : requestedLongPreview;
    QVERIFY(QDir().mkpath(QFileInfo(longPdfPath).absolutePath()));
    const auto longExported = ReportExporter::savePdf(
        longPdfPath, pdfReportFixture(60));
    QVERIFY2(longExported.success, qPrintable(longExported.errorMessage));
    QFile longPdf(longPdfPath);
    QVERIFY(longPdf.open(QIODevice::ReadOnly));
    const auto longBytes = longPdf.readAll();
    QVERIFY(longBytes.startsWith("%PDF-"));
    QVERIFY(longBytes.contains("/Count 3") || longBytes.contains("/Count 4"));

    const QJsonObject station{
        {QStringLiteral("stationId"), QStringLiteral("STATION-01")},
        {QStringLiteral("pdfReportEnabled"), true},
        {QStringLiteral("devices"), QJsonArray{}},
    };
    const auto settings = runArtifactSettingsFromStation(station);
    QVERIFY(settings.pdfReportEnabled);
    QVERIFY(!settings.txtLogEnabled);
    QVERIFY(!settings.csvReportEnabled);
    QVERIFY(!settings.xlsxReportEnabled);

    auto archiveSettings = settings;
    archiveSettings.outputDirectory = directory.path();
    const auto startedAt = QDateTime(QDate(2026, 8, 13), QTime(19, 53, 10, 123));
    RunArtifactWriter writer;
    const auto begun = writer.begin(
        archiveSettings, QStringLiteral("SN-PDF"), startedAt);
    QVERIFY2(begun.success, qPrintable(begun.errorMessage));
    const auto archived = writer.finalize(report);
    QVERIFY2(archived.success, qPrintable(archived.errorMessage));
    const auto archivedPdf = QDir(directory.path()).filePath(
        QStringLiteral("20260813/PASS/SN-PDF_195310123.pdf"));
    QVERIFY(QFileInfo::exists(archivedPdf));
    QVERIFY(archived.filePaths.contains(archivedPdf));
}

void MainWindowLifecycleTests::fieldDeviceDialogAppliesCurrentDeviceAndSavesAll()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({
        "stationId":"field-devices",
        "devices":[
            {"deviceId":"DMM1","deviceType":"DMM","driverId":"",
             "connectionKind":"manual","resource":"OLD-DMM","enabled":true},
            {"deviceId":"PSU1","deviceType":"PSU","driverId":"",
             "connectionKind":"manual","resource":"OLD-PSU","enabled":true}
        ]
    })");
    station.close();

    FieldDeviceDialog dialog(stationPath);
    auto* devices = dialog.findChild<QListWidget*>(QStringLiteral("fieldDeviceList"));
    auto* resource = dialog.findChild<QComboBox*>(QStringLiteral("fieldResourceCombo"));
    auto* apply = dialog.findChild<QPushButton*>(QStringLiteral("fieldApplyButton"));
    auto* saveAll = dialog.findChild<QPushButton*>(QStringLiteral("fieldSaveAllButton"));
    QVERIFY(devices);
    QVERIFY(resource);
    QVERIFY(apply);
    QVERIFY(saveAll);
    QCOMPARE(devices->count(), 2);

    QSignalSpy savedSpy(&dialog, &FieldDeviceDialog::stationSaved);
    resource->setEditText(QStringLiteral("DMM-NEW"));
    QVERIFY(apply->isEnabled());
    apply->click();
    QCOMPARE(savedSpy.count(), 1);
    QVERIFY(!saveAll->isEnabled());

    QFile currentSaved(stationPath);
    QVERIFY(currentSaved.open(QIODevice::ReadOnly));
    auto root = QJsonDocument::fromJson(currentSaved.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("devices")).toArray().at(0).toObject()
                 .value(QStringLiteral("resource")).toString(),
             QStringLiteral("DMM-NEW"));
    QCOMPARE(root.value(QStringLiteral("devices")).toArray().at(1).toObject()
                 .value(QStringLiteral("resource")).toString(),
             QStringLiteral("OLD-PSU"));
    currentSaved.close();

    devices->setCurrentRow(1);
    QCOMPARE(resource->currentText(), QStringLiteral("OLD-PSU"));
    resource->setEditText(QStringLiteral("PSU-NEW"));
    saveAll->click();
    QCOMPARE(savedSpy.count(), 2);
    QVERIFY(!saveAll->isEnabled());

    QFile saved(stationPath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    root = QJsonDocument::fromJson(saved.readAll()).object();
    const auto savedDevices = root.value(QStringLiteral("devices")).toArray();
    QCOMPARE(savedDevices.at(0).toObject().value(QStringLiteral("resource")).toString(),
             QStringLiteral("DMM-NEW"));
    QCOMPARE(savedDevices.at(1).toObject().value(QStringLiteral("resource")).toString(),
             QStringLiteral("PSU-NEW"));
}

void MainWindowLifecycleTests::productionFieldDeviceDialogDefersScannerUntilClosed()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("product_sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    QJsonObject stationRoot{
        {QStringLiteral("stationId"), QStringLiteral("field-device-production")},
        {QStringLiteral("scanDialogEnabled"), true},
        {QStringLiteral("pluginRegistry"),
         QFileInfo(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                   + QStringLiteral("/out/build/vs2022-qt6-all/ui/src/Debug/plugins/PluginRegistry.json"))
             .absoluteFilePath()},
        {QStringLiteral("devices"), QJsonArray{
            QJsonObject{
                {QStringLiteral("deviceId"), QStringLiteral("MODBUS1")},
                {QStringLiteral("deviceType"), QStringLiteral("MODBUS")},
                {QStringLiteral("driverId"), QStringLiteral("plugin.modbus.tcp")},
                {QStringLiteral("connectionKind"), QStringLiteral("tcpIp")},
                {QStringLiteral("resource"), QStringLiteral("127.0.0.1:502")},
                {QStringLiteral("enabled"), true}}}}};
    station.write(QJsonDocument(stationRoot).toJson(QJsonDocument::Indented));
    station.close();

    StartupSelection selection;
    selection.mode = UiMode::Test;
    selection.sequencePath = sequencePath;
    selection.stationPath = stationPath;
    selection.scanDialogEnabled = true;
    ProductionWindow window(selection);
    window.show();

    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* scan = window.findChild<ScanDialog*>();
    auto* deviceAction = window.findChild<QAction*>(
        QStringLiteral("productionFieldDeviceAction"));
    QVERIFY(viewModel);
    QVERIFY(scan);
    QVERIFY(deviceAction);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(scan->isVisible(), 1000);
    QVERIFY(deviceAction->isEnabled());

    bool foundDialog = false;
    bool compileCompletedWhileOpen = false;
    bool scannerStayedHidden = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<FieldDeviceDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        foundDialog = true;
        auto* resource = dialog->findChild<QComboBox*>(
            QStringLiteral("fieldResourceCombo"));
        auto* apply = dialog->findChild<QPushButton*>(
            QStringLiteral("fieldApplyButton"));
        if (!resource || !apply) {
            dialog->reject();
            return;
        }
        connect(viewModel, &ExecutionViewModel::compileSummaryChanged,
                dialog, [&, dialog] {
                    if (!viewModel->compileSummary().success) {
                        return;
                    }
                    QTimer::singleShot(0, dialog, [&, dialog] {
                        compileCompletedWhileOpen =
                            viewModel->state() == UiRunState::Ready;
                        scannerStayedHidden = scan->isHidden();
                        dialog->reject();
                    });
        });
        resource->setEditText(QStringLiteral("127.0.0.1:1502"));
        apply->click();
        QTimer::singleShot(3000, dialog, [dialog] {
            if (dialog->isVisible()) dialog->reject();
        });
    });
    deviceAction->trigger();

    QVERIFY(foundDialog);
    QVERIFY(compileCompletedWhileOpen);
    QVERIFY(scannerStayedHidden);
    QTRY_VERIFY_WITH_TIMEOUT(scan->isVisible(), 1000);
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::scanDialogAcceptsRepeatedBarcodeAndHasNoWindowButtons()
{
    ScanDialog dialog;
    dialog.setAttribute(Qt::WA_DontShowOnScreen);
    QSignalSpy barcodeSpy(&dialog, &ScanDialog::barcodeAccepted);
    auto* barcode = dialog.findChild<QLineEdit*>(QStringLiteral("barcodeEdit"));
    auto* error = dialog.findChild<QLabel*>(QStringLiteral("scanErrorLabel"));
    QVERIFY(barcode);
    QVERIFY(error);
    QVERIFY(dialog.windowFlags().testFlag(Qt::CustomizeWindowHint));
    QVERIFY(dialog.windowFlags().testFlag(Qt::WindowTitleHint));
    QVERIFY(!dialog.windowFlags().testFlag(Qt::WindowCloseButtonHint));
    QVERIFY(!dialog.windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
    QVERIFY(!dialog.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
    QVERIFY(!dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
    QCOMPARE(dialog.windowModality(), Qt::NonModal);
    QVERIFY(!dialog.isModal());
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("scanAdminUnlockButton")));
    QCOMPARE(barcode->alignment(), Qt::AlignCenter);
    QVERIFY(barcode->font().bold());
    QVERIFY(barcode->font().pointSize() >= 18);
    QVERIFY(barcode->minimumHeight() >= 66);
    QCOMPARE(dialog.focusProxy(), barcode);

    QCloseEvent closeEvent;
    QCoreApplication::sendEvent(&dialog, &closeEvent);
    QVERIFY(!closeEvent.isAccepted());

    QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    QCOMPARE(barcodeSpy.count(), 0);
    QVERIFY(!error->isHidden());
    SnValidationRules rules;
    rules.exactLength = 6;
    rules.wildcardPattern = QStringLiteral("SN-*");
    rules.allowedRegex = QStringLiteral("^[A-Z0-9-]+$");
    dialog.setValidationRules(rules);
    barcode->setText(QStringLiteral("12345"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    QCOMPARE(barcodeSpy.count(), 0);
    QVERIFY(!error->isHidden());
    barcode->setText(QStringLiteral("1234567"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    QCOMPARE(barcodeSpy.count(), 0);
    QVERIFY(!error->isHidden());
    barcode->setText(QStringLiteral("AB-001"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    QCOMPARE(barcodeSpy.count(), 0);
    QVERIFY(!error->isHidden());
    barcode->setText(QStringLiteral("SN-001"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    QCOMPARE(barcodeSpy.count(), 1);
    QCOMPARE(barcodeSpy.first().first().toString(), QStringLiteral("SN-001"));
    QVERIFY(dialog.isHidden());

    dialog.showForNextScan();
    barcode->setText(QStringLiteral("SN-001"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    QCOMPARE(barcodeSpy.count(), 2);
    QCOMPARE(barcodeSpy.last().first().toString(), QStringLiteral("SN-001"));
    QVERIFY(dialog.isHidden());
}

void MainWindowLifecycleTests::scanDialogCollectsCarouselBatchAndSupportsReplacement()
{
    ScanDialog dialog;
    dialog.setAttribute(Qt::WA_DontShowOnScreen);
    dialog.setSlotCount(4);
    QSignalSpy batchSpy(&dialog, &ScanDialog::barcodesAccepted);
    QSignalSpy singleSpy(&dialog, &ScanDialog::barcodeAccepted);

    auto* barcode = dialog.findChild<QLineEdit*>(QStringLiteral("barcodeEdit"));
    auto* titleLabel = dialog.findChild<QLabel*>(QStringLiteral("scanTitleLabel"));
    auto* progress = dialog.findChild<QLabel*>(QStringLiteral("scanProgressLabel"));
    auto* error = dialog.findChild<QLabel*>(QStringLiteral("scanErrorLabel"));
    auto* previous = dialog.findChild<QToolButton*>(
        QStringLiteral("scanPreviousButton"));
    auto* next = dialog.findChild<QToolButton*>(QStringLiteral("scanNextButton"));
    auto* undo = dialog.findChild<QPushButton*>(QStringLiteral("scanUndoButton"));
    auto* clear = dialog.findChild<QPushButton*>(QStringLiteral("scanClearButton"));
    QVERIFY(barcode);
    QVERIFY(titleLabel);
    QVERIFY(progress);
    QVERIFY(error);
    QVERIFY(previous);
    QVERIFY(next);
    QVERIFY(undo);
    QVERIFY(clear);

    const auto submit = [&](const QString& value) {
        barcode->setText(value);
        QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    };

    dialog.showForNextScan();
    QCOMPARE(dialog.size(), QSize(460, 230));
    const auto screenshotPath = qEnvironmentVariable(
        "PICOATE_SCAN_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        QCoreApplication::processEvents();
        QVERIFY2(dialog.grab().save(screenshotPath), qPrintable(screenshotPath));
    }
    QCOMPARE(dialog.slotCount(), 4);
    QCOMPARE(titleLabel->text(), QStringLiteral("Scan UUT 1 SN"));
    QCOMPARE(progress->text(), QStringLiteral("0 / 4 scanned"));
    QVERIFY(!previous->isEnabled());
    QVERIFY(next->isEnabled());

    barcode->setFocus(Qt::OtherFocusReason);
    QTest::keyClicks(barcode, QStringLiteral("SN-001"));
    QTest::keyClick(barcode, Qt::Key_Return);
    QCoreApplication::processEvents();
    QCOMPARE(titleLabel->text(), QStringLiteral("Scan UUT 2 SN"));
    QCOMPARE(progress->text(), QStringLiteral("1 / 4 scanned"));
    QCOMPARE(dialog.barcodes().at(0), QStringLiteral("SN-001"));
    QCOMPARE(batchSpy.count(), 0);

    submit(QStringLiteral("SN-001"));
    QCOMPARE(titleLabel->text(), QStringLiteral("Scan UUT 2 SN"));
    QVERIFY(!error->isHidden());
    QVERIFY(error->text().contains(QStringLiteral("UUT 1")));
    QCOMPARE(dialog.barcodes().at(1), QString{});

    submit(QStringLiteral("SN-002"));
    QCOMPARE(titleLabel->text(), QStringLiteral("Scan UUT 3 SN"));
    previous->click();
    QCOMPARE(titleLabel->text(), QStringLiteral("Scan UUT 2 SN"));
    QCOMPARE(barcode->text(), QStringLiteral("SN-002"));
    QVERIFY(barcode->styleSheet().contains(QStringLiteral("#7b858c")));

    barcode->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QTest::keyClicks(barcode, QStringLiteral("SN-002-NEW"));
    QCOMPARE(barcode->text(), QStringLiteral("SN-002-NEW"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    QCOMPARE(dialog.barcodes().at(1), QStringLiteral("SN-002-NEW"));
    QCOMPARE(titleLabel->text(), QStringLiteral("Scan UUT 3 SN"));

    undo->click();
    QCOMPARE(titleLabel->text(), QStringLiteral("Scan UUT 2 SN"));
    QCOMPARE(dialog.barcodes().at(1), QStringLiteral("SN-002"));
    next->click();
    QCOMPARE(titleLabel->text(), QStringLiteral("Scan UUT 3 SN"));
    submit(QStringLiteral("SN-003"));
    QCOMPARE(titleLabel->text(), QStringLiteral("Scan UUT 4 SN"));
    QCOMPARE(batchSpy.count(), 0);
    submit(QStringLiteral("SN-004"));

    QCOMPARE(batchSpy.count(), 1);
    QCOMPARE(singleSpy.count(), 0);
    QCOMPARE(batchSpy.first().first().toStringList(),
             QStringList({QStringLiteral("SN-001"),
                          QStringLiteral("SN-002"),
                          QStringLiteral("SN-003"),
                          QStringLiteral("SN-004")}));
    QVERIFY(dialog.isHidden());

    dialog.showForNextScan();
    submit(QStringLiteral("ONE"));
    submit(QStringLiteral("TWO"));
    next->click();
    submit(QStringLiteral("THREE"));
    previous->click();
    QCOMPARE(titleLabel->text(), QStringLiteral("Scan UUT 2 SN"));
    clear->click();
    QCOMPARE(dialog.barcodes(), QStringList(4, QString{}));
    QCOMPARE(titleLabel->text(), QStringLiteral("Scan UUT 1 SN"));
    QCOMPARE(progress->text(), QStringLiteral("0 / 4 scanned"));
}

void MainWindowLifecycleTests::scanDialogSkipsDisabledSlotWithoutCollapsingBatchPositions()
{
    ScanDialog dialog;
    dialog.setAttribute(Qt::WA_DontShowOnScreen);
    dialog.setSlotCount(4);
    dialog.setSlotEnabledStates({true, true, false, true});

    QCOMPARE(dialog.slotEnabledStates(), QVector<bool>({true, true, false, true}));
    QSignalSpy batchSpy(&dialog, &ScanDialog::barcodesAccepted);
    auto* barcode = dialog.findChild<QLineEdit*>(QStringLiteral("barcodeEdit"));
    auto* title = dialog.findChild<QLabel*>(QStringLiteral("scanTitleLabel"));
    auto* progress = dialog.findChild<QLabel*>(QStringLiteral("scanProgressLabel"));
    auto* active = dialog.findChild<QPushButton*>(
        QStringLiteral("scanSlotEnabledButton"));
    auto* previous = dialog.findChild<QToolButton*>(
        QStringLiteral("scanPreviousButton"));
    QVERIFY(barcode);
    QVERIFY(title);
    QVERIFY(progress);
    QVERIFY(active);
    QVERIFY(previous);

    const auto submit = [&](const QString& value) {
        barcode->setText(value);
        QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    };

    dialog.showForNextScan();
    QCOMPARE(title->text(), QStringLiteral("Scan UUT 1 SN"));
    QCOMPARE(progress->text(), QStringLiteral("0 / 3 scanned  |  1 disabled"));
    submit(QStringLiteral("SN-001"));
    QCOMPARE(title->text(), QStringLiteral("Scan UUT 2 SN"));
    submit(QStringLiteral("SN-002"));
    QCOMPARE(title->text(), QStringLiteral("Scan UUT 4 SN"));
    submit(QStringLiteral("SN-004"));

    QCOMPARE(batchSpy.count(), 1);
    const auto accepted = batchSpy.first().first().toStringList();
    QCOMPARE(accepted.size(), 4);
    QCOMPARE(accepted[0], QStringLiteral("SN-001"));
    QCOMPARE(accepted[1], QStringLiteral("SN-002"));
    QVERIFY(accepted[2].isEmpty());
    QCOMPARE(accepted[3], QStringLiteral("SN-004"));

    dialog.setSlotEnabledStates({true, true, true, true});
    dialog.showForNextScan();
    submit(QStringLiteral("SN-A"));
    submit(QStringLiteral("SN-B"));
    submit(QStringLiteral("SN-C"));
    QCOMPARE(title->text(), QStringLiteral("Scan UUT 4 SN"));
    previous->click();
    QCOMPARE(title->text(), QStringLiteral("Scan UUT 3 SN"));
    active->click();
    QCOMPARE(title->text(), QStringLiteral("Scan UUT 4 SN"));
    submit(QStringLiteral("SN-C"));

    QCOMPARE(batchSpy.count(), 2);
    const auto replaced = batchSpy.last().first().toStringList();
    QCOMPARE(replaced,
             QStringList({QStringLiteral("SN-A"),
                          QStringLiteral("SN-B"),
                          QString{},
                          QStringLiteral("SN-C")}));
}

void MainWindowLifecycleTests::scanDialogExpandsValidatedBatchAndRejectsCurrentSlot()
{
    ScanDialog dialog;
    dialog.setAttribute(Qt::WA_DontShowOnScreen);
    dialog.setSlotCount(1);
    dialog.setSubmissionValidator(
        [](const QStringList& proposed, int currentSlot) {
            for (const auto& barcode : proposed) {
                if (!barcode.isEmpty() &&
                    !barcode.startsWith(QStringLiteral("A-"))) {
                    return ScanSubmissionDecision{
                        false,
                        QStringLiteral("UUT %1 belongs to another project")
                            .arg(currentSlot + 1),
                        0,
                        {}};
                }
            }
            return ScanSubmissionDecision{
                true, {}, 3, QStringLiteral("Product A")};
        });

    auto* barcode = dialog.findChild<QLineEdit*>(QStringLiteral("barcodeEdit"));
    auto* title = dialog.findChild<QLabel*>(QStringLiteral("scanTitleLabel"));
    auto* progress = dialog.findChild<QLabel*>(
        QStringLiteral("scanProgressLabel"));
    auto* error = dialog.findChild<QLabel*>(QStringLiteral("scanErrorLabel"));
    QVERIFY(barcode);
    QVERIFY(title);
    QVERIFY(progress);
    QVERIFY(error);
    QSignalSpy batchSpy(&dialog, &ScanDialog::barcodesAccepted);

    const auto submit = [&](const QString& value) {
        barcode->setText(value);
        QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    };

    dialog.showForNextScan();
    QCOMPARE(dialog.slotCount(), 1);
    submit(QStringLiteral("A-001"));
    QCOMPARE(dialog.slotCount(), 3);
    QCOMPARE(dialog.barcodes().at(0), QStringLiteral("A-001"));
    QCOMPARE(title->text(), QStringLiteral("Scan UUT 2 SN"));
    QCOMPARE(progress->text(), QStringLiteral("Product A  |  1 / 3 scanned"));

    submit(QStringLiteral("B-002"));
    QVERIFY(error->isVisible());
    QVERIFY(error->text().contains(QStringLiteral("UUT 2")));
    QVERIFY(dialog.barcodes().at(1).isEmpty());
    QCOMPARE(title->text(), QStringLiteral("Scan UUT 2 SN"));

    submit(QStringLiteral("A-002"));
    submit(QStringLiteral("A-003"));
    QCOMPARE(batchSpy.count(), 1);
    QCOMPARE(batchSpy.first().first().toStringList(),
             QStringList({QStringLiteral("A-001"),
                          QStringLiteral("A-002"),
                          QStringLiteral("A-003")}));
    QVERIFY(dialog.isHidden());

    dialog.showForNextScan();
    QCOMPARE(dialog.slotCount(), 1);
    QCOMPARE(dialog.barcodes(), QStringList{QString{}});
}

void MainWindowLifecycleTests::adminStartsOnProductionDashboardAndOpensScannerOnDemand()
{
    QSettings().clear();
    const QString projectDir = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("simple_sequence.json"));
    QVERIFY(QFile::copy(projectDir + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({"stationId":"bench-01","model":"PICO-M1","customerId":"CUSTOMER-01","scanDialogEnabled":true,"devices":[]})");
    station.close();
    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    QVERIFY(window.openStationFile(stationPath));
    window.showRunPage();
    window.show();
    QTest::qWait(20);

    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* resultView = window.findChild<QTreeView*>(QStringLiteral("resultView"));
    auto* scanAction = window.findChild<QAction*>(QStringLiteral("adminScanAction"));
    auto* compileAction = window.findChild<QAction*>(QStringLiteral("compileAction"));
    auto* scanDialog = window.findChild<ScanDialog*>();
    auto* sequenceLabel = window.findChild<QLabel*>(QStringLiteral("adminSequenceLabel"));
    auto* brandLogo = window.findChild<QLabel*>(QStringLiteral("adminBrandLogo"));
    auto* brandSlot = window.findChild<QWidget*>(QStringLiteral("adminBrandSlot"));
    auto* runSplitter = window.findChild<QSplitter*>(QStringLiteral("runSplitter"));
    auto* runSidebar = window.findChild<QWidget*>(QStringLiteral("adminRunSidebar"));
    auto* stationLabel = window.findChild<QLabel*>(QStringLiteral("adminStationLabel"));
    auto* modelLabel = window.findChild<QLabel*>(QStringLiteral("adminModelLabel"));
    auto* customerIdLabel = window.findChild<QLabel*>(
        QStringLiteral("adminCustomerIdLabel"));
    QVERIFY(tabs);
    QVERIFY(viewModel);
    QVERIFY(resultView);
    QVERIFY(scanAction);
    QVERIFY(compileAction);
    QVERIFY(scanDialog);
    QVERIFY(sequenceLabel);
    QVERIFY(brandLogo);
    QVERIFY(brandSlot);
    QVERIFY(runSplitter);
    QVERIFY(runSidebar);
    QVERIFY(stationLabel);
    QVERIFY(modelLabel);
    QVERIFY(customerIdLabel);
    QCOMPARE(tabs->count(), 4);
    QCOMPARE(tabs->currentIndex(), 0);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Run Test"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("Flow Editor"));
    QCOMPARE(tabs->tabText(2), QStringLiteral("Station Config"));
    QCOMPARE(tabs->tabText(3), QStringLiteral("Reports"));
    QCOMPARE(sequenceLabel->text(), QStringLiteral("simple_sequence.json"));
    QCOMPARE(brandLogo->accessibleName(), QStringLiteral("SINEXCEL"));
    const auto headerMatchesRunColumns = [&] {
        return brandSlot->width() == runSidebar->width() &&
            qAbs(sequenceLabel->mapTo(&window, QPoint()).x()
                 - runSplitter->widget(1)->mapTo(&window, QPoint()).x()) <= 4;
    };
    QVERIFY(headerMatchesRunColumns());
    window.resize(1600, 900);
    QTest::qWait(20);
    QVERIFY(headerMatchesRunColumns());
    QCOMPARE(stationLabel->text(), QStringLiteral("bench-01"));
    QCOMPARE(modelLabel->text(), QStringLiteral("PICO-M1"));
    QCOMPARE(customerIdLabel->text(), QStringLiteral("CUSTOMER-01"));
    QVERIFY(scanDialog->isHidden());
    QVERIFY(!scanAction->isEnabled());

    compileAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    QVERIFY(scanAction->isEnabled());
    QCOMPARE(resultView->model()->rowCount(), 3);
    QCOMPARE(resultView->model()->data(resultView->model()->index(0, 0)).toString(),
             QStringLiteral("SETUP"));
    QCOMPARE(resultView->model()->data(resultView->model()->index(1, 0)).toString(),
             QStringLiteral("MAIN"));
    QCOMPARE(resultView->model()->data(resultView->model()->index(2, 0)).toString(),
             QStringLiteral("CLEANUP"));
    QVERIFY(scanDialog->isHidden());

    scanAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(scanDialog->isVisible(), 1000);

    auto* barcode = scanDialog->findChild<QLineEdit*>(QStringLiteral("barcodeEdit"));
    auto* serialLabel = window.findChild<QLabel*>(QStringLiteral("adminSerialLabel"));
    QVERIFY(barcode);
    QVERIFY(serialLabel);
    barcode->setText(QStringLiteral("CANCELLED-SN"));
    scanAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(scanDialog->isHidden(), 1000);
    QCOMPARE(viewModel->state(), UiRunState::Ready);

    scanAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(scanDialog->isVisible(), 1000);
    QVERIFY(barcode->text().isEmpty());

    const auto screenshotPath = qEnvironmentVariable("PICOATE_ADMIN_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        scanDialog->hide();
        QVERIFY2(window.grab().save(screenshotPath), qPrintable(screenshotPath));
    }

    barcode->setText(QStringLiteral("ADMIN-SN-001"));
    QVERIFY(QMetaObject::invokeMethod(scanDialog, "submitBarcode"));
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             3000);
    QCOMPARE(viewModel->state(), UiRunState::Completed);
    QCOMPARE(serialLabel->text(), QStringLiteral("ADMIN-SN-001"));
    QVERIFY(scanDialog->isHidden());
    QVERIFY(!resultView->currentIndex().isValid());
    auto* resultModel = qobject_cast<UutStepModel*>(resultView->model());
    QVERIFY(resultModel);
    const auto powerOff = resultModel->indexForStep({}, QStringLiteral("power-off"));
    QVERIFY(powerOff.isValid());
    QCOMPARE(resultModel->data(powerOff.siblingAtColumn(UutStepModel::NameColumn)).toString(),
             QStringLiteral("Power Off"));
    QCOMPARE(resultModel->data(powerOff.siblingAtColumn(UutStepModel::StateColumn)).toString(),
             QStringLiteral("Passed"));
}

void MainWindowLifecycleTests::adminScannerRunsFourExplicitUuts()
{
    QSettings().clear();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("simple_sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({"stationId":"admin-four","uutCount":4,"scanDialogEnabled":true,"devices":[]})");
    station.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    QVERIFY(window.openStationFile(stationPath));
    window.showRunPage();
    window.show();

    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* compileAction = window.findChild<QAction*>(QStringLiteral("compileAction"));
    auto* scanAction = window.findChild<QAction*>(QStringLiteral("adminScanAction"));
    auto* uutCount = window.findChild<QSpinBox*>(QStringLiteral("uutCountSpinBox"));
    auto* scanDialog = window.findChild<ScanDialog*>();
    auto* barcode = scanDialog
        ? scanDialog->findChild<QLineEdit*>(QStringLiteral("barcodeEdit"))
        : nullptr;
    QVERIFY(viewModel);
    QVERIFY(compileAction);
    QVERIFY(scanAction);
    QVERIFY(uutCount);
    QVERIFY(scanDialog);
    QVERIFY(barcode);

    uutCount->setValue(4);
    compileAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    scanAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(scanDialog->isVisible(), 1000);
    QCOMPARE(scanDialog->slotCount(), 4);

    for (int index = 1; index <= 4; ++index) {
        barcode->setText(QStringLiteral("ADMIN%1").arg(index, 2, 10,
                                                        QLatin1Char('0')));
        QVERIFY(QMetaObject::invokeMethod(scanDialog, "submitBarcode"));
    }

    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             5000);
    QCOMPARE(viewModel->state(), UiRunState::Completed);
    const auto report = viewModel->report();
    QCOMPARE(report.uuts.size(), 4);
    for (int index = 0; index < report.uuts.size(); ++index) {
        QCOMPARE(report.uuts[index].uutId,
                 QStringLiteral("UUT-%1").arg(index + 1));
        QCOMPARE(report.uuts[index].serialNumber,
                 QStringLiteral("ADMIN%1").arg(index + 1, 2, 10,
                                                   QLatin1Char('0')));
        QVERIFY(report.uuts[index].completed);
        QVERIFY(!report.uuts[index].hasError);
    }
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::productionWindowPreloadsFlowAndRunsWithoutScanner()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("product_sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({"stationId":"line-1","model":"PICO-M2","customerId":"CUSTOMER-02","scanDialogEnabled":false,"devices":[]})");
    station.close();

    StartupSelection selection;
    selection.mode = UiMode::Test;
    selection.sequencePath = sequencePath;
    selection.stationPath = stationPath;
    selection.scanDialogEnabled = false;
    ProductionWindow window(selection);
    window.show();
    QTest::qWait(20);
    QVERIFY(!window.windowFlags().testFlag(Qt::FramelessWindowHint));

    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* resultView = window.findChild<QTreeView*>(
        QStringLiteral("productionResultView"));
    auto* start = window.findChild<QAction*>(QStringLiteral("productionStartAction"));
    auto* deviceAction = window.findChild<QAction*>(
        QStringLiteral("productionFieldDeviceAction"));
    auto* routingAction = window.findChild<QAction*>(
        QStringLiteral("productionProductRoutingAction"));
    auto* overall = window.findChild<QLabel*>(
        QStringLiteral("productionOverallResult"));
    auto* stationLabel = window.findChild<QLabel*>(
        QStringLiteral("productionStationLabel"));
    auto* modelLabel = window.findChild<QLabel*>(
        QStringLiteral("productionModelLabel"));
    auto* customerIdLabel = window.findChild<QLabel*>(
        QStringLiteral("productionCustomerIdLabel"));
    auto* sequenceLabel = window.findChild<QLabel*>(
        QStringLiteral("productionSequenceLabel"));
    auto* brandLogo = window.findChild<QLabel*>(
        QStringLiteral("productionBrandLogo"));
    auto* brandSlot = window.findChild<QWidget*>(
        QStringLiteral("productionBrandSlot"));
    auto* toolbar = window.findChild<QToolBar*>(
        QStringLiteral("productionToolbar"));
    auto* passCount = window.findChild<QLabel*>(
        QStringLiteral("productionPassCount"));
    auto* failCount = window.findChild<QLabel*>(
        QStringLiteral("productionFailCount"));
    auto* totalCount = window.findChild<QLabel*>(
        QStringLiteral("productionTotalCount"));
    auto* yieldChart = window.findChild<QWidget*>(
        QStringLiteral("productionYieldChart"));
    auto* progressPanel = window.findChild<QWidget*>(
        QStringLiteral("productionProgressPanel"));
    auto* statsBar = window.findChild<QWidget*>(QStringLiteral("productionStatsBar"));
    auto* progress = window.findChild<QProgressBar*>(
        QStringLiteral("productionProgress"));
    auto* averageTime = window.findChild<QLabel*>(
        QStringLiteral("productionAverageTime"));
    auto* productionStatusBar = window.findChild<QStatusBar*>(
        QStringLiteral("productionStatusBar"));
    auto* contentSplitter = window.findChild<QSplitter*>(
        QStringLiteral("productionContentSplitter"));
    auto* dataSplitter = window.findChild<QSplitter*>(
        QStringLiteral("productionDataSplitter"));
    auto* sidebar = window.findChild<QWidget*>(
        QStringLiteral("productionSidebar"));
    auto* scan = window.findChild<ScanDialog*>();
    QVERIFY(viewModel);
    QVERIFY(resultView);
    QVERIFY(start);
    QVERIFY(deviceAction);
    QVERIFY(routingAction);
    QVERIFY(overall);
    QVERIFY(stationLabel);
    QVERIFY(modelLabel);
    QVERIFY(customerIdLabel);
    QVERIFY(sequenceLabel);
    QVERIFY(brandLogo);
    QVERIFY(brandSlot);
    QVERIFY(toolbar);
    QVERIFY(passCount);
    QVERIFY(failCount);
    QVERIFY(totalCount);
    QVERIFY(yieldChart);
    QVERIFY(progressPanel);
    QVERIFY(statsBar);
    QVERIFY(progress);
    QVERIFY(averageTime);
    QVERIFY(productionStatusBar);
    QVERIFY(contentSplitter);
    QVERIFY(dataSplitter);
    QVERIFY(sidebar);
    QVERIFY(scan);
    QCOMPARE(stationLabel->text(), QStringLiteral("line-1"));
    QCOMPARE(modelLabel->text(), QStringLiteral("PICO-M2"));
    QCOMPARE(customerIdLabel->text(), QStringLiteral("CUSTOMER-02"));
    QCOMPARE(contentSplitter->orientation(), Qt::Horizontal);
    QCOMPARE(dataSplitter->orientation(), Qt::Vertical);
    QCOMPARE(brandLogo->accessibleName(), QStringLiteral("SINEXCEL"));
    QVERIFY(toolbar->mapTo(&window, QPoint()).y()
            < brandLogo->mapTo(&window, QPoint()).y());
    const auto headerMatchesRunColumns = [&] {
        return brandSlot->width() == sidebar->width() &&
            qAbs(sequenceLabel->mapTo(&window, QPoint()).x()
                 - contentSplitter->widget(1)->mapTo(&window, QPoint()).x()) <= 4;
    };
    QVERIFY(headerMatchesRunColumns());
    window.resize(1600, 900);
    QTest::qWait(20);
    QVERIFY(headerMatchesRunColumns());
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    QCOMPARE(resultView->model()->rowCount(), 3);
    QVERIFY(resultView->isColumnHidden(UutStepModel::StateColumn));
    const QStringList phaseNames = {QStringLiteral("SETUP"),
                                    QStringLiteral("MAIN"),
                                    QStringLiteral("CLEANUP")};
    for (int row = 0; row < phaseNames.size(); ++row) {
        const auto phase = resultView->model()->index(row, UutStepModel::NameColumn);
        QCOMPARE(resultView->model()->data(phase).toString(), phaseNames[row]);
        QVERIFY(resultView->model()->rowCount(phase) > 0);
    }
    const auto setup = resultView->model()->index(0, 0);
    const auto pendingStep = resultView->model()->index(
        0, UutStepModel::OutcomeColumn, setup);
    QCOMPARE(resultView->model()->data(pendingStep).toString(), QStringLiteral("Pending"));
    QVERIFY(start->isVisible());
    QVERIFY(start->isEnabled());
    QVERIFY(deviceAction->isVisible());
    QVERIFY(deviceAction->isEnabled());
    QVERIFY(!routingAction->isVisible());
    QVERIFY(scan->isHidden());
    QCOMPARE(passCount->text(), QStringLiteral("PASS 0"));
    QCOMPARE(failCount->text(), QStringLiteral("FAIL 0"));
    QCOMPARE(totalCount->text(), QStringLiteral("TOTAL 0"));
    QCOMPARE(yieldChart->property("passedCount").toInt(), 0);
    QCOMPARE(yieldChart->property("failedCount").toInt(), 0);
    QCOMPARE(yieldChart->property("yieldPercent").toDouble(), 0.0);
    QCOMPARE(progress->parentWidget(), progressPanel);
    QCOMPARE(statsBar->parentWidget(), productionStatusBar);
    QVERIFY(progressPanel->parentWidget() != statsBar->parentWidget());
    QVERIFY(passCount->geometry().left() < failCount->geometry().left());
    QVERIFY(failCount->geometry().left() < totalCount->geometry().left());
    QVERIFY(averageTime->geometry().left() > totalCount->geometry().right());

    const auto screenshotPath = qEnvironmentVariable("PICOATE_TEST_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        QVERIFY2(window.grab().save(screenshotPath), qPrintable(screenshotPath));
    }

    start->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             3000);
    QCOMPARE(viewModel->state(), UiRunState::Completed);
    QCOMPARE(overall->text(), QStringLiteral("PASS"));
    QCOMPARE(passCount->text(), QStringLiteral("PASS 1"));
    QCOMPARE(failCount->text(), QStringLiteral("FAIL 0"));
    QCOMPARE(totalCount->text(), QStringLiteral("TOTAL 1"));
    QCOMPARE(yieldChart->property("passedCount").toInt(), 1);
    QCOMPARE(yieldChart->property("failedCount").toInt(), 0);
    QCOMPARE(yieldChart->property("yieldPercent").toDouble(), 100.0);
    QCOMPARE(resultView->model()->rowCount(), 3);
    const auto completedSetup = resultView->model()->index(0, 0);
    QVERIFY(resultView->model()->rowCount(completedSetup) > 0);

    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 1000);
    start->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(passCount->text(), QStringLiteral("PASS 2"), 3000);
    QCOMPARE(failCount->text(), QStringLiteral("FAIL 0"));
    QCOMPARE(totalCount->text(), QStringLiteral("TOTAL 2"));
    QCOMPARE(yieldChart->property("passedCount").toInt(), 2);
    QCOMPARE(yieldChart->property("failedCount").toInt(), 0);
    QCOMPARE(yieldChart->property("yieldPercent").toDouble(), 100.0);
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::productionScannerRunsFourExplicitUutsAndCountsYield()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("four_uut_sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({"stationId":"line-four","uutCount":4,"scanDialogEnabled":true,"devices":[]})");
    station.close();

    StartupSelection selection;
    selection.mode = UiMode::Test;
    selection.sequencePath = sequencePath;
    selection.stationPath = stationPath;
    selection.scanDialogEnabled = true;
    ProductionWindow window(selection);
    window.show();

    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* scanDialog = window.findChild<ScanDialog*>();
    auto* barcode = scanDialog
        ? scanDialog->findChild<QLineEdit*>(QStringLiteral("barcodeEdit"))
        : nullptr;
    auto* passCount = window.findChild<QLabel*>(QStringLiteral("productionPassCount"));
    auto* failCount = window.findChild<QLabel*>(QStringLiteral("productionFailCount"));
    auto* totalCount = window.findChild<QLabel*>(QStringLiteral("productionTotalCount"));
    QVERIFY(viewModel);
    QVERIFY(scanDialog);
    QVERIFY(barcode);
    QVERIFY(passCount);
    QVERIFY(failCount);
    QVERIFY(totalCount);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(scanDialog->isVisible(), 1000);
    QCOMPARE(scanDialog->slotCount(), 4);

    for (int index = 1; index <= 4; ++index) {
        barcode->setText(QStringLiteral("PROD%1").arg(index, 2, 10,
                                                       QLatin1Char('0')));
        QVERIFY(QMetaObject::invokeMethod(scanDialog, "submitBarcode"));
    }

    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             5000);
    QCOMPARE(viewModel->state(), UiRunState::Completed);
    const auto report = viewModel->report();
    QCOMPARE(report.uuts.size(), 4);
    for (int index = 0; index < report.uuts.size(); ++index) {
        QCOMPARE(report.uuts[index].uutId,
                 QStringLiteral("UUT-%1").arg(index + 1));
        QCOMPARE(report.uuts[index].serialNumber,
                 QStringLiteral("PROD%1").arg(index + 1, 2, 10,
                                                  QLatin1Char('0')));
        QVERIFY(report.uuts[index].completed);
        QVERIFY(!report.uuts[index].hasError);
    }
    QCOMPARE(passCount->text(), QStringLiteral("PASS 4"));
    QCOMPARE(failCount->text(), QStringLiteral("FAIL 0"));
    QCOMPARE(totalCount->text(), QStringLiteral("TOTAL 4"));
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::productionUutControlsConfigureRuntimeSlots()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(
        QStringLiteral("runtime_uut_slots_sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    const auto stationPath = directory.filePath(
        QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({"stationId":"runtime-slots","uutCount":4,"scanDialogEnabled":false,"devices":[]})");
    station.close();

    StartupSelection selection;
    selection.mode = UiMode::Test;
    selection.sequencePath = sequencePath;
    selection.stationPath = stationPath;
    selection.scanDialogEnabled = false;
    ProductionWindow window(selection);
    window.show();

    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* uutCount = window.findChild<QSpinBox*>(
        QStringLiteral("productionUutCountSpinBox"));
    auto* slotAction = window.findChild<QAction*>(
        QStringLiteral("productionUutSlotsAction"));
    auto* start = window.findChild<QAction*>(
        QStringLiteral("productionStartAction"));
    auto* scan = window.findChild<ScanDialog*>();
    QVERIFY(viewModel);
    QVERIFY(uutCount);
    QVERIFY(slotAction);
    QVERIFY(start);
    QVERIFY(scan);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    QCOMPARE(uutCount->value(), 4);
    QCOMPARE(uutCount->maximum(), 4);
    uutCount->setValue(2);
    QVERIFY(slotAction->isVisible());
    QCOMPARE(slotAction->text(), QStringLiteral("UUT Slots 2/2"));

    uutCount->setValue(4);
    QCOMPARE(scan->slotCount(), 4);
    QCOMPARE(slotAction->text(), QStringLiteral("UUT Slots 4/4"));
    auto* overview = window.findChild<MultiUutOverviewWidget*>(
        QStringLiteral("productionUutOverview"));
    auto* stack = window.findChild<QStackedWidget*>(
        QStringLiteral("productionRunStack"));
    auto* overviewPage = window.findChild<QWidget*>(
        QStringLiteral("productionRunOverviewPage"));
    QVERIFY(overview);
    QVERIFY(stack);
    QVERIFY(overviewPage);
    auto* overviewModel = overview->model();
    QVERIFY(overviewModel);
    QTRY_COMPARE(overviewModel->rowCount(), 4);
    QCOMPARE(stack->currentWidget(), overviewPage);
    auto* sidebar = window.findChild<QWidget*>(
        QStringLiteral("productionSidebar"));
    auto* overviewSummary = window.findChild<QWidget*>(
        QStringLiteral("productionOverviewSummary"));
    auto* overviewStatus = window.findChild<QLabel*>(
        QStringLiteral("productionOverviewSummaryState"));
    auto* overviewStation = window.findChild<QLabel*>(
        QStringLiteral("productionOverviewStationValue"));
    auto* overviewCustomerId = window.findChild<QLabel*>(
        QStringLiteral("productionOverviewCustomerIdValue"));
    auto* overviewJig = window.findChild<QLabel*>(
        QStringLiteral("productionOverviewJigValue"));
    auto* overviewWaiting = window.findChild<QLabel*>(
        QStringLiteral("productionOverviewWaitingValue"));
    QVERIFY(sidebar);
    QVERIFY(overviewSummary);
    QVERIFY(overviewStatus);
    QVERIFY(overviewStation);
    QVERIFY(overviewCustomerId);
    QVERIFY(overviewJig);
    QVERIFY(overviewWaiting);
    QTRY_VERIFY(!sidebar->isVisible());
    QVERIFY(overviewSummary->isVisible());
    QCOMPARE(overviewStation->text(), QStringLiteral("runtime-slots"));
    QCOMPARE(overviewWaiting->text(), QStringLiteral("4"));
    QCOMPARE(overviewStatus->text(), QStringLiteral("READY"));
    QVERIFY(overviewStatus->styleSheet().contains(
        QStringLiteral("border:1px solid")));
    auto* overviewButton = window.findChild<QPushButton*>(
        QStringLiteral("productionOverviewButton"));
    QVERIFY(overviewButton);
    QVERIFY(overviewButton->isChecked());
    QTRY_VERIFY_WITH_TIMEOUT(
        overviewButton->property("overviewIndicatorFill").toReal() > 0.99,
        500);

    bool configured = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(
            QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() !=
                QStringLiteral("uutSlotConfigurationDialog")) {
            return;
        }
        auto* uut3 = dialog->findChild<QPushButton*>(
            QStringLiteral("uutSlotToggle3"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>(
            QStringLiteral("uutSlotConfigurationButtons"));
        if (!uut3 || !buttons) {
            dialog->reject();
            return;
        }
        uut3->setChecked(false);
        configured = true;
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    slotAction->trigger();
    QVERIFY(configured);
    QCOMPARE(slotAction->text(), QStringLiteral("UUT Slots 3/4"));
    QCOMPARE(scan->slotEnabledStates(),
             QVector<bool>({true, true, false, true}));
    QTRY_COMPARE(overviewModel->rowCount(), 4);
    const auto disabledPreview = overviewModel->entryAt(2);
    QVERIFY(disabledPreview.has_value());
    QCOMPARE(disabledPreview->uutId, QStringLiteral("UUT-3"));
    QVERIFY(!disabledPreview->enabled);
    QCOMPARE(disabledPreview->state, UutOverviewState::Disabled);
    auto* disabledCard = window.findChild<QAbstractButton*>(
        QStringLiteral("uutOverviewCard_3"));
    auto* disabledButton = window.findChild<QPushButton*>(
        QStringLiteral("productionUutButton_3"));
    QVERIFY(disabledCard);
    QVERIFY(disabledButton);
    QVERIFY(!disabledCard->isEnabled());
    QVERIFY(!disabledCard->isHidden());
    QVERIFY(!disabledButton->isEnabled());
    QVERIFY(!disabledButton->isHidden());

    start->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             5000);
    QCOMPARE(viewModel->state(), UiRunState::Completed);
    const auto report = viewModel->report();
    QCOMPARE(report.uuts.size(), 3);
    QCOMPARE(report.uuts[0].uutId, QStringLiteral("UUT-1"));
    QCOMPARE(report.uuts[1].uutId, QStringLiteral("UUT-2"));
    QCOMPARE(report.uuts[2].uutId, QStringLiteral("UUT-4"));
    QCOMPARE(overviewModel->rowCount(), 4);
    const auto disabledFinal = overviewModel->entryAt(2);
    QVERIFY(disabledFinal.has_value());
    QCOMPARE(disabledFinal->uutId, QStringLiteral("UUT-3"));
    QCOMPARE(disabledFinal->state, UutOverviewState::Disabled);
    QVERIFY(!disabledFinal->enabled);
    QCOMPARE(stack->currentWidget(), overviewPage);

    auto* uut2Button = window.findChild<QPushButton*>(
        QStringLiteral("productionUutButton_2"));
    auto* detailPage = window.findChild<QWidget*>(
        QStringLiteral("productionRunDetailPage"));
    QVERIFY(uut2Button);
    QVERIFY(detailPage);
    uut2Button->click();
    QCOMPARE(stack->currentWidget(), detailPage);
    QTRY_VERIFY(sidebar->isVisible());
    QVERIFY(!overviewSummary->isVisible());
    auto* resultModel = window.findChild<UutStepModel*>();
    auto* logProxy = window.findChild<UutRuntimeTimelineProxyModel*>();
    QVERIFY(resultModel);
    QVERIFY(logProxy);
    QCOMPARE(resultModel->visibleUutId(), QStringLiteral("UUT-2"));
    QCOMPARE(logProxy->visibleUutId(), QStringLiteral("UUT-2"));
    overviewButton->click();
    QCOMPARE(stack->currentWidget(), overviewPage);
    QTRY_VERIFY(!sidebar->isVisible());
    QVERIFY(overviewSummary->isVisible());
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::productionWindowRoutesScannedSnBeforeCompiling()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto projectPath = directory.filePath(
        QStringLiteral("projects/AutoProduct"));
    QVERIFY(QDir().mkpath(projectPath));
    const auto sequencePath = QDir(projectPath).filePath(
        QStringLiteral("auto_product_sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    const auto stationPath = QDir(projectPath).filePath(
        QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({
        "stationId":"auto-test","scanDialogEnabled":false,
        "uutCount":4,
        "snLength":99,"snPattern":"STATION-ONLY-*",
        "snAllowedRegex":"^[A-Z0-9-]+$","devices":[]
    })");
    station.close();
    const auto otherProjectPath = directory.filePath(
        QStringLiteral("projects/OtherProduct"));
    QVERIFY(QDir().mkpath(otherProjectPath));
    const auto otherSequencePath = QDir(otherProjectPath).filePath(
        QStringLiteral("other_product_sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       otherSequencePath));
    const auto otherStationPath = QDir(otherProjectPath).filePath(
        QStringLiteral("StationSystem.json"));
    QFile otherStation(otherStationPath);
    QVERIFY(otherStation.open(QIODevice::WriteOnly));
    otherStation.write(R"({
        "stationId":"other-test","uutCount":2,
        "snAllowedRegex":"^[A-Z0-9-]+$","devices":[]
    })");
    otherStation.close();
    const auto routingPath = directory.filePath(QStringLiteral("ProductRouting.json"));
    QFile routing(routingPath);
    QVERIFY(routing.open(QIODevice::WriteOnly));
    routing.write(R"({
        "allowManualInTest":false,
        "projectRoot":"projects",
        "routes":[
            {
                "name":"Auto product","pattern":"AUTO-*","snLength":9,
                "project":"AutoProduct"
            },
            {
                "name":"Other product","pattern":"OTHER-*","snLength":10,
                "project":"OtherProduct"
            }
        ]
    })");
    routing.close();

    StartupSelection selection;
    selection.mode = UiMode::Test;
    selection.sequenceLoadMode = SequenceLoadMode::AutoBySn;
    selection.productRoutingPath = routingPath;
    selection.scanDialogEnabled = true;
    selection.snValidationRules.wildcardPattern = QStringLiteral("SH*");
    ProductionWindow window(selection);
    window.show();

    auto* scan = window.findChild<ScanDialog*>();
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* title = window.findChild<QLabel*>(
        QStringLiteral("productionSequenceLabel"));
    auto* routingAction = window.findChild<QAction*>(
        QStringLiteral("productionProductRoutingAction"));
    auto* deviceAction = window.findChild<QAction*>(
        QStringLiteral("productionFieldDeviceAction"));
    QVERIFY(scan);
    QVERIFY(viewModel);
    QVERIFY(title);
    QVERIFY(routingAction);
    QVERIFY(deviceAction);
    QVERIFY(routingAction->isVisible());
    QVERIFY(routingAction->isEnabled());
    QVERIFY(!deviceAction->isVisible());
    auto* barcode = scan->findChild<QLineEdit*>(QStringLiteral("barcodeEdit"));
    auto* scanError = scan->findChild<QLabel*>(QStringLiteral("scanErrorLabel"));
    QVERIFY(barcode);
    QVERIFY(scanError);
    QSignalSpy acceptedSpy(scan, &ScanDialog::barcodesAccepted);
    QTRY_VERIFY_WITH_TIMEOUT(scan->isVisible(), 1000);
    QCOMPARE(scan->slotCount(), 1);

    const auto submit = [&](const QString& value) {
        barcode->setText(value);
        QVERIFY(QMetaObject::invokeMethod(scan, "submitBarcode"));
    };
    submit(QStringLiteral("AUTO-0001"));
    QCOMPARE(scan->slotCount(), 4);
    QCOMPARE(scan->barcodes().first(), QStringLiteral("AUTO-0001"));
    QCOMPARE(acceptedSpy.count(), 0);

    submit(QStringLiteral("OTHER-0002"));
    QVERIFY(scanError->isVisible());
    QVERIFY(scanError->text().contains(QStringLiteral("UUT 2")));
    QVERIFY(scanError->text().contains(QStringLiteral("batch"),
                                       Qt::CaseInsensitive));
    QCOMPARE(scan->barcodes().at(1), QString{});
    QCOMPARE(scan->barcodes().first(), QStringLiteral("AUTO-0001"));
    QCOMPARE(acceptedSpy.count(), 0);

    submit(QStringLiteral("AUTO-0002"));
    submit(QStringLiteral("AUTO-0003"));
    submit(QStringLiteral("AUTO-0004"));
    QCOMPARE(acceptedSpy.count(), 1);
    QCOMPARE(acceptedSpy.first().first().toStringList(),
             QStringList({QStringLiteral("AUTO-0001"),
                          QStringLiteral("AUTO-0002"),
                          QStringLiteral("AUTO-0003"),
                          QStringLiteral("AUTO-0004")}));
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             8000);
    QCOMPARE(viewModel->state(), UiRunState::Completed);
    QCOMPARE(viewModel->sequencePath(), QFileInfo(sequencePath).absoluteFilePath());
    QCOMPARE(viewModel->stationPath(), QFileInfo(stationPath).absoluteFilePath());
    QCOMPARE(title->text(), QFileInfo(sequencePath).fileName());
    const auto& report = viewModel->report();
    QCOMPARE(report.uuts.size(), 4);
    for (int index = 0; index < report.uuts.size(); ++index) {
        QCOMPARE(report.uuts[index].uutId,
                 QStringLiteral("UUT-%1").arg(index + 1));
        QCOMPARE(report.uuts[index].serialNumber,
                 QStringLiteral("AUTO-000%1").arg(index + 1));
    }
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::adminWindowRoutesScannedSnBeforeCompiling()
{
    QSettings().clear();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto projectPath = directory.filePath(
        QStringLiteral("projects/AdminProduct"));
    QVERIFY(QDir().mkpath(projectPath));
    const auto sequencePath = QDir(projectPath).filePath(
        QStringLiteral("admin_auto_sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    const auto stationPath = QDir(projectPath).filePath(
        QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({
        "stationId":"auto-admin","uutCount":2,"snLength":99,
        "snPattern":"STATION-ONLY-*","snAllowedRegex":"^[A-Z0-9-]+$",
        "devices":[]
    })");
    station.close();
    const auto previousStationPath = directory.filePath(
        QStringLiteral("previous/StationSystem.json"));
    QVERIFY(QDir().mkpath(QFileInfo(previousStationPath).absolutePath()));
    QFile previousStation(previousStationPath);
    QVERIFY(previousStation.open(QIODevice::WriteOnly));
    previousStation.write(R"({
        "stationId":"previous-sh-product","snPattern":"SH*","devices":[]
    })");
    previousStation.close();
    const auto routingPath = directory.filePath(QStringLiteral("ProductRouting.json"));
    QFile routing(routingPath);
    QVERIFY(routing.open(QIODevice::WriteOnly));
    routing.write(R"({
        "allowManualInTest":false,
        "projectRoot":"projects",
        "routes":[{
            "name":"Admin product","pattern":"ADMIN-*","snLength":10,
            "project":"AdminProduct"
        }]
    })");
    routing.close();

    MainWindow window;
    QVERIFY(window.openStationFile(previousStationPath));
    window.configureAutoRouting(routingPath);
    window.show();
    auto* scan = window.findChild<ScanDialog*>();
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* routingAction = window.findChild<QAction*>(
        QStringLiteral("adminProductRoutingAction"));
    QVERIFY(scan);
    QVERIFY(viewModel);
    QVERIFY(routingAction);
    QVERIFY(routingAction->isEnabled());
    window.showStartupScanDialog();
    QTRY_VERIFY_WITH_TIMEOUT(scan->isVisible(), 1000);
    auto* barcode = scan->findChild<QLineEdit*>(QStringLiteral("barcodeEdit"));
    QVERIFY(barcode);
    QSignalSpy acceptedSpy(scan, &ScanDialog::barcodesAccepted);
    QCOMPARE(scan->slotCount(), 1);
    barcode->setText(QStringLiteral("ADMIN-0001"));
    QVERIFY(QMetaObject::invokeMethod(scan, "submitBarcode"));
    QCOMPARE(scan->slotCount(), 2);
    QCOMPARE(acceptedSpy.count(), 0);
    QCOMPARE(scan->barcodes().first(), QStringLiteral("ADMIN-0001"));
    barcode->setText(QStringLiteral("ADMIN-0002"));
    QVERIFY(QMetaObject::invokeMethod(scan, "submitBarcode"));
    QCOMPARE(acceptedSpy.count(), 1);
    QCOMPARE(acceptedSpy.first().first().toStringList(),
             QStringList({QStringLiteral("ADMIN-0001"),
                          QStringLiteral("ADMIN-0002")}));
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             6000);
    QCOMPARE(viewModel->state(), UiRunState::Completed);
    QCOMPARE(viewModel->sequencePath(), QFileInfo(sequencePath).absoluteFilePath());
    QCOMPARE(viewModel->stationPath(), QFileInfo(stationPath).absoluteFilePath());
    const auto& report = viewModel->report();
    QCOMPARE(report.uuts.size(), 2);
    for (int index = 0; index < report.uuts.size(); ++index) {
        QCOMPARE(report.uuts[index].uutId,
                 QStringLiteral("UUT-%1").arg(index + 1));
        QCOMPARE(report.uuts[index].serialNumber,
                 QStringLiteral("ADMIN-000%1").arg(index + 1));
    }
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::productionStoppedRunCountsAsFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const auto sequencePath = directory.filePath(QStringLiteral("stop_sequence.json"));
    QFile sequence(sequencePath);
    QVERIFY(sequence.open(QIODevice::WriteOnly));
    sequence.write(R"json({
      "id": "stop-count",
      "name": "Stop Count",
      "groups": [
        { "id": "setup", "kind": "setup", "steps": [
          { "id": "prepare", "kind": "noop" }
        ]},
        { "id": "main", "kind": "main", "steps": [
          { "id": "long-wait", "kind": "wait", "ms": 5000 }
        ]},
        { "id": "cleanup", "kind": "cleanup", "steps": [
          { "id": "safe-off", "kind": "cleanup" }
        ]}
      ]
    })json");
    sequence.close();

    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({"stationId":"line-stop","scanDialogEnabled":false,"devices":[]})");
    station.close();

    StartupSelection selection;
    selection.mode = UiMode::Test;
    selection.sequencePath = sequencePath;
    selection.stationPath = stationPath;
    selection.scanDialogEnabled = false;
    ProductionWindow window(selection);
    window.show();

    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* start = window.findChild<QAction*>(QStringLiteral("productionStartAction"));
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("productionToolbar"));
    auto* stop = window.findChild<QAction*>(QStringLiteral("productionStopAction"));
    auto* overall = window.findChild<QLabel*>(QStringLiteral("productionOverallResult"));
    auto* passCount = window.findChild<QLabel*>(QStringLiteral("productionPassCount"));
    auto* failCount = window.findChild<QLabel*>(QStringLiteral("productionFailCount"));
    auto* totalCount = window.findChild<QLabel*>(QStringLiteral("productionTotalCount"));
    QVERIFY(viewModel);
    QVERIFY(start);
    QVERIFY(toolbar);
    QCOMPARE(toolbar->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    QCOMPARE(toolbar->iconSize(), QSize(20, 20));
    QVERIFY(!start->icon().isNull());
    QVERIFY(stop);
    QVERIFY(overall);
    QVERIFY(passCount);
    QVERIFY(failCount);
    QVERIFY(totalCount);

    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    start->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Running, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(stop->isEnabled(), 1000);
    stop->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Failed, 3000);

    const auto& report = viewModel->report();
    QVERIFY(report.completed);
    QVERIFY(report.hasError);
    QCOMPARE(report.state, PicoATE::Core::ExecutionState::CompletedWithError);
    QCOMPARE(overall->text(), QStringLiteral("FAIL"));
    QCOMPARE(passCount->text(), QStringLiteral("PASS 0"));
    QCOMPARE(failCount->text(), QStringLiteral("FAIL 1"));
    QCOMPARE(totalCount->text(), QStringLiteral("TOTAL 1"));
    QVERIFY(window.close());
}

void MainWindowLifecycleTests::productionLoopTestCountsAndArchivesEveryIteration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("loop_sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    const auto reportRoot = directory.filePath(QStringLiteral("reports"));
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(QJsonDocument(QJsonObject{
        {QStringLiteral("stationId"), QStringLiteral("loop-line")},
        {QStringLiteral("scanDialogEnabled"), false},
        {QStringLiteral("loopTestEnabled"), true},
        {QStringLiteral("loopTestCount"), 3},
        {QStringLiteral("txtLogEnabled"), true},
        {QStringLiteral("reportOutputDirectory"), reportRoot},
        {QStringLiteral("devices"), QJsonArray{}}}).toJson());
    station.close();

    StartupSelection selection;
    selection.mode = UiMode::Test;
    selection.sequencePath = sequencePath;
    selection.stationPath = stationPath;
    selection.scanDialogEnabled = false;
    ProductionWindow window(selection);
    window.show();

    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* resultView = window.findChild<QTreeView*>(
        QStringLiteral("productionResultView"));
    auto* start = window.findChild<QAction*>(QStringLiteral("productionStartAction"));
    auto* passCount = window.findChild<QLabel*>(QStringLiteral("productionPassCount"));
    auto* totalCount = window.findChild<QLabel*>(QStringLiteral("productionTotalCount"));
    auto* averageTime = window.findChild<QLabel*>(
        QStringLiteral("productionAverageTime"));
    QVERIFY(viewModel);
    QVERIFY(resultView);
    QVERIFY(start);
    QVERIFY(passCount);
    QVERIFY(totalCount);
    QVERIFY(averageTime);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);

    int resetRounds = 0;
    connect(viewModel,
            &ExecutionViewModel::runIterationStarted,
            &window,
            [&](int iteration, int) {
                if (iteration <= 1) {
                    return;
                }
                const auto setup = resultView->model()->index(0, 0);
                const auto firstStep = resultView->model()->index(
                    0, UutStepModel::OutcomeColumn, setup);
                if (firstStep.data().toString() == QStringLiteral("Pending")) {
                    ++resetRounds;
                }
            });

    start->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Completed, 5000);
    QCOMPARE(passCount->text(), QStringLiteral("PASS 3"));
    QCOMPARE(totalCount->text(), QStringLiteral("TOTAL 3"));
    QCOMPARE(resetRounds, 2);
    QVERIFY(averageTime->text().startsWith(QStringLiteral("AVERAGE TIME ")));

    const auto dateDirectory = QDir(reportRoot).filePath(
        QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
    const auto logs = QDir(QDir(dateDirectory).filePath(QStringLiteral("PASS")))
                          .entryList({QStringLiteral("*.txt")}, QDir::Files);
    QCOMPARE(logs.size(), 3);
    for (const auto& log : logs) {
        QVERIFY(QRegularExpression(
            QStringLiteral("_\\d{9}\\.txt$")).match(log).hasMatch());
    }
}

void MainWindowLifecycleTests::runTestBreakpointGutterControlsExecutionBreakpoints()
{
    const QString projectDir = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR);

    MainWindow window;
    QVERIFY(window.openSequenceFile(
        projectDir + QStringLiteral("/examples/simple_sequence.json")));
    window.show();
    QTest::qWait(20);

    auto* resultView = window.findChild<QTreeView*>(QStringLiteral("resultView"));
    auto* treeModel = window.findChild<SequenceTreeModel*>();
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* delegate = window.findChild<QAbstractItemDelegate*>(
        QStringLiteral("runTestBreakpointDelegate"));
    QVERIFY(resultView);
    QVERIFY(treeModel);
    QVERIFY(viewModel);
    QVERIFY(delegate);

    viewModel->compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    resultView->expandAll();
    QTest::qWait(20);

    const auto setupPhase = resultView->model()->index(
        0, UutStepModel::NameColumn);
    const auto firstStep = resultView->model()->index(
        0, UutStepModel::NameColumn, setupPhase);
    QVERIFY(firstStep.isValid());
    const auto breakpointIndex = firstStep.siblingAtColumn(
        UutStepModel::BreakpointVisualColumn);
    const auto stepRect = resultView->visualRect(breakpointIndex);
    QVERIFY(stepRect.isValid());

    const QPoint breakpointPoint(stepRect.left() + 11,
                                 stepRect.center().y());
    QTest::mouseClick(resultView->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      breakpointPoint);
    QCOMPARE(delegate->property("visualBreakpointCount").toInt(), 1);
    QCOMPARE(treeModel->breakpointSpecs().size(), 1);
    QCOMPARE(treeModel->breakpointSpecs().first().address.value,
             QString("open-fixture"));

    const auto screenshotPath = qEnvironmentVariable(
        "PICOATE_BREAKPOINT_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        QVERIFY2(window.grab().save(screenshotPath), qPrintable(screenshotPath));
    }

    QTest::mouseClick(resultView->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      breakpointPoint);
    QCOMPARE(delegate->property("visualBreakpointCount").toInt(), 0);
    QVERIFY(treeModel->breakpointSpecs().isEmpty());
}

void MainWindowLifecycleTests::productionWindowShowsSkippedStepsAndCleanupAfterFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("failure_sequence.json"));
    QFile sequence(sequencePath);
    QVERIFY(sequence.open(QIODevice::WriteOnly));
    sequence.write(R"({
      "id": "production-failure",
      "name": "Production Failure",
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "steps": [
            {
              "id": "failed-item",
              "name": "Failed Item",
              "kind": "action",
              "parameters": {"outcome": "Failed"}
            },
            {"id": "not-run", "name": "Not Run", "kind": "noop"}
          ]
        },
        {
          "id": "cleanup",
          "kind": "cleanup",
          "steps": [
            {"id": "close-device", "name": "Close Device", "kind": "cleanup"}
          ]
        }
      ]
    })");
    sequence.close();
    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({"stationId":"line-1","scanDialogEnabled":false,"devices":[]})");
    station.close();

    StartupSelection selection;
    selection.mode = UiMode::Test;
    selection.sequencePath = sequencePath;
    selection.stationPath = stationPath;
    selection.scanDialogEnabled = false;
    ProductionWindow window(selection);
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    auto* start = window.findChild<QAction*>(QStringLiteral("productionStartAction"));
    auto* passCount = window.findChild<QLabel*>(QStringLiteral("productionPassCount"));
    auto* failCount = window.findChild<QLabel*>(QStringLiteral("productionFailCount"));
    auto* totalCount = window.findChild<QLabel*>(QStringLiteral("productionTotalCount"));
    auto* yieldChart = window.findChild<QWidget*>(QStringLiteral("productionYieldChart"));
    QVERIFY(viewModel);
    QVERIFY(start);
    QVERIFY(passCount);
    QVERIFY(failCount);
    QVERIFY(totalCount);
    QVERIFY(yieldChart);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    start->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Failed, 3000);

    const auto& report = viewModel->report();
    QCOMPARE(report.uuts.size(), 1);
    const auto& steps = report.uuts.first().steps;
    const auto findStep = [&steps](const QString& id) {
        return std::find_if(steps.cbegin(), steps.cend(), [&id](const auto& step) {
            return step.stepId == id;
        });
    };
    const auto failed = findStep(QStringLiteral("failed-item"));
    const auto skipped = findStep(QStringLiteral("not-run"));
    const auto cleanup = std::find_if(
        report.sessionSteps.cbegin(), report.sessionSteps.cend(), [](const auto& step) {
            return step.stepId == QStringLiteral("close-device");
        });
    QVERIFY(failed != steps.cend());
    QVERIFY(skipped != steps.cend());
    QVERIFY(cleanup != report.sessionSteps.cend());
    QCOMPARE(failed->state, PicoATE::Core::ActivationState::Failed);
    QCOMPARE(skipped->state, PicoATE::Core::ActivationState::Skipped);
    QCOMPARE(cleanup->state, PicoATE::Core::ActivationState::Passed);
    QCOMPARE(passCount->text(), QStringLiteral("PASS 0"));
    QCOMPARE(failCount->text(), QStringLiteral("FAIL 1"));
    QCOMPARE(totalCount->text(), QStringLiteral("TOTAL 1"));
    QCOMPARE(yieldChart->property("passedCount").toInt(), 0);
    QCOMPARE(yieldChart->property("failedCount").toInt(), 1);
    QCOMPARE(yieldChart->property("yieldPercent").toDouble(), 0.0);
}

void MainWindowLifecycleTests::promptCountdownTracksRequestAndModes()
{
    using namespace PicoATE::Core;
    const bool wasChinese = UiLanguage::instance().isChinese();
    const auto restore = qScopeGuard([&] { UiLanguage::instance().setChinese(wasChinese, false); });
    QVERIFY(UiLanguage::instance().setChinese(false, false));
    RuntimeEvent event;
    event.kind = RuntimeEventKind::OperatorPromptRequested;
    event.details = {{"promptInstanceId", "timed"}, {"mode", "confirm"}, {"timeoutMs", 3000}};
    PromptCountdownWidget widget;
    widget.resize(400, 40);
    widget.show();
    auto* progress = widget.findChild<QProgressBar*>("promptTimeoutProgress");
    auto* label = widget.findChild<QLabel*>("promptTimeoutLabel");
    QVERIFY(progress && label);
    QTRY_COMPARE(progress->height(), 7);
    // Cold Debug widget creation can consume the whole short test timeout.
    event.timestampUtc = QDateTime::currentDateTimeUtc().addMSecs(-1500);
    widget.configure(PromptCountdownWidget::withTiming(event));
    QVERIFY(widget.remainingMs() > 800 && widget.remainingMs() <= 1500);
    const auto before = widget.remainingMs();
    const auto beforeValue = progress->value();
    QTest::qWait(160);
    QVERIFY(widget.remainingMs() < before);
    QVERIFY(progress->value() < beforeValue);
    QVERIFY(UiLanguage::instance().setChinese(true, false));
    QVERIFY(label->text().contains(QStringLiteral("剩余")));
    QVERIFY(widget.remainingMs() < before);
    widget.setResponsePending(true);
    const auto frozen = widget.remainingMs();
    QTest::qWait(120);
    QCOMPARE(widget.remainingMs(), frozen);
    QVERIFY(label->text().contains(QStringLiteral("已提交")));
    event.details["promptInstanceId"] = "unlimited";
    event.details["timeoutMs"] = 0;
    widget.configure(event);
    QCOMPARE(widget.remainingMs(), qint64(-1));
    QCOMPARE(label->text(), QStringLiteral("无超时限制"));
    event.details["promptInstanceId"] = "condition-notice";
    event.details["mode"] = "notice";
    event.details["timeoutMs"] = 60000;
    widget.configure(event);
    QCOMPARE(widget.remainingMs(), qint64(-1));
    QCOMPARE(label->text(), QStringLiteral("后续自动关闭"));
    event.details["promptInstanceId"] = "expired";
    event.details["mode"] = "judgment";
    event.details["timeoutMs"] = 100;
    widget.configure(event);
    QCOMPARE(widget.remainingMs(), qint64(0));
    QCOMPARE(progress->value(), 0);
    QVERIFY(widget.isVisible());
    QCOMPARE(label->text(), QStringLiteral("等待超时处理"));
}

void MainWindowLifecycleTests::promptCountdownSurvivesRehostingAndRebuilds()
{
    using namespace PicoATE::Core;
    QWidget owner;
    auto* layout = new QVBoxLayout(&owner);
    auto* stack = new QStackedWidget(&owner);
    auto* details = new QWidget(stack);
    auto* overview = new MultiUutOverviewWidget(stack);
    auto* model = new UutOverviewModel(&owner);
    stack->addWidget(details);
    stack->addWidget(overview);
    layout->addWidget(stack);
    overview->setModel(model);
    QVector<RunRequest::UutInput> uuts;
    for (int i = 1; i <= 2; ++i) {
        RunRequest::UutInput uut;
        uut.uutId = QString("UUT-%1").arg(i);
        uuts.push_back(uut);
    }
    model->resetForRun({}, uuts);
    owner.resize(1100, 680);
    owner.show();
    ExecutionViewModel viewModel;
    OperatorPromptPresenter presenter(&viewModel, &owner);
    presenter.setOverviewHost(overview);
    RuntimeEvent event;
    event.kind = RuntimeEventKind::OperatorPromptRequested;
    event.timestampUtc = QDateTime::currentDateTimeUtc().addMSecs(-15000);
    event.uutId = "UUT-1";
    event.nodeId = "input";
    event.details = {{"promptInstanceId", "input-1"}, {"mode", "input"}, {"timeoutMs", 120000},
        {"message", "Enter the measured voltage"}, {"inputType", "number"}};
    presenter.applyRuntimeEvents({event});
    auto* dialog = owner.findChild<QDialog*>("operatorPromptDialog");
    QVERIFY(dialog);
    auto* countdown = dialog->findChild<PromptCountdownWidget*>();
    QVERIFY(countdown);
    QVERIFY(countdown->remainingMs() <= 105000);
    dialog->findChild<QLineEdit*>("operatorPromptInput")->setText("13.75");
    const auto screenshotDir = qEnvironmentVariable("PICOATE_PROMPT_COUNTDOWN_SCREENSHOTS");
    if (!screenshotDir.isEmpty()) {
        QTest::qWait(50);
        QVERIFY(dialog->grab().save(QDir(screenshotDir).filePath("dialog-countdown.png")));
    }
    QTest::qWait(130);
    const auto before = countdown->remainingMs();
    event.timestampUtc = QDateTime::currentDateTimeUtc();
    presenter.applyRuntimeEvents({event});
    QVERIFY(countdown->remainingMs() <= before);
    stack->setCurrentWidget(overview);
    presenter.rehostActivePromptsInOverview();
    QTRY_VERIFY(overview->isVisible());
    auto* card = owner.findChild<QAbstractButton*>("uutOverviewCard_1");
    QVERIFY(card);
    auto* hosted = card->findChild<PromptCountdownWidget*>();
    QVERIFY(hosted && hosted->isVisible());
    QVERIFY(hosted->remainingMs() <= before);
    QCOMPARE(card->findChild<QLineEdit*>("uutOverviewPromptInput")->text(), QString("13.75"));
    const auto beforeRebuild = hosted->remainingMs();
    QPointer<QAbstractButton> oldCard(card);
    model->resetForRun({}, uuts);
    QTRY_VERIFY(oldCard.isNull());
    card = owner.findChild<QAbstractButton*>("uutOverviewCard_1");
    QVERIFY(card);
    hosted = card->findChild<PromptCountdownWidget*>();
    QVERIFY(hosted && hosted->isVisible());
    QVERIFY(hosted->remainingMs() <= beforeRebuild);
    stack->setCurrentWidget(details);
    QTest::qWait(150);
    stack->setCurrentWidget(overview);
    presenter.rehostActivePromptsInOverview();
    QVERIFY(hosted->remainingMs() <= beforeRebuild - 100);
    if (!screenshotDir.isEmpty()) {
        QTest::qWait(50);
        QVERIFY(owner.grab().save(QDir(screenshotDir).filePath("card-countdown.png")));
    }
    presenter.closeAll();
    QVERIFY(!hosted->isVisible());
    viewModel.shutdown();
}

void MainWindowLifecycleTests::sharedPromptHasCompactCountdown()
{
    using namespace PicoATE::Core;
    QWidget owner;
    auto* layout = new QVBoxLayout(&owner);
    auto* overview = new MultiUutOverviewWidget(&owner);
    auto* model = new UutOverviewModel(&owner);
    overview->setModel(model);
    layout->addWidget(overview);
    QVector<RunRequest::UutInput> uuts;
    for (int i = 1; i <= 4; ++i) {
        RunRequest::UutInput uut;
        uut.uutId = QString("UUT-%1").arg(i);
        uuts.push_back(uut);
    }
    model->resetForRun({}, uuts);
    owner.resize(1100, 760);
    owner.show();
    QTest::qWait(30);
    RuntimeEvent event;
    event.kind = RuntimeEventKind::OperatorPromptRequested;
    event.timestampUtc = QDateTime::currentDateTimeUtc().addMSecs(-5000);
    event.uutId = "UUT-1";
    event.details = {{"promptInstanceId", "batch-judgment"}, {"mode", "judgment"}, {"timeoutMs", 30000},
        {"executionScope", "oncePerBatch"}, {"message", "Confirm the indicators on all UUTs"}};
    QVERIFY(overview->presentOperatorPrompt(event, {}));
    auto* overlay = owner.findChild<QWidget*>("multiUutOverviewPromptOverlay");
    QVERIFY(overlay);
    auto* countdown = overlay->findChild<PromptCountdownWidget*>();
    auto* pass = overlay->findChild<QPushButton*>("uutOverviewPromptPassButton");
    QVERIFY(countdown && pass);
    QVERIFY(countdown->remainingMs() > 22000 && countdown->remainingMs() <= 25000);
    QTRY_VERIFY(countdown->geometry().bottom() < pass->geometry().top());
    QVERIFY(!countdown->findChild<QProgressBar*>()->isVisible());
    const auto clock = countdown->findChild<QLabel*>("promptTimeoutIcon")->pixmap().toImage();
    QVERIFY(clock.hasAlphaChannel());
    QCOMPARE(clock.pixelColor(0, 0).alpha(), 0);
    const auto before = countdown->remainingMs();
    QTest::qWait(150);
    event.timestampUtc = QDateTime::currentDateTimeUtc();
    QVERIFY(overview->presentOperatorPrompt(event, {}));
    QVERIFY(countdown->remainingMs() < before);
    const auto screenshotDir = qEnvironmentVariable("PICOATE_PROMPT_COUNTDOWN_SCREENSHOTS");
    if (!screenshotDir.isEmpty()) {
        QVERIFY(owner.grab().save(QDir(screenshotDir).filePath("shared-countdown.png")));
    }
    QVERIFY(overview->closeOperatorPrompt("batch-judgment"));
    QVERIFY(!countdown->isVisible());
}

void MainWindowLifecycleTests::compactPromptPanelsPreserveStateAndBounds()
{
    using namespace PicoATE::Core;
    MultiUutOverviewWidget overview;
    UutOverviewModel model;
    overview.setModel(&model);
    QVector<RunRequest::UutInput> uuts;
    for (int i = 1; i <= 4; ++i) {
        RunRequest::UutInput uut;
        uut.uutId = QString("UUT-%1").arg(i);
        uuts.push_back(uut);
    }
    model.resetForRun({}, uuts);
    overview.resize(1100, 740);
    overview.show();
    QTest::qWait(30);
    RuntimeEvent event;
    event.kind = RuntimeEventKind::OperatorPromptRequested;
    event.timestampUtc = QDateTime::currentDateTimeUtc();
    event.uutId = "UUT-2";
    event.details = {{"promptInstanceId", "compact-input"}, {"mode", "input"},
        {"timeoutMs", 60000}, {"inputType", "number"}, {"message", "Enter measured voltage"}};
    QVERIFY(overview.presentOperatorPrompt(event, {}));
    auto* card = overview.findChild<QAbstractButton*>("uutOverviewCard_2");
    QVERIFY(card);
    auto* mask = card->findChild<QWidget*>("uutOverviewPromptOverlay");
    auto* panel = mask->findChild<QFrame*>("uutOverviewPromptPanel");
    QTRY_VERIFY(mask->rect().contains(panel->geometry()));
    QTRY_VERIFY(qAbs(panel->geometry().center().x() - mask->rect().center().x()) <= 1);
    QTRY_VERIFY(qAbs(panel->geometry().center().y() - mask->rect().center().y()) <= 1);
    QVERIFY(panel->width() <= 360);
    QVERIFY(panel->height() < mask->height() - 20);
    QSignalSpy selected(&overview, &MultiUutOverviewWidget::uutActivated);
    QSignalSpy responses(&overview, &MultiUutOverviewWidget::operatorPromptResponseRequested);
    QTest::mouseClick(mask, Qt::LeftButton, Qt::NoModifier, QPoint(6, 6));
    QCOMPARE(selected.count(), 0);
    QCOMPARE(responses.count(), 0);
    card->findChild<QLineEdit*>("uutOverviewPromptInput")->setText("13.82");
    QPointer<QAbstractButton> oldCard(card);
    model.resetForRun({}, uuts);
    QTRY_VERIFY(oldCard.isNull());
    card = overview.findChild<QAbstractButton*>("uutOverviewCard_2");
    QCOMPARE(card->findChild<QLineEdit*>("uutOverviewPromptInput")->text(), QString("13.82"));
    QVERIFY(overview.setOperatorPromptResponsePending("compact-input", true));
    oldCard = card;
    model.resetForRun({}, uuts);
    QTRY_VERIFY(oldCard.isNull());
    card = overview.findChild<QAbstractButton*>("uutOverviewCard_2");
    QVERIFY(!card->findChild<QLineEdit*>("uutOverviewPromptInput")->isEnabled());
    QVERIFY(!card->findChild<QPushButton*>("uutOverviewPromptConfirmButton")->isEnabled());
    QVERIFY(overview.closeOperatorPrompt("compact-input"));

    event.details = {{"promptInstanceId", "long-shared"}, {"mode", "input"},
        {"timeoutMs", 60000}, {"executionScope", "oncePerBatch"},
        {"message", QString("Long operator instruction with all required details. ").repeated(25)}};
    QVERIFY(overview.presentOperatorPrompt(event, {}));
    overview.resize(780, 520);
    mask = overview.findChild<QWidget*>("multiUutOverviewPromptOverlay");
    panel = mask->findChild<QFrame*>("uutOverviewPromptPanel");
    auto* scroll = panel->findChild<QScrollArea*>("uutOverviewPromptScroll");
    auto* confirm = panel->findChild<QPushButton*>("uutOverviewPromptConfirmButton");
    QTRY_VERIFY(mask->rect().contains(panel->geometry()));
    QTRY_VERIFY(panel->rect().contains(confirm->geometry()));
    QTRY_VERIFY(qAbs(panel->geometry().center().y() - mask->rect().center().y()) <= 1);
    QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
    panel->findChild<QLineEdit*>("uutOverviewPromptInput")->setText("batch draft");
    model.resetForRun({}, uuts);
    QTest::qWait(30);
    QCOMPARE(panel->findChild<QLineEdit*>("uutOverviewPromptInput")->text(), QString("batch draft"));
    QVERIFY(overview.setOperatorPromptResponsePending("long-shared", true));
    model.resetForRun({}, uuts);
    QTest::qWait(30);
    QVERIFY(!confirm->isEnabled());
    QVERIFY(overview.closeOperatorPrompt("long-shared"));

    auto& language = UiLanguage::instance();
    const auto restoreLanguage = qScopeGuard([&] { language.setChinese(false, false); });
    QVERIFY(language.setChinese(true, false));
    overview.resize(1100, 740);
    QTest::qWait(30);
    const QStringList modes{"judgment", "notice", "input", "confirm"};
    const QStringList messages{QStringLiteral("请确认终端风扇是否正常启动"),
        QStringLiteral("请保持门禁开启，等待后续测试完成"),
        QStringLiteral("请输入当前测得的输出电压"), QStringLiteral("请连接测试线缆并确认")};
    for (int i = 0; i < modes.size(); ++i) {
        event.uutId = QString("UUT-%1").arg(i + 1);
        event.timestampUtc = QDateTime::currentDateTimeUtc();
        event.details = {{"promptInstanceId", QString("compact-preview-%1").arg(i)},
            {"mode", modes[i]}, {"timeoutMs", 30000}, {"message", messages[i]}};
        QVERIFY(overview.presentOperatorPrompt(event, {}));
    }
    QTest::qWait(100);
    const auto screenshotDir = qEnvironmentVariable("PICOATE_PROMPT_COUNTDOWN_SCREENSHOTS");
    if (!screenshotDir.isEmpty()) {
        QVERIFY(overview.grab().save(QDir(screenshotDir).filePath("four-uuts-compact-zh.png")));
    }
    for (int i = 1; i <= 4; ++i) {
        card = overview.findChild<QAbstractButton*>(QString("uutOverviewCard_%1").arg(i));
        mask = card->findChild<QWidget*>("uutOverviewPromptOverlay");
        panel = mask->findChild<QFrame*>("uutOverviewPromptPanel");
        QVERIFY(mask->rect().contains(panel->geometry()));
        auto* text = panel->findChild<QLabel*>("uutOverviewPromptMessage");
        QVERIFY(text->height() >= text->fontMetrics().height());
    }
    card = overview.findChild<QAbstractButton*>("uutOverviewCard_2");
    QCOMPARE(card->findChild<QLabel*>("promptTimeoutLabel")->text(), QStringLiteral("后续自动关闭"));

    auto* retiringOverview = new MultiUutOverviewWidget;
    retiringOverview->setModel(&model);
    retiringOverview->resize(900, 640);
    retiringOverview->show();
    event.details = {{"promptInstanceId", "teardown-input"}, {"mode", "input"},
        {"executionScope", "oncePerBatch"}, {"message", "Input during teardown"}};
    QVERIFY(retiringOverview->presentOperatorPrompt(event, {}));
    QPointer<QLineEdit> retiringInput = retiringOverview->findChild<QLineEdit*>("uutOverviewPromptInput");
    QVERIFY(retiringInput);
    QObject observer;
    bool teardownEditDelivered = false;
    connect(retiringOverview, &QObject::destroyed, &observer, [&] {
        if (retiringInput) {
            teardownEditDelivered = true;
            retiringInput->setText("Late input-method commit");
        }
    });
    delete retiringOverview;
    QVERIFY(teardownEditDelivered);
    QVERIFY(retiringInput.isNull());
}

void MainWindowLifecycleTests::singleUutNavigationStaysVisible_data()
{
    QTest::addColumn<bool>("production");
    QTest::newRow("admin") << false;
    QTest::newRow("test") << true;
}

void MainWindowLifecycleTests::singleUutNavigationStaysVisible()
{
    QFETCH(bool, production);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto sequencePath = dir.filePath("sequence.json");
    QFile sequence(sequencePath);
    QVERIFY(sequence.open(QIODevice::WriteOnly));
    sequence.write(R"({"id":"single-nav","name":"Single UUT Navigation","groups":[{"id":"main","kind":"main","steps":[{"id":"done","kind":"noop"}]}]})");
    sequence.close();
    const auto stationPath = dir.filePath("StationSystem.json");
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({"stationId":"NAV","uutCount":2,"scanDialogEnabled":false,"devices":[]})");
    station.close();
    std::unique_ptr<QWidget> window;
    if (production) {
        StartupSelection selection;
        selection.sequencePath = sequencePath;
        selection.stationPath = stationPath;
        selection.scanDialogEnabled = false;
        window = createProductionWindow(selection);
    } else {
        auto admin = createMainWindow();
        QVERIFY(admin->openStationFile(stationPath));
        QVERIFY(admin->openSequenceFile(sequencePath));
        admin->findChild<QAction*>("compileAction")->trigger();
        admin->showRunPage();
        window = std::move(admin);
    }
    window->resize(1200, 820);
    window->show();
    auto* model = window->findChild<ExecutionViewModel*>();
    QVERIFY(model);
    QTRY_COMPARE(model->state(), UiRunState::Ready);
    auto* count = window->findChild<QSpinBox*>(production ? "productionUutCountSpinBox" : "uutCountSpinBox");
    auto* navigation = window->findChild<QWidget*>(production ? "productionUutNavigation" : "adminUutDetailNavigation");
    auto* overviewButton = window->findChild<QPushButton*>(production ? "productionOverviewButton" : "adminBackToUutOverview");
    auto* overviewModel = window->findChild<UutOverviewModel*>();
    auto* stack = window->findChild<QStackedWidget*>(production ? "productionRunStack" : "adminRunStack");
    auto* overviewPage = window->findChild<QWidget*>(production ? "productionRunOverviewPage" : "adminRunOverviewPage");
    auto* detailsPage = window->findChild<QWidget*>(production ? "productionRunDetailPage" : "adminRunDetailPage");
    QVERIFY(count && navigation && overviewButton && overviewModel && stack && overviewPage && detailsPage);
    QVERIFY(navigation->isVisible());
    const auto navigationHeight = navigation->height();
    for (int iteration = 0; iteration < 2; ++iteration) {
        count->setValue(1);
        QTRY_COMPARE(overviewModel->rowCount(), 1);
        QTRY_VERIFY(navigation->isVisible());
        QTRY_VERIFY(overviewButton->isVisible());
        QTRY_VERIFY(!overviewButton->isEnabled());
        QTRY_COMPARE(stack->currentWidget(), detailsPage);
        QTRY_COMPARE(overviewButton->palette().color(QPalette::Disabled, QPalette::ButtonText),
                     QColor(QStringLiteral("#a0a8ae")));
        const auto screenshotDir = qEnvironmentVariable("PICOATE_RELEASE_SCREENSHOTS");
        if (iteration == 0 && !screenshotDir.isEmpty()) {
            QVERIFY(window->grab().save(QDir(screenshotDir).filePath(production
                ? "test-single-uut.png" : "admin-single-uut.png")));
        }
        QCOMPARE(navigation->height(), navigationHeight);
        overviewButton->click();
        QTRY_COMPARE(stack->currentWidget(), detailsPage);
        QVERIFY(!overviewButton->isChecked());
        auto* group = window->findChild<QButtonGroup*>(production ? "productionUutNavigationGroup" : "adminUutNavigationGroup");
        QVERIFY(group && group->buttons().size() == 1);
        group->buttons().first()->click();
        QTRY_COMPARE(stack->currentWidget(), detailsPage);
        QVERIFY(navigation->isVisible());
        count->setValue(2);
        QTRY_COMPARE(overviewModel->rowCount(), 2);
        QVERIFY(navigation->isVisible());
        QTRY_VERIFY(overviewButton->isEnabled());
        overviewButton->click();
        QTRY_COMPARE(stack->currentWidget(), overviewPage);
        QVERIFY(overviewButton->isChecked());
    }
    count->setValue(1);
    QTRY_VERIFY(!overviewButton->isEnabled());
    auto* run = window->findChild<QAction*>(production ? "productionStartAction" : "runAction");
    QVERIFY(run);
    run->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(model->report().completed, 10000);
    QTRY_COMPARE(stack->currentWidget(), detailsPage);
    QVERIFY(overviewButton->isVisible());
    QVERIFY(!overviewButton->isEnabled());
}

void MainWindowLifecycleTests::operatorPromptDialogCannotBeDismissedByKeyboardOrWindowControls()
{
    QTemporaryDir project;
    QVERIFY(project.isValid());
    const auto sequencePath = project.filePath(QStringLiteral("sequence.json"));
    const auto imageDirectory = project.filePath(QStringLiteral("images"));
    QVERIFY(QDir().mkpath(imageDirectory));
    const auto imagePath = QDir(imageDirectory).filePath(
        QStringLiteral("picoate_prompt_presenter_test.png"));
    QPixmap sourceImage(80, 40);
    sourceImage.fill(QColor(QStringLiteral("#2f7ed8")));
    QVERIFY(sourceImage.save(imagePath, "PNG"));

    QWidget owner;
    owner.show();
    ExecutionViewModel viewModel;
    OperatorPromptPresenter presenter(&viewModel, &owner);
    presenter.setSequencePath(sequencePath);

    PicoATE::Core::RuntimeEvent requested;
    requested.kind = PicoATE::Core::RuntimeEventKind::OperatorPromptRequested;
    requested.message = QStringLiteral("Press the product button");
    requested.details = {
        {QStringLiteral("promptInstanceId"), QStringLiteral("prompt-1")},
        {QStringLiteral("mode"), QStringLiteral("confirm")},
        {QStringLiteral("title"), QStringLiteral("Operator Action")},
        {QStringLiteral("message"), QStringLiteral("Press the product button")},
        {QStringLiteral("image"), QStringLiteral("picoate_prompt_presenter_test.png")},
        {QStringLiteral("confirmText"), QStringLiteral("Continue")},
    };
    presenter.applyRuntimeEvents({requested});

    auto* dialog = owner.findChild<QDialog*>(QStringLiteral("operatorPromptDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    QVERIFY(!dialog->windowFlags().testFlag(Qt::WindowCloseButtonHint));
    QVERIFY(!dialog->windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
    QVERIFY(!dialog->windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
    auto* image = dialog->findChild<QLabel*>(
        QStringLiteral("operatorPromptImage"));
    QVERIFY(image);
    QVERIFY(!image->pixmap(Qt::ReturnByValue).isNull());

    QTest::keyClick(dialog, Qt::Key_Return);
    QVERIFY(dialog->isVisible());
    QTest::keyClick(dialog, Qt::Key_Escape);
    QVERIFY(dialog->isVisible());

    PicoATE::Core::RuntimeEvent closed;
    closed.kind = PicoATE::Core::RuntimeEventKind::OperatorPromptClosed;
    closed.details.insert(QStringLiteral("promptInstanceId"),
                          QStringLiteral("prompt-1"));
    presenter.applyRuntimeEvents({closed});
    QTRY_VERIFY(!dialog->isVisible());
    viewModel.shutdown();
    QVERIFY(QFile::remove(imagePath));
}

void MainWindowLifecycleTests::operatorPromptDialogValidatesInputMode()
{
    QWidget owner;
    owner.show();
    ExecutionViewModel viewModel;
    OperatorPromptPresenter presenter(&viewModel, &owner);

    PicoATE::Core::RuntimeEvent requested;
    requested.kind = PicoATE::Core::RuntimeEventKind::OperatorPromptRequested;
    requested.message = QStringLiteral("Enter measured voltage");
    requested.details = {
        {QStringLiteral("promptInstanceId"), QStringLiteral("input-1")},
        {QStringLiteral("mode"), QStringLiteral("input")},
        {QStringLiteral("title"), QStringLiteral("Voltage")},
        {QStringLiteral("message"), QStringLiteral("Enter measured voltage")},
        {QStringLiteral("inputType"), QStringLiteral("number")},
        {QStringLiteral("inputPlaceholder"), QStringLiteral("Example: 12.5")},
        {QStringLiteral("defaultValue"), 12.5},
        {QStringLiteral("confirmText"), QStringLiteral("Submit")},
    };
    presenter.applyRuntimeEvents({requested});

    auto* dialog = owner.findChild<QDialog*>(QStringLiteral("operatorPromptDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    auto* input = dialog->findChild<QLineEdit*>(QStringLiteral("operatorPromptInput"));
    auto* error = dialog->findChild<QLabel*>(QStringLiteral("operatorPromptInputError"));
    auto* submit = dialog->findChild<QPushButton*>(
        QStringLiteral("operatorPromptConfirmButton"));
    QVERIFY(input && error && submit);
    QVERIFY(!input->isHidden());
    QCOMPARE(input->placeholderText(), QStringLiteral("Example: 12.5"));
    QCOMPARE(input->text(), QStringLiteral("12.5"));
    QCOMPARE(submit->text(), QStringLiteral("Submit"));

    input->setText(QStringLiteral("not-a-number"));
    submit->click();
    QVERIFY(!error->isHidden());
    QVERIFY(error->text().contains(QStringLiteral("valid number")));
    QCOMPARE(input->property("invalid").toBool(), true);

    input->setText(QStringLiteral("13.75"));
    submit->click();
    QVERIFY(error->isHidden());
    QCOMPARE(input->property("invalid").toBool(), false);

    presenter.closeAll();
    viewModel.shutdown();
}

void MainWindowLifecycleTests::projectImagePathsStayInsideCurrentProject()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto projectOne = root.filePath(QStringLiteral("projects/ProductOne"));
    const auto projectTwo = root.filePath(QStringLiteral("projects/ProductTwo"));
    QVERIFY(QDir().mkpath(QDir(projectOne).filePath(QStringLiteral("images"))));
    QVERIFY(QDir().mkpath(QDir(projectTwo).filePath(QStringLiteral("images"))));

    const auto sequenceOne = QDir(projectOne).filePath(QStringLiteral("sequence.json"));
    const auto sequenceTwo = QDir(projectTwo).filePath(QStringLiteral("sequence.json"));
    const auto imageOne = QDir(projectOne).filePath(
        QStringLiteral("images/instruction.png"));
    const auto imageTwo = QDir(projectTwo).filePath(
        QStringLiteral("images/instruction.png"));
    QPixmap blue(16, 16);
    blue.fill(Qt::blue);
    QVERIFY(blue.save(imageOne, "PNG"));
    QPixmap red(16, 16);
    red.fill(Qt::red);
    QVERIFY(red.save(imageTwo, "PNG"));

    QCOMPARE(ProjectResourcePaths::imagesDirectoryForSequence(sequenceOne),
             QFileInfo(QDir(projectOne).filePath(QStringLiteral("images")))
                 .absoluteFilePath());
    QCOMPARE(ProjectResourcePaths::resolveImage(
                 sequenceOne, QStringLiteral("instruction.png")),
             QFileInfo(imageOne).absoluteFilePath());
    QCOMPARE(ProjectResourcePaths::resolveImage(
                 sequenceTwo, QStringLiteral("images/instruction.png")),
             QFileInfo(imageTwo).absoluteFilePath());
    QCOMPARE(ProjectResourcePaths::resolveImage(
                 sequenceOne, QStringLiteral("image/instruction.png")),
             QFileInfo(imageOne).absoluteFilePath());
    QVERIFY(ProjectResourcePaths::resolveImage(
                sequenceOne, QStringLiteral("../ProductTwo/images/instruction.png"))
                .isEmpty());
}

void MainWindowLifecycleTests::operatorPromptDialogReusesKeyForJudgment()
{
    QWidget owner;
    owner.show();
    ExecutionViewModel viewModel;
    OperatorPromptPresenter presenter(&viewModel, &owner);

    PicoATE::Core::RuntimeEvent notice;
    notice.kind = PicoATE::Core::RuntimeEventKind::OperatorPromptRequested;
    notice.uutId = QStringLiteral("UUT-1");
    notice.details = {
        {QStringLiteral("promptInstanceId"), QStringLiteral("notice-1")},
        {QStringLiteral("dialogKey"), QStringLiteral("rgb-lamp")},
        {QStringLiteral("mode"), QStringLiteral("notice")},
        {QStringLiteral("title"), QStringLiteral("RGB Lamp")},
        {QStringLiteral("message"), QStringLiteral("Observe the lamp cycle")},
    };
    presenter.applyRuntimeEvents({notice});

    auto dialogs = owner.findChildren<QDialog*>(
        QStringLiteral("operatorPromptDialog"));
    QCOMPARE(dialogs.size(), 1);
    auto* dialog = dialogs.first();
    QTRY_VERIFY(dialog->isVisible());

    PicoATE::Core::RuntimeEvent judgment = notice;
    judgment.details.insert(QStringLiteral("promptInstanceId"),
                            QStringLiteral("judgment-1"));
    judgment.details.insert(QStringLiteral("mode"), QStringLiteral("judgment"));
    judgment.details.insert(QStringLiteral("message"),
                            QStringLiteral("Did all three colors illuminate?"));
    judgment.details.insert(QStringLiteral("passText"), QStringLiteral("Looks Good"));
    judgment.details.insert(QStringLiteral("failText"), QStringLiteral("Fault Found"));
    presenter.applyRuntimeEvents({judgment});

    dialogs = owner.findChildren<QDialog*>(QStringLiteral("operatorPromptDialog"));
    QCOMPARE(dialogs.size(), 1);
    QCOMPARE(dialogs.first(), dialog);
    QPointer<QDialog> guardedDialog(dialog);
    QCOMPARE(dialog->findChild<QLabel*>(QStringLiteral("operatorPromptMessage"))->text(),
             QStringLiteral("Did all three colors illuminate?"));
    auto* pass = dialog->findChild<QPushButton*>(
        QStringLiteral("operatorPromptPassButton"));
    auto* fail = dialog->findChild<QPushButton*>(
        QStringLiteral("operatorPromptFailButton"));
    QVERIFY(pass && fail);
    QVERIFY(!pass->isHidden());
    QVERIFY(!fail->isHidden());
    QCOMPARE(pass->text(), QStringLiteral("Looks Good"));
    QCOMPARE(fail->text(), QStringLiteral("Fault Found"));

    auto* countdown = dialog->findChild<PromptCountdownWidget*>();
    auto* hint = dialog->findChild<QLabel*>(QStringLiteral("operatorPromptWaitingLabel"));
    QVERIFY(countdown && hint);
    QVERIFY(!hint->isVisible());
    auto* message = dialog->findChild<QLabel*>(QStringLiteral("operatorPromptMessage"));
    QTRY_VERIFY(countdown->geometry().bottom() < message->geometry().top());
    auto* timeoutText = countdown->findChild<QLabel*>(QStringLiteral("promptTimeoutLabel"));
    QTRY_VERIFY(timeoutText->contentsRect().width() >= timeoutText->fontMetrics().horizontalAdvance(timeoutText->text()));
    auto* clockIcon = countdown->findChild<QLabel*>(QStringLiteral("promptTimeoutIcon"));
    QTRY_VERIFY(timeoutText->geometry().left() - clockIcon->geometry().right() <= 8);
    QTRY_VERIFY(countdown->geometry().bottom() < pass->geometry().top());
    QVERIFY(countdown->geometry().bottom() < fail->geometry().top());
    const auto screenshotDir = qEnvironmentVariable("PICOATE_PROMPT_COUNTDOWN_SCREENSHOTS");
    if (!screenshotDir.isEmpty()) {
        QVERIFY(dialog->grab().save(QDir(screenshotDir).filePath("judgment-compact.png")));
    }

    PicoATE::Core::RuntimeEvent oldNoticeClosed;
    oldNoticeClosed.kind = PicoATE::Core::RuntimeEventKind::OperatorPromptClosed;
    oldNoticeClosed.details.insert(QStringLiteral("promptInstanceId"),
                                   QStringLiteral("notice-1"));
    presenter.applyRuntimeEvents({oldNoticeClosed});
    QVERIFY(dialog->isVisible());

    PicoATE::Core::RuntimeEvent judgmentClosed = oldNoticeClosed;
    judgmentClosed.details.insert(QStringLiteral("promptInstanceId"),
                                  QStringLiteral("judgment-1"));
    presenter.applyRuntimeEvents({judgmentClosed});
    QTRY_VERIFY(guardedDialog.isNull() || !guardedDialog->isVisible());
    viewModel.shutdown();
}

void MainWindowLifecycleTests::operatorPromptsUseTheirMatchingOverviewCards()
{
    QWidget owner;
    auto* layout = new QVBoxLayout(&owner);
    auto* overview = new MultiUutOverviewWidget(&owner);
    auto* model = new UutOverviewModel(&owner);
    layout->addWidget(overview);
    overview->setModel(model);

    QVector<RunRequest::UutInput> uuts;
    for (int index = 1; index <= 2; ++index) {
        RunRequest::UutInput input;
        input.uutId = QStringLiteral("UUT-%1").arg(index);
        input.variables.insert(
            QStringLiteral("serialNumber"),
            QStringLiteral("BTSN00000%1").arg(index));
        uuts.push_back(std::move(input));
    }
    model->resetForRun({}, uuts);
    owner.resize(980, 520);
    owner.show();
    QTRY_VERIFY(overview->isVisible());

    ExecutionViewModel viewModel;
    OperatorPromptPresenter presenter(&viewModel, &owner);
    presenter.setOverviewHost(overview);
    QString responseInstanceId;
    PicoATE::Core::OperatorPromptResponse response =
        PicoATE::Core::OperatorPromptResponse::None;
    QVariantMap responseValues;
    connect(overview,
            &MultiUutOverviewWidget::operatorPromptResponseRequested,
            &owner,
            [&](const QString& instanceId,
                PicoATE::Core::OperatorPromptResponse nextResponse,
                const QVariantMap& values) {
                responseInstanceId = instanceId;
                response = nextResponse;
                responseValues = values;
            });

    PicoATE::Core::RuntimeEvent judgment;
    judgment.kind = PicoATE::Core::RuntimeEventKind::OperatorPromptRequested;
    judgment.uutId = QStringLiteral("UUT-2");
    judgment.details = {
        {QStringLiteral("promptInstanceId"), QStringLiteral("uut-2-judge")},
        {QStringLiteral("mode"), QStringLiteral("judgment")},
        {QStringLiteral("title"), QStringLiteral("Indicator Check")},
        {QStringLiteral("message"), QStringLiteral("Is the indicator green?")},
        {QStringLiteral("passText"), QStringLiteral("PASS")},
        {QStringLiteral("failText"), QStringLiteral("FAIL")},
    };
    presenter.applyRuntimeEvents({judgment});

    auto* firstCard = owner.findChild<QAbstractButton*>(
        QStringLiteral("uutOverviewCard_1"));
    auto* secondCard = owner.findChild<QAbstractButton*>(
        QStringLiteral("uutOverviewCard_2"));
    QVERIFY(firstCard && secondCard);
    QVERIFY(!firstCard->findChild<QWidget*>(
        QStringLiteral("uutOverviewPromptOverlay")));
    auto* secondOverlay = secondCard->findChild<QWidget*>(
        QStringLiteral("uutOverviewPromptOverlay"));
    QVERIFY(secondOverlay);
    QTRY_VERIFY(secondOverlay->isVisible());
    QCOMPARE(secondOverlay->findChild<QLabel*>(
                 QStringLiteral("uutOverviewPromptContext"))->text(),
             QStringLiteral("UUT-2  |  SN BTSN000002"));
    QCOMPARE(secondOverlay->findChild<QLabel*>(
                 QStringLiteral("uutOverviewPromptMessage"))->text(),
             QStringLiteral("Is the indicator green?"));
    auto* passButton = secondOverlay->findChild<QPushButton*>(
        QStringLiteral("uutOverviewPromptPassButton"));
    QVERIFY(passButton->isVisible());
    QVERIFY(secondOverlay->findChild<QPushButton*>(
        QStringLiteral("uutOverviewPromptFailButton"))->isVisible());
    QVERIFY(owner.findChildren<QDialog*>(
        QStringLiteral("operatorPromptDialog")).isEmpty());
    passButton->click();
    QCOMPARE(responseInstanceId, QStringLiteral("uut-2-judge"));
    QCOMPARE(response, PicoATE::Core::OperatorPromptResponse::Passed);
    QVERIFY(responseValues.isEmpty());

    PicoATE::Core::RuntimeEvent input = judgment;
    input.uutId = QStringLiteral("UUT-1");
    input.details = {
        {QStringLiteral("promptInstanceId"), QStringLiteral("uut-1-input")},
        {QStringLiteral("mode"), QStringLiteral("input")},
        {QStringLiteral("title"), QStringLiteral("Measured Value")},
        {QStringLiteral("message"), QStringLiteral("Enter the observed value")},
        {QStringLiteral("inputType"), QStringLiteral("number")},
        {QStringLiteral("confirmText"), QStringLiteral("Submit")},
    };
    presenter.applyRuntimeEvents({input});
    auto* firstOverlay = firstCard->findChild<QWidget*>(
        QStringLiteral("uutOverviewPromptOverlay"));
    QVERIFY(firstOverlay);
    QTRY_VERIFY(firstOverlay->isVisible());
    QTRY_VERIFY(secondOverlay->isVisible());
    QVERIFY(owner.findChildren<QDialog*>(
        QStringLiteral("operatorPromptDialog")).isEmpty());
    const auto screenshotPath = qEnvironmentVariable(
        "PICOATE_CARD_PROMPT_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        QVERIFY2(owner.grab().save(screenshotPath), qPrintable(screenshotPath));
    }
    auto* inputEdit = firstOverlay->findChild<QLineEdit*>(
        QStringLiteral("uutOverviewPromptInput"));
    auto* submitButton = firstOverlay->findChild<QPushButton*>(
        QStringLiteral("uutOverviewPromptConfirmButton"));
    QVERIFY(inputEdit && submitButton);
    inputEdit->setText(QStringLiteral("13.75"));
    submitButton->click();
    QCOMPARE(responseInstanceId, QStringLiteral("uut-1-input"));
    QCOMPARE(response, PicoATE::Core::OperatorPromptResponse::Submitted);
    QCOMPARE(responseValues.value(QStringLiteral("value")).toDouble(), 13.75);

    PicoATE::Core::RuntimeEvent closed;
    closed.kind = PicoATE::Core::RuntimeEventKind::OperatorPromptClosed;
    closed.details.insert(QStringLiteral("promptInstanceId"),
                          QStringLiteral("uut-2-judge"));
    presenter.applyRuntimeEvents({closed});
    QVERIFY(!secondOverlay->isVisible());
    QVERIFY(firstOverlay->isVisible());

    presenter.closeAll();
    QVERIFY(!firstOverlay->isVisible());
    viewModel.shutdown();
}

void MainWindowLifecycleTests::operatorPromptsReturnToCardsAfterOverviewNavigation()
{
    QWidget owner;
    auto* layout = new QVBoxLayout(&owner);
    auto* stack = new QStackedWidget(&owner);
    auto* overview = new MultiUutOverviewWidget(stack);
    auto* details = new QWidget(stack);
    auto* model = new UutOverviewModel(&owner);
    stack->addWidget(overview);
    stack->addWidget(details);
    layout->addWidget(stack);
    overview->setModel(model);

    QVector<RunRequest::UutInput> uuts;
    for (int index = 1; index <= 2; ++index) {
        RunRequest::UutInput input;
        input.uutId = QStringLiteral("UUT-%1").arg(index);
        input.variables.insert(
            QStringLiteral("serialNumber"),
            QStringLiteral("BTSN00000%1").arg(index));
        uuts.push_back(std::move(input));
    }
    model->resetForRun({}, uuts);
    owner.resize(980, 520);
    owner.show();
    QTRY_VERIFY(overview->isVisible());

    ExecutionViewModel viewModel;
    OperatorPromptPresenter presenter(&viewModel, &owner);
    presenter.setOverviewHost(overview);

    QVector<PicoATE::Core::RuntimeEvent> notices;
    for (int index = 1; index <= 2; ++index) {
        PicoATE::Core::RuntimeEvent notice;
        notice.kind = PicoATE::Core::RuntimeEventKind::OperatorPromptRequested;
        notice.uutId = QStringLiteral("UUT-%1").arg(index);
        notice.details = {
            {QStringLiteral("promptInstanceId"),
             QStringLiteral("uut-%1-notice").arg(index)},
            {QStringLiteral("mode"), QStringLiteral("notice")},
            {QStringLiteral("message"),
             QStringLiteral("Observe UUT-%1").arg(index)},
        };
        notices.push_back(std::move(notice));
    }
    presenter.applyRuntimeEvents(notices);

    stack->setCurrentWidget(details);
    QTRY_VERIFY(!overview->isVisible());

    QVector<PicoATE::Core::RuntimeEvent> judgments;
    for (int index = 1; index <= 2; ++index) {
        auto judgment = notices.at(index - 1);
        judgment.details.insert(
            QStringLiteral("promptInstanceId"),
            QStringLiteral("uut-%1-judgment").arg(index));
        judgment.details.insert(QStringLiteral("mode"),
                                QStringLiteral("judgment"));
        judgment.details.insert(
            QStringLiteral("message"),
            QStringLiteral("Judge UUT-%1").arg(index));
        judgments.push_back(std::move(judgment));
    }
    presenter.applyRuntimeEvents(judgments);
    QTRY_COMPARE(owner.findChildren<QDialog*>(
                     QStringLiteral("operatorPromptDialog")).size(),
                 2);

    stack->setCurrentWidget(overview);
    presenter.rehostActivePromptsInOverview();
    QTRY_VERIFY(overview->isVisible());
    QTRY_VERIFY([&owner] {
        const auto dialogs = owner.findChildren<QDialog*>(
            QStringLiteral("operatorPromptDialog"));
        return std::none_of(
            dialogs.cbegin(), dialogs.cend(),
            [](const QDialog* dialog) { return dialog->isVisible(); });
    }());

    for (int index = 1; index <= 2; ++index) {
        auto* card = owner.findChild<QAbstractButton*>(
            QStringLiteral("uutOverviewCard_%1").arg(index));
        QVERIFY(card);
        auto* overlay = card->findChild<QWidget*>(
            QStringLiteral("uutOverviewPromptOverlay"));
        QVERIFY(overlay);
        QTRY_VERIFY(overlay->isVisible());
        QCOMPARE(overlay->findChild<QLabel*>(
                     QStringLiteral("uutOverviewPromptMessage"))->text(),
                 QStringLiteral("Judge UUT-%1").arg(index));
        QVERIFY(overlay->findChild<QPushButton*>(
            QStringLiteral("uutOverviewPromptPassButton"))->isVisible());
        QVERIFY(overlay->findChild<QPushButton*>(
            QStringLiteral("uutOverviewPromptFailButton"))->isVisible());
    }

    QVector<PicoATE::Core::RuntimeEvent> noticeClosedEvents;
    for (int index = 1; index <= 2; ++index) {
        PicoATE::Core::RuntimeEvent closed;
        closed.kind = PicoATE::Core::RuntimeEventKind::OperatorPromptClosed;
        closed.details.insert(
            QStringLiteral("promptInstanceId"),
            QStringLiteral("uut-%1-notice").arg(index));
        noticeClosedEvents.push_back(std::move(closed));
    }
    presenter.applyRuntimeEvents(noticeClosedEvents);
    for (int index = 1; index <= 2; ++index) {
        auto* card = owner.findChild<QAbstractButton*>(
            QStringLiteral("uutOverviewCard_%1").arg(index));
        auto* overlay = card->findChild<QWidget*>(
            QStringLiteral("uutOverviewPromptOverlay"));
        QVERIFY(overlay->isVisible());
        QCOMPARE(overlay->findChild<QLabel*>(
                     QStringLiteral("uutOverviewPromptMessage"))->text(),
                 QStringLiteral("Judge UUT-%1").arg(index));
    }

    presenter.closeAll();
    viewModel.shutdown();
}

void MainWindowLifecycleTests::oncePerBatchOperatorPromptCoversAllOverviewCards()
{
    QWidget owner;
    auto* layout = new QVBoxLayout(&owner);
    auto* overview = new MultiUutOverviewWidget(&owner);
    auto* model = new UutOverviewModel(&owner);
    layout->addWidget(overview);
    overview->setModel(model);

    QVector<RunRequest::UutInput> uuts;
    for (int index = 1; index <= 4; ++index) {
        RunRequest::UutInput input;
        input.uutId = QStringLiteral("UUT-%1").arg(index);
        input.variables.insert(
            QStringLiteral("serialNumber"),
            QStringLiteral("BTSN00000%1").arg(index));
        uuts.push_back(std::move(input));
    }
    model->resetForRun({}, uuts);
    owner.resize(1180, 720);
    owner.show();
    QTRY_VERIFY(overview->isVisible());

    ExecutionViewModel viewModel;
    OperatorPromptPresenter presenter(&viewModel, &owner);
    presenter.setOverviewHost(overview);
    QString responseInstanceId;
    PicoATE::Core::OperatorPromptResponse response =
        PicoATE::Core::OperatorPromptResponse::None;
    connect(overview,
            &MultiUutOverviewWidget::operatorPromptResponseRequested,
            &owner,
            [&](const QString& instanceId,
                PicoATE::Core::OperatorPromptResponse nextResponse,
                const QVariantMap&) {
                responseInstanceId = instanceId;
                response = nextResponse;
            });

    PicoATE::Core::RuntimeEvent requested;
    requested.kind = PicoATE::Core::RuntimeEventKind::OperatorPromptRequested;
    requested.uutId = QStringLiteral("UUT-3");
    requested.details = {
        {QStringLiteral("promptInstanceId"), QStringLiteral("batch-confirm")},
        {QStringLiteral("executionScope"), QStringLiteral("OncePerBatch")},
        {QStringLiteral("mode"), QStringLiteral("confirm")},
        {QStringLiteral("title"), QStringLiteral("Connect Shared Fixture")},
        {QStringLiteral("message"),
         QStringLiteral("Confirm the shared fixture is ready for all UUTs.")},
        {QStringLiteral("confirmText"), QStringLiteral("Continue Batch")},
    };
    presenter.applyRuntimeEvents({requested});

    auto* cardsHost = owner.findChild<QWidget*>(
        QStringLiteral("multiUutOverviewCards"));
    auto* overlay = owner.findChild<QWidget*>(
        QStringLiteral("multiUutOverviewPromptOverlay"));
    QVERIFY(cardsHost);
    QVERIFY(overlay);
    auto* cardsScroll = owner.findChild<QScrollArea*>(QStringLiteral("multiUutOverviewScroll"));
    QVERIFY(cardsScroll);
    QCOMPARE(overlay->parentWidget(), cardsScroll->viewport());
    QTRY_VERIFY(overlay->isVisible());
    QTRY_COMPARE(overlay->geometry(), cardsScroll->viewport()->rect());
    QCOMPARE(overlay->findChild<QLabel*>(
                 QStringLiteral("uutOverviewPromptContext"))->text(),
             QStringLiteral("ALL 4 UUTs  |  ONCE PER BATCH"));
    QCOMPARE(overlay->findChild<QLabel*>(
                 QStringLiteral("uutOverviewPromptMessage"))->text(),
             QStringLiteral("Confirm the shared fixture is ready for all UUTs."));
    for (int index = 1; index <= 4; ++index) {
        auto* card = owner.findChild<QAbstractButton*>(
            QStringLiteral("uutOverviewCard_%1").arg(index));
        QVERIFY(card);
        QVERIFY(!card->findChild<QWidget*>(
            QStringLiteral("uutOverviewPromptOverlay")));
    }
    QVERIFY(owner.findChildren<QDialog*>(
        QStringLiteral("operatorPromptDialog")).isEmpty());
    const auto screenshotPath = qEnvironmentVariable(
        "PICOATE_BATCH_PROMPT_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        QVERIFY2(owner.grab().save(screenshotPath), qPrintable(screenshotPath));
    }

    auto* confirm = overlay->findChild<QPushButton*>(
        QStringLiteral("uutOverviewPromptConfirmButton"));
    QVERIFY(confirm);
    confirm->click();
    QCOMPARE(responseInstanceId, QStringLiteral("batch-confirm"));
    QCOMPARE(response, PicoATE::Core::OperatorPromptResponse::Confirmed);
    QVERIFY(overview->setOperatorPromptResponsePending(
        QStringLiteral("batch-confirm"), true));
    QVERIFY(!confirm->isEnabled());

    PicoATE::Core::RuntimeEvent closed;
    closed.kind = PicoATE::Core::RuntimeEventKind::OperatorPromptClosed;
    closed.details.insert(QStringLiteral("promptInstanceId"),
                          QStringLiteral("batch-confirm"));
    presenter.applyRuntimeEvents({closed});
    QTRY_VERIFY(!overlay->isVisible());
    QVERIFY(!overview->hasOperatorPrompt(QStringLiteral("batch-confirm")));
    viewModel.shutdown();
}

void MainWindowLifecycleTests::messageBoxPropertyEditorSwitchesConfirmationMode()
{
    QTemporaryDir project;
    QVERIFY(project.isValid());
    const auto imageDirectory = project.filePath(QStringLiteral("images"));
    QVERIFY(QDir().mkpath(imageDirectory));
    const auto imageFileName = QStringLiteral("picoate_prompt_editor_test.jpg");
    const auto imagePath = QDir(imageDirectory).filePath(imageFileName);
    QPixmap sourceImage(60, 30);
    sourceImage.fill(QColor(QStringLiteral("#d7ecff")));
    QVERIFY(sourceImage.save(imagePath, "PNG"));

    const auto path = project.filePath(QStringLiteral("sequence.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/operator_prompt_sequence.json"),
                       path));
    SequenceDocument document;
    QVERIFY(document.load(path));
    StepPropertyEditor editor(&document);

    auto* mode = editor.findChild<QComboBox*>(QStringLiteral("propertyPromptModeCombo"));
    auto* buttonText = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyPromptConfirmTextEdit"));
    auto* closeOnStep = editor.findChild<QComboBox*>(
        QStringLiteral("propertyPromptCloseOnStepCombo"));
    auto* image = editor.findChild<QComboBox*>(
        QStringLiteral("propertyPromptImageCombo"));
    QVERIFY(mode);
    QVERIFY(buttonText);
    QVERIFY(closeOnStep);
    QVERIFY(image);

    const SequenceItemPath promptPath{0, {0}};
    editor.setCurrentItem(promptPath);
    QCOMPARE(mode->currentData().toString(), QStringLiteral("confirm"));
    QVERIFY(!buttonText->isHidden());
    QVERIFY(closeOnStep->isHidden());

    mode->setCurrentIndex(mode->findData(QStringLiteral("notice")));
    QVERIFY(buttonText->isHidden());
    QVERIFY(!closeOnStep->isHidden());
    QCOMPARE(closeOnStep->itemData(0).toString(), QString());
    QCOMPARE(closeOnStep->itemText(0), QStringLiteral("Next enabled step (default)"));
    const int targetIndex = closeOnStep->findData(QStringLiteral("003"));
    QVERIFY(targetIndex > 0);
    QVERIFY(closeOnStep->itemText(targetIndex).contains(
        QStringLiteral("Button State Detected")));
    closeOnStep->setCurrentIndex(targetIndex);
    const int imageIndex = image->findData(imageFileName);
    QVERIFY(imageIndex > 0);
    image->setCurrentIndex(imageIndex);
    QVERIFY(editor.commitPendingChanges());

    const auto prompt = document.objectAt(promptPath)
        .value(QStringLiteral("prompt")).toObject();
    QCOMPARE(prompt.value(QStringLiteral("mode")).toString(),
             QStringLiteral("notice"));
    QCOMPARE(prompt.value(QStringLiteral("closeOnStep")).toString(),
             QStringLiteral("003"));
    QCOMPARE(prompt.value(QStringLiteral("image")).toString(), imageFileName);
    QVERIFY(!prompt.contains(QStringLiteral("confirmText")));
    QVERIFY(QFile::remove(imagePath));
}

void MainWindowLifecycleTests::messageBoxPropertyEditorConfiguresJudgmentMode()
{
    const auto path = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/operator_prompt_sequence.json");
    SequenceDocument document;
    QVERIFY(document.load(path));
    StepPropertyEditor editor(&document);
    editor.setCurrentItem(SequenceItemPath{0, {0}});

    auto* mode = editor.findChild<QComboBox*>(
        QStringLiteral("propertyPromptModeCombo"));
    auto* dialogKey = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyPromptDialogKeyEdit"));
    auto* passText = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyPromptPassTextEdit"));
    auto* failText = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyPromptFailTextEdit"));
    auto* failureCode = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyPromptFailureCodeEdit"));
    auto* confirmText = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyPromptConfirmTextEdit"));
    auto* closeOnStep = editor.findChild<QComboBox*>(
        QStringLiteral("propertyPromptCloseOnStepCombo"));
    QVERIFY(mode && dialogKey && passText && failText && failureCode);
    QVERIFY(confirmText && closeOnStep);

    mode->setCurrentIndex(mode->findData(QStringLiteral("judgment")));
    QVERIFY(!dialogKey->isHidden());
    QVERIFY(!passText->isHidden());
    QVERIFY(!failText->isHidden());
    QVERIFY(!failureCode->isHidden());
    QVERIFY(confirmText->isHidden());
    QVERIFY(closeOnStep->isHidden());
    dialogKey->setText(QStringLiteral("rgb-lamp"));
    passText->setText(QStringLiteral("PASS"));
    failText->setText(QStringLiteral("FAIL"));
    failureCode->setText(QStringLiteral("RgbLampOperatorFail"));
    QVERIFY(editor.commitPendingChanges());

    const auto prompt = document.objectAt(SequenceItemPath{0, {0}})
                            .value(QStringLiteral("prompt")).toObject();
    QCOMPARE(prompt.value(QStringLiteral("mode")).toString(),
             QStringLiteral("judgment"));
    QCOMPARE(prompt.value(QStringLiteral("dialogKey")).toString(),
             QStringLiteral("rgb-lamp"));
    QCOMPARE(prompt.value(QStringLiteral("failureCode")).toString(),
             QStringLiteral("RgbLampOperatorFail"));
    QVERIFY(!prompt.contains(QStringLiteral("confirmText")));
    QVERIFY(!prompt.contains(QStringLiteral("closeOnStep")));
}

void MainWindowLifecycleTests::messageBoxPropertyEditorConfiguresInputMode()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("operator_input.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"json({
      "id":"operator-input-editor","name":"Operator Input Editor","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"enter-voltage","name":"Enter Voltage","kind":"operatorPrompt",
           "prompt":{"mode":"confirm","title":"Voltage","message":"Enter voltage",
                     "confirmText":"OK","timeoutMs":60000}},
          {"id":"check-voltage","name":"Check Voltage","kind":"limit",
           "inputs":{"actual":""},
           "parameters":{"comparison":"equal","expected":12.5}}
        ]
      }]
    })json");
    file.close();

    SequenceDocument document;
    QVERIFY(document.load(path));
    StepPropertyEditor editor(&document);
    const SequenceItemPath promptPath{0, {0}};
    editor.setCurrentItem(promptPath);

    auto* mode = editor.findChild<QComboBox*>(
        QStringLiteral("propertyPromptModeCombo"));
    auto* inputType = editor.findChild<QComboBox*>(
        QStringLiteral("propertyPromptInputTypeCombo"));
    auto* inputHint = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyPromptInputPlaceholderEdit"));
    auto* defaultValue = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyPromptDefaultValueEdit"));
    auto* buttonText = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyPromptConfirmTextEdit"));
    QVERIFY(mode && inputType && inputHint && defaultValue && buttonText);

    mode->setCurrentIndex(mode->findData(QStringLiteral("input")));
    QVERIFY(!inputType->isHidden());
    QVERIFY(!inputHint->isHidden());
    QVERIFY(!defaultValue->isHidden());
    QVERIFY(!buttonText->isHidden());
    inputType->setCurrentIndex(inputType->findData(QStringLiteral("number")));
    inputHint->setText(QStringLiteral("Example: 12.5"));
    defaultValue->setText(QStringLiteral("12.5"));
    buttonText->setText(QStringLiteral("Submit"));
    QVERIFY(editor.commitPendingChanges());

    const auto prompt = document.objectAt(promptPath)
                            .value(QStringLiteral("prompt"))
                            .toObject();
    QCOMPARE(prompt.value(QStringLiteral("mode")).toString(),
             QStringLiteral("input"));
    QCOMPARE(prompt.value(QStringLiteral("inputType")).toString(),
             QStringLiteral("number"));
    QCOMPARE(prompt.value(QStringLiteral("inputPlaceholder")).toString(),
             QStringLiteral("Example: 12.5"));
    QCOMPARE(prompt.value(QStringLiteral("defaultValue")).toDouble(), 12.5);
    QCOMPARE(prompt.value(QStringLiteral("confirmText")).toString(),
             QStringLiteral("Submit"));
    QVERIFY(!prompt.contains(QStringLiteral("closeOnStep")));
    QVERIFY(!prompt.contains(QStringLiteral("passText")));

    const auto candidates = buildStepOutputExpressionCandidates(
        document.rootObject(), SequenceItemPath{0, {1}}, {});
    const auto value = std::find_if(
        candidates.cbegin(), candidates.cend(), [](const auto& candidate) {
            return candidate.expression ==
                   QStringLiteral("${step:enter-voltage.outputs.value}");
        });
    QVERIFY(value != candidates.cend());
    QCOMPARE(value->type, PluginParameterType::Number);
    QVERIFY(std::any_of(candidates.cbegin(), candidates.cend(),
                        [](const auto& candidate) {
                            return candidate.expression ==
                                   QStringLiteral("${step:enter-voltage.outputs.text}");
                        }));
}

void MainWindowLifecycleTests::messageBoxPropertyEditorInsertsRuntimeValues()
{
    const auto path = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/operator_prompt_sequence.json");
    SequenceDocument document;
    QVERIFY(document.load(path));
    QVERIFY(document.setSequenceVariables(QJsonArray{
        QJsonObject{{QStringLiteral("name"), QStringLiteral("targetVoltage")},
                    {QStringLiteral("type"), QStringLiteral("number")},
                    {QStringLiteral("scope"), QStringLiteral("shared")},
                    {QStringLiteral("value"), 800.0}}
    }));

    StepPropertyEditor editor(&document);
    const SequenceItemPath promptPath{0, {0}};
    editor.setCurrentItem(promptPath);

    auto* message = editor.findChild<QPlainTextEdit*>(
        QStringLiteral("propertyPromptMessageEdit"));
    auto* picker = editor.findChild<QToolButton*>(
        QStringLiteral("promptValuePickerButton"));
    QVERIFY(message);
    QVERIFY(picker);

    message->moveCursor(QTextCursor::End);
    message->insertPlainText(QStringLiteral("\nUUT: "));
    QVERIFY(chooseExpression(picker, QStringLiteral("${uut.id}")));
    message->insertPlainText(QStringLiteral("; Target: "));
    QVERIFY(chooseExpression(picker,
                             QStringLiteral("${var.targetVoltage}")));
    QVERIFY(message->toPlainText().endsWith(
        QStringLiteral("UUT: ${uut.id}; Target: ${var.targetVoltage}")));
    QVERIFY(editor.commitPendingChanges());

    const auto prompt = document.objectAt(promptPath)
        .value(QStringLiteral("prompt")).toObject();
    QVERIFY(prompt.value(QStringLiteral("message")).toString().endsWith(
        QStringLiteral("UUT: ${uut.id}; Target: ${var.targetVoltage}")));
}

void MainWindowLifecycleTests::parserPropertyEditorCreatesNamedOutputsForFx()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(
        QStringLiteral("named_parser_editor.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"json({
      "id":"named-parser-editor","name":"Named Parser Editor","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"parse","name":"Parse SN List","kind":"action",
           "moduleId":"builtin.data-parser","function":"splitText",
           "inputs":{"source":"SN001,812.5","delimiter":","}},
          {"id":"check","name":"Check Voltage","kind":"limit",
           "inputs":{"actual":""},
           "parameters":{"comparison":"equal","expected":812.5}}
        ]
      }]
    })json");
    sequenceFile.close();

    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    editor.setPluginRegistry({});
    editor.setCurrentItem(SequenceItemPath{0, {0}});
    editor.show();
    QTest::qWait(20);

    auto* mode = editor.findChild<QComboBox*>(
        QStringLiteral("pluginInput_resultMode"));
    auto* table = editor.findChild<QTableWidget*>(
        QStringLiteral("parserNamedFieldsTable"));
    auto* add = editor.findChild<QToolButton*>(
        QStringLiteral("parserNamedFieldAddButton"));
    QVERIFY(mode);
    QVERIFY(table);
    QVERIFY(add);
    QVERIFY(!table->isVisible());

    mode->setCurrentIndex(mode->findData(QStringLiteral("multiple")));
    QVERIFY(!table->isHidden());
    QCOMPARE(table->rowCount(), 1);
    auto* firstIndex = qobject_cast<QSpinBox*>(table->cellWidget(0, 0));
    auto* firstName = qobject_cast<QLineEdit*>(table->cellWidget(0, 1));
    auto* firstType = qobject_cast<QComboBox*>(table->cellWidget(0, 2));
    QVERIFY(firstIndex);
    QVERIFY(firstName);
    QVERIFY(firstType);
    firstIndex->setValue(0);
    firstName->setText(QStringLiteral("SN1"));

    add->click();
    QCOMPARE(table->rowCount(), 2);
    auto* secondIndex = qobject_cast<QSpinBox*>(table->cellWidget(1, 0));
    auto* secondName = qobject_cast<QLineEdit*>(table->cellWidget(1, 1));
    auto* secondType = qobject_cast<QComboBox*>(table->cellWidget(1, 2));
    QVERIFY(secondIndex);
    QVERIFY(secondName);
    QVERIFY(secondType);
    secondIndex->setValue(1);
    secondName->setText(QStringLiteral("sn1"));
    secondType->setCurrentIndex(
        secondType->findData(QStringLiteral("number")));
    QVERIFY(!editor.commitPendingChanges());
    auto* error = editor.findChild<QLabel*>(
        QStringLiteral("propertyErrorLabel"));
    QVERIFY(error);
    QVERIFY(error->isVisible());
    QVERIFY(error->text().contains(QStringLiteral("duplicated")));

    secondName->setText(QStringLiteral("voltage"));
    QVERIFY(editor.commitPendingChanges());

    const auto parserInputs = document.objectAt(SequenceItemPath{0, {0}})
                                  .value(QStringLiteral("inputs")).toObject();
    QCOMPARE(parserInputs.value(QStringLiteral("resultMode")).toString(),
             QStringLiteral("multiple"));
    QCOMPARE(parserInputs.value(QStringLiteral("fields")).toArray().size(), 2);
    QVERIFY(!parserInputs.contains(QStringLiteral("fieldIndex")));
    QVERIFY(!parserInputs.contains(QStringLiteral("outputType")));

    editor.setCurrentItem(SequenceItemPath{0, {1}});
    auto* actual = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyLimitActualEdit"));
    QVERIFY(actual);
    auto* picker = actual->parentWidget()->findChild<QToolButton*>(
        QStringLiteral("expressionPickerButton"));
    QVERIFY(picker);
    QVERIFY(chooseExpression(
        picker, QStringLiteral("${step:parse.outputs.fields.voltage}")));
    QCOMPARE(actual->text(),
             QStringLiteral("${step:parse.outputs.fields.voltage}"));
    QVERIFY(editor.commitPendingChanges());
}

void MainWindowLifecycleTests::parserPropertyEditorSwitchesRegisterModes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(
        QStringLiteral("register_parser_modes.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"json({
      "id":"register-parser-modes","name":"Register Parser Modes","groups":[{
        "id":"main","kind":"main","steps":[{
          "id":"decode","name":"Decode Registers","kind":"action",
          "moduleId":"builtin.data-parser","function":"decodeRegisters",
          "inputs":{"source":"${step:read.outputs.registers}","registerOffset":0,
                    "dataType":"uint32","layout":"normal","scale":1,
                    "valueOffset":0,"registerCount":2,
                    "byteOrder":"highByteFirst","padding":"keep"}
        }]
      }]
    })json");
    sequenceFile.close();

    const SequenceItemPath parserPath{0, {0}};
    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    editor.setPluginRegistry({});
    editor.setCurrentItem(parserPath);
    editor.show();
    QTest::qWait(20);

    auto* dataType = editor.findChild<QComboBox*>(
        QStringLiteral("pluginInput_dataType"));
    auto* layout = editor.findChild<QComboBox*>(
        QStringLiteral("pluginInput_layout"));
    auto* scale = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_scale"));
    auto* registerCount = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_registerCount"));
    auto* byteOrder = editor.findChild<QComboBox*>(
        QStringLiteral("pluginInput_byteOrder"));
    auto* padding = editor.findChild<QComboBox*>(
        QStringLiteral("pluginInput_padding"));
    QVERIFY(dataType && layout && scale && registerCount && byteOrder && padding);
    QVERIFY(!layout->isHidden());
    QVERIFY(!scale->isHidden());
    QVERIFY(registerCount->parentWidget()->isHidden());
    QVERIFY(byteOrder->isHidden());
    QVERIFY(padding->isHidden());

    const int textIndex = dataType->findData(QStringLiteral("asciiText"));
    QVERIFY(textIndex >= 0);
    dataType->setCurrentIndex(textIndex);
    QVERIFY(layout->isHidden());
    QVERIFY(scale->parentWidget()->isHidden());
    QVERIFY(!registerCount->isHidden());
    QVERIFY(!byteOrder->isHidden());
    QVERIFY(!padding->isHidden());
    registerCount->setText(QStringLiteral("24"));
    QVERIFY(editor.commitPendingChanges());

    const auto inputs = document.objectAt(parserPath)
                            .value(QStringLiteral("inputs")).toObject();
    QCOMPARE(inputs.value(QStringLiteral("dataType")).toString(),
             QStringLiteral("asciiText"));
    QCOMPARE(inputs.value(QStringLiteral("registerCount")).toInt(), 24);
    QVERIFY(inputs.contains(QStringLiteral("byteOrder")));
    QVERIFY(inputs.contains(QStringLiteral("padding")));
    QVERIFY(!inputs.contains(QStringLiteral("layout")));
    QVERIFY(!inputs.contains(QStringLiteral("scale")));
    QVERIFY(!inputs.contains(QStringLiteral("valueOffset")));
}

void MainWindowLifecycleTests::parserPropertyEditorPreservesExplicitEmptyEndMarker()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(
        QStringLiteral("empty_end_marker.json"));
    QFile sequenceFile(sequencePath);
    QVERIFY(sequenceFile.open(QIODevice::WriteOnly));
    sequenceFile.write(R"json({
      "id":"empty-end-marker","name":"Empty End Marker","groups":[{
        "id":"main","kind":"main","steps":[{
          "id":"parse","name":"Extract Remaining Text","kind":"action",
          "moduleId":"builtin.data-parser","function":"extractBetween",
          "inputs":{"source":"SN:1234567890","startMarker":"SN:"}
        }]
      }]
    })json");
    sequenceFile.close();

    const SequenceItemPath parserPath{0, {0}};
    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    editor.setPluginRegistry({});
    editor.setCurrentItem(parserPath);

    auto* endMarker = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_endMarker"));
    QVERIFY(endMarker);
    QCOMPARE(endMarker->text(), QStringLiteral("\\r\\n"));
    endMarker->clear();
    QVERIFY(editor.commitPendingChanges());

    const auto inputs = document.objectAt(parserPath)
                            .value(QStringLiteral("inputs")).toObject();
    QVERIFY(inputs.contains(QStringLiteral("endMarker")));
    QCOMPARE(inputs.value(QStringLiteral("endMarker")).toString(), QString{});

    QString errorMessage;
    QVERIFY2(document.save(&errorMessage), qPrintable(errorMessage));

    SequenceDocument reloaded;
    QVERIFY(reloaded.load(sequencePath));
    StepPropertyEditor reloadedEditor(&reloaded);
    reloadedEditor.setPluginRegistry({});
    reloadedEditor.setCurrentItem(parserPath);
    auto* reloadedEndMarker = reloadedEditor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_endMarker"));
    QVERIFY(reloadedEndMarker);
    QVERIFY(reloadedEndMarker->text().isEmpty());
}

void MainWindowLifecycleTests::whileLoopPropertyEditorUsesTypedFields()
{
    const auto path = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/while_loop_sequence.json");
    SequenceDocument document;
    QVERIFY(document.load(path));
    StepPropertyEditor editor(&document);

    auto* type = editor.findChild<QComboBox*>(QStringLiteral("propertyLoopTypeCombo"));
    auto* interval = editor.findChild<QSpinBox*>(
        QStringLiteral("propertyConditionIntervalSpin"));
    auto* maximum = editor.findChild<QSpinBox*>(
        QStringLiteral("propertyConditionMaxIterationsSpin"));
    auto* timeout = editor.findChild<QSpinBox*>(
        QStringLiteral("propertyConditionTimeoutSpin"));
    QVERIFY(type);
    QVERIFY(interval);
    QVERIFY(maximum);
    QVERIFY(timeout);

    const SequenceItemPath loopPath{1, {0}};
    editor.setCurrentItem(loopPath);
    QCOMPARE(type->currentData().toString(), QStringLiteral("while"));
    QVERIFY(!interval->isHidden());
    interval->setValue(200);
    maximum->setValue(500);
    timeout->setValue(60000);
    QVERIFY(editor.commitPendingChanges());

    const auto loop = document.objectAt(loopPath).value(QStringLiteral("loop")).toObject();
    QCOMPARE(loop.value(QStringLiteral("type")).toString(), QStringLiteral("while"));
    QCOMPARE(loop.value(QStringLiteral("intervalMs")).toInt(), 200);
    QCOMPARE(loop.value(QStringLiteral("maxIterations")).toInt(), 500);
    QCOMPARE(loop.value(QStringLiteral("timeoutMs")).toInt(), 60000);
    QVERIFY(!loop.contains(QStringLiteral("condition")));
    QVERIFY(!loop.contains(QStringLiteral("sample")));
    QVERIFY(!loop.contains(QStringLiteral("completionMode")));
    QVERIFY(!loop.contains(QStringLiteral("variable")));

    auto* kind = editor.findChild<QComboBox*>(
        QStringLiteral("propertyKindCombo"));
    auto* maxAttempts = editor.findChild<QSpinBox*>(
        QStringLiteral("propertyMaxAttemptsSpin"));
    QVERIFY(kind);
    QVERIFY(maxAttempts);
    const int testItemIndex = kind->findData(QStringLiteral("testItem"));
    QVERIFY(testItemIndex >= 0);
    kind->setCurrentIndex(testItemIndex);
    QCOMPARE(maxAttempts->value(), 1);
    QVERIFY(editor.commitPendingChanges());

    const auto converted = document.objectAt(loopPath);
    QCOMPARE(converted.value(QStringLiteral("kind")).toString(),
             QStringLiteral("testItem"));
    QVERIFY(!converted.contains(QStringLiteral("loop")));
    QVERIFY(!converted.contains(QStringLiteral("moduleId")));
    QVERIFY(!converted.contains(QStringLiteral("function")));
    QVERIFY(!converted.contains(QStringLiteral("inputs")));
    QVERIFY(!converted.contains(QStringLiteral("parameters")));
    QVERIFY(!converted.contains(QStringLiteral("enabled")));
    QVERIFY(!converted.contains(QStringLiteral("alwaysRun")));
    QVERIFY(!converted.contains(QStringLiteral("resultRecording")));
    QVERIFY(!converted.contains(QStringLiteral("checkpointBefore")));
    QVERIFY(!converted.contains(QStringLiteral("checkpointAfter")));
    QVERIFY(!converted.contains(QStringLiteral("timeout")));
    QVERIFY(!converted.contains(QStringLiteral("retry")));
    QCOMPARE(converted.value(QStringLiteral("steps")).toArray().size(), 5);
}

void MainWindowLifecycleTests::valueToolsPropertyEditorUsesExpressionList()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("value_tools_editor.json"));
    QFile file(sequencePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"json({
      "id":"value-tools-editor","name":"Value Tools Editor","groups":[{
        "id":"main","kind":"main","steps":[{
          "id":"temperature-statistics","kind":"action",
          "moduleId":"builtin.value-tools","function":"statistics",
          "inputs":{"values":[
            {"name":"Gun 1","value":"${var.temperature1}"},
            {"name":"Gun 2","value":42.5}
          ]}
        },{
          "id":"calculate","kind":"action",
          "moduleId":"builtin.value-tools","function":"calculate",
          "inputs":{"operation":"add","a":5,"b":2}
        }]
      }]
    })json");
    file.close();

    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    editor.setPluginRegistry({});
    const SequenceItemPath stepPath{0, {0}};
    editor.setCurrentItem(stepPath);

    auto* function = editor.findChild<QComboBox*>(
        QStringLiteral("propertyFunctionEdit"));
    auto* arguments = editor.findChild<QGroupBox*>(
        QStringLiteral("pluginInputsGroup"));
    QVERIFY(function);
    QVERIFY(arguments);
    QCOMPARE(function->currentData().toString(), QStringLiteral("statistics"));
    QVERIFY(!arguments->isHidden());
    auto* values = editor.findChild<QTableWidget*>(
        QStringLiteral("pluginInput_values"));
    QVERIFY(values);
    QCOMPARE(values->rowCount(), 2);
    auto* secondValueField = values->cellWidget(1, 1);
    QVERIFY(secondValueField);
    auto* secondValue = secondValueField->findChild<QLineEdit*>(
        QStringLiteral("expressionListValue"));
    QVERIFY(secondValue);
    QCOMPARE(secondValue->text(), QStringLiteral("42.5"));
    auto* firstName = qobject_cast<QLineEdit*>(values->cellWidget(0, 0));
    auto* addValue = editor.findChild<QToolButton*>(
        QStringLiteral("expressionListAddButton"));
    auto* removeValue = editor.findChild<QToolButton*>(
        QStringLiteral("expressionListRemoveButton"));
    QVERIFY(firstName);
    QVERIFY(addValue);
    QVERIFY(removeValue);
    QCOMPARE(values->verticalHeader()->defaultSectionSize(), 38);
    QCOMPARE(values->rowHeight(0), 38);
    QCOMPARE(firstName->height(), 32);
    QCOMPARE(secondValue->height(), 32);
    QCOMPARE(addValue->text(), QStringLiteral("Add value"));
    QCOMPARE(removeValue->text(), QStringLiteral("Remove"));
    QVERIFY(addValue->minimumWidth() >= 96);
    QVERIFY(removeValue->minimumWidth() >= 88);
    addValue->click();
    QCOMPARE(values->rowCount(), 3);
    removeValue->click();
    QCOMPARE(values->rowCount(), 2);
    secondValue->setText(QStringLiteral("44.5"));
    QVERIFY(editor.commitPendingChanges());

    const auto savedValues = document.objectAt(stepPath)
        .value(QStringLiteral("inputs")).toObject()
        .value(QStringLiteral("values")).toArray();
    QCOMPARE(savedValues.size(), 2);
    QCOMPARE(savedValues[0].toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("Gun 1"));
    QCOMPARE(savedValues[0].toObject().value(QStringLiteral("value")).toString(),
             QStringLiteral("${var.temperature1}"));
    QCOMPARE(savedValues[1].toObject().value(QStringLiteral("value")).toDouble(),
             44.5);

    const SequenceItemPath calculatePath{0, {1}};
    editor.setCurrentItem(calculatePath);
    auto* operation = editor.findChild<QComboBox*>(
        QStringLiteral("pluginInput_operation"));
    auto* operandB = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_b"));
    QVERIFY(operation);
    QVERIFY(operandB);
    QCOMPARE(operandB->text(), QStringLiteral("2"));
    const int absoluteIndex = operation->findData(QStringLiteral("absolute"));
    QVERIFY(absoluteIndex >= 0);
    operation->setCurrentIndex(absoluteIndex);
    QVERIFY(operandB->parentWidget()->property("valueToolInactive").toBool());
    QVERIFY(editor.commitPendingChanges());
    const auto calculateInputs = document.objectAt(calculatePath)
        .value(QStringLiteral("inputs")).toObject();
    QCOMPARE(calculateInputs.value(QStringLiteral("operation")).toString(),
             QStringLiteral("absolute"));
    QVERIFY(!calculateInputs.contains(QStringLiteral("b")));
}

void MainWindowLifecycleTests::periodicActionPropertyEditorUsesTypedPolicyFields()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(QStringLiteral("periodic_editor.json"));
    QFile file(sequencePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"json({
      "id":"periodic-editor","name":"Periodic Editor","groups":[{
        "id":"setup","kind":"setup","steps":[{
          "id":"heartbeat","name":"Heartbeat","kind":"action",
          "moduleId":"test.periodic","function":"send",
          "inputs":{"deviceId":"DEVICE1"}
        }]
      }]
    })json");
    file.close();

    SequenceDocument document;
    QVERIFY(document.load(sequencePath));
    StepPropertyEditor editor(&document);
    const SequenceItemPath heartbeatPath{0, {0}};
    editor.setCurrentItem(heartbeatPath);

    auto* enabled = editor.findChild<QCheckBox*>(
        QStringLiteral("propertyPeriodicEnabledCheck"));
    auto* interval = editor.findChild<QSpinBox*>(
        QStringLiteral("propertyPeriodicIntervalSpin"));
    auto* immediate = editor.findChild<QCheckBox*>(
        QStringLiteral("propertyPeriodicRunImmediatelyCheck"));
    auto* counterStart = editor.findChild<QSpinBox*>(
        QStringLiteral("propertyPeriodicCounterStartSpin"));
    auto* counterIncrement = editor.findChild<QSpinBox*>(
        QStringLiteral("propertyPeriodicCounterIncrementSpin"));
    auto* counterWrapAt = editor.findChild<QSpinBox*>(
        QStringLiteral("propertyPeriodicCounterWrapAtSpin"));
    QVERIFY(enabled);
    QVERIFY(interval);
    QVERIFY(immediate);
    QVERIFY(counterStart);
    QVERIFY(counterIncrement);
    QVERIFY(counterWrapAt);
    QVERIFY(!enabled->isHidden());
    QVERIFY(interval->isHidden());
    QVERIFY(counterStart->isHidden());

    enabled->setChecked(true);
    QVERIFY(!interval->isHidden());
    QVERIFY(!counterStart->isHidden());
    interval->setValue(5000);
    immediate->setChecked(false);
    counterStart->setValue(1);
    counterIncrement->setValue(1);
    counterWrapAt->setValue(255);
    QVERIFY(editor.commitPendingChanges());

    const auto periodic = document.objectAt(heartbeatPath)
        .value(QStringLiteral("periodic")).toObject();
    QCOMPARE(periodic.value(QStringLiteral("intervalMs")).toInt(), 5000);
    QCOMPARE(periodic.value(QStringLiteral("runImmediately")).toBool(), false);
    const auto counter = periodic.value(QStringLiteral("counter")).toObject();
    QCOMPARE(counter.value(QStringLiteral("start")).toInt(), 1);
    QCOMPARE(counter.value(QStringLiteral("increment")).toInt(), 1);
    QCOMPARE(counter.value(QStringLiteral("wrapAt")).toInt(), 255);
}

void MainWindowLifecycleTests::stepExecutionScopeEditorPersistsSharedMode()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("shared-scope.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({
      "id":"shared-scope","name":"Shared Scope","groups":[
        {"id":"setup","kind":"setup","steps":[
          {"id":"setup-step","kind":"noop","name":"Setup Step"}
        ]},{
        "id":"main","kind":"main","steps":[
          {"id":"shared-step","kind":"noop","name":"Shared Step"}
        ]
      },{
        "id":"cleanup","kind":"cleanup","steps":[
          {"id":"cleanup-step","kind":"noop","name":"Cleanup Step"}
        ]}
      ]
    })");
    file.close();

    SequenceDocument document;
    QVERIFY(document.load(path));
    StepPropertyEditor editor(&document);
    const SequenceItemPath stepPath{1, {0}};
    editor.setCurrentItem(stepPath);

    auto* scope = editor.findChild<QComboBox*>(
        QStringLiteral("propertyExecutionScopeCombo"));
    QVERIFY(scope);
    QCOMPARE(scope->currentData().toString(), QStringLiteral("perUut"));

    const int sharedIndex = scope->findData(QStringLiteral("oncePerBatch"));
    QVERIFY(sharedIndex >= 0);
    scope->setCurrentIndex(sharedIndex);
    QVERIFY(editor.commitPendingChanges());
    QCOMPARE(document.objectAt(stepPath)
                 .value(QStringLiteral("executionScope"))
                 .toString(),
             QStringLiteral("oncePerBatch"));

    editor.setCurrentItem(stepPath);
    scope->setCurrentIndex(scope->findData(QStringLiteral("perUut")));
    QVERIFY(editor.commitPendingChanges());
    QVERIFY(!document.objectAt(stepPath).contains(
        QStringLiteral("executionScope")));

    const SequenceItemPath setupPath{0, {0}};
    editor.setCurrentItem(setupPath);
    QCOMPARE(scope->count(), 1);
    QCOMPARE(scope->currentData().toString(),
             QStringLiteral("sessionLifecycle"));
    QVERIFY(!scope->isEnabled());
    QVERIFY(scope->toolTip().contains(QStringLiteral("once for the whole batch")));

    const SequenceItemPath cleanupPath{2, {0}};
    editor.setCurrentItem(cleanupPath);
    QCOMPARE(scope->count(), 1);
    QCOMPARE(scope->currentData().toString(),
             QStringLiteral("sessionLifecycle"));
    QVERIFY(!scope->isEnabled());
}

void MainWindowLifecycleTests::barrierPropertyEditorUsesMinimalConfiguration()
{
    const auto path = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/simple_sequence.json");
    SequenceDocument document;
    QVERIFY(document.load(path));
    StepPropertyEditor editor(&document);
    const SequenceItemPath stepPath{1, {0}};
    editor.setCurrentItem(stepPath);

    auto* kind = editor.findChild<QComboBox*>(
        QStringLiteral("propertyKindCombo"));
    auto* name = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyBarrierNameEdit"));
    auto* expectedUuts = editor.findChild<QSpinBox*>(
        QStringLiteral("propertyBarrierExpectedUutSpin"));
    auto* arrivalPolicy = editor.findChild<QComboBox*>(
        QStringLiteral("propertyBarrierArrivalPolicyCombo"));
    auto* failurePolicy = editor.findChild<QComboBox*>(
        QStringLiteral("propertyBarrierFailurePolicyCombo"));
    QVERIFY(kind);
    QVERIFY(name);
    QVERIFY(expectedUuts);
    QVERIFY(arrivalPolicy);
    QVERIFY(failurePolicy);

    kind->setCurrentIndex(kind->findData(QStringLiteral("barrier")));
    QCoreApplication::processEvents();
    QVERIFY(!name->isHidden());
    QVERIFY(expectedUuts->isHidden());
    QVERIFY(arrivalPolicy->isHidden());
    QVERIFY(failurePolicy->isHidden());

    name->clear();
    QVERIFY(editor.commitPendingChanges());
    auto step = document.objectAt(stepPath);
    QCOMPARE(step.value(QStringLiteral("kind")).toString(),
             QStringLiteral("barrier"));
    QVERIFY(!step.contains(QStringLiteral("barrier")));

    editor.setCurrentItem(stepPath);
    name->setText(QStringLiteral("batch-ready"));
    QVERIFY(editor.commitPendingChanges());
    step = document.objectAt(stepPath);
    const auto barrier = step.value(QStringLiteral("barrier")).toObject();
    QCOMPARE(barrier.size(), 1);
    QCOMPARE(barrier.value(QStringLiteral("barrierName")).toString(),
             QStringLiteral("batch-ready"));
}

void MainWindowLifecycleTests::stepFailurePolicyEditorUsesThreeOutcomeCombos()
{
    const auto path = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/simple_sequence.json");
    SequenceDocument document;
    QVERIFY(document.load(path));
    StepPropertyEditor editor(&document);
    const SequenceItemPath stepPath{1, {0}};
    editor.setCurrentItem(stepPath);

    auto* onFail = editor.findChild<QComboBox*>(
        QStringLiteral("propertyOnFailPolicyCombo"));
    auto* onError = editor.findChild<QComboBox*>(
        QStringLiteral("propertyOnErrorPolicyCombo"));
    auto* onTimeout = editor.findChild<QComboBox*>(
        QStringLiteral("propertyOnTimeoutPolicyCombo"));
    auto* onFailTarget = editor.findChild<QComboBox*>(
        QStringLiteral("propertyOnFailTargetCombo"));
    auto* advanced = editor.findChild<QPlainTextEdit*>(
        QStringLiteral("propertyLegacyErrorPolicyEdit"));
    auto* retryWhen = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyRetryWhenEdit"));
    QVERIFY(onFail);
    QVERIFY(onError);
    QVERIFY(onTimeout);
    QVERIFY(onFailTarget);
    QVERIFY(advanced);
    QVERIFY(retryWhen);
    QVERIFY(retryWhen->isHidden());
    QCOMPARE(onFail->currentData().toString(), QStringLiteral("Inherit"));
    QCOMPARE(onError->currentData().toString(), QStringLiteral("Inherit"));
    QCOMPARE(onTimeout->currentData().toString(), QStringLiteral("Inherit"));
    QCOMPARE(onFail->findData(QStringLiteral("Retry")), -1);
    QCOMPARE(onError->findData(QStringLiteral("Retry")), -1);
    QCOMPARE(onTimeout->findData(QStringLiteral("Retry")), -1);
    QVERIFY(onFail->findData(QStringLiteral("Abort")) >= 0);
    QVERIFY(onError->findData(QStringLiteral("Abort")) >= 0);
    QVERIFY(onTimeout->findData(QStringLiteral("Abort")) >= 0);
    QVERIFY(onFail->findData(QStringLiteral("JumpTo")) >= 0);
    QVERIFY(onFailTarget->isHidden());
    QVERIFY(advanced->toPlainText().trimmed().isEmpty());

    onFail->setCurrentIndex(onFail->findData(QStringLiteral("JumpTo")));
    QVERIFY(!onFailTarget->isHidden());
    const int measureTarget = onFailTarget->findData(QStringLiteral("measure"));
    QVERIFY(measureTarget >= 0);
    onFailTarget->setCurrentIndex(measureTarget);
    onError->setCurrentIndex(onError->findData(QStringLiteral("Abort")));
    onTimeout->setCurrentIndex(onTimeout->findData(QStringLiteral("Continue")));
    advanced->setPlainText(QStringLiteral(
        "{\n  \"cleanupRegionId\": \"main-cleanup\"\n}"));
    QVERIFY(editor.commitPendingChanges());

    auto policy = document.objectAt(stepPath)
                      .value(QStringLiteral("errorPolicy")).toObject();
    QCOMPARE(policy.value(QStringLiteral("onFail")).toString(),
             QStringLiteral("JumpTo"));
    QCOMPARE(policy.value(QStringLiteral("onFailTarget")).toString(),
             QStringLiteral("measure"));
    QCOMPARE(policy.value(QStringLiteral("onError")).toString(),
             QStringLiteral("Abort"));
    QCOMPARE(policy.value(QStringLiteral("onTimeout")).toString(),
             QStringLiteral("Continue"));
    QCOMPARE(policy.value(QStringLiteral("cleanupRegionId")).toString(),
             QStringLiteral("main-cleanup"));

    editor.setCurrentItem(stepPath);
    QCOMPARE(onFail->currentData().toString(), QStringLiteral("JumpTo"));
    QCOMPARE(onFailTarget->currentData().toString(), QStringLiteral("measure"));
    QCOMPARE(onError->currentData().toString(), QStringLiteral("Abort"));
    QCOMPARE(onTimeout->currentData().toString(), QStringLiteral("Continue"));
    QVERIFY(!advanced->toPlainText().contains(QStringLiteral("onFail")));
    QVERIFY(!advanced->toPlainText().contains(QStringLiteral("onError")));
    QVERIFY(!advanced->toPlainText().contains(QStringLiteral("onTimeout")));
    onFail->setCurrentIndex(onFail->findData(QStringLiteral("Inherit")));
    onError->setCurrentIndex(onError->findData(QStringLiteral("Inherit")));
    onTimeout->setCurrentIndex(onTimeout->findData(QStringLiteral("Inherit")));
    QVERIFY(editor.commitPendingChanges());
    policy = document.objectAt(stepPath)
                 .value(QStringLiteral("errorPolicy")).toObject();
    QVERIFY(!policy.contains(QStringLiteral("onFail")));
    QVERIFY(!policy.contains(QStringLiteral("onError")));
    QVERIFY(!policy.contains(QStringLiteral("onTimeout")));
    QVERIFY(!policy.contains(QStringLiteral("onFailTarget")));
    QCOMPARE(policy.value(QStringLiteral("cleanupRegionId")).toString(),
             QStringLiteral("main-cleanup"));
}

QTEST_MAIN(MainWindowLifecycleTests)

#include "MainWindowLifecycleTests.moc"
