#pragma once

#include "StartupSupport.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace PicoATE::Ui {

struct StartupSelection {
    UiMode mode = UiMode::Test;
    QString sequencePath;
    QString stationPath;
    bool scanDialogEnabled = true;
};

class LoginDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QString sequenceRootDirectory,
                         QWidget* parent = nullptr);

    StartupSelection selection() const;
    void setInitialSequencePath(const QString& filePath);

private slots:
    void updateModeUi();
    void updateStationPath();
    void submit();

private:
    void populateSequences();
    void showError(const QString& message);

    QString m_sequenceRootDirectory;
    StartupSelection m_selection;
    QComboBox* m_modeCombo = nullptr;
    QComboBox* m_sequenceCombo = nullptr;
    QLabel* m_passwordLabel = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_loginButton = nullptr;
};

} // namespace PicoATE::Ui
