#pragma once

#include <QColor>
#include <QWidget>

class QTimer;

namespace PicoATE::Ui {

class LoadingSpinner final : public QWidget
{
public:
    explicit LoadingSpinner(QWidget* parent = nullptr);

    bool isRunning() const;
    void setColor(const QColor& color);
    void setRunning(bool running);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QTimer* m_timer = nullptr;
    QColor m_color = QColor(40, 120, 208);
    int m_frame = 0;
};

} // namespace PicoATE::Ui
