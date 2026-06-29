#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTextStream>
#include <QThread>

namespace {

QJsonObject errorResponse(const QString& code, const QString& message)
{
    QJsonObject response;
    response.insert("outcome", "Error");
    response.insert("outputs", QJsonObject{});
    response.insert("measurements", QJsonObject{});
    response.insert("errorCode", code);
    response.insert("errorMessage", message);
    return response;
}

void writeResponse(const QJsonObject& response)
{
    QTextStream out(stdout);
    out << QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)) << Qt::endl;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QTextStream in(stdin);
    while (!in.atEnd()) {
        const auto line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            writeResponse(errorResponse("InvalidRequest", parseError.errorString()));
            continue;
        }

        const auto request = document.object();
        const auto context = request.value("context").toObject();
        const auto inputs = context.value("inputs").toObject();

        const auto delayMs = inputs.value("mockDelayMs").toInt(0);
        if (delayMs > 0) {
            QThread::msleep(static_cast<unsigned long>(delayMs));
        }

        if (inputs.contains("mockExitCode")) {
            return inputs.value("mockExitCode").toInt(1);
        }

        QJsonObject response;
        response.insert("outcome", inputs.value("outcome").toString("Passed"));
        response.insert("outputs", inputs);
        response.insert("measurements", inputs.value("measurements").toObject());
        response.insert("errorCode", inputs.value("errorCode").toString());
        response.insert("errorMessage", inputs.value("errorMessage").toString());
        writeResponse(response);
    }

    return 0;
}
