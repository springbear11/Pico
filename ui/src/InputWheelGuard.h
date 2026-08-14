#pragma once

#include <QObject>

namespace PicoATE::Ui {

class InputWheelGuard final : public QObject
{
public:
    explicit InputWheelGuard(QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};

} // namespace PicoATE::Ui
