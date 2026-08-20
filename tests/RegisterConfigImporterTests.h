#pragma once

#include <QObject>

class RegisterConfigImporterTests final : public QObject
{
    Q_OBJECT

private slots:
    void importsWorkbookAndBuildsCompilableTestItem();
    void usesStableThreeDigitIdsForKnownSheets();
    void rejectsDecimalRegisterAddress();
    void rejectsFractionalRawRegisterValue();
    void rejectsAmbiguousRegisterDirectory();
};
