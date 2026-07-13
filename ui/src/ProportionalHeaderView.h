#pragma once

#include <QHeaderView>
#include <QVector>

class QResizeEvent;
class QShowEvent;

namespace PicoATE::Ui {

class ProportionalHeaderView final : public QHeaderView
{
public:
    explicit ProportionalHeaderView(QWidget* parent = nullptr);

    void setSectionWeights(QVector<int> weights);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void redistribute();

    QVector<int> m_weights;
    bool m_redistributing = false;
};

} // namespace PicoATE::Ui
