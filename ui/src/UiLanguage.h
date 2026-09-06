#pragma once

#include <QObject>
#include <QString>
#include <QTranslator>

class QAbstractItemModel;

namespace PicoATE::Ui {

class UiLanguage final : public QObject
{
    Q_OBJECT
public:
    static UiLanguage& instance();
    bool isChinese() const { return m_chinese; }
    bool setChinese(bool chinese, bool remember = true);
    void restorePreference();

signals:
    void languageChanged();

private:
    explicit UiLanguage(QObject* parent);
    QTranslator m_translator;
    bool m_chinese = false;
};

// This context contains display text only, never configuration or report tokens.
QString uiText(const char* source);
QString uiStateText(const QString& source);
void enableUiModelTranslation(QAbstractItemModel* model, bool translateCells = true);

} // namespace PicoATE::Ui
