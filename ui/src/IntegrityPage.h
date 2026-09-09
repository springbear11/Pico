#pragma once

#include "RuntimeIntegrity.h"

#include <QWidget>
#include <atomic>
#include <memory>

class QLabel;
class QPushButton;
class QTableWidget;
class QThread;

namespace PicoATE::Ui {

class IntegrityPage final : public QWidget {
    Q_OBJECT
public:
    explicit IntegrityPage(QString directory, AdminAccess access, QWidget* parent = nullptr);
    ~IntegrityPage() override;
    void setRunActive(bool active);
    bool busy() const { return m_worker != nullptr; }
    IntegrityReport report() const { return m_report; }

public slots:
    void refresh();

signals:
    void verificationFinished();

private:
    void render();
    void updateButtons();
    void requestApproval();
    void startJob(QStringList files = {}, QString password = {}, QString reason = {});

    QString m_directory;
    AdminAccess m_access;
    IntegrityReport m_report;
    bool m_runActive = false;
    QThread* m_worker = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QLabel* m_summary = nullptr;
    QLabel* m_metadata = nullptr;
    QLabel* m_error = nullptr;
    QPushButton* m_refresh = nullptr;
    QPushButton* m_approve = nullptr;
    QTableWidget* m_files = nullptr;
};

} // namespace PicoATE::Ui
