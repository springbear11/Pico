#include "RunArtifactWriter.h"

#include "ReportExporter.h"
#include "PicoATE/Core/StationConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <initializer_list>

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
            QFileInfo::exists(QDir(directory).filePath(baseName + QStringLiteral(".xlsx"))) ||
            QFileInfo::exists(QDir(directory).filePath(baseName + QStringLiteral(".pdf")))) {
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

QString metadataValue(const QVariantMap& metadata,
                      std::initializer_list<QString> keys)
{
    for (const auto& key : keys) {
        const auto value = metadata.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString displayedValue(const QString& value)
{
    return value.trimmed().isEmpty() ? QStringLiteral("--") : value.trimmed();
}

QByteArray executionLogHeader(const RunArtifactContext& context,
                              const QDateTime& localStart)
{
    QString header;
    header += QStringLiteral("================================================================================\r\n");
    header += QStringLiteral("PICOATE EXECUTION LOG\r\n");
    header += QStringLiteral("================================================================================\r\n");
    const auto appendField = [&header](const QString& label, const QString& value) {
        header += label.leftJustified(16, QLatin1Char(' '));
        header += QStringLiteral(": ");
        header += displayedValue(value);
        header += QStringLiteral("\r\n");
    };
    appendField(QStringLiteral("Sequence Name"), context.sequenceName);
    appendField(QStringLiteral("Sequence Path"), context.sequenceFilePath);
    appendField(QStringLiteral("Serial Number"), context.serialNumber);
    appendField(QStringLiteral("Station ID"), context.stationId);
    appendField(QStringLiteral("Model"), context.model);
    appendField(QStringLiteral("Customer ID"), context.customerId);
    appendField(QStringLiteral("Station Path"), context.stationFilePath);
    appendField(QStringLiteral("Order"), context.order);
    appendField(QStringLiteral("Tester"), context.tester);
    appendField(QStringLiteral("Jig No"), context.jigNo);
    appendField(QStringLiteral("Start Time"),
                localStart.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    header += QStringLiteral("================================================================================\r\n\r\n");
    return header.toUtf8();
}

const PicoATE::Core::UutReport* findUutReport(
    const PicoATE::Core::ExecutionReport& report,
    const PicoATE::Core::UutId& uutId)
{
    for (const auto& uut : report.uuts) {
        if (uut.uutId == uutId) {
            return &uut;
        }
    }
    return nullptr;
}

PicoATE::Core::ExecutionReport reportForUut(
    const PicoATE::Core::ExecutionReport& source,
    const PicoATE::Core::UutReport& uut,
    const QString& serialNumberFallback,
    bool useUutTiming)
{
    using namespace PicoATE::Core;

    auto result = source;
    result.uuts = {uut};
    result.metadata.serialNumber = uut.serialNumber.trimmed();
    if (result.metadata.serialNumber.isEmpty()) {
        result.metadata.serialNumber = serialNumberFallback.trimmed();
    }
    if (result.metadata.serialNumber.isEmpty()) {
        result.metadata.serialNumber = uut.uutId;
    }
    if (useUutTiming) {
        if (uut.startedAt.isValid()) {
            result.metadata.startedAt = uut.startedAt;
        }
        if (uut.finishedAt.isValid()) {
            result.metadata.finishedAt = uut.finishedAt;
        }
        if (uut.durationMs >= 0) {
            result.metadata.durationMs = uut.durationMs;
        } else if (result.metadata.startedAt.isValid() &&
                   result.metadata.finishedAt.isValid()) {
            result.metadata.durationMs = result.metadata.startedAt.msecsTo(
                result.metadata.finishedAt);
        }
    }

    const bool aborted = source.state == ExecutionState::Aborted;
    result.completed = source.completed && uut.completed;
    result.hasError = source.sessionHasError || uut.hasError || aborted;
    if (aborted) {
        result.state = ExecutionState::Aborted;
    } else if (result.completed) {
        result.state = result.hasError
            ? ExecutionState::CompletedWithError
            : ExecutionState::Completed;
    } else if (source.state == ExecutionState::Completed ||
               source.state == ExecutionState::CompletedWithError) {
        result.state = ExecutionState::CompletedWithError;
        result.hasError = true;
    }
    return result;
}

void appendError(RunArtifactResult& result, const QString& message)
{
    result.success = false;
    if (!result.errorMessage.isEmpty()) {
        result.errorMessage += QStringLiteral("; ");
    }
    result.errorMessage += message;
}

} // namespace

struct RunArtifactWriter::ArtifactChannel {
    PicoATE::Core::UutId uutId;
    RunArtifactContext context;
    QFile txtFile;
    QFile csvFile;
    QString xlsxFilePath;
    QString pdfFilePath;
    QString baseName;
};

RunArtifactWriter::RunArtifactWriter() = default;

RunArtifactSettings runArtifactSettingsFromStation(
    const QJsonObject& station,
    const QString& stationFilePath)
{
    PicoATE::Core::VariableResolverOptions resolverOptions;
    resolverOptions.sequenceFilePath = stationFilePath;
    resolverOptions.projectDir = QCoreApplication::applicationDirPath();
    const auto parsed = PicoATE::Core::parseStationConfigJson(
        station, resolverOptions);
    RunArtifactSettings settings;
    settings.txtLogEnabled = parsed.config.txtLogEnabled;
    settings.csvReportEnabled = parsed.config.csvReportEnabled;
    settings.xlsxReportEnabled = parsed.config.xlsxReportEnabled;
    settings.pdfReportEnabled = parsed.config.pdfReportEnabled;
    settings.outputDirectory = resolvedOutputDirectory(
        parsed.config.reportOutputDirectory,
        stationFilePath);
    return settings;
}

RunArtifactContext runArtifactContextFromDocuments(
    const QJsonObject& sequence,
    const QString& sequenceFilePath,
    const QJsonObject& station,
    const QString& stationFilePath,
    const QString& serialNumber)
{
    RunArtifactContext context;
    if (!sequenceFilePath.trimmed().isEmpty()) {
        context.sequenceName = QFileInfo(sequenceFilePath).fileName();
    }
    if (context.sequenceName.isEmpty()) {
        context.sequenceName =
            sequence.value(QStringLiteral("name")).toString().trimmed();
    }
    context.sequenceFilePath = sequenceFilePath.trimmed().isEmpty()
        ? QString{}
        : QFileInfo(sequenceFilePath).absoluteFilePath();
    context.serialNumber = serialNumber.trimmed();

    PicoATE::Core::VariableResolverOptions resolverOptions;
    resolverOptions.sequenceFilePath = stationFilePath;
    resolverOptions.projectDir = QCoreApplication::applicationDirPath();
    const auto parsed = PicoATE::Core::parseStationConfigJson(station, resolverOptions);
    context.stationId = parsed.config.stationId.trimmed();
    context.model = parsed.config.model.trimmed();
    context.customerId = parsed.config.customerId.trimmed();
    context.stationFilePath = stationFilePath.trimmed().isEmpty()
        ? QString{}
        : QFileInfo(stationFilePath).absoluteFilePath();
    context.order = metadataValue(
        parsed.config.metadata,
        {QStringLiteral("order"), QStringLiteral("workOrder")});
    context.tester = metadataValue(
        parsed.config.metadata,
        {QStringLiteral("tester"), QStringLiteral("operator")});
    context.jigNo = metadataValue(
        parsed.config.metadata,
        {QStringLiteral("jigNo"), QStringLiteral("fixtureId"),
         QStringLiteral("fixture")});
    return context;
}

RunArtifactWriter::~RunArtifactWriter()
{
    abandon();
}

RunArtifactResult RunArtifactWriter::begin(const RunArtifactSettings& settings,
                                           const QString& serialNumber,
                                           const QDateTime& startedAt)
{
    RunArtifactContext context;
    context.serialNumber = serialNumber;
    return begin(settings, context, startedAt);
}

RunArtifactResult RunArtifactWriter::begin(const RunArtifactSettings& settings,
                                           const RunArtifactContext& context,
                                           const QDateTime& startedAt)
{
    RunArtifactUutContext uut;
    uut.uutId = context.serialNumber.trimmed();
    uut.serialNumber = context.serialNumber.trimmed();
    return beginForUuts(settings, context, {uut}, startedAt);
}

RunArtifactResult RunArtifactWriter::beginForUuts(
    const RunArtifactSettings& settings,
    const RunArtifactContext& context,
    const QVector<RunArtifactUutContext>& uuts,
    const QDateTime& startedAt)
{
    abandon();
    m_settings = settings;
    if (!settings.txtLogEnabled && !settings.csvReportEnabled &&
        !settings.xlsxReportEnabled && !settings.pdfReportEnabled) {
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

    QVector<RunArtifactUutContext> requestedUuts = uuts;
    if (requestedUuts.isEmpty()) {
        requestedUuts.push_back(
            {context.serialNumber.trimmed(), context.serialNumber.trimmed()});
    }

    QSet<QString> seenUutIds;
    QSet<QString> reservedBaseNames;
    RunArtifactResult result;
    for (qsizetype index = 0; index < requestedUuts.size(); ++index) {
        auto uut = requestedUuts.at(index);
        uut.uutId = uut.uutId.trimmed();
        uut.serialNumber = uut.serialNumber.trimmed();
        if (uut.uutId.isEmpty()) {
            uut.uutId = uut.serialNumber;
        }
        if (uut.uutId.isEmpty()) {
            uut.uutId = QStringLiteral("UUT-%1").arg(index + 1);
        }
        if (seenUutIds.contains(uut.uutId)) {
            closeFiles();
            m_channels.clear();
            return failure(QStringLiteral("Duplicate report UUT ID: %1")
                               .arg(uut.uutId));
        }
        seenUutIds.insert(uut.uutId);

        auto channel = std::make_unique<ArtifactChannel>();
        channel->uutId = uut.uutId;
        channel->context = context;
        channel->context.serialNumber = uut.serialNumber;
        if (channel->context.serialNumber.isEmpty() &&
            requestedUuts.size() > 1) {
            channel->context.serialNumber = uut.uutId;
        }

        auto filePrefix = safeFileName(channel->context.serialNumber);
        const auto uutPrefix = safeFileName(uut.uutId);
        if (requestedUuts.size() > 1 && !uutPrefix.isEmpty() &&
            filePrefix.compare(uutPrefix, Qt::CaseInsensitive) != 0) {
            filePrefix = filePrefix.isEmpty()
                ? uutPrefix
                : filePrefix + QLatin1Char('_') + uutPrefix;
        }

        auto fileTimestamp = localStart;
        do {
            channel->baseName = filePrefix.isEmpty()
                ? fileTimestamp.toString(QStringLiteral("yyyyMMdd_HHmmsszzz"))
                : filePrefix + fileTimestamp.toString(
                      QStringLiteral("_HHmmsszzz"));
            if (!artifactExists(m_dateDirectory, channel->baseName) &&
                !reservedBaseNames.contains(channel->baseName)) {
                break;
            }
            fileTimestamp = fileTimestamp.addMSecs(1);
        } while (true);
        reservedBaseNames.insert(channel->baseName);

        if (settings.txtLogEnabled) {
            const auto path = QDir(m_dateDirectory).filePath(
                channel->baseName + QStringLiteral(".txt"));
            channel->txtFile.setFileName(path);
            if (!channel->txtFile.open(QIODevice::WriteOnly |
                                       QIODevice::Truncate)) {
                const auto message = channel->txtFile.errorString();
                closeFiles();
                m_channels.clear();
                return failure(message);
            }
            channel->txtFile.write("\xEF\xBB\xBF");
            const auto header = executionLogHeader(channel->context, localStart);
            if (channel->txtFile.write(header) != header.size()) {
                const auto message = channel->txtFile.errorString();
                closeFiles();
                m_channels.clear();
                return failure(message);
            }
            channel->txtFile.flush();
            result.filePaths.push_back(path);
        }
        if (settings.csvReportEnabled) {
            const auto path = QDir(m_dateDirectory).filePath(
                channel->baseName + QStringLiteral(".csv"));
            channel->csvFile.setFileName(path);
            if (!channel->csvFile.open(QIODevice::WriteOnly |
                                       QIODevice::Truncate)) {
                const auto message = channel->csvFile.errorString();
                closeFiles();
                m_channels.clear();
                return failure(message);
            }
            channel->csvFile.write(ReportExporter::csvHeader());
            channel->csvFile.flush();
            result.filePaths.push_back(path);
        }
        if (settings.xlsxReportEnabled) {
            channel->xlsxFilePath = QDir(m_dateDirectory).filePath(
                channel->baseName + QStringLiteral(".xlsx"));
        }
        if (settings.pdfReportEnabled) {
            channel->pdfFilePath = QDir(m_dateDirectory).filePath(
                channel->baseName + QStringLiteral(".pdf"));
        }
        m_channels.push_back(std::move(channel));
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
        const bool sharedLine = line.uutId.trimmed().isEmpty();
        for (auto& channel : m_channels) {
            const bool belongsToChannel = sharedLine ||
                channel->uutId == line.uutId || m_channels.size() == 1;
            if (!belongsToChannel || !channel->txtFile.isOpen()) {
                continue;
            }
            if (channel->txtFile.write(bytes) != bytes.size()) {
                return failure(channel->txtFile.errorString());
            }
            if (line.message.contains(QStringLiteral("_TESTITEM_END ")) &&
                line.message.contains(QStringLiteral("========================"))) {
                constexpr auto separator = "\r\n\r\n\r\n";
                if (channel->txtFile.write(separator) != 6) {
                    return failure(channel->txtFile.errorString());
                }
            }
        }
    }
    for (auto& channel : m_channels) {
        if (channel->txtFile.isOpen() && !channel->txtFile.flush()) {
            return failure(channel->txtFile.errorString());
        }
    }
    return {};
}

RunArtifactResult RunArtifactWriter::finalize(
    const PicoATE::Core::ExecutionReport& report)
{
    if (!m_active) {
        return {};
    }

    RunArtifactResult result;
    for (auto& channel : m_channels) {
        const auto* uut = findUutReport(report, channel->uutId);
        if (!uut && m_channels.size() == 1 && report.uuts.size() == 1) {
            uut = &report.uuts.constFirst();
        }

        if (channel->csvFile.isOpen()) {
            channel->csvFile.flush();
            channel->csvFile.close();
        }
        if (channel->txtFile.isOpen()) {
            channel->txtFile.flush();
            channel->txtFile.close();
        }

        if (!uut) {
            appendError(result,
                        QStringLiteral("No execution report found for UUT %1")
                            .arg(channel->uutId));
        }
        const auto uutReport = uut
            ? reportForUut(report,
                           *uut,
                           channel->context.serialNumber,
                           m_channels.size() > 1)
            : report;

        const auto exportReport = [&](const ReportExportResult& exported,
                                      const QString& format) {
            if (!exported.success) {
                appendError(result,
                            QStringLiteral("%1 export failed for UUT %2: %3")
                                .arg(format, channel->uutId,
                                     exported.errorMessage));
            }
        };
        if (uut && m_settings.csvReportEnabled) {
            exportReport(ReportExporter::saveCsv(
                             channel->csvFile.fileName(), uutReport),
                         QStringLiteral("CSV"));
        }
        if (uut && m_settings.xlsxReportEnabled) {
            exportReport(ReportExporter::saveXlsx(
                             channel->xlsxFilePath, uutReport),
                         QStringLiteral("XLSX"));
        }
        if (uut && m_settings.pdfReportEnabled) {
            exportReport(ReportExporter::savePdf(
                             channel->pdfFilePath, uutReport),
                         QStringLiteral("PDF"));
        }

        const bool passed = uut && uutReport.completed && !uutReport.hasError &&
            uutReport.state == PicoATE::Core::ExecutionState::Completed;
        const auto destinationDirectory = QDir(m_dateDirectory).filePath(
            passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
        const QStringList sources = {
            channel->txtFile.fileName(), channel->csvFile.fileName(),
            channel->xlsxFilePath, channel->pdfFilePath};
        for (const auto& source : sources) {
            if (source.isEmpty() || !QFileInfo::exists(source)) {
                continue;
            }
            const auto destination = QDir(destinationDirectory).filePath(
                QFileInfo(source).fileName());
            if (!QFile::rename(source, destination)) {
                appendError(result, QStringLiteral("Cannot move %1 to %2")
                                        .arg(source, destination));
                result.filePaths.push_back(source);
            } else {
                result.filePaths.push_back(destination);
            }
        }
    }
    m_active = false;
    return result;
}

void RunArtifactWriter::abandon()
{
    closeFiles();
    m_channels.clear();
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
    return m_channels.empty() ? QString{} : m_channels.front()->baseName;
}

QStringList RunArtifactWriter::baseNames() const
{
    QStringList names;
    names.reserve(static_cast<qsizetype>(m_channels.size()));
    for (const auto& channel : m_channels) {
        names.push_back(channel->baseName);
    }
    return names;
}

void RunArtifactWriter::closeFiles()
{
    for (auto& channel : m_channels) {
        if (channel->txtFile.isOpen()) {
            channel->txtFile.flush();
            channel->txtFile.close();
        }
        if (channel->csvFile.isOpen()) {
            channel->csvFile.flush();
            channel->csvFile.close();
        }
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
