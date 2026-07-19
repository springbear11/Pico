#pragma once

#include "UiExecutionTypes.h"

#include <QJsonObject>
#include <QObject>
#include <QVector>

class QUndoStack;

namespace PicoATE::Ui {

class StationRootCommand;

struct StationDocumentSnapshot {
    QString filePath;
    QJsonObject root;
    QByteArray json;
    quint64 revision = 0;

    bool isValid() const { return !root.isEmpty(); }
};

class StationDocument final : public QObject
{
    Q_OBJECT

public:
    explicit StationDocument(QObject* parent = nullptr);
    ~StationDocument() override;

    QString filePath() const;
    QString displayName() const;
    bool isModified() const;
    bool isEmpty() const;
    quint64 revision() const;
    QJsonObject rootObject() const;
    QVector<UiDiagnostic> diagnostics() const;
    StationDocumentSnapshot snapshot() const;
    QUndoStack* undoStack() const;

    bool load(const QString& filePath);
    bool save(QString* errorMessage = nullptr);
    bool saveAs(const QString& filePath, QString* errorMessage = nullptr);
    void clear();

    int deviceCount() const;
    QJsonObject deviceAt(int row) const;
    bool setRootValue(const QString& key, const QJsonValue& value);
    bool replaceRootObject(QJsonObject root);
    bool insertDevice(int row = -1, QJsonObject device = {});
    bool removeDevice(int row);
    bool duplicateDevice(int row);
    bool moveDevice(int row, int offset);
    bool moveDeviceConfiguration(int sourceRow, int targetRow);
    bool setDeviceValue(int row, const QString& key, const QJsonValue& value);
    bool replaceDevice(int row, QJsonObject device);
    QString nextDeviceIdForType(const QString& deviceType,
                                int excludedRow = -1) const;
    bool isLastDeviceOfType(int row) const;
    bool isDeviceSlotEmpty(int row) const;
    int previousEmptyDeviceRow(int row) const;

signals:
    void documentChanged();
    void filePathChanged(const QString& filePath);
    void modifiedChanged(bool modified);
    void diagnosticsChanged();

private:
    friend class StationRootCommand;

    bool commitRoot(QJsonObject root, const QString& commandText);
    void applyCommandRoot(QJsonObject root);
    void acceptRoot(QJsonObject root, QString filePath);
    void setModified(bool modified);
    void validate();
    void setLoadError(QString path, QString message, QString suggestion = {});

    QString m_filePath;
    QJsonObject m_root;
    QVector<UiDiagnostic> m_diagnostics;
    QUndoStack* m_undoStack = nullptr;
    quint64 m_revision = 0;
    bool m_modified = false;
};

} // namespace PicoATE::Ui

Q_DECLARE_METATYPE(PicoATE::Ui::StationDocumentSnapshot)
