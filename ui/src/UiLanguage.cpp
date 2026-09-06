#include "UiLanguage.h"

#include <QCoreApplication>
#include <QAbstractItemModel>
#include <QSettings>

static void initializeUiTranslationResource()
{
    Q_INIT_RESOURCE(picoate_ui_translations);
}

namespace PicoATE::Ui {

UiLanguage::UiLanguage(QObject* parent) : QObject(parent)
{
    initializeUiTranslationResource();
}

UiLanguage& UiLanguage::instance()
{
    static auto* language = new UiLanguage(QCoreApplication::instance());
    return *language;
}

bool UiLanguage::setChinese(bool chinese, bool remember)
{
    if (m_chinese != chinese) {
        if (chinese) {
            if (!m_translator.load(QStringLiteral(":/i18n/picoate_zh_CN.qm"))) {
                return false;
            }
            if (!QCoreApplication::installTranslator(&m_translator)) {
                return false;
            }
        } else {
            QCoreApplication::removeTranslator(&m_translator);
        }
        m_chinese = chinese;
        emit languageChanged();
    }
    if (remember) {
        QSettings().setValue(QStringLiteral("ui/language"),
                             chinese ? QStringLiteral("zh_CN")
                                     : QStringLiteral("en"));
    }
    return true;
}

void UiLanguage::restorePreference()
{
    setChinese(QSettings().value(QStringLiteral("ui/language"),
                                 QStringLiteral("en")).toString() ==
                   QStringLiteral("zh_CN"), false);
}

QString uiText(const char* source)
{
    return QCoreApplication::translate("UiShell", source);
}

QString uiStateText(const QString& source)
{
    const auto utf8 = source.toUtf8();
    return uiText(utf8.constData());
}

void enableUiModelTranslation(QAbstractItemModel* model, bool translateCells)
{
    QObject::connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                     model, [model, translateCells] {
        const int columns = model->columnCount();
        if (columns > 0) {
            emit model->headerDataChanged(Qt::Horizontal, 0, columns - 1);
        }
        if (!translateCells) return;
        // Notify views without resetting indexes, selection, or expanded rows.
        const auto refresh = [model](auto&& self, const QModelIndex& parent) -> void {
            const int rows = model->rowCount(parent);
            const int columns = model->columnCount(parent);
            if (rows == 0 || columns == 0) return;
            emit model->dataChanged(model->index(0, 0, parent),
                                    model->index(rows - 1, columns - 1, parent),
                                    {Qt::DisplayRole});
            for (int row = 0; row < rows; ++row) {
                const auto child = model->index(row, 0, parent);
                if (child.isValid() && model->rowCount(child) > 0) {
                    self(self, child);
                }
            }
        };
        refresh(refresh, {});
    });
}

} // namespace PicoATE::Ui
