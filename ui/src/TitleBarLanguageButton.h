#pragma once

#include <QToolButton>
#include <QTimer>
#include <QVariantAnimation>

class QMainWindow;

namespace PicoATE::Ui {

class TitleBarLanguageButton final : public QToolButton
{
public:
    explicit TitleBarLanguageButton(QMainWindow* owner);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void refreshLanguage(bool animate);
    void synchronizeCaption();

    QMainWindow* m_owner;
    QVariantAnimation m_animation;
    QTimer m_positionUpdate;
    qreal m_chineseProgress = 0;
    bool m_nativeCaption = false;
    WId m_styledOwner = 0;
};

} // namespace PicoATE::Ui
