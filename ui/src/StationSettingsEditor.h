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

private slots:
    void reload();
    void apply();

private:
    void showError(const QString& message);

    QPointer<StationDocument> m_document;
    QLineEdit* m_stationIdEdit = nullptr;
    QLineEdit* m_stationNameEdit = nullptr;
    QAbstractButton* m_stopOnFailureSwitch = nullptr;
    QAbstractButton* m_scanDialogSwitch = nullptr;
    QSpinBox* m_snLengthSpin = nullptr;
    QPlainTextEdit* m_metadataEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
    bool m_editable = true;
};

} // namespace PicoATE::Ui
