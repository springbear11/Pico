#pragma once

#include <QPointer>
#include <QWidget>

class QAbstractButton;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

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

private slots:
    void reload();

private:
    void showError(const QString& message);
    void markPendingChanges();
    void setPendingChanges(bool pending);

    QPointer<StationDocument> m_document;
    QLineEdit* m_stationIdEdit = nullptr;
    QLineEdit* m_stationNameEdit = nullptr;
    QAbstractButton* m_stopOnFailureSwitch = nullptr;
    QAbstractButton* m_scanDialogSwitch = nullptr;
    QSpinBox* m_snLengthSpin = nullptr;
    QPlainTextEdit* m_metadataEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    QLabel* m_title = nullptr;
    bool m_editable = true;
    bool m_loading = false;
    bool m_pendingChanges = false;
};

} // namespace PicoATE::Ui
