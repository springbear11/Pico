#include "StationDocument.h"

#include "PicoATE/Core/StationConfig.h"
#include "PicoATE/Core/DeviceDiscovery.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>
#include <QUndoCommand>
#include <QUndoStack>

#include <utility>

namespace PicoATE::Ui {

namespace {

QString normalizedAbsolutePath(const QString& path)
{
    return path.trimmed().isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

QString normalizedDeviceType(QString deviceType)
{
    deviceType = deviceType.trimmed().toUpper();
    return deviceType.isEmpty() ? QStringLiteral("PLUGIN") : deviceType;
}

QString deviceId(const QJsonObject& device)
{
    return device.value(QStringLiteral("deviceId")).toString(
        device.value(QStringLiteral("id")).toString()).trimmed();
}

QString deviceType(const QJsonObject& device)
{
    return normalizedDeviceType(
        device.value(QStringLiteral("deviceType")).toString(
            device.value(QStringLiteral("type")).toString()));
}

int generatedDeviceNumber(const QString& id, const QString& type)
{
    const QRegularExpression expression(
        QStringLiteral("^%1(\\d+)$")
            .arg(QRegularExpression::escape(normalizedDeviceType(type))),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(id.trimmed());
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

} // namespace

class StationRootCommand final : public QUndoCommand
{
public:
    StationRootCommand(StationDocument* document,
                       QJsonObject before,
                       QJsonObject after,
                       QString text)
        : m_document(document)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
        setText(std::move(text));
    }

    void undo() override { m_document->applyCommandRoot(m_before); }
    void redo() override { m_document->applyCommandRoot(m_after); }

private:
    StationDocument* m_document = nullptr;
    QJsonObject m_before;
    QJsonObject m_after;
};

StationDocument::StationDocument(QObject* parent)
    : QObject(parent)
{
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(200);
    connect(m_undoStack, &QUndoStack::cleanChanged,
            this, [this](bool clean) { setModified(!clean); });
}

StationDocument::~StationDocument()
{
    if (!m_undoStack) {
        return;
    }
    QObject::disconnect(m_undoStack, nullptr, nullptr, nullptr);
    m_undoStack->clear();
}

QString StationDocument::filePath() const { return m_filePath; }

QString StationDocument::displayName() const
{
    if (!m_filePath.isEmpty()) {
        return QFileInfo(m_filePath).fileName();
    }
    return m_root.value("name").toString(tr("Untitled Station"));
}

bool StationDocument::isModified() const { return m_modified; }
bool StationDocument::isEmpty() const { return m_root.isEmpty(); }
quint64 StationDocument::revision() const { return m_revision; }
QJsonObject StationDocument::rootObject() const { return m_root; }
QVector<UiDiagnostic> StationDocument::diagnostics() const { return m_diagnostics; }
QUndoStack* StationDocument::undoStack() const { return m_undoStack; }

StationDocumentSnapshot StationDocument::snapshot() const
{
    StationDocumentSnapshot result;
    result.filePath = m_filePath;
    result.root = m_root;
    result.json = m_root.isEmpty()
        ? QByteArray{}
        : QJsonDocument(m_root).toJson(QJsonDocument::Compact);
    result.revision = m_revision;
    return result;
}

bool StationDocument::load(const QString& filePath)
{
    const auto absolutePath = normalizedAbsolutePath(filePath);
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setLoadError({}, tr("Failed to open station file: %1").arg(absolutePath),
                     file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setLoadError(QString("offset %1").arg(parseError.offset),
                     parseError.errorString(),
                     tr("Fix the JSON syntax before opening the document"));
        return false;
    }
    if (!document.isObject()) {
        setLoadError({}, tr("Station JSON root must be an object"));
        return false;
    }

    acceptRoot(document.object(), absolutePath);
    return true;
}

bool StationDocument::save(QString* errorMessage)
{
    if (m_filePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Station file path is empty");
        }
        return false;
    }
    return saveAs(m_filePath, errorMessage);
}

bool StationDocument::saveAs(const QString& filePath, QString* errorMessage)
{
    const auto absolutePath = normalizedAbsolutePath(filePath);
    if (absolutePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Station file path is empty");
        }
        return false;
    }

    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    const auto bytes = QJsonDocument(m_root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const bool pathChanged = m_filePath != absolutePath;
    m_filePath = absolutePath;
    m_undoStack->setClean();
    if (pathChanged) {
        emit filePathChanged(m_filePath);
    }
    return true;
}

void StationDocument::clear()
{
    const bool hadPath = !m_filePath.isEmpty();
    m_undoStack->clear();
    m_filePath.clear();
    m_root = {};
    m_diagnostics.clear();
    ++m_revision;
    setModified(false);
    if (hadPath) {
        emit filePathChanged({});
    }
    emit diagnosticsChanged();
    emit documentChanged();
}

int StationDocument::deviceCount() const
{
    return m_root.value("devices").toArray().size();
}

QJsonObject StationDocument::deviceAt(int row) const
{
    const auto devices = m_root.value("devices").toArray();
    if (row < 0 || row >= devices.size() || !devices[row].isObject()) {
        return {};
    }
    return devices[row].toObject();
}

bool StationDocument::setRootValue(const QString& key, const QJsonValue& value)
{
    if (key.trimmed().isEmpty() || m_root.isEmpty()) {
        return false;
    }
    auto root = m_root;
    root.insert(key, value);
    return commitRoot(std::move(root), tr("Edit Station Property"));
}

bool StationDocument::replaceRootObject(QJsonObject root)
{
    if (root.isEmpty()) {
        return false;
    }
    return commitRoot(std::move(root), tr("Edit Station Properties"));
}

bool StationDocument::insertDevice(int row, QJsonObject device)
{
    if (m_root.isEmpty()) {
        return false;
    }
    if (device.isEmpty()) {
        const auto type = QStringLiteral("PLUGIN");
        device.insert("deviceId", nextDeviceIdForType(type));
        device.insert("deviceType", type);
        device.insert("driverId", "");
        device.insert("connectionKind", "manual");
        device.insert("resource", "");
        device.insert("lifetime", "Station");
        device.insert("enabled", false);
        device.insert("options", QJsonObject{});
    }

    auto root = m_root;
    auto devices = root.value("devices").toArray();
    const int insertionRow = row < 0 ? devices.size() : qBound(0, row, devices.size());
    devices.insert(insertionRow, device);
    root.insert("devices", devices);
    return commitRoot(std::move(root), tr("Add Device"));
}

bool StationDocument::removeDevice(int row)
{
    auto root = m_root;
    auto devices = root.value("devices").toArray();
    if (row < 0 || row >= devices.size()) {
        return false;
    }
    devices.removeAt(row);
    root.insert("devices", devices);
    return commitRoot(std::move(root), tr("Delete Device"));
}

bool StationDocument::duplicateDevice(int row)
{
    auto copy = deviceAt(row);
    if (copy.isEmpty()) {
        return false;
    }
    copy.insert("deviceId", nextDeviceIdForType(deviceType(copy)));
    copy.remove("id");
    return insertDevice(row + 1, std::move(copy));
}

bool StationDocument::moveDevice(int row, int offset)
{
    if (offset == 0) {
        return false;
    }
    auto root = m_root;
    auto devices = root.value("devices").toArray();
    const int target = row + offset;
    if (row < 0 || row >= devices.size() || target < 0 || target >= devices.size()) {
        return false;
    }
    const auto value = devices.takeAt(row);
    devices.insert(target, value);
    root.insert("devices", devices);
    return commitRoot(std::move(root), tr("Move Device"));
}

bool StationDocument::moveDeviceConfiguration(int sourceRow, int targetRow)
{
    auto source = deviceAt(sourceRow);
    auto target = deviceAt(targetRow);
    if (source.isEmpty() || target.isEmpty() || sourceRow == targetRow ||
        deviceType(source) != deviceType(target) || !isDeviceSlotEmpty(targetRow)) {
        return false;
    }

    const auto targetId = deviceId(target);
    target = source;
    target.insert(QStringLiteral("deviceId"), targetId);
    target.remove(QStringLiteral("id"));

    QJsonObject cleared;
    cleared.insert(QStringLiteral("deviceId"), deviceId(source));
    cleared.insert(QStringLiteral("deviceType"), deviceType(source));
    cleared.insert(QStringLiteral("driverId"), QString());
    cleared.insert(QStringLiteral("connectionKind"), QStringLiteral("manual"));
    cleared.insert(QStringLiteral("resource"), QString());
    cleared.insert(QStringLiteral("timeoutMs"), 30000);
    cleared.insert(QStringLiteral("lifetime"), QStringLiteral("Station"));
    cleared.insert(QStringLiteral("enabled"), false);
    cleared.insert(QStringLiteral("options"), QJsonObject{});

    auto root = m_root;
    auto devices = root.value(QStringLiteral("devices")).toArray();
    devices[targetRow] = target;
    devices[sourceRow] = cleared;
    root.insert(QStringLiteral("devices"), devices);
    return commitRoot(std::move(root), tr("Move Device Configuration"));
}

bool StationDocument::setDeviceValue(int row,
                                     const QString& key,
                                     const QJsonValue& value)
{
    auto device = deviceAt(row);
    if (device.isEmpty() || key.trimmed().isEmpty()) {
        return false;
    }
    device.insert(key, value);
    return replaceDevice(row, std::move(device));
}

bool StationDocument::replaceDevice(int row, QJsonObject device)
{
    if (device.isEmpty()) {
        return false;
    }
    auto root = m_root;
    auto devices = root.value("devices").toArray();
    if (row < 0 || row >= devices.size() || !devices[row].isObject()) {
        return false;
    }
    devices[row] = std::move(device);
    root.insert("devices", devices);
    return commitRoot(std::move(root), tr("Edit Device"));
}

bool StationDocument::commitRoot(QJsonObject root, const QString& commandText)
{
    if (root == m_root) {
        return false;
    }
    m_undoStack->push(new StationRootCommand(
        this, m_root, std::move(root), commandText));
    return true;
}

void StationDocument::applyCommandRoot(QJsonObject root)
{
    m_root = std::move(root);
    ++m_revision;
    validate();
    emit documentChanged();
}

QString StationDocument::nextDeviceIdForType(const QString& requestedType,
                                             int excludedRow) const
{
    const auto type = normalizedDeviceType(requestedType);
    QSet<QString> ids;
    const auto devices = m_root.value(QStringLiteral("devices")).toArray();
    for (int row = 0; row < devices.size(); ++row) {
        if (row == excludedRow) {
            continue;
        }
        const auto id = deviceId(devices[row].toObject()).toUpper();
        ids.insert(id);
        const int separator = id.indexOf(QStringLiteral(".CH"));
        if (separator > 0) {
            ids.insert(id.left(separator));
        }
    }
    for (int index = 1; ; ++index) {
        const auto candidate = QStringLiteral("%1%2").arg(type).arg(index);
        if (!ids.contains(candidate)) {
            return candidate;
        }
    }
}

bool StationDocument::isLastDeviceOfType(int row) const
{
    const auto selected = deviceAt(row);
    if (selected.isEmpty()) {
        return false;
    }
    const auto selectedType = deviceType(selected);
    const int selectedNumber = generatedDeviceNumber(deviceId(selected), selectedType);
    const auto devices = m_root.value(QStringLiteral("devices")).toArray();
    for (int candidateRow = 0; candidateRow < devices.size(); ++candidateRow) {
        if (candidateRow == row) {
            continue;
        }
        const auto candidate = devices[candidateRow].toObject();
        if (deviceType(candidate) != selectedType) {
            continue;
        }
        const int candidateNumber = generatedDeviceNumber(
            deviceId(candidate), selectedType);
        if ((selectedNumber >= 0 && candidateNumber > selectedNumber) ||
            (selectedNumber < 0 && candidateRow > row)) {
            return false;
        }
    }
    return true;
}

bool StationDocument::isDeviceSlotEmpty(int row) const
{
    const auto device = deviceAt(row);
    if (device.isEmpty()) {
        return false;
    }
    return device.value(QStringLiteral("driverId")).toString(
               device.value(QStringLiteral("driver")).toString()).trimmed().isEmpty() &&
           device.value(QStringLiteral("address")).toString(
               device.value(QStringLiteral("visaAddress")).toString()).trimmed().isEmpty() &&
           device.value(QStringLiteral("resource")).toString().trimmed().isEmpty() &&
           device.value(QStringLiteral("options")).toObject().isEmpty();
}

int StationDocument::previousEmptyDeviceRow(int row) const
{
    const auto selected = deviceAt(row);
    if (selected.isEmpty()) {
        return -1;
    }
    const auto selectedType = deviceType(selected);
    const int selectedNumber = generatedDeviceNumber(deviceId(selected), selectedType);
    if (selectedNumber < 1) {
        return -1;
    }

    int bestRow = -1;
    int bestNumber = -1;
    const auto devices = m_root.value(QStringLiteral("devices")).toArray();
    for (int candidateRow = 0; candidateRow < devices.size(); ++candidateRow) {
        if (candidateRow == row || !isDeviceSlotEmpty(candidateRow)) {
            continue;
        }
        const auto candidate = devices[candidateRow].toObject();
        if (deviceType(candidate) != selectedType) {
            continue;
        }
        const int candidateNumber = generatedDeviceNumber(
            deviceId(candidate), selectedType);
        if (candidateNumber > 0 && candidateNumber < selectedNumber &&
            (bestNumber < 0 || candidateNumber < bestNumber)) {
            bestNumber = candidateNumber;
            bestRow = candidateRow;
        }
    }
    return bestRow;
}

void StationDocument::acceptRoot(QJsonObject root, QString filePath)
{
    if (!root.contains(QStringLiteral("pluginRegistry"))) {
        root.insert(QStringLiteral("pluginRegistry"),
                    QStringLiteral("plugins/PluginRegistry.json"));
    }
    auto devices = root.value(QStringLiteral("devices")).toArray();
    for (int index = 0; index < devices.size(); ++index) {
        if (!devices[index].isObject()) {
            continue;
        }
        auto device = devices[index].toObject();
        device.remove(QStringLiteral("pluginPath"));
        auto options = device.value(QStringLiteral("options")).toObject();
        auto resource = device.value(QStringLiteral("resource")).toString().trimmed();
        if (resource.isEmpty()) {
            resource = device.value(QStringLiteral("address")).toString(
                device.value(QStringLiteral("visaAddress")).toString()).trimmed();
        }
        if (resource.isEmpty()) {
            resource = options.value(QStringLiteral("serialNumber")).toString().trimmed();
        }
        if (resource.isEmpty()) {
            resource = options.value(QStringLiteral("address"))
                           .toString(options.value(QStringLiteral("visaAddress")).toString())
                           .trimmed();
        }
        auto kind = PicoATE::Core::deviceConnectionKindFromString(
            device.value(QStringLiteral("connectionKind")).toString());
        if (!kind) {
            kind = PicoATE::Core::inferDeviceConnectionKind(
                deviceType(device), resource);
        }
        device.insert(QStringLiteral("connectionKind"),
                      PicoATE::Core::deviceConnectionKindName(*kind));
        device.insert(QStringLiteral("resource"), resource);
        device.remove(QStringLiteral("address"));
        device.remove(QStringLiteral("visaAddress"));
        options.remove(QStringLiteral("address"));
        options.remove(QStringLiteral("visaAddress"));
        options.remove(QStringLiteral("serialNumber"));
        options.remove(QStringLiteral("deviceIndex"));
        device.insert(QStringLiteral("options"), options);
        devices[index] = device;
    }
    root.insert(QStringLiteral("devices"), devices);
    const bool pathChanged = m_filePath != filePath;
    m_undoStack->clear();
    m_root = std::move(root);
    m_filePath = std::move(filePath);
    ++m_revision;
    setModified(false);
    validate();
    if (pathChanged) {
        emit filePathChanged(m_filePath);
    }
    emit documentChanged();
}

void StationDocument::setModified(bool modified)
{
    if (m_modified == modified) {
        return;
    }
    m_modified = modified;
    emit modifiedChanged(m_modified);
}

void StationDocument::validate()
{
    m_diagnostics.clear();
    if (!m_root.isEmpty()) {
        PicoATE::Core::VariableResolverOptions options;
        options.sequenceFilePath = m_filePath;
        const auto result = PicoATE::Core::parseStationConfigJson(m_root, options);
        for (const auto& diagnostic : result.errors) {
            m_diagnostics.push_back({UiDiagnosticSeverity::Error,
                                     diagnostic.path,
                                     diagnostic.message,
                                     diagnostic.suggestion});
        }
    }
    emit diagnosticsChanged();
}

void StationDocument::setLoadError(QString path,
                                   QString message,
                                   QString suggestion)
{
    m_diagnostics = {{UiDiagnosticSeverity::Error,
                      std::move(path),
                      std::move(message),
                      std::move(suggestion)}};
    emit diagnosticsChanged();
}

} // namespace PicoATE::Ui
