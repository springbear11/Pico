#include "RunArtifactWriter.h"

#include "ReportExporter.h"
#include "PicoATE/Core/StationConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace PicoATE::Ui {

namespace {

QString safeFileName(QString value)
{
    value = value.trimmed();
    static const QString invalid = QStringLiteral("<>:\"/\\|?*");
    for (const auto character : invalid) {
        value.replace(character, QLatin1Char('_'));
    }
    while (value.endsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char(' '))) {
        value.chop(1);
    }
    return value.left(120);
}

bool artifactExists(const QString& dateDirectory, const QString& baseName)
{
    const QStringList directories = {
        dateDirectory,
        QDir(dateDirectory).filePath(QStringLiteral("PASS")),
        QDir(dateDirectory).filePath(QStringLiteral("FAIL"))};
    for (const auto& directory : directories) {
        if (QFileInfo::exists(QDir(directory).filePath(baseName + QStringLiteral(".txt"))) ||
            QFileInfo::exists(QDir(directory).filePath(baseName + QStringLiteral(".csv"))) ||
            QFileInfo::exists(QDir(directory).filePath(baseName + QStringLiteral(".xlsx")))) {
            return true;
        }
    }
    return false;
}

QString resolvedOutputDirectory(QString configured, const QString& stationFilePath)
{
    configured = configured.trimmed();
    if (configured.isEmpty()) {
        return QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QStringLiteral("log"));
    }
    if (QDir::isAbsolutePath(configured)) {
        return QDir(configured).absolutePath();
    }
    const auto base = stationFilePath.isEmpty()
        ? QCoreApplication::applicationDirPath()
        : QFileInfo(stationFilePath).absolutePath();
    return QDir(base).absoluteFilePath(configured);
}

} // namespace

RunArtifactSettings runArtifactSettingsFromStation(
    const QJsonObject& station,
    const QString& stationFilePath)
{
    PicoATE::Core::VariableResolverOptions resolverOptions;
    resolverOptions.sequenceFilePath = stationFilePath;
#if defined(PICOATE_PROJECT_DIR)
    resolverOptions.projectDir = QStringLiteral(PICOATE_PROJECT_DIR);
#endif
    const auto parsed = PicoATE::Core::parseStationConfigJson(
        station, resolverOptions);
    RunArtifactSettings settings;
    settings.txtLogEnabled = parsed.config.txtLogEnabled;
    settings.csvReportEnabled = parsed.config.csvReportEnabled;
    settings.xlsxReportEnabled = parsed.config.xlsxReportEnabled;
    settings.outputDirectory = resolvedOutputDirectory(
        parsed.config.reportOutputDirectory,
        stationFilePath);
    return settings;
}

RunArtifactWriter::~RunArtifactWriter()
{
    abandon();
}

RunArtifactResult RunArtifactWriter::begin(const RunArtifactSettings& settings,
                                           const QString& serialNumber,
                                           const QDateTime& startedAt)
{
    abandon();
    m_settings = settings;
    if (!settings.txtLogEnabled && !settings.csvReportEnabled &&
        !settings.xlsxReportEnabled) {
        return {};
    }

    const auto localStart = startedAt.isValid()
        ? startedAt.toLocalTime()
        : QDateTime::currentDateTime();
    const auto outputRoot = settings.outputDirectory.trimmed().isEmpty()
        ? QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("log"))
        : settings.outputDirectory;
    m_dateDirectory = QDir(outputRoot).absoluteFilePath(
        localStart.toString(QStringLiteral("yyyyMMdd")));
    QDir directory;
    if (!directory.mkpath(QDir(m_dateDirectory).filePath(QStringLiteral("PASS"))) ||
        !directory.mkpath(QDir(m_dateDirectory).filePath(QStringLiteral("FAIL")))) {
        return failure(QStringLiteral("Cannot create report directory: %1")
                           .arg(m_dateDirectory));
    }

    const auto serialPrefix = safeFileName(serialNumber);
    auto fileTimestamp = localStart;
    do {
        m_baseName = serialPrefix.isEmpty()
            ? fileTimestamp.toString(QStringLiteral("yyyyMMdd_HHmmsszzz"))
            : serialPrefix + fileTimestamp.toString(QStringLiteral("_HHmmsszzz"));
        if (!artifactExists(m_dateDirectory, m_baseName)) {
            break;
        }
        fileTimestamp = fileTimestamp.addMSecs(1);
    } while (true);

    RunArtifactResult result;
    if (settings.txtLogEnabled) {
        const auto path = QDir(m_dateDirectory).filePath(m_baseName + QStringLiteral(".txt"));
        m_txtFile.setFileName(path);
        if (!m_txtFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            closeFiles();
            return failure(m_txtFile.errorString());
        }
        m_txtFile.write("\xEF\xBB\xBF");
        m_txtFile.flush();
        result.filePaths.push_back(path);
    }
    if (settings.csvReportEnabled) {
        const auto path = QDir(m_dateDirectory).filePath(m_baseName + QStringLiteral(".csv"));
        m_csvFile.setFileName(path);
        if (!m_csvFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            closeFiles();
            return failure(m_csvFile.errorString());
        }
        m_csvFile.write(ReportExporter::csvHeader());
        m_csvFile.flush();
        result.filePaths.push_back(path);
    }
    if (settings.xlsxReportEnabled) {
        m_xlsxFilePath = QDir(m_dateDirectory).filePath(
            m_baseName + QStringLiteral(".xlsx"));
    }
    m_active = true;
    return result;
}

RunArtifactResult RunArtifactWriter::appendLogLines(
    const QVector<RuntimeLogLine>& lines)
{
    if (!m_active || !m_settings.txtLogEnabled || lines.isEmpty()) {
        return {};
    }
    for (const auto& line : lines) {
        const auto timestamp = line.timestampUtc.isValid()
            ? line.timestampUtc.toLocalTime().toString(QStringLiteral("HH:mm:ss.zzz"))
            : QStringLiteral("--:--:--.---");
        const auto bytes = QStringLiteral("[%1] %2\r\n")
                               .arg(timestamp, line.message)
                               .toUtf8();
        if (m_txtFile.write(bytes) != bytes.size()) {
            return failure(m_txtFile.errorString());
        }
        if (line.message.contains(QStringLiteral("_TESTITEM_END ")) &&
            line.message.contains(QStringLiteral("========================"))) {
            constexpr auto separator = "\r\n\r\n\r\n";
            if (m_txtFile.write(separator) != 6) {
                return failure(m_txtFile.errorString());
            }
        }
    }
    if (!m_txtFile.flush()) {
        return failure(m_txtFile.errorString());
    }
    return {};
}

RunArtifactResult RunArtifactWriter::finalize(
    const PicoATE::Core::ExecutionReport& report)
{
    if (!m_active) {
        return {};
    }

    if (m_settings.csvReportEnabled) {
        m_csvFile.close();
        const auto exported = ReportExporter::saveCsv(m_csvFile.fileName(), report);
        if (!exported.success) {
            m_txtFile.close();
            m_active = false;
            return failure(exported.errorMessage);
        }
    }
    if (m_settings.xlsxReportEnabled) {
        const auto exported = ReportExporter::saveXlsx(m_xlsxFilePath, report);
        if (!exported.success) {
            m_txtFile.close();
            m_active = false;
            return failure(exported.errorMessage);
        }
    }
    if (m_txtFile.isOpen()) {
        m_txtFile.flush();
        m_txtFile.close();
    }

    const bool passed = report.completed && !report.hasError &&
        report.state == PicoATE::Core::ExecutionState::Completed;
    const auto classification = passed ? QStringLiteral("PASS") : QStringLiteral("FAIL");
    const auto destinationDirectory = QDir(m_dateDirectory).filePath(classification);
    RunArtifactResult result;
    for (const auto& source : {m_txtFile.fileName(), m_csvFile.fileName(), m_xlsxFilePath}) {
        if (source.isEmpty() || !QFileInfo::exists(source)) {
            continue;
        }
        const auto destination = QDir(destinationDirectory).filePath(QFileInfo(source).fileName());
        if (!QFile::rename(source, destination)) {
            result.success = false;
            if (!result.errorMessage.isEmpty()) {
                result.errorMessage += QStringLiteral("; ");
            }
            result.errorMessage += QStringLiteral("Cannot move %1 to %2")
                                       .arg(source, destination);
            result.filePaths.push_back(source);
        } else {
            result.filePaths.push_back(destination);
        }
    }
    m_active = false;
    return result;
}

void RunArtifactWriter::abandon()
{
    closeFiles();
    m_txtFile.setFileName({});
    m_csvFile.setFileName({});
    m_xlsxFilePath.clear();
    m_active = false;
}

bool RunArtifactWriter::active() const
{
    return m_active;
}

QString RunArtifactWriter::dateDirectory() const
{
    return m_dateDirectory;
}

QString RunArtifactWriter::baseName() const
{
    return m_baseName;
}

void RunArtifactWriter::closeFiles()
{
    if (m_txtFile.isOpen()) {
        m_txtFile.flush();
        m_txtFile.close();
    }
    if (m_csvFile.isOpen()) {
        m_csvFile.flush();
        m_csvFile.close();
    }
}

RunArtifactResult RunArtifactWriter::failure(const QString& message) const
{
    RunArtifactResult result;
    result.success = false;
    result.errorMessage = message;
    return result;
}

} // namespace PicoATE::Ui
