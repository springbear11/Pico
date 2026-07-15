#pragma once

#include "SequenceDocument.h"
#include "StepOutputCatalog.h"

#include <QWidget>
#include <QPointer>
#include <QHash>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QSpinBox;
class QTabWidget;

namespace PicoATE::Ui {

class StepPropertyEditor final : public QWidget
{
    Q_OBJECT

public:
    explicit StepPropertyEditor(SequenceDocument* document,
                                QWidget* parent = nullptr);

    SequenceItemPath currentPath() const;
    void setCurrentItem(const SequenceItemPath& path);
    void setPreviewObject(QJsonObject object);
    void setEditable(bool editable);
    void setPluginRegistry(QVector<PluginManifest> plugins);
    void setDevicePluginBindings(QHash<QString, QString> pluginByDeviceId);
    void setDeviceConfigurations(QHash<QString, QJsonObject> deviceConfigurations);
    bool focusField(const QString& fieldPath);
    bool hasPendingChanges() const;
    bool commitPendingChanges();
    void discardPendingChanges();

signals:
    void itemApplied(const PicoATE::Ui::SequenceItemPath& path);
    void pendingChangesChanged(bool pending);

private:
    void buildGeneralPage();
    void buildDataPage();
    void buildPolicyPage();
    void loadCurrentObject();
    void updateKindRows();
    void updateLimitRows();
    void rebuildPluginInputEditors();
    void observeDraftWidget(QWidget* widget);
    void markDraftDirty();
    void setDraftDirty(bool dirty);
    QWidget* wrapExpressionEditor(QLineEdit* editor);
    void rebuildExpressionMenu(QMenu* menu, QLineEdit* editor);
    const PluginFunctionDefinition* currentPluginFunction() const;
    bool mergePluginInputValues(QJsonObject& inputs, QString& errorMessage) const;
    void showError(const QString& message);

    QPointer<SequenceDocument> m_document;
    SequenceItemPath m_path;
    QJsonObject m_sourceObject;
    QJsonObject m_previewObject;
    bool m_previewing = false;
    bool m_isGroup = false;
    bool m_loading = false;
    bool m_editable = true;
    bool m_draftDirty = false;
    QVector<PluginManifest> m_plugins;
    QHash<QString, QString> m_pluginByDeviceId;
    QHash<QString, QJsonObject> m_deviceConfigurations;

    struct PluginInputEditor {
        PluginParameterDefinition definition;
        QWidget* widget = nullptr;
        bool inheritedFromStation = false;
    };
    QVector<PluginInputEditor> m_pluginInputEditors;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QTabWidget* m_tabs = nullptr;
    QLabel* m_errorLabel = nullptr;
    QFormLayout* m_generalForm = nullptr;
    QLineEdit* m_idEdit = nullptr;
    QLineEdit* m_keyEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_kindCombo = nullptr;
    QCheckBox* m_enabledCheck = nullptr;
    QCheckBox* m_alwaysRunCheck = nullptr;
    QCheckBox* m_resultRecordingCheck = nullptr;
    QCheckBox* m_checkpointBeforeCheck = nullptr;
    QCheckBox* m_checkpointAfterCheck = nullptr;
    QLineEdit* m_tagsEdit = nullptr;

    QFormLayout* m_dataForm = nullptr;
    QLineEdit* m_moduleIdEdit = nullptr;
    QLineEdit* m_functionEdit = nullptr;
    QGroupBox* m_pluginInputsGroup = nullptr;
    QFormLayout* m_pluginInputsForm = nullptr;
    QPlainTextEdit* m_inputsEdit = nullptr;
    QLineEdit* m_limitActualEdit = nullptr;
    QWidget* m_limitActualField = nullptr;
    QMenu* m_limitExpressionMenu = nullptr;
    QComboBox* m_limitComparisonCombo = nullptr;
    QLineEdit* m_limitExpectedEdit = nullptr;
    QLineEdit* m_limitLowerEdit = nullptr;
    QLineEdit* m_limitUpperEdit = nullptr;
    QDoubleSpinBox* m_limitToleranceSpin = nullptr;
    QCheckBox* m_limitInclusiveCheck = nullptr;
    QLineEdit* m_limitMeasurementNameEdit = nullptr;
    QLineEdit* m_limitUnitEdit = nullptr;
    QPlainTextEdit* m_parametersEdit = nullptr;
    QSpinBox* m_waitMsSpin = nullptr;
    QLineEdit* m_loopVariableEdit = nullptr;
    QSpinBox* m_loopFromSpin = nullptr;
    QSpinBox* m_loopToSpin = nullptr;
    QSpinBox* m_loopStepSpin = nullptr;
    QLineEdit* m_barrierNameEdit = nullptr;
    QLineEdit* m_cohortIdEdit = nullptr;
    QSpinBox* m_expectedUutSpin = nullptr;
    QSpinBox* m_quorumCountSpin = nullptr;
    QDoubleSpinBox* m_quorumRatioSpin = nullptr;
    QSpinBox* m_arrivalTimeoutSpin = nullptr;
    QSpinBox* m_releaseTimeoutSpin = nullptr;
    QComboBox* m_arrivalPolicyCombo = nullptr;
    QComboBox* m_releasePolicyCombo = nullptr;
    QComboBox* m_failurePolicyCombo = nullptr;
    QComboBox* m_barrierTimeoutPolicyCombo = nullptr;
    QCheckBox* m_releaseResourcesCheck = nullptr;

    QFormLayout* m_policyForm = nullptr;
    QSpinBox* m_maxAttemptsSpin = nullptr;
    QSpinBox* m_retryDelaySpin = nullptr;
    QLineEdit* m_retryWhenEdit = nullptr;
    QSpinBox* m_timeoutSpin = nullptr;
    QComboBox* m_onFailCombo = nullptr;
    QComboBox* m_onErrorCombo = nullptr;
    QComboBox* m_onTimeoutCombo = nullptr;
    QLineEdit* m_cleanupRegionEdit = nullptr;
    QCheckBox* m_stopUutCheck = nullptr;
    QPlainTextEdit* m_resourcesEdit = nullptr;
};

} // namespace PicoATE::Ui
