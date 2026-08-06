#include <QtTest/QtTest>

#include "ExecutionViewModel.h"
#include "FlowTargetSelector.h"
#include "LoginDialog.h"
#include "MainWindow.h"
#include "OperatorPromptPresenter.h"
#include "ProductionWindow.h"
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

#include <QApplication>
#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QPixmap>
#include <QPainter>
#include <QSettings>
#include <QScreen>
#include <QScrollBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleOption>
#include <QStandardItemModel>
#include <QSplitter>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUndoStack>

#include <algorithm>

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

} // namespace

class MainWindowLifecycleTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void picoStyleDrawsFilledCheckedIndicators();
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
    void persistsLayoutAndRecentFiles();
    void invalidOrOffscreenGeometryFallsBackToPrimaryScreen();
    void loginDialogDiscoversSequenceAndValidatesAdminPassword();
    void adminStartupInitializationShowsBusyOverlay();
    void stationScanDialogTogglePersists();
    void scanDialogAcceptsRepeatedBarcodeAndHasNoWindowButtons();
    void adminStartsOnProductionDashboardAndOpensScannerOnDemand();
    void productionWindowPreloadsFlowAndRunsWithoutScanner();
    void productionLoopTestCountsAndArchivesEveryIteration();
    void productionWindowShowsSkippedStepsAndCleanupAfterFailure();
    void pluginPropertyEditorValidatesRequiredAndRangeAndSavesInputs();
    void pluginPropertyEditorAcceptsRevertedInvalidDraftAsNoOp();
    void pluginPropertyEditorInsertsPreviousStepOutputExpression();
    void pluginPropertyEditorSwitchesConditionalInputs();
    void logicalDeviceOpenKeepsStationParametersOutOfStepInputs();
    void pluginPropertyEditorPrefersTypedControlsAndPreservesAdvancedJson();
    void flowEditorAddsAndLocksStandardSequenceGroups();
    void resourceRegionGutterTogglesBoundariesAndSelectsHardware();
    void sequenceVariablesToolbarEditsPerUutValuesAndFeedsFxMenu();
    void ctrlSaveCommitsCurrentStepDraftWithoutPrompt();
    void switchingStepsKeepsDraftWithoutPrompt();
    void leavingFlowPromptsOnceAndCanKeepDraft();
    void stepEditorRelocatesDraftAfterSequenceStructureChanges();
    void limitPropertyEditorSwitchesComparisonFieldsAndRemovesStaleValues();
    void wrapsSelectedStepsInTestItemFromToolbar();
    void copiesAndPastesSelectedItemsFromToolbar();
    void flowBlankClickClearsSelection();
    void compileSilentlySavesCurrentDraft();
    void functionPalettePreviewsParametersAndShowsDragHandles();
    void flowTargetSelectorHandlesChannelsAndMoreDevices();
    void flowFieldInspectionAppearsImmediatelyAndFillsPanel();
    void proportionalHeaderDistributesAvailableWidthByWeight();
    void flowEnableTogglePreservesTreePosition();
    void flowDropTargetPrefersTestItemInterior();
    void operatorPromptDialogCannotBeDismissedByKeyboardOrWindowControls();
    void operatorPromptDialogReusesKeyForJudgment();
    void messageBoxPropertyEditorSwitchesConfirmationMode();
    void messageBoxPropertyEditorConfiguresJudgmentMode();
    void messageBoxPropertyEditorInsertsRuntimeValues();
    void parserPropertyEditorCreatesNamedOutputsForFx();
    void parserPropertyEditorSwitchesRegisterModes();
    void parserPropertyEditorPreservesExplicitEmptyEndMarker();
    void whileLoopPropertyEditorUsesTypedFields();
    void valueToolsPropertyEditorUsesExpressionList();
    void periodicActionPropertyEditorUsesTypedPolicyFields();

private:
    QTemporaryDir m_settingsDirectory;
};

void MainWindowLifecycleTests::initTestCase()
{
    qApp->setStyle(new PicoATEStyle);
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("PicoATE.Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("PicoATEUiWindowTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,
                       QSettings::UserScope,
                       m_settingsDirectory.path());
    QSettings().clear();
}

void MainWindowLifecycleTests::cleanupTestCase()
{
    QSettings().clear();
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

void MainWindowLifecycleTests::picoStyleDrawsFilledCheckedIndicators()
{
    auto* style = dynamic_cast<PicoATEStyle*>(qApp->style());
    QVERIFY(style);
    const auto renderIndicator = [&](QStyle::PrimitiveElement element,
                                     QStyle::State state) {
        QImage image(24, 24, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        QStyleOption option;
        option.rect = image.rect();
        option.state = state;
        style->drawPrimitive(element, &option, &painter);
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
    QCOMPARE(style->pixelMetric(QStyle::PM_IndicatorWidth), 18);
    QCOMPARE(style->pixelMetric(QStyle::PM_IndicatorHeight), 18);
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
          {"id":"001","name":"Read Frame","kind":"action",
           "moduleId":"plugin.can.test","function":"read"},
          {"id":"002","name":"Send Frame","kind":"action",
           "moduleId":"plugin.can.test","function":"write","inputs":{}},
          {"id":"003","name":"Check DLC","kind":"limit",
           "inputs":{"actual":""},"parameters":{"comparison":"between","expected":8}}
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
    editor.setCurrentItem(SequenceItemPath{0, {1}});
    editor.show();
    QTest::qWait(20);

    auto* timeout = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_timeoutMs"));
    QVERIFY(timeout);
    auto* picker = timeout->parentWidget()->findChild<QToolButton*>(
        QStringLiteral("expressionPickerButton"));
    QVERIFY(picker);
    QVERIFY(picker->menu());
    const auto findExpressionMenu = [](QMenu* menu,
                                       const QString& expression) -> QMenu* {
        for (auto* topLevelAction : menu->actions()) {
            auto* candidateMenu = topLevelAction->menu();
            if (!candidateMenu) {
                continue;
            }
            for (auto* action : candidateMenu->actions()) {
                if (action->data().toString() == expression) {
                    return candidateMenu;
                }
            }
        }
        return nullptr;
    };
    const auto outputExpression = QStringLiteral("${step:001.outputs.dlc}");
    auto* sourceMenu = findExpressionMenu(picker->menu(), outputExpression);
    QVERIFY(sourceMenu);
    QCOMPARE(sourceMenu->actions().size(), 1);
    sourceMenu->actions().first()->trigger();
    QCOMPARE(timeout->text(), outputExpression);
    QVERIFY(editor.commitPendingChanges());

    editor.setCurrentItem(SequenceItemPath{0, {2}});
    auto* actual = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyLimitActualEdit"));
    QVERIFY(actual);
    auto* limitPicker = actual->parentWidget()->findChild<QToolButton*>(
        QStringLiteral("expressionPickerButton"));
    QVERIFY(limitPicker);
    QVERIFY(limitPicker->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(limitPicker->menu(), "aboutToShow",
                                      Qt::DirectConnection));
    auto* limitSourceMenu = findExpressionMenu(limitPicker->menu(), outputExpression);
    QVERIFY(limitSourceMenu);
    limitSourceMenu->actions().first()->trigger();
    QCOMPARE(actual->text(), outputExpression);
    QVERIFY(editor.commitPendingChanges());
    QCOMPARE(document.objectAt(SequenceItemPath{0, {2}})
                 .value(QStringLiteral("inputs")).toObject()
                 .value(QStringLiteral("actual")).toString(),
             QStringLiteral("${step:001.outputs.dlc}"));
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
    QVERIFY(tree && model && document && targetSelector && tabs && flowPage);
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
        auto* table = dialog->findChild<QTableWidget*>(
            QStringLiteral("sequenceVariablesTable"));
        QVERIFY(add);
        QVERIFY(table);
        add->click();
        QCOMPARE(table->rowCount(), 1);
        table->item(0, 0)->setText(QStringLiteral("CAN_ID"));
        auto* type = qobject_cast<QComboBox*>(table->cellWidget(0, 1));
        auto* scope = qobject_cast<QComboBox*>(table->cellWidget(0, 2));
        QVERIFY(type);
        QVERIFY(scope);
        type->setCurrentIndex(type->findData(QStringLiteral("hex")));
        scope->setCurrentIndex(scope->findData(QStringLiteral("perUut")));
        table->item(0, 4)->setText(QStringLiteral("0x101"));
        table->item(0, 5)->setText(QStringLiteral("0x102"));
        table->item(0, 6)->setText(QStringLiteral("0x103"));
        table->item(0, 7)->setText(QStringLiteral("0x104"));
        table->item(0, 8)->setText(QStringLiteral("CAN identifier by fixture slot"));
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
    QCOMPARE(variable.value("values").toArray().size(), 4);
    QVERIFY(document->isModified());

    StepPropertyEditor editor(document);
    editor.setCurrentItem(SequenceItemPath{0, {0}});
    QToolButton* expressionButton = nullptr;
    QMenu* variableMenu = nullptr;
    for (auto* button : editor.findChildren<QToolButton*>(
             QStringLiteral("expressionPickerButton"))) {
        if (!button->menu()) {
            continue;
        }
        QMetaObject::invokeMethod(button->menu(), "aboutToShow",
                                  Qt::DirectConnection);
        for (auto* menuAction : button->menu()->actions()) {
            if (menuAction->menu() &&
                menuAction->text() == QStringLiteral("Sequence Variables")) {
                expressionButton = button;
                variableMenu = menuAction->menu();
                break;
            }
        }
        if (variableMenu) {
            break;
        }
    }
    QVERIFY(expressionButton);
    QVERIFY(variableMenu);
    QVERIFY(expressionButton->isEnabled());
    QCOMPARE(variableMenu->actions().size(), 1);
    variableMenu->actions().first()->trigger();
    bool expressionInserted = false;
    for (auto* lineEdit : editor.findChildren<QLineEdit*>()) {
        if (lineEdit->text() == QStringLiteral("${var.CAN_ID}")) {
            expressionInserted = true;
            break;
        }
    }
    QVERIFY(expressionInserted);

    QMenu* runtimeMenu = nullptr;
    for (auto* menuAction : expressionButton->menu()->actions()) {
        if (menuAction->menu() &&
            menuAction->text() == QStringLiteral("Runtime Values")) {
            runtimeMenu = menuAction->menu();
            break;
        }
    }
    QVERIFY(runtimeMenu);
    const auto runtimeActions = runtimeMenu->actions();
    const auto serialAction = std::find_if(
        runtimeActions.cbegin(), runtimeActions.cend(),
        [](const QAction* action) {
            return action->data().toString() ==
                   QStringLiteral("${var.serialNumber}");
        });
    QVERIFY(serialAction != runtimeActions.cend());
    (*serialAction)->trigger();
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
    QVERIFY(expectedPicker->menu());
    QVERIFY(QMetaObject::invokeMethod(expectedPicker->menu(), "aboutToShow",
                                      Qt::DirectConnection));
    QAction* serialNumberAction = nullptr;
    for (auto* sourceAction : expectedPicker->menu()->actions()) {
        if (!sourceAction->menu()) {
            continue;
        }
        for (auto* expressionAction : sourceAction->menu()->actions()) {
            if (expressionAction->data().toString() ==
                QStringLiteral("${var.serialNumber}")) {
                serialNumberAction = expressionAction;
                break;
            }
        }
        if (serialNumberAction) {
            break;
        }
    }
    QVERIFY(serialNumberAction);
    serialNumberAction->trigger();
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
    action->trigger();
    QVERIFY(search->isVisible());
    QVERIFY(search->width() <= 360);
    QVERIFY(search->width() < tree->width());
    QTest::keyClicks(search, QStringLiteral("deviceId"));
    QTest::keyPress(search, Qt::Key_Return);
    QCoreApplication::processEvents();

    QVERIFY(!tree->isColumnHidden(SequenceTreeModel::InspectionColumn));
    auto* model = qobject_cast<SequenceTreeModel*>(tree->model());
    QVERIFY(model);
    const auto mainGroup = sequenceGroupByKind(model, QStringLiteral("main"));
    QVERIFY(mainGroup.isValid());
    const auto keyIndex = model->index(
        0, SequenceTreeModel::InspectionColumn, mainGroup);
    QCOMPARE(keyIndex.data().toString(), QStringLiteral("CAN1.CH1"));
    QVERIFY(tree->visualRect(keyIndex).width() > 0);
    QCOMPARE(tree->maximumWidth(), QWIDGETSIZE_MAX);
    QVERIFY(tree->width() >= tree->parentWidget()->width() - 4);

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

    auto* model = qobject_cast<RuntimeTimelineModel*>(timelineView->model());
    QVERIFY(model);
    QVERIFY(model->rowCount() > 0);
    bool sawNodeEvent = false;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto event = model->eventAt(row);
        if (event &&
            (event->kind == PicoATE::Core::RuntimeEventKind::AttemptStarted ||
             event->kind == PicoATE::Core::RuntimeEventKind::AttemptCompleted)) {
            sawNodeEvent = true;
            break;
        }
    }
    QVERIFY(sawNodeEvent);

    QModelIndex measureEvent;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto event = model->eventAt(row);
        if (event && event->nodeDisplayName == QStringLiteral("Measure")) {
            measureEvent = model->index(row, RuntimeTimelineModel::MessageColumn);
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
    const auto focusedLog = model->eventAt(timelineView->currentIndex().row());
    QVERIFY(focusedLog.has_value());
    QCOMPARE(focusedLog->nodeDisplayName, QStringLiteral("Measure"));
    QCOMPARE(focusedLog->kind,
             PicoATE::Core::RuntimeEventKind::AttemptStarted);
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
    QCOMPARE(saved.value(QStringLiteral("MainWindow/UutCount")).toInt(), 3);

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
    QCOMPARE(restored.findChild<QSpinBox*>(
                 QStringLiteral("uutCountSpinBox"))->value(), 3);
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
    const auto sequencePath = directory.filePath(
        QStringLiteral("product_seq_v1.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));
    QFile station(directory.filePath(QStringLiteral("StationSystem.json")));
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({
        "stationId":"line-1",
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
    QCOMPARE(brand->accessibleName(), QStringLiteral("PICO"));
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
    QCOMPARE(password->text(), QStringLiteral("Admin 密码错误"));
    QCOMPARE(password->echoMode(), QLineEdit::Normal);
    QVERIFY(password->property("invalid").toBool());
    QVERIFY(password->hasFocus());
    QVERIFY(brandIsAboveFields());
    QCOMPARE(dialog.result(), 0);

    QTest::keyClicks(password, QStringLiteral("7"));
    QCOMPARE(password->text(), QStringLiteral("7"));
    QCOMPARE(password->echoMode(), QLineEdit::Password);
    QVERIFY(!password->property("invalid").toBool());

    password->setText(QString::number(StartupSupport::dailyAdminPassword()));
    login->click();
    QVERIFY(!login->isEnabled());
    QVERIFY(!spinner->isHidden());
    QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.selection().mode, UiMode::Admin);
    QCOMPARE(dialog.selection().sequencePath,
             QFileInfo(sequencePath).absoluteFilePath());
    QVERIFY(dialog.selection().scanDialogEnabled);
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

    window.initializeAdminWorkspace();
    QVERIFY(!overlay->isHidden());
    QVERIFY(!spinner->isHidden());
    QVERIFY(status->text().contains(QStringLiteral("Admin")));
    QTRY_VERIFY_WITH_TIMEOUT(overlay->isHidden(), 5000);
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
    auto* snLength = window.findChild<QSpinBox*>(
        QStringLiteral("stationSnLengthSpin"));
    auto* snPattern = window.findChild<QLineEdit*>(
        QStringLiteral("stationSnPatternEdit"));
    auto* snAllowedRegex = window.findChild<QLineEdit*>(
        QStringLiteral("stationSnAllowedRegexEdit"));
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
    QVERIFY(snLength);
    QVERIFY(snPattern);
    QVERIFY(snAllowedRegex);
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
    const auto paneSizes = workArea->sizes();
    QCOMPARE(paneSizes.size(), 3);
    QVERIFY(paneSizes[0] >= 210);
    QVERIFY(paneSizes[1] >= 360);
    QVERIFY(paneSizes[2] >= 240);
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
    QCOMPARE(snLength->value(), 0);
    QCOMPARE(jigNo->text(), QStringLiteral("JIG-01"));
    QCOMPARE(order->text(), QStringLiteral("ORDER-01"));
    QCOMPARE(tester->text(), QStringLiteral("Tester A"));
    stopOnFailure->setChecked(false);
    scanEnabled->setChecked(false);
    snLength->setValue(10);
    snPattern->setText(QStringLiteral("BTSN*"));
    snAllowedRegex->setText(QStringLiteral("^[A-Z0-9]+$"));
    jigNo->setText(QStringLiteral("JIG-02"));
    order->setText(QStringLiteral("ORDER-02"));
    tester->setText(QStringLiteral("Tester B"));
    QVERIFY(settingsEditor->hasPendingChanges());
    QVERIFY(settingsEditor->commitPendingChanges());
    QCOMPARE(document->rootObject().value(QStringLiteral("stopOnFailure")).toBool(),
             false);
    QCOMPARE(document->rootObject().value(QStringLiteral("scanDialogEnabled")).toBool(),
             false);
    QCOMPARE(document->rootObject().value(QStringLiteral("snLength")).toInt(), 10);
    QCOMPARE(document->rootObject().value(QStringLiteral("snPattern")).toString(),
             QStringLiteral("BTSN*"));
    QCOMPARE(document->rootObject().value(
                 QStringLiteral("snAllowedRegex")).toString(),
             QStringLiteral("^[A-Z0-9]+$"));
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
    const auto rules = StartupSupport::stationSnValidationRules(stationPath);
    QCOMPARE(rules.wildcardPattern, QStringLiteral("BTSN*"));
    QCOMPARE(rules.allowedRegex, QStringLiteral("^[A-Z0-9]+$"));
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
    station.write(R"({"stationId":"bench-01","scanDialogEnabled":true,"devices":[]})");
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
    auto* stationLabel = window.findChild<QLabel*>(QStringLiteral("adminStationLabel"));
    QVERIFY(tabs);
    QVERIFY(viewModel);
    QVERIFY(resultView);
    QVERIFY(scanAction);
    QVERIFY(compileAction);
    QVERIFY(scanDialog);
    QVERIFY(sequenceLabel);
    QVERIFY(stationLabel);
    QCOMPARE(tabs->count(), 4);
    QCOMPARE(tabs->currentIndex(), 0);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Run Test"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("Flow Editor"));
    QCOMPARE(tabs->tabText(2), QStringLiteral("Station Config"));
    QCOMPARE(tabs->tabText(3), QStringLiteral("Reports"));
    QCOMPARE(sequenceLabel->text(), QStringLiteral("simple_sequence.json"));
    QCOMPARE(stationLabel->text(), QStringLiteral("bench-01"));
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

    const auto screenshotPath = qEnvironmentVariable("PICOATE_ADMIN_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        scanDialog->hide();
        QVERIFY2(window.grab().save(screenshotPath), qPrintable(screenshotPath));
    }

    auto* barcode = scanDialog->findChild<QLineEdit*>(QStringLiteral("barcodeEdit"));
    auto* serialLabel = window.findChild<QLabel*>(QStringLiteral("adminSerialLabel"));
    QVERIFY(barcode);
    QVERIFY(serialLabel);
    barcode->setText(QStringLiteral("ADMIN-SN-001"));
    QVERIFY(QMetaObject::invokeMethod(scanDialog, "submitBarcode"));
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             3000);
    QCOMPARE(viewModel->state(), UiRunState::Completed);
    QCOMPARE(serialLabel->text(), QStringLiteral("ADMIN-SN-001"));
    QVERIFY(scanDialog->isHidden());
    QCOMPARE(resultView->model()
                 ->data(resultView->currentIndex().siblingAtColumn(
                     UutStepModel::NameColumn))
                 .toString(),
             QStringLiteral("Power Off"));
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
    station.write(R"({"stationId":"line-1","scanDialogEnabled":false,"devices":[]})");
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
    auto* overall = window.findChild<QLabel*>(
        QStringLiteral("productionOverallResult"));
    auto* stationLabel = window.findChild<QLabel*>(
        QStringLiteral("productionStationLabel"));
    auto* passCount = window.findChild<QLabel*>(
        QStringLiteral("productionPassCount"));
    auto* failCount = window.findChild<QLabel*>(
        QStringLiteral("productionFailCount"));
    auto* totalCount = window.findChild<QLabel*>(
        QStringLiteral("productionTotalCount"));
    auto* yieldLabel = window.findChild<QLabel*>(
        QStringLiteral("productionYield"));
    auto* contentSplitter = window.findChild<QSplitter*>(
        QStringLiteral("productionContentSplitter"));
    auto* dataSplitter = window.findChild<QSplitter*>(
        QStringLiteral("productionDataSplitter"));
    auto* scan = window.findChild<ScanDialog*>();
    QVERIFY(viewModel);
    QVERIFY(resultView);
    QVERIFY(start);
    QVERIFY(overall);
    QVERIFY(stationLabel);
    QVERIFY(passCount);
    QVERIFY(failCount);
    QVERIFY(totalCount);
    QVERIFY(yieldLabel);
    QVERIFY(contentSplitter);
    QVERIFY(dataSplitter);
    QVERIFY(scan);
    QCOMPARE(stationLabel->text(), QStringLiteral("line-1"));
    QCOMPARE(contentSplitter->orientation(), Qt::Horizontal);
    QCOMPARE(dataSplitter->orientation(), Qt::Vertical);
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
    QVERIFY(scan->isHidden());
    QCOMPARE(passCount->text(), QStringLiteral("PASS 0"));
    QCOMPARE(failCount->text(), QStringLiteral("FAIL 0"));
    QCOMPARE(totalCount->text(), QStringLiteral("TOTAL 0"));
    QCOMPARE(yieldLabel->text(), QStringLiteral("YIELD 0.00%"));

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
    QCOMPARE(yieldLabel->text(), QStringLiteral("YIELD 100.00%"));
    QCOMPARE(resultView->model()->rowCount(), 3);
    const auto completedSetup = resultView->model()->index(0, 0);
    QVERIFY(resultView->model()->rowCount(completedSetup) > 0);

    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 1000);
    start->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(passCount->text(), QStringLiteral("PASS 2"), 3000);
    QCOMPARE(failCount->text(), QStringLiteral("FAIL 0"));
    QCOMPARE(totalCount->text(), QStringLiteral("TOTAL 2"));
    QCOMPARE(yieldLabel->text(), QStringLiteral("YIELD 100.00%"));
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
    QVERIFY(averageTime->text().startsWith(QStringLiteral("AVG ")));

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
    auto* yieldLabel = window.findChild<QLabel*>(QStringLiteral("productionYield"));
    QVERIFY(viewModel);
    QVERIFY(start);
    QVERIFY(passCount);
    QVERIFY(failCount);
    QVERIFY(totalCount);
    QVERIFY(yieldLabel);
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
    QCOMPARE(yieldLabel->text(), QStringLiteral("YIELD 0.00%"));
}

void MainWindowLifecycleTests::operatorPromptDialogCannotBeDismissedByKeyboardOrWindowControls()
{
    const auto imageDirectory = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("image"));
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

void MainWindowLifecycleTests::messageBoxPropertyEditorSwitchesConfirmationMode()
{
    const auto imageDirectory = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("image"));
    QVERIFY(QDir().mkpath(imageDirectory));
    const auto imageFileName = QStringLiteral("picoate_prompt_editor_test.jpg");
    const auto imagePath = QDir(imageDirectory).filePath(imageFileName);
    QPixmap sourceImage(60, 30);
    sourceImage.fill(QColor(QStringLiteral("#d7ecff")));
    QVERIFY(sourceImage.save(imagePath, "PNG"));

    const auto path = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/operator_prompt_sequence.json");
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
    QVERIFY(picker->menu());
    QVERIFY(QMetaObject::invokeMethod(
        picker->menu(), "aboutToShow", Qt::DirectConnection));

    const auto findExpression = [](QMenu* root, const QString& expression,
                                   const auto& self) -> QAction* {
        if (!root) {
            return nullptr;
        }
        for (auto* action : root->actions()) {
            if (action->data().toString() == expression) {
                return action;
            }
            if (auto* child = action->menu()) {
                if (auto* found = self(child, expression, self)) {
                    return found;
                }
            }
        }
        return nullptr;
    };
    auto* uutAction = findExpression(
        picker->menu(), QStringLiteral("${uut.id}"), findExpression);
    auto* variableAction = findExpression(
        picker->menu(), QStringLiteral("${var.targetVoltage}"), findExpression);
    QVERIFY(uutAction);
    QVERIFY(variableAction);

    message->moveCursor(QTextCursor::End);
    message->insertPlainText(QStringLiteral("\nUUT: "));
    uutAction->trigger();
    message->insertPlainText(QStringLiteral("; Target: "));
    variableAction->trigger();
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
    QVERIFY(picker->menu());
    QVERIFY(QMetaObject::invokeMethod(
        picker->menu(), "aboutToShow", Qt::DirectConnection));

    QAction* voltageAction = nullptr;
    for (auto* sourceAction : picker->menu()->actions()) {
        if (!sourceAction->menu()) {
            continue;
        }
        for (auto* outputAction : sourceAction->menu()->actions()) {
            if (outputAction->text().startsWith(QStringLiteral("voltage "))) {
                voltageAction = outputAction;
                break;
            }
        }
    }
    QVERIFY(voltageAction);
    voltageAction->trigger();
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
    QVERIFY(kind);
    const int testItemIndex = kind->findData(QStringLiteral("testItem"));
    QVERIFY(testItemIndex >= 0);
    kind->setCurrentIndex(testItemIndex);
    QVERIFY(editor.commitPendingChanges());

    const auto converted = document.objectAt(loopPath);
    QCOMPARE(converted.value(QStringLiteral("kind")).toString(),
             QStringLiteral("testItem"));
    QVERIFY(!converted.contains(QStringLiteral("loop")));
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

QTEST_MAIN(MainWindowLifecycleTests)

#include "MainWindowLifecycleTests.moc"
