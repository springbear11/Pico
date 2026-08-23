#pragma once

#include <QPointer>
#include <QWidget>

class QAbstractButton;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;

namespace PicoATE::Ui {

class StationDocument;

class StationSettingsEditor final : public QWidget
{
    Q_OBJECT

public:
    explicit StationSettingsEditor(StationDocument* document,
                                   QWidget* parent = nullptr);

    void setEditable(bool editable);
    bool focusField(const QString& path);
    bool hasPendingChanges() const;
    bool commitPendingChanges();
    void discardPendingChanges();

signals:
    void pendingChangesChanged(bool pending);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void reload();

private:
    void applyResponsiveFormLayout();
    void showError(const QString& message);
    void markPendingChanges();
    void setPendingChanges(bool pending);

    QPointer<StationDocument> m_document;
    QFormLayout* m_form = nullptr;
    QLineEdit* m_stationIdEdit = nullptr;
    QLineEdit* m_stationModelEdit = nullptr;
    QLineEdit* m_customerIdEdit = nullptr;
    QAbstractButton* m_stopOnFailureSwitch = nullptr;
    QAbstractButton* m_scanDialogSwitch = nullptr;
    QAbstractButton* m_loopTestSwitch = nullptr;
    QLineEdit* m_loopTestCountEdit = nullptr;
    QLineEdit* m_uutCountEdit = nullptr;
    QAbstractButton* m_txtLogSwitch = nullptr;
    QAbstractButton* m_csvReportSwitch = nullptr;
    QAbstractButton* m_xlsxReportSwitch = nullptr;
    QAbstractButton* m_pdfReportSwitch = nullptr;
    QLineEdit* m_reportOutputEdit = nullptr;
    QPushButton* m_browseReportOutputButton = nullptr;
    QLineEdit* m_snLengthEdit = nullptr;
    QLineEdit* m_snPatternEdit = nullptr;
    QLineEdit* m_snAllowedRegexEdit = nullptr;
    QLineEdit* m_jigNoEdit = nullptr;
    QLineEdit* m_orderEdit = nullptr;
    QLineEdit* m_testerEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    QLabel* m_title = nullptr;
    bool m_editable = true;
    bool m_loading = false;
    bool m_pendingChanges = false;
    bool m_compactFormLayout = false;
};

} // namespace PicoATE::Ui
