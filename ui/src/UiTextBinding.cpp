#include "UiTextBinding.h"
#include "TitleBarLanguageButton.h"

#include <QAction>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTabWidget>
#include <QToolBar>
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

void installLanguageButton(QMainWindow* window)
{
    new TitleBarLanguageButton(window);
}

} // namespace PicoATE::Ui
