#include <QtTest/QtTest>

#include "ExecutionViewModel.h"
#include "LoginDialog.h"
#include "MainWindow.h"
#include "ProductionWindow.h"
#include "PluginCatalog.h"
#include "RunnerModels.h"
#include "ScanDialog.h"
#include "SequenceDocument.h"
#include "SequenceTreeModel.h"
#include "StepPropertyEditor.h"
#include "StationDeviceModel.h"
#include "StationDocument.h"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QLineEdit>
#include <QPushButton>
#include <QPixmap>
#include <QSettings>
#include <QScreen>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
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
    void wrapsSelectedStepsInTestItemFromToolbar();

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
    auto* apply = editor.findChild<QPushButton*>(
        QStringLiteral("applyPropertiesButton"));
    auto* error = editor.findChild<QLabel*>(QStringLiteral("propertyErrorLabel"));
    QVERIFY(parameterGroup && !parameterGroup->isHidden());
    QVERIFY(data);
    QVERIFY(timeout);
    QVERIFY(apply);
    QVERIFY(error);

    apply->click();
    QVERIFY(error->isVisible());
    QVERIFY(error->text().contains(QStringLiteral("required"), Qt::CaseInsensitive));

    data->setText(QStringLiteral("01 02 03 04"));
    timeout->setText(QStringLiteral("70000"));
    apply->click();
    QVERIFY(error->text().contains(QStringLiteral("range"), Qt::CaseInsensitive));

    timeout->setText(QStringLiteral("1500"));
    apply->click();
    QVERIFY(!error->isVisible());
    const auto inputs = document.objectAt(path).value(QStringLiteral("inputs")).toObject();
    QCOMPARE(inputs.value(QStringLiteral("data")).toString(),
             QStringLiteral("01 02 03 04"));
    QCOMPARE(inputs.value(QStringLiteral("timeoutMs")).toInt(), 1500);
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
    auto* applyButton = window.findChild<QPushButton*>(
        QStringLiteral("applyDevicePropertiesButton"));
    auto* viewModel = window.findChild<ExecutionViewModel*>();
    QVERIFY(stationDocument);
    QVERIFY(stationModel);
    QVERIFY(stationView);
    QVERIFY(driverEdit);
    QVERIFY(applyButton);
    QVERIFY(viewModel);

    stationView->setCurrentIndex(stationModel->index(0, 0));
    driverEdit->setText(QStringLiteral("snapshot.driver"));
    QTest::mouseClick(applyButton, Qt::LeftButton);
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
    station.write(R"({"stationId":"line-1","scanDialogEnabled":true,"devices":[]})");
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
    station.write(R"({"stationId":"line-1","devices":[]})");
    station.close();

    MainWindow window;
    QVERIFY(window.openSequenceFile(sequencePath));
    QVERIFY(window.openStationFile(stationPath));
    window.show();
    QTest::qWait(20);
    auto* scanEnabled = window.findChild<QCheckBox*>(
        QStringLiteral("scanDialogEnabledCheck"));
    auto* apply = window.findChild<QPushButton*>(
        QStringLiteral("applyStationPropertiesButton"));
    auto* document = window.findChild<StationDocument*>();
    QVERIFY(scanEnabled);
    QVERIFY(apply);
    QVERIFY(document);
    QVERIFY(scanEnabled->isChecked());
    scanEnabled->setChecked(false);
    apply->click();
    QCOMPARE(document->rootObject().value(QStringLiteral("scanDialogEnabled")).toBool(),
             false);
    QString errorMessage;
    QVERIFY(document->save(&errorMessage));
    QVERIFY2(!StartupSupport::stationScanDialogEnabled(stationPath),
             qPrintable(errorMessage));
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
