#include "PicoATE/Core/ValueToolsModule.h"

#include <QDateTime>
#include <QMetaType>

#include <algorithm>
#include <cmath>
#include <limits>

namespace PicoATE::Core {

namespace {

QString normalized(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char('_'));
    value.remove(QLatin1Char(' '));
    return value;
}

void publishLog(const ModuleExecutionContext& context, const QString& message)
{
    if (!context.logSink) {
        return;
    }
    ModuleLogRecord record;
    record.timestampUtc = QDateTime::currentDateTimeUtc();
    record.message = message;
    context.logSink->publishModuleLog(record);
}

ModuleResult valueError(const ModuleExecutionContext& context,
                        const QString& code,
                        const QString& message)
{
    publishLog(context, QStringLiteral("VALUE_ERROR code=%1 message=%2")
                            .arg(code, message));
    ModuleResult result;
    result.outcome = ModuleOutcome::Error;
    result.errorCode = code;
    result.errorMessage = message;
    return result;
}

bool finiteNumber(const QVariant& value, double& number)
{
    bool ok = false;
    number = value.toDouble(&ok);
    return ok && std::isfinite(number);
}

bool integerValue(const QVariant& value, qint64& number)
{
    bool ok = false;
    const auto type = value.metaType().id();
    if (type == QMetaType::QString || type == QMetaType::QByteArray) {
        number = value.toString().trimmed().toLongLong(&ok, 10);
        return ok;
    }

    const double numeric = value.toDouble(&ok);
    if (!ok || !std::isfinite(numeric) || std::trunc(numeric) != numeric ||
        numeric < static_cast<double>(std::numeric_limits<qint64>::min()) ||
        numeric > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return false;
    }
    number = static_cast<qint64>(numeric);
    return true;
}

QString formatInteger(qint64 value,
                      int base,
                      int width,
                      bool prefix,
                      bool uppercase)
{
    const bool negative = value < 0;
    const quint64 magnitude = negative
        ? static_cast<quint64>(-(value + 1)) + 1
        : static_cast<quint64>(value);
    QString digits = QString::number(magnitude, base);
    if (uppercase) {
        digits = digits.toUpper();
    }
    if (width > digits.size()) {
        digits.prepend(QString(width - digits.size(), QLatin1Char('0')));
    }

    QString basePrefix;
    if (prefix) {
        if (base == 16) basePrefix = QStringLiteral("0x");
        else if (base == 8) basePrefix = QStringLiteral("0o");
        else if (base == 2) basePrefix = QStringLiteral("0b");
    }
    return (negative ? QStringLiteral("-") : QString{}) + basePrefix + digits;
}

int inputBase(const QVariant& value, int fallback)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    return ok ? parsed : fallback;
}

bool supportedBase(int base)
{
    return base == 2 || base == 8 || base == 10 || base == 16;
}

ModuleResult statistics(const ModuleExecutionContext& context)
{
    const auto values = context.inputs.value(QStringLiteral("values")).toList();
    if (values.isEmpty()) {
        return valueError(context, QStringLiteral("StatisticsValuesMissing"),
                          QStringLiteral("Statistics requires at least one value"));
    }

    double sum = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    int minimumIndex = -1;
    int maximumIndex = -1;
    QString minimumName;
    QString maximumName;

    for (int index = 0; index < values.size(); ++index) {
        QVariant sample = values[index];
        QString name = QStringLiteral("Value %1").arg(index + 1);
        if (sample.metaType().id() == QMetaType::QVariantMap) {
            const auto item = sample.toMap();
            sample = item.value(QStringLiteral("value"));
            const auto configuredName = item.value(QStringLiteral("name")).toString().trimmed();
            if (!configuredName.isEmpty()) {
                name = configuredName;
            }
        }

        double numeric = 0.0;
        if (!finiteNumber(sample, numeric)) {
            return valueError(
                context,
                QStringLiteral("StatisticsValueNotNumeric"),
                QStringLiteral("Statistics value %1 (%2) must be a finite number")
                    .arg(index + 1)
                    .arg(name));
        }

        if (index == 0 || numeric < minimum) {
            minimum = numeric;
            minimumIndex = index;
            minimumName = name;
        }
        if (index == 0 || numeric > maximum) {
            maximum = numeric;
            maximumIndex = index;
            maximumName = name;
        }
        sum += numeric;
    }

    ModuleResult result;
    result.outputs.insert(QStringLiteral("count"), values.size());
    result.outputs.insert(QStringLiteral("sum"), sum);
    result.outputs.insert(QStringLiteral("minimum"), minimum);
    result.outputs.insert(QStringLiteral("maximum"), maximum);
    result.outputs.insert(QStringLiteral("range"), maximum - minimum);
    result.outputs.insert(QStringLiteral("average"), sum / values.size());
    result.outputs.insert(QStringLiteral("minimumIndex"), minimumIndex);
    result.outputs.insert(QStringLiteral("maximumIndex"), maximumIndex);
    result.outputs.insert(QStringLiteral("minimumName"), minimumName);
    result.outputs.insert(QStringLiteral("maximumName"), maximumName);
    publishLog(context,
               QStringLiteral("STATISTICS count=%1 minimum=%2 (%3) maximum=%4 (%5) range=%6 average=%7")
                   .arg(values.size())
                   .arg(minimum, 0, 'g', 15)
                   .arg(minimumName)
                   .arg(maximum, 0, 'g', 15)
                   .arg(maximumName)
                   .arg(maximum - minimum, 0, 'g', 15)
                   .arg(sum / values.size(), 0, 'g', 15));
    return result;
}

ModuleResult calculate(const ModuleExecutionContext& context)
{
    const auto operation = normalized(
        context.inputs.value(QStringLiteral("operation")).toString());
    double a = 0.0;
    if (!finiteNumber(context.inputs.value(QStringLiteral("a")), a)) {
        return valueError(context, QStringLiteral("CalculationOperandMissing"),
                          QStringLiteral("Operand A must be a finite number"));
    }

    const bool unary = operation == QStringLiteral("absolute") ||
                       operation == QStringLiteral("negate") ||
                       operation == QStringLiteral("squareroot") ||
                       operation == QStringLiteral("round") ||
                       operation == QStringLiteral("floor") ||
                       operation == QStringLiteral("ceil") ||
                       operation == QStringLiteral("clamp");
    double b = 0.0;
    if (!unary && !finiteNumber(context.inputs.value(QStringLiteral("b")), b)) {
        return valueError(context, QStringLiteral("CalculationOperandMissing"),
                          QStringLiteral("Operand B must be a finite number"));
    }

    double value = 0.0;
    if (operation == QStringLiteral("add")) value = a + b;
    else if (operation == QStringLiteral("subtract")) value = a - b;
    else if (operation == QStringLiteral("multiply")) value = a * b;
    else if (operation == QStringLiteral("divide")) {
        if (b == 0.0) {
            return valueError(context, QStringLiteral("DivisionByZero"),
                              QStringLiteral("Division by zero is not allowed"));
        }
        value = a / b;
    } else if (operation == QStringLiteral("modulo")) {
        if (b == 0.0) {
            return valueError(context, QStringLiteral("ModuloByZero"),
                              QStringLiteral("Modulo by zero is not allowed"));
        }
        value = std::fmod(a, b);
    } else if (operation == QStringLiteral("power")) value = std::pow(a, b);
    else if (operation == QStringLiteral("minimum")) value = std::min(a, b);
    else if (operation == QStringLiteral("maximum")) value = std::max(a, b);
    else if (operation == QStringLiteral("absolutedifference")) value = std::abs(a - b);
    else if (operation == QStringLiteral("absolute")) value = std::abs(a);
    else if (operation == QStringLiteral("negate")) value = -a;
    else if (operation == QStringLiteral("squareroot")) {
        if (a < 0.0) {
            return valueError(context, QStringLiteral("SquareRootDomainError"),
                              QStringLiteral("Square root requires a non-negative value"));
        }
        value = std::sqrt(a);
    } else if (operation == QStringLiteral("floor")) value = std::floor(a);
    else if (operation == QStringLiteral("ceil")) value = std::ceil(a);
    else if (operation == QStringLiteral("round")) {
        const int decimals = std::clamp(
            context.inputs.value(QStringLiteral("decimals"), 0).toInt(), 0, 12);
        const double factor = std::pow(10.0, decimals);
        value = std::round(a * factor) / factor;
    } else if (operation == QStringLiteral("clamp")) {
        double minimum = 0.0;
        double maximum = 0.0;
        if (!finiteNumber(context.inputs.value(QStringLiteral("minimum")), minimum) ||
            !finiteNumber(context.inputs.value(QStringLiteral("maximum")), maximum) ||
            minimum > maximum) {
            return valueError(context, QStringLiteral("ClampRangeInvalid"),
                              QStringLiteral("Clamp requires minimum <= maximum"));
        }
        value = std::clamp(a, minimum, maximum);
    } else {
        return valueError(context, QStringLiteral("CalculationOperationUnsupported"),
                          QStringLiteral("Unsupported calculation operation: %1")
                              .arg(operation));
    }

    if (!std::isfinite(value)) {
        return valueError(context, QStringLiteral("CalculationResultNotFinite"),
                          QStringLiteral("Calculation result is not finite"));
    }

    ModuleResult result;
    result.outputs.insert(QStringLiteral("value"), value);
    result.outputs.insert(QStringLiteral("operation"), operation);
    publishLog(context, QStringLiteral("CALCULATE operation=%1 a=%2 b=%3 result=%4")
                            .arg(operation)
                            .arg(a, 0, 'g', 15)
                            .arg(b, 0, 'g', 15)
                            .arg(value, 0, 'g', 15));
    return result;
}

ModuleResult textToNumber(const ModuleExecutionContext& context)
{
    auto text = context.inputs.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty()) {
        return valueError(context, QStringLiteral("NumberTextMissing"),
                          QStringLiteral("Text to Number requires input text"));
    }
    text.remove(QLatin1Char('_'));
    text.remove(QLatin1Char(' '));

    bool negative = false;
    if (text.startsWith(QLatin1Char('+')) || text.startsWith(QLatin1Char('-'))) {
        negative = text.startsWith(QLatin1Char('-'));
        text.remove(0, 1);
    }

    int base = inputBase(context.inputs.value(QStringLiteral("base")), 0);
    if (base == 0) {
        if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) base = 16;
        else if (text.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) base = 2;
        else if (text.startsWith(QStringLiteral("0o"), Qt::CaseInsensitive)) base = 8;
        else base = 10;
    }
    if (!supportedBase(base)) {
        return valueError(context, QStringLiteral("NumberBaseUnsupported"),
                          QStringLiteral("Number base must be 2, 8, 10, or 16"));
    }
    if ((base == 16 && text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) ||
        (base == 2 && text.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) ||
        (base == 8 && text.startsWith(QStringLiteral("0o"), Qt::CaseInsensitive))) {
        text.remove(0, 2);
    }
    if (text.isEmpty()) {
        return valueError(context, QStringLiteral("NumberTextInvalid"),
                          QStringLiteral("Number text does not contain digits"));
    }

    bool ok = false;
    quint64 magnitude = text.toULongLong(&ok, base);
    if (!ok || (negative && magnitude > (quint64(1) << 63)) ||
        (!negative && magnitude > static_cast<quint64>(std::numeric_limits<qint64>::max()))) {
        return valueError(context, QStringLiteral("NumberTextInvalid"),
                          QStringLiteral("Text is not a signed 64-bit integer in base %1")
                              .arg(base));
    }
    const qint64 number = negative
        ? (magnitude == (quint64(1) << 63)
               ? std::numeric_limits<qint64>::min()
               : -static_cast<qint64>(magnitude))
        : static_cast<qint64>(magnitude);

    ModuleResult result;
    result.outputs.insert(QStringLiteral("number"), number);
    result.outputs.insert(QStringLiteral("decimalText"), QString::number(number));
    result.outputs.insert(QStringLiteral("hexText"),
                          formatInteger(number, 16, 0, true, true));
    result.outputs.insert(QStringLiteral("binaryText"),
                          formatInteger(number, 2, 0, true, false));
    result.outputs.insert(QStringLiteral("base"), base);
    publishLog(context, QStringLiteral("TEXT_TO_NUMBER text=%1 base=%2 number=%3 hex=%4")
                            .arg(context.inputs.value(QStringLiteral("text")).toString())
                            .arg(base)
                            .arg(number)
                            .arg(result.outputs.value(QStringLiteral("hexText")).toString()));
    return result;
}

ModuleResult numberToText(const ModuleExecutionContext& context)
{
    qint64 number = 0;
    if (!integerValue(context.inputs.value(QStringLiteral("value")), number)) {
        return valueError(context, QStringLiteral("IntegerValueRequired"),
                          QStringLiteral("Number to Text requires a signed integer"));
    }
    const int base = inputBase(context.inputs.value(QStringLiteral("base")), 16);
    if (!supportedBase(base)) {
        return valueError(context, QStringLiteral("NumberBaseUnsupported"),
                          QStringLiteral("Number base must be 2, 8, 10, or 16"));
    }
    const int width = context.inputs.value(QStringLiteral("width"), 0).toInt();
    if (width < 0 || width > 64) {
        return valueError(context, QStringLiteral("NumberWidthInvalid"),
                          QStringLiteral("Number text width must be between 0 and 64"));
    }
    const bool prefix = context.inputs.value(QStringLiteral("prefix"), false).toBool();
    const bool uppercase = context.inputs.value(QStringLiteral("uppercase"), true).toBool();
    const auto text = formatInteger(number, base, width, prefix, uppercase);

    ModuleResult result;
    result.outputs.insert(QStringLiteral("number"), number);
    result.outputs.insert(QStringLiteral("text"), text);
    result.outputs.insert(QStringLiteral("base"), base);
    publishLog(context, QStringLiteral("NUMBER_TO_TEXT number=%1 base=%2 width=%3 text=%4")
                            .arg(number)
                            .arg(base)
                            .arg(width)
                            .arg(text));
    return result;
}

} // namespace

ModuleId ValueToolsModule::moduleId() const
{
    return QString::fromLatin1(BuiltInValueToolsModuleId);
}

ModuleResult ValueToolsModule::execute(const ModuleFunction& functionName,
                                       const ModuleExecutionContext& context)
{
    const auto function = normalized(functionName);
    if (function == QStringLiteral("statistics")) return statistics(context);
    if (function == QStringLiteral("calculate")) return calculate(context);
    if (function == QStringLiteral("texttonumber")) return textToNumber(context);
    if (function == QStringLiteral("numbertotext")) return numberToText(context);
    return valueError(context,
                      QStringLiteral("ValueToolFunctionNotSupported"),
                      QStringLiteral("Unsupported value tool function: %1")
                          .arg(functionName));
}

} // namespace PicoATE::Core
