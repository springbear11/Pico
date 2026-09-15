#pragma once

#include "RunInformation.h"

#include <QDialog>
#include <QToolButton>
#include <functional>

class QAction;
class QLineEdit;
class QPushButton;

namespace PicoATE::Ui {

QToolButton* makeRunInformationButton(QAction* action, QWidget* parent, const QString& name);

class RunInformationDialog final : public QDialog
{
    Q_OBJECT
public:
    using ModelSaver = std::function<bool(const QString&, QString*)>;
    RunInformationDialog(const RunInformation& current, bool modelEditable,
                         ModelSaver saveModel, QWidget* parent = nullptr);
    RunInformation information() const { return m_information; }

public slots:
    void accept() override;

private:
    void loadPrevious();
    RunInformation m_information;
    ModelSaver m_saveModel;
    QLineEdit* m_model = nullptr;
    QLineEdit* m_customer = nullptr;
    QLineEdit* m_order = nullptr;
    QLineEdit* m_tester = nullptr;
    QLineEdit* m_jig = nullptr;
};

} // namespace PicoATE::Ui
