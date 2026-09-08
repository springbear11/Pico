#pragma once

#include "ScanDialog.h"

#include "PicoATE/Core/ProductRouting.h"
#include <QFileInfo>

namespace PicoATE::Ui {

inline int routedUutCount(const PicoATE::Core::ProductBatchRouteResolution& batch,
                          const QString& currentStationPath, int currentCount)
{
    const bool sameStation = !currentStationPath.isEmpty() &&
        QFileInfo(currentStationPath).absoluteFilePath().compare(
            QFileInfo(batch.route.stationPath).absoluteFilePath(), Qt::CaseInsensitive) == 0;
    return sameStation && currentCount > 0
        ? qBound(1, currentCount, batch.uutCount) : batch.uutCount;
}

inline ScanSubmissionDecision validateAutoRoutedScan(
    const PicoATE::Core::ProductRoutingConfig& config,
    const QStringList& proposedBarcodes,
    int currentSlot,
    const QString& currentStationPath = {}, int currentCount = 0)
{
    const auto batch = PicoATE::Core::resolveProductBatchRoute(
        config, proposedBarcodes);
    if (!batch.ok()) {
        const auto currentPath =
            QStringLiteral("serialNumbers[%1]").arg(currentSlot);
        const PicoATE::Core::ProductRoutingDiagnostic* selected = nullptr;
        for (const auto& diagnostic : batch.errors) {
            if (diagnostic.path.startsWith(currentPath)) {
                selected = &diagnostic;
                break;
            }
        }
        if (!selected && !batch.errors.isEmpty()) {
            selected = &batch.errors.first();
        }
        return {false,
                selected ? selected->message
                         : QObject::tr("Product routing failed"),
                0,
                {}};
    }

    auto context = batch.route.projectName.trimmed();
    if (context.isEmpty()) {
        context = batch.route.routeName.trimmed();
    }
    return {true, {}, routedUutCount(batch, currentStationPath, currentCount), context};
}

} // namespace PicoATE::Ui
