#pragma once

#include <QProxyStyle>

class QApplication;

namespace PicoATE::Ui {

class PicoATEStyle final : public QProxyStyle
{
public:
    PicoATEStyle();

    int pixelMetric(PixelMetric metric,
                    const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override;
    QIcon standardIcon(StandardPixmap standardIcon,
                       const QStyleOption* option = nullptr,
                       const QWidget* widget = nullptr) const override;
    void drawPrimitive(PrimitiveElement element,
                       const QStyleOption* option,
                       QPainter* painter,
                       const QWidget* widget = nullptr) const override;
};

void applyPicoATEApplicationTheme(QApplication& application);

} // namespace PicoATE::Ui
