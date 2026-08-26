#pragma once

#include "ScanDialog.h"

#include "PicoATE/Core/ProductRouting.h"

namespace PicoATE::Ui {

inline ScanSubmissionDecision validateAutoRoutedScan(
    const PicoATE::Core::ProductRoutingConfig& config,
    const QStringList& proposedBarcodes,
    int currentSlot)
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
    return {true, {}, batch.uutCount, context};
}

} // namespace PicoATE::Ui
