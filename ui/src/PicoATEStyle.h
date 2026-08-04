#pragma once

#include <QProxyStyle>

namespace PicoATE::Ui {

class PicoATEStyle final : public QProxyStyle
{
public:
    PicoATEStyle();

    int pixelMetric(PixelMetric metric,
                    const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override;
    void drawPrimitive(PrimitiveElement element,
                       const QStyleOption* option,
                       QPainter* painter,
                       const QWidget* widget = nullptr) const override;
};

} // namespace PicoATE::Ui
