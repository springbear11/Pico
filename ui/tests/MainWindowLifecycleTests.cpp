#include <QtTest/QtTest>

#include "ExecutionViewModel.h"
#include "LoginDialog.h"
#include "MainWindow.h"
#include "ProductionWindow.h"
#include "ProportionalHeaderView.h"
#include "PluginCatalog.h"
#include "PluginFunctionModel.h"
#include "RunnerModels.h"
#include "ScanDialog.h"
#include "SequenceDocument.h"
#include "SequenceTreeModel.h"
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
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QLineEdit>
#include <QPushButton>
#include <QPixmap>
#include <QSettings>
#include <QScreen>
#include <QScrollBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardItemModel>
#include <QSplitter>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUndoStack>

#include <algorithm>

using namespace PicoATE::Ui;

class MainWindowLifecycleTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void closeAfterEditedRun_data();
    void closeAfterEditedRun();
    void stationEditorFeedsCompileSnapshot_data();
    void stationEditorFeedsCompileSnapshot();
    void stationConnectionActionUpdatesStatus();
    void stationDeviceApplyPreservesDllPathWhenModelIsUnchanged();
    void compileFailureFocusesDiagnosticAndExplainsDisabledRun();
    void stationCtrlSaveCommitsDraftAndClearsWindowMarker();
    void switchingStationDevicesCanDiscardCurrentDraft();
    void disabledReferencedDeviceDiagnosticPersistsAcrossEditors();
    void runActionSyncsTreeBreakpointsAndStopsAtBreakpoint();
    void runPopulatesRuntimeTimeline();
    void persistsLayoutAndRecentFiles();
    void invalidOrOffscreenGeometryFallsBackToPrimaryScreen();
    void loginDialogDiscoversSequenceAndValidatesAdminPassword();
    void stationScanDialogTogglePersists();
    void scanDialogAcceptsRepeatedBarcodeAndHasNoWindowButtons();
    void adminStartsOnProductionDashboardAndOpensScannerOnDemand();
    void productionWindowPreloadsFlowAndRunsWithoutScanner();
    void productionWindowShowsSkippedStepsAndCleanupAfterFailure();
    void pluginPropertyEditorValidatesRequiredAndRangeAndSavesInputs();
    void pluginPropertyEditorInsertsPreviousStepOutputExpression();
    void logicalDeviceOpenKeepsStationParametersOutOfStepInputs();
    void ctrlSaveConfirmsAndCommitsCurrentStepDraft();
    void switchingStepsCanDiscardCurrentDraft();
    void limitPropertyEditorSwitchesComparisonFieldsAndRemovesStaleValues();
    void wrapsSelectedStepsInTestItemFromToolbar();
    void copiesAndPastesSelectedItemsFromToolbar();
    void flowBlankClickClearsSelection();
    void compileSilentlySavesCurrentDraft();
    void functionPalettePreviewsParametersAndShowsDragHandles();
    void proportionalHeaderDistributesAvailableWidthByWeight();
    void flowEnableTogglePreservesTreePosition();

private:
    QTemporaryDir m_settingsDirectory;
};

void MainWindowLifecycleTests::initTestCase()
{
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

    const auto group = model->index(0, SequenceTreeModel::NameColumn);
    const auto first = model->index(0, SequenceTreeModel::NameColumn, group);
    const auto second = model->index(1, SequenceTreeModel::NameColumn, group);
    tree->setCurrentIndex(first);
    tree->selectionModel()->select(
        first, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    tree->selectionModel()->select(
        second, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QVERIFY(action->isEnabled());

    action->trigger();
    const auto refreshedGroup = model->index(0, SequenceTreeModel::NameColumn);
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
    QVERIFY(!document->isModified());
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

    const auto group = model->index(0, SequenceTreeModel::NameColumn);
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
    const auto refreshedGroup = model->index(0, SequenceTreeModel::NameColumn);
    QCOMPARE(model->rowCount(refreshedGroup), 4);
    QCOMPARE(document->undoStack()->undoText(),
             QStringLiteral("Paste Selected Steps"));
    QCOMPARE(document->objectAt(SequenceItemPath{0, {1}})
             .value(QStringLiteral("id")).toString(),
             QStringLiteral("001"));
    QCOMPARE(document->objectAt(SequenceItemPath{0, {2}})
             .value(QStringLiteral("id")).toString(),
             QStringLiteral("002"));
    document->undoStack()->undo();
    QCOMPARE(model->rowCount(model->index(0, SequenceTreeModel::NameColumn)), 2);
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

    const auto group = sequenceModel->index(0, SequenceTreeModel::NameColumn);
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
        {QStringLiteral("groups"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("main")},
            {QStringLiteral("name"), QStringLiteral("Main")},
            {QStringLiteral("kind"), QStringLiteral("main")},
            {QStringLiteral("steps"), steps}}}}};
    QFile file(sequencePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const auto bytes = QJsonDocument(root).toJson();
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    window.resize(1100, 650);
    window.show();
    QTest::qWait(30);

    auto* tree = window.findChild<QTreeView*>(QStringLiteral("sequenceTreeView"));
    auto* model = window.findChild<SequenceTreeModel*>();
    QVERIFY(tree && model);
    const auto group = model->index(0, SequenceTreeModel::NameColumn);
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
                            model->index(0, SequenceTreeModel::NameColumn));
    QVERIFY(target.isValid());
    QCOMPARE(model->pathForIndex(tree->currentIndex()), targetPath);
    QVERIFY(qAbs(tree->verticalScrollBar()->value() - scrollBefore) <= 1);
    QVERIFY(!tree->isExpanded(testItem));
    QVERIFY(tree->visualRect(target).intersects(tree->viewport()->rect()));
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

    const QByteArray description = R"({
      "name": "GCAN USB-CAN",
      "category": "CAN",
      "functions": [{
        "id": "write",
        "name": "Send CAN Frame",
        "inputs": [
          {"key": "data", "name": "Frame Data", "type": "hex-bytes", "required": true},
          {"key": "timeoutMs", "name": "Timeout", "type": "integer", "required": false,
           "minimum": 1, "maximum": 60000, "unit": "ms"}
        ],
        "outputs": []
      }]
    })";
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
    auto* data = editor.findChild<QLineEdit*>(QStringLiteral("pluginInput_data"));
    auto* timeout = editor.findChild<QLineEdit*>(
        QStringLiteral("pluginInput_timeoutMs"));
    auto* error = editor.findChild<QLabel*>(QStringLiteral("propertyErrorLabel"));
    QVERIFY(parameterGroup && !parameterGroup->isHidden());
    QVERIFY(data);
    QVERIFY(timeout);
    QVERIFY(error);

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
    QVERIFY(editor.commitPendingChanges());
    QVERIFY(!error->isVisible());
    const auto inputs = document.objectAt(path).value(QStringLiteral("inputs")).toObject();
    QCOMPARE(inputs.value(QStringLiteral("data")).toString(),
             QStringLiteral("01 02 03 04"));
    QCOMPARE(inputs.value(QStringLiteral("timeoutMs")).toInt(), 1500);
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
    QCOMPARE(picker->menu()->actions().size(), 1);
    auto* sourceMenu = picker->menu()->actions().first()->menu();
    QVERIFY(sourceMenu);
    QCOMPARE(sourceMenu->actions().size(), 1);
    sourceMenu->actions().first()->trigger();
    QCOMPARE(timeout->text(), QStringLiteral("${step:001.outputs.dlc}"));
    QVERIFY(editor.commitPendingChanges());

    editor.setCurrentItem(SequenceItemPath{0, {2}});
    auto* actual = editor.findChild<QLineEdit*>(
        QStringLiteral("propertyLimitActualEdit"));
    QVERIFY(actual);
    auto* limitPicker = actual->parentWidget()->findChild<QToolButton*>(
        QStringLiteral("expressionPickerButton"));
    QVERIFY(limitPicker);
    QVERIFY(limitPicker->isEnabled());
    auto* limitSourceMenu = limitPicker->menu()->actions().first()->menu();
    QVERIFY(limitSourceMenu);
    limitSourceMenu->actions().first()->trigger();
    QCOMPARE(actual->text(), QStringLiteral("${step:001.outputs.dlc}"));
    QVERIFY(editor.commitPendingChanges());
    QCOMPARE(document.objectAt(SequenceItemPath{0, {2}})
                 .value(QStringLiteral("inputs")).toObject()
                 .value(QStringLiteral("actual")).toString(),
             QStringLiteral("${step:001.outputs.dlc}"));
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
          "inputs":{"deviceId":"CX_CAN2"}
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
    QCOMPARE(group->title(), QStringLiteral("Connection Parameters - Station Config"));
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

void MainWindowLifecycleTests::ctrlSaveConfirmsAndCommitsCurrentStepDraft()
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

    QFile saved(sequencePath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto root = QJsonDocument::fromJson(saved.readAll()).object();
    const auto savedStep = root.value(QStringLiteral("groups")).toArray().at(0)
                               .toObject().value(QStringLiteral("steps")).toArray().at(0)
                               .toObject();
    QCOMPARE(savedStep.value(QStringLiteral("name")).toString(),
             QStringLiteral("Saved through Ctrl+S"));
}

void MainWindowLifecycleTests::switchingStepsCanDiscardCurrentDraft()
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
    const auto originalName = document->objectAt(firstPath)
                                  .value(QStringLiteral("name")).toString();
    auto* name = editor->findChild<QLineEdit*>(QStringLiteral("propertyNameEdit"));
    QVERIFY(name);
    name->setText(QStringLiteral("Discard this draft"));
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
    tree->setCurrentIndex(second);
    QCOMPARE(editor->currentPath(), model->pathForIndex(second));
    QVERIFY(!editor->hasPendingChanges());
    QCOMPARE(document->objectAt(firstPath).value(QStringLiteral("name")).toString(),
             originalName);
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
    QCOMPARE(comparison->currentData().toString(),
             QStringLiteral("betweenTolerance"));
    QVERIFY(!expected->isHidden());
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
    QVERIFY(expected->isHidden());
    QVERIFY(editor.commitPendingChanges());
    parameters = document.objectAt(path).value(QStringLiteral("parameters")).toObject();
    QCOMPARE(parameters.value(QStringLiteral("comparison")).toString(),
             QStringLiteral("between"));
    QCOMPARE(parameters.value(QStringLiteral("lower")).toDouble(), 1.5);
    QCOMPARE(parameters.value(QStringLiteral("upper")).toDouble(), 9.5);
    QVERIFY(!parameters.contains(QStringLiteral("expected")));

    comparison->setCurrentIndex(comparison->findData(QStringLiteral("isTrue")));
    QVERIFY(expected->isHidden());
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
    const auto sequencePath = temporaryDirectory.filePath(
        QStringLiteral("sequence.json"));
    QVERIFY(QFile::copy(
        QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
            + QStringLiteral("/examples/simple_sequence.json"),
        sequencePath));
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
            "x-device": 42
        }]
    })");
    stationFile.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    QVERIFY(window.openStationFile(stationPath));
    window.show();
    QTest::qWait(20);

    auto* stationDocument = window.findChild<StationDocument*>();
    auto* stationModel = window.findChild<StationDeviceModel*>();
    auto* stationView = window.findChild<QTableView*>(
        QStringLiteral("stationDeviceView"));
    auto* driverEdit = window.findChild<QLineEdit*>(
        QStringLiteral("deviceDriverIdEdit"));
    auto* propertyEditor = window.findChild<StationPropertyEditor*>();
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    QVERIFY(stationDocument);
    QVERIFY(stationModel);
    QVERIFY(stationView);
    QVERIFY(driverEdit);
    QVERIFY(propertyEditor);
    QVERIFY(!window.findChild<QPushButton*>(
        QStringLiteral("applyDevicePropertiesButton")));
    QVERIFY(viewModel);

    stationView->setCurrentIndex(stationModel->index(0, 0));
    driverEdit->setFocus();
    driverEdit->selectAll();
    QTest::keyClicks(driverEdit, QStringLiteral("snapshot.driver"));
    QVERIFY(propertyEditor->hasPendingChanges());
    QVERIFY(propertyEditor->commitPendingChanges());
    QCOMPARE(stationDocument->deviceAt(0).value("driverId").toString(),
             QString("snapshot.driver"));
    QCOMPARE(stationDocument->deviceAt(0).value("x-device").toInt(), 42);
    QVERIFY(stationDocument->isModified());

    viewModel->compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);

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
            {QStringLiteral("deviceId"), QStringLiteral("GCAN_CAN1")},
            {QStringLiteral("deviceType"), QStringLiteral("CAN")},
            {QStringLiteral("driverId"), QStringLiteral("plugin.can.gcan")},
            {QStringLiteral("pluginPath"), deployedDll},
            {QStringLiteral("enabled"), true}}}}})
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
    editor.setPluginRegistry({plugin});
    editor.setCurrentDevice(0);

    auto* enabled = editor.findChild<QAbstractButton*>(
        QStringLiteral("deviceEnabledSwitch"));
    auto* combo = editor.findChild<QComboBox*>(
        QStringLiteral("devicePluginCombo"));
    auto* errorLabel = editor.findChild<QLabel*>(
        QStringLiteral("devicePropertyError"));
    QVERIFY(enabled);
    QVERIFY(!editor.findChild<QPushButton*>(
        QStringLiteral("applyDevicePropertiesButton")));
    QVERIFY(combo);
    QVERIFY(errorLabel);
    QCOMPARE(combo->currentData().toString(), QStringLiteral("plugin.can.gcan"));

    enabled->setChecked(false);
    QVERIFY(editor.hasPendingChanges());
    QVERIFY(editor.commitPendingChanges());
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("pluginPath")).toString(),
             deployedDll);
    QVERIFY(!document.deviceAt(0).value(QStringLiteral("enabled")).toBool());

    enabled->setChecked(true);
    QVERIFY(editor.commitPendingChanges());
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("pluginPath")).toString(),
             deployedDll);
    QVERIFY(document.deviceAt(0).value(QStringLiteral("enabled")).toBool());

    QVERIFY(editor.commitPendingChanges());
    QVERIFY(errorLabel->isHidden());
    QCOMPARE(document.deviceAt(0).value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("GCAN_CAN1"));
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
            "deviceId":"CAN1",
            "deviceType":"CAN",
            "driverId":"manual.can",
            "pluginPath":"manual.dll",
            "address":"CAN:0:0",
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
    QTest::keyClicks(address, QStringLiteral("CAN:0:1"));
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
    QCOMPARE(stationDocument->deviceAt(0).value(QStringLiteral("address")).toString(),
             QStringLiteral("CAN:0:1"));
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
                 .value(QStringLiteral("address")).toString(),
             QStringLiteral("CAN:0:1"));
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
            {"deviceId":"CAN1","deviceType":"CAN","driverId":"manual.can",
             "address":"CAN:0:0","enabled":false},
            {"deviceId":"CAN2","deviceType":"CAN","driverId":"manual.can",
             "address":"CAN:0:1","enabled":false}
        ]
    })");
    stationFile.close();

    MainWindow window;
    QVERIFY(window.openStationFile(stationPath));
    window.show();
    auto* document = window.findChild<StationDocument*>();
    auto* model = window.findChild<StationDeviceModel*>();
    auto* view = window.findChild<QTableView*>(QStringLiteral("stationDeviceView"));
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
    QCOMPARE(document->deviceAt(0).value(QStringLiteral("address")).toString(),
             QStringLiteral("CAN:0:0"));
    QCOMPARE(address->text(), QStringLiteral("CAN:0:1"));
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
            if (message.contains(QStringLiteral("GCAN_CAN1")) &&
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
    const auto oldAddress = qgetenv("DMM1_ADDRESS");
    qputenv("DMM1_ADDRESS", QByteArray("USB0::TEST::INSTR"));
    const QString projectDir = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR);

    MainWindow window;
    QVERIFY(window.openSequenceFile(
        projectDir + QStringLiteral("/examples/dmm_can_adapter_sequence.json")));
    QVERIFY(window.openStationFile(
        projectDir + QStringLiteral("/examples/stations/basic_station.json")));
    window.show();
    QTest::qWait(20);

    auto* stationModel = window.findChild<StationDeviceModel*>();
    auto* stationView = window.findChild<QTableView*>(
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
    QVERIFY(window.close());

    if (oldAddress.isNull()) {
        qunsetenv("DMM1_ADDRESS");
    } else {
        qputenv("DMM1_ADDRESS", oldAddress);
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
    QVERIFY(treeView);
    QVERIFY(treeModel);
    QVERIFY(viewModel);
    QVERIFY(runAction);

    const auto setupGroup = treeModel->index(0, SequenceTreeModel::NameColumn);
    const auto firstStep = treeModel->index(0, SequenceTreeModel::NameColumn, setupGroup);
    QVERIFY(firstStep.isValid());
    QCOMPARE(treeModel->nodePathForIndex(firstStep), QString("open-fixture"));
    QVERIFY(treeModel->setData(firstStep.siblingAtColumn(
                                   SequenceTreeModel::BreakpointColumn),
                               Qt::Checked,
                               Qt::CheckStateRole));
    QCOMPARE(treeModel->breakpointSpecs().size(), 1);

    viewModel->compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 3000);
    runAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Paused, 3000);
    QVERIFY(viewModel->debugSnapshot().has_value());
    QCOMPARE(viewModel->debugSnapshot()->currentNodeId, QString("open-fixture"));
    QTRY_COMPARE_WITH_TIMEOUT(
        treeModel->nodePathForIndex(treeView->currentIndex()),
        QString("open-fixture"),
        1000);

    viewModel->stop();
    QTRY_VERIFY_WITH_TIMEOUT(viewModel->state() == UiRunState::Completed ||
                             viewModel->state() == UiRunState::Failed,
                             3000);
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

    auto* model = timelineView->model();
    QVERIFY(model);
    QVERIFY(model->rowCount() > 0);
    bool sawNodeEvent = false;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto eventName = model->data(
            model->index(row, RuntimeTimelineModel::EventColumn)).toString();
        if (eventName == QStringLiteral("NodeStateChanged") ||
            eventName == QStringLiteral("AttemptCompleted")) {
            sawNodeEvent = true;
            break;
        }
    }
    QVERIFY(sawNodeEvent);

    QModelIndex measureEvent;
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->data(model->index(row, RuntimeTimelineModel::StepColumn)).toString() ==
            QStringLiteral("Measure")) {
            measureEvent = model->index(row, RuntimeTimelineModel::EventColumn);
            break;
        }
    }
    QVERIFY(measureEvent.isValid());

    auto* details = window.findChild<QTabWidget*>(QStringLiteral("runDetailsTabs"));
    QVERIFY(details);
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
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("loginModeCombo"));
    auto* sequences = dialog.findChild<QComboBox*>(
        QStringLiteral("loginSequenceCombo"));
    auto* password = dialog.findChild<QLineEdit*>(
        QStringLiteral("loginAdminPassword"));
    auto* login = dialog.findChild<QPushButton*>(QStringLiteral("loginButton"));
    auto* error = dialog.findChild<QLabel*>(QStringLiteral("loginErrorLabel"));
    QVERIFY(mode);
    QVERIFY(sequences);
    QVERIFY(password);
    QVERIFY(login);
    QVERIFY(error);
    QVERIFY(!dialog.findChild<QLabel*>(QStringLiteral("loginStationPath")));
    QCOMPARE(UiMode(mode->currentData().toInt()), UiMode::Test);
    QCOMPARE(sequences->count(), 1);
    QVERIFY(password->isHidden());

    login->click();
    QVERIFY(!error->isHidden());
    QCOMPARE(dialog.result(), 0);

    mode->setCurrentIndex(1);
    QVERIFY(!password->isHidden());
    password->setText(QStringLiteral("-1"));
    login->click();
    QVERIFY(!error->isHidden());
    QCOMPARE(dialog.result(), 0);

    password->setText(QString::number(StartupSupport::dailyAdminPassword()));
    login->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.selection().mode, UiMode::Admin);
    QCOMPARE(dialog.selection().sequencePath,
             QFileInfo(sequencePath).absoluteFilePath());
    QVERIFY(dialog.selection().scanDialogEnabled);
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
    auto* settingsEditor = window.findChild<StationSettingsEditor*>();
    auto* workArea = window.findChild<QSplitter*>(
        QStringLiteral("stationWorkSplitter"));
    auto* deviceView = window.findChild<QTableView*>(
        QStringLiteral("stationDeviceView"));
    auto* document = window.findChild<StationDocument*>();
    QVERIFY(stopOnFailure);
    QVERIFY(scanEnabled);
    QVERIFY(snLength);
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
    QTest::mouseClick(deviceView->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      deviceView->visualRect(enabledIndex).center());
    QCOMPARE(document->deviceAt(0).value(QStringLiteral("enabled")).toBool(),
             true);
    QVERIFY(stopOnFailure->isChecked());
    QVERIFY(scanEnabled->isChecked());
    QCOMPARE(snLength->value(), 0);
    stopOnFailure->setChecked(false);
    scanEnabled->setChecked(false);
    snLength->setValue(10);
    QVERIFY(settingsEditor->hasPendingChanges());
    QVERIFY(settingsEditor->commitPendingChanges());
    QCOMPARE(document->rootObject().value(QStringLiteral("stopOnFailure")).toBool(),
             false);
    QCOMPARE(document->rootObject().value(QStringLiteral("scanDialogEnabled")).toBool(),
             false);
    QCOMPARE(document->rootObject().value(QStringLiteral("snLength")).toInt(), 10);
    QString errorMessage;
    QVERIFY(document->save(&errorMessage));
    QVERIFY2(!StartupSupport::stationScanDialogEnabled(stationPath),
             qPrintable(errorMessage));
    QCOMPARE(StartupSupport::stationSnLength(stationPath), 10);
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

    QCloseEvent closeEvent;
    QCoreApplication::sendEvent(&dialog, &closeEvent);
    QVERIFY(!closeEvent.isAccepted());

    QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    QCOMPARE(barcodeSpy.count(), 0);
    QVERIFY(!error->isHidden());
    dialog.setExpectedLength(6);
    barcode->setText(QStringLiteral("12345"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "submitBarcode"));
    QCOMPARE(barcodeSpy.count(), 0);
    QVERIFY(!error->isHidden());
    barcode->setText(QStringLiteral("1234567"));
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
    const auto cleanup = findStep(QStringLiteral("close-device"));
    QVERIFY(failed != steps.cend());
    QVERIFY(skipped != steps.cend());
    QVERIFY(cleanup != steps.cend());
    QCOMPARE(failed->state, PicoATE::Core::ActivationState::Failed);
    QCOMPARE(skipped->state, PicoATE::Core::ActivationState::Skipped);
    QCOMPARE(cleanup->state, PicoATE::Core::ActivationState::Passed);
    QCOMPARE(passCount->text(), QStringLiteral("PASS 0"));
    QCOMPARE(failCount->text(), QStringLiteral("FAIL 1"));
    QCOMPARE(totalCount->text(), QStringLiteral("TOTAL 1"));
    QCOMPARE(yieldLabel->text(), QStringLiteral("YIELD 0.00%"));
}

QTEST_MAIN(MainWindowLifecycleTests)

#include "MainWindowLifecycleTests.moc"
