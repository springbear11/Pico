#pragma once

#include "StartupSupport.h"

#include <QDialog>

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPushButton;
class QToolButton;

namespace PicoATE::Ui {

class LoadingSpinner;

struct StartupSelection {
    UiMode mode = UiMode::Test;
    QString sequencePath;
    QString stationPath;
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
    void updateStationPath();
    void submit();

private:
    void populateSequences();
    void showError(const QString& message);
    void setBusy(bool busy, const QString& message = {});
    void setPasswordError(bool invalid);
    void updateDialogGeometry();
    UiMode selectedMode() const;

    QString m_sequenceRootDirectory;
    StartupSelection m_selection;
    QComboBox* m_sequenceCombo = nullptr;
    QFrame* m_header = nullptr;
    QToolButton* m_testModeButton = nullptr;
    QToolButton* m_adminModeButton = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    LoadingSpinner* m_spinner = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_loginButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    bool m_busy = false;
    bool m_passwordError = false;
};

} // namespace PicoATE::Ui
