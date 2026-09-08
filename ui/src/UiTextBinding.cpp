#include "UiTextBinding.h"
#include "UiExecutionTypes.h"

#include <QAction>
#include <QActionGroup>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVariant>

namespace PicoATE::Ui {

void bindUiText(QObject* object, const char* property, const char* source)
{
    const QByteArray key(property);
    const QByteArray text(source);
    const auto refresh = [object, key, text] {
        object->setProperty(key.constData(), uiText(text.constData()));
    };
    refresh();
    QObject::connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                     object, refresh);
}

QLabel* makeUiLabel(const char* source, QWidget* parent)
{
    auto* label = new QLabel(parent);
    bindUiText(label, "text", source);
    return label;
}

QAction* makeUiAction(const char* source, QObject* parent)
{
    auto* action = new QAction(parent);
    bindUiText(action, "text", source);
    return action;
}

QAction* makeUiAction(const QIcon& icon, const char* source, QObject* parent)
{
    auto* action = makeUiAction(source, parent);
    action->setIcon(icon);
    return action;
}

QPushButton* makeUiButton(const char* source, QWidget* parent)
{
    auto* button = new QPushButton(parent);
    bindUiText(button, "text", source);
    return button;
}

QPushButton* makeUiButton(const QIcon& icon, const char* source, QWidget* parent)
{
    auto* button = makeUiButton(source, parent);
    button->setIcon(icon);
    return button;
}

QMenu* addUiMenu(QMenuBar* menuBar, const char* source)
{
    auto* menu = menuBar->addMenu(uiText(source));
    bindUiText(menu, "title", source);
    return menu;
}

QAction* addUiAction(QToolBar* toolbar, const QIcon& icon, const char* source)
{
    auto* action = makeUiAction(icon, source, toolbar);
    toolbar->addAction(action);
    return action;
}

void addUiTab(QTabWidget* tabs, QWidget* page, const char* source, int index)
{
    if (index < 0) {
        tabs->addTab(page, uiText(source));
    } else {
        tabs->insertTab(index, page, uiText(source));
    }
    QObject::connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                     page, [tabs, page, source = QByteArray(source)] {
        const int currentIndex = tabs->indexOf(page);
        if (currentIndex >= 0) {
            tabs->setTabText(currentIndex, uiText(source.constData()));
        }
    });
}

void addUiRow(QFormLayout* layout, const char* source, QWidget* field)
{
    layout->addRow(makeUiLabel(source, field->parentWidget()), field);
}

QToolButton* makeLanguageButton(QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("uiLanguageButton"));
    button->setIcon(QIcon(QStringLiteral(":/icons/languages.svg")));
    button->setIconSize(QSize(22, 22));
    button->setFixedSize(40, 36);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setPopupMode(QToolButton::InstantPopup);
    bindUiText(button, "toolTip", "Language");
    bindUiText(button, "accessibleName", "Language");
    auto* menu = new QMenu(button);
    menu->setObjectName(QStringLiteral("uiLanguageMenu"));
    menu->setMinimumWidth(172);
    auto* group = new QActionGroup(menu);
    group->setExclusive(true);
    const auto addLanguage = [menu, group](const QString& label,
                                         const QString& code, bool chinese) {
        auto* action = menu->addAction(label);
        action->setObjectName(QStringLiteral("uiLanguage_%1").arg(code));
        action->setData(code);
        action->setCheckable(true);
        group->addAction(action);
        QObject::connect(action, &QAction::triggered, menu, [chinese] {
            UiLanguage::instance().setChinese(chinese);
        });
        const auto refresh = [action, chinese] {
            action->setChecked(UiLanguage::instance().isChinese() == chinese);
        };
        QObject::connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                         action, refresh);
        refresh();
    };
    addLanguage(QString::fromUtf8("简体中文"), QStringLiteral("zh_CN"), true);
    addLanguage(QStringLiteral("English"), QStringLiteral("en"), false);
    button->setMenu(menu);
    button->setStyleSheet(QStringLiteral(
        "QToolButton {border:0;border-radius:6px;background:transparent;padding:6px;}"
        "QToolButton:hover {background:#e7ebee;}"
        "QToolButton:pressed,QToolButton:open {background:#d8dfe4;}"
        "QToolButton::menu-indicator {image:none;width:0;}"));
    menu->setStyleSheet(QStringLiteral(
        "QMenu {background:#ffffff;color:#20262b;border:1px solid #cdd4da;"
        "border-radius:6px;padding:6px;font-size:15px;}"
        "QMenu::item {padding:9px 24px 9px 12px;}"
        "QMenu::item:selected {background:#edf1f4;color:#20262b;}"));
    return button;
}

void configureRunInfoValue(QLabel* label)
{
    auto font = label->font();
    font.setPixelSize(14);
    label->setFont(font);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(false);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    label->setMinimumWidth(0);
    label->setMinimumHeight(label->fontMetrics().height() + 2);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    label->setProperty("runInfoValue", true);
}

QString runStatusStyle(UiRunState state, bool stopRequested)
{
    if (stopRequested && state == UiRunState::Completed) state = UiRunState::Failed;
    switch (state) {
    case UiRunState::Starting:
    case UiRunState::Running:
    case UiRunState::Pausing:
    case UiRunState::Paused:
    case UiRunState::Stopping:
        return QStringLiteral("background:#f4d768;color:#493a00;border:1px solid #cbaa39;border-radius:6px;");
    case UiRunState::Completed:
        return QStringLiteral("background:#cfe8d5;color:#1f5d35;border:1px solid #86b794;border-radius:6px;");
    case UiRunState::Failed:
    case UiRunState::CompileFailed:
        return QStringLiteral("background:#efc9c9;color:#862a2a;border:1px solid #c98282;border-radius:6px;");
    default:
        return QStringLiteral("background:#e7eaec;color:#303940;border:1px solid #c8cfd4;border-radius:6px;");
    }
}

} // namespace PicoATE::Ui
