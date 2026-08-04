#pragma once

#include <QElapsedTimer>
#include <QString>

namespace PicoATE::Ui {

class ApplicationDiagnostics final
{
public:
    static void install();
    static void recordAction(const QString& action,
                             const QString& detail = {});
    static void recordSlowOperation(const QString& operation,
                                    qint64 elapsedMs,
                                    qint64 thresholdMs = 25);
    static QString outputDirectory();
};

class ScopedOperationTimer final
{
public:
    explicit ScopedOperationTimer(QString operation,
                                  qint64 thresholdMs = 25);
    ~ScopedOperationTimer();

    ScopedOperationTimer(const ScopedOperationTimer&) = delete;
    ScopedOperationTimer& operator=(const ScopedOperationTimer&) = delete;

private:
    QString m_operation;
    qint64 m_thresholdMs = 25;
    QElapsedTimer m_timer;
};

} // namespace PicoATE::Ui
