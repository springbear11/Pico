#include "ProportionalHeaderView.h"

#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>

#include <utility>

namespace PicoATE::Ui {

ProportionalHeaderView::ProportionalHeaderView(QWidget* parent)
    : QHeaderView(Qt::Horizontal, parent)
{
    setSectionResizeMode(QHeaderView::Interactive);
    setMinimumSectionSize(48);
    setStretchLastSection(false);
}

void ProportionalHeaderView::setSectionWeights(QVector<int> weights)
{
    m_weights = std::move(weights);
    QTimer::singleShot(0, this, [this] { redistribute(); });
}

void ProportionalHeaderView::resizeEvent(QResizeEvent* event)
{
    QHeaderView::resizeEvent(event);
    redistribute();
}

void ProportionalHeaderView::showEvent(QShowEvent* event)
{
    QHeaderView::showEvent(event);
    QTimer::singleShot(0, this, [this] { redistribute(); });
}

void ProportionalHeaderView::redistribute()
{
    if (m_redistributing || count() <= 0 || width() <= 0) {
        return;
    }

    QVector<int> visibleSections;
    int totalWeight = 0;
    for (int section = 0; section < count(); ++section) {
        if (isSectionHidden(section)) {
            continue;
        }
        visibleSections.push_back(section);
        totalWeight += section < m_weights.size() ? qMax(1, m_weights[section]) : 1;
    }
    if (visibleSections.isEmpty() || totalWeight <= 0) {
        return;
    }

    m_redistributing = true;
    const int available = viewport()->width();
    int assigned = 0;
    int remainingWeight = totalWeight;
    for (int index = 0; index < visibleSections.size(); ++index) {
        const int section = visibleSections[index];
        const int weight = section < m_weights.size() ? qMax(1, m_weights[section]) : 1;
        const int remainingWidth = qMax(0, available - assigned);
        const int size = index + 1 == visibleSections.size()
            ? remainingWidth
            : qRound(static_cast<double>(remainingWidth) * weight / remainingWeight);
        resizeSection(section, qMax(minimumSectionSize(), size));
        assigned += qMax(minimumSectionSize(), size);
        remainingWeight -= weight;
    }
    m_redistributing = false;
}

} // namespace PicoATE::Ui
