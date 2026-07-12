#include "StationDocument.h"

#include "PicoATE/Core/StationConfig.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUndoCommand>
#include <QUndoStack>

#include <utility>

namespace PicoATE::Ui {

namespace {

QString normalizedAbsolutePath(const QString& path)
{
    return path.trimmed().isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
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
        device.insert("deviceId", nextDeviceId());
        device.insert("deviceType", "Generic");
        device.insert("driverId", "");
        device.insert("address", "");
        device.insert("lifetime", "Station");
        device.insert("enabled", true);
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
    copy.insert("deviceId", nextDeviceId());
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

QString StationDocument::nextDeviceId() const
{
    QSet<QString> ids;
    for (const auto& value : m_root.value("devices").toArray()) {
        const auto object = value.toObject();
        ids.insert(object.value("deviceId").toString(
            object.value("id").toString()).trimmed().toUpper());
    }
    for (int index = 1; ; ++index) {
        const auto candidate = QString("DEVICE%1").arg(index);
        if (!ids.contains(candidate)) {
            return candidate;
        }
    }
}

void StationDocument::acceptRoot(QJsonObject root, QString filePath)
{
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
