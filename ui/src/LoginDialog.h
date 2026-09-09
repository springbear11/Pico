#pragma once

#include "StartupSupport.h"

#include <QDialog>

#include <memory>

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace PicoATE::Ui {

class LoadingSpinner;

struct StartupSelection {
    UiMode mode = UiMode::Test;
    AdminAccess adminAccess = AdminAccess::None;
    SequenceLoadMode sequenceLoadMode = SequenceLoadMode::Manual;
    QString projectName;
    QString projectPath;
    QString projectRootPath;
    QString sequencePath;
    QString stationPath;
    QString productRoutingPath;
    bool newProjectTemplate = false;
    bool scanDialogEnabled = true;
    SnValidationRules snValidationRules;
};

class LoginDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QString sequenceRootDirectory,
                         QWidget* parent = nullptr);

    StartupSelection selection() const;
    void setInitialSequencePath(const QString& filePath);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void updateModeUi();
    void updateLoadModeUi();
    void updateStationPath();
    void submit();

private:
    void populateSequences();
    void refreshRoutingPolicy();
    void restorePreferencesForMode();
    void savePreferences() const;
    void showError(const QString& message);
    void setBusy(bool busy, const QString& message = {});
    void setPasswordError(bool invalid);
    void updateDialogGeometry();
    UiMode selectedMode() const;
    SequenceLoadMode selectedLoadMode() const;

    QString m_sequenceRootDirectory;
    StartupSelection m_selection;
    QComboBox* m_sequenceCombo = nullptr;
    QFrame* m_header = nullptr;
    QToolButton* m_testModeButton = nullptr;
    QToolButton* m_adminModeButton = nullptr;
    QToolButton* m_autoLoadButton = nullptr;
    QStackedWidget* m_sequenceStack = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    LoadingSpinner* m_spinner = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_loginButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    bool m_busy = false;
    bool m_passwordError = false;
    bool m_restoringPreferences = false;
    bool m_routingFilePresent = false;
    bool m_routingValid = false;
    bool m_routingHasEnabledRoutes = false;
    bool m_allowManualInTest = true;
    QString m_productRoutingPath;
    QString m_projectRootPath;
    QString m_settingsGroup;
};

std::unique_ptr<LoginDialog> createLoginDialog(QString sequenceRootDirectory);

} // namespace PicoATE::Ui
