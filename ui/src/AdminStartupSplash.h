#pragma once

#include <QWidget>

class QHideEvent;
class QLabel;
class QShowEvent;

namespace PicoATE::Ui {

class LoadingSpinner;

class AdminStartupSplash final : public QWidget
{
public:
    explicit AdminStartupSplash(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QLabel* m_logo = nullptr;
    LoadingSpinner* m_spinner = nullptr;
};

} // namespace PicoATE::Ui
