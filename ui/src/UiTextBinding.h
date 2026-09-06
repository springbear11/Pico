#pragma once

#include "UiLanguage.h"

#include <QIcon>

class QAction;
class QFormLayout;
class QLabel;
class QMainWindow;
class QMenu;
class QMenuBar;
class QPushButton;
class QTabWidget;
class QToolBar;
class QWidget;

namespace PicoATE::Ui {

void bindUiText(QObject* object, const char* property, const char* source);
QLabel* makeUiLabel(const char* source, QWidget* parent);
QAction* makeUiAction(const char* source, QObject* parent);
QAction* makeUiAction(const QIcon& icon, const char* source, QObject* parent);
QPushButton* makeUiButton(const char* source, QWidget* parent);
QPushButton* makeUiButton(const QIcon& icon, const char* source, QWidget* parent);
QMenu* addUiMenu(QMenuBar* menuBar, const char* source);
QAction* addUiAction(QToolBar* toolbar, const QIcon& icon, const char* source);
void addUiTab(QTabWidget* tabs, QWidget* page, const char* source, int index = -1);
void addUiRow(QFormLayout* layout, const char* source, QWidget* field);
void installLanguageButton(QMainWindow* window);

} // namespace PicoATE::Ui
