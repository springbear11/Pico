#pragma once

#include "UiExecutionTypes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVector>

#include <functional>

class QUndoStack;

namespace PicoATE::Ui {

class SequenceRootCommand;

struct SequenceItemPath {
    int groupIndex = -1;
    QVector<int> stepIndices;

    bool isValid() const { return groupIndex >= 0; }
    bool isGroup() const { return isValid() && stepIndices.isEmpty(); }
    QString jsonPath() const;

    friend bool operator==(const SequenceItemPath&, const SequenceItemPath&) = default;
};

struct SequenceDocumentSnapshot {
    QString filePath;
    QJsonObject root;
    QByteArray json;
    quint64 revision = 0;

    bool isValid() const { return !root.isEmpty(); }
};

struct SequenceDiagnosticTarget {
    SequenceItemPath itemPath;
    QString fieldPath;

    bool isValid() const { return itemPath.isValid(); }
};

SequenceDiagnosticTarget parseSequenceDiagnosticTarget(const QString& path);

class SequenceDocument final : public QObject
{
    Q_OBJECT

public:
    explicit SequenceDocument(QObject* parent = nullptr);
    ~SequenceDocument() override;

    QString filePath() const;
    QString displayName() const;
    bool isModified() const;
    bool isEmpty() const;
    quint64 revision() const;
    QJsonObject rootObject() const;
    QVector<UiDiagnostic> diagnostics() const;
    SequenceDocumentSnapshot snapshot() const;
    QUndoStack* undoStack() const;

    bool load(const QString& filePath);
    bool save(QString* errorMessage = nullptr);
    bool saveAs(const QString& filePath, QString* errorMessage = nullptr);
    void clear();

    bool ensureStandardGroups();
    bool isStandardGroup(const SequenceItemPath& path) const;
    bool replaceRootObject(QJsonObject root);
    QJsonObject objectAt(const SequenceItemPath& path) const;
    SequenceItemPath findItemPath(const QJsonObject& object,
                                  const SequenceItemPath& preferredPath = {}) const;
    bool canContainSteps(const SequenceItemPath& path) const;
    bool insertStep(const SequenceItemPath& parentPath,
                    int row = -1,
                    QJsonObject step = {});
    bool removeStep(const SequenceItemPath& path);
    bool removeSteps(QVector<SequenceItemPath> paths);
    bool setStepsEnabled(const QVector<SequenceItemPath>& paths, bool enabled);
    QVector<QJsonObject> copiedSteps(QVector<SequenceItemPath> paths) const;
    bool pasteSteps(const SequenceItemPath& parentPath,
                    int row,
                    const QVector<QJsonObject>& steps,
                    QVector<SequenceItemPath>* pastedPaths = nullptr);
    bool duplicateStep(const SequenceItemPath& path);
    bool duplicateSteps(QVector<SequenceItemPath> paths);
    bool moveStep(const SequenceItemPath& path, int offset);
    bool relocateStep(const SequenceItemPath& sourcePath,
                      const SequenceItemPath& destinationParent,
                      int destinationRow = -1,
                      SequenceItemPath* relocatedPath = nullptr);
    bool canWrapStepsInTestItem(const QVector<SequenceItemPath>& paths) const;
    bool wrapStepsInTestItem(QVector<SequenceItemPath> paths,
                             SequenceItemPath* testItemPath = nullptr);
    bool setItemValue(const SequenceItemPath& path,
                      const QString& key,
                      const QJsonValue& value);
    bool replaceItemObject(const SequenceItemPath& path,
                           QJsonObject object);

signals:
    void documentChanged();
    void filePathChanged(const QString& filePath);
    void modifiedChanged(bool modified);
    void diagnosticsChanged();

private:
    friend class SequenceRootCommand;

    using StepsMutation = std::function<bool(QJsonArray&)>;

    bool mutateSteps(const SequenceItemPath& parentPath,
                     const StepsMutation& mutation,
                     const QString& commandText);
    bool commitRoot(QJsonObject root, const QString& commandText);
    void applyCommandRoot(QJsonObject root);
    QString nextStepId(const SequenceItemPath& parentPath) const;
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

Q_DECLARE_METATYPE(PicoATE::Ui::SequenceItemPath)
Q_DECLARE_METATYPE(PicoATE::Ui::SequenceDocumentSnapshot)
