#include "RegisterConfigImporter.h"
#include "RegisterConfigImporterTests.h"

#include "PicoATE/Core/SequenceCompiler.h"

#include <QtCore/private/qzipwriter_p.h>

#include <QDir>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace PicoATE::RegisterImport;

namespace {

QByteArray workbookXml(const QString& sheetName)
{
    return QStringLiteral(
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"
 xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
 <sheets><sheet name="%1" sheetId="1" r:id="rId1"/></sheets>
</workbook>)")
        .arg(sheetName)
        .toUtf8();
}

QString inlineCell(const QString& reference, const QString& value)
{
    auto escaped = value.toHtmlEscaped();
    return QStringLiteral("<c r=\"%1\" t=\"inlineStr\"><is><t>%2</t></is></c>")
        .arg(reference, escaped);
}

QByteArray worksheetXml(const QVector<QStringList>& rows)
{
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<sheetData>");
    for (int row = 0; row < rows.size(); ++row) {
        xml += QStringLiteral("<row r=\"%1\">").arg(row + 1);
        for (int column = 0; column < rows[row].size(); ++column) {
            const QChar letter(QLatin1Char('A').unicode() + column);
            xml += inlineCell(QStringLiteral("%1%2").arg(letter).arg(row + 1),
                              rows[row][column]);
        }
        xml += QStringLiteral("</row>");
    }
    xml += QStringLiteral("</sheetData></worksheet>");
    return xml.toUtf8();
}

void writeWorkbook(const QString& path,
                   const QString& sheetName,
                   const QVector<QStringList>& rows)
{
    QZipWriter archive(path);
    archive.addFile(QStringLiteral("[Content_Types].xml"), QByteArray(R"(<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
 <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
 <Default Extension="xml" ContentType="application/xml"/>
 <Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
 <Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
</Types>)"));
    archive.addFile(QStringLiteral("_rels/.rels"), QByteArray(R"(<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
 <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>)"));
    archive.addFile(QStringLiteral("xl/workbook.xml"), workbookXml(sheetName));
    archive.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"), QByteArray(R"(<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
 <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
</Relationships>)"));
    archive.addFile(QStringLiteral("xl/worksheets/sheet1.xml"),
                    worksheetXml(rows));
    archive.close();
    QCOMPARE(archive.status(), QZipWriter::NoError);
}

QVector<QStringList> standardRows(const QString& scale = QStringLiteral("10"))
{
    return {
        {QStringLiteral("起始地址"), QStringLiteral("参数名称"),
         QStringLiteral("数据类型"), QStringLiteral("寄存器数"),
         QStringLiteral("功能"), QStringLiteral("寄存器值"),
         QStringLiteral("偏移"), QStringLiteral("缩放比例")},
        {QStringLiteral("0xA001"), QStringLiteral("最高允许电流"),
         QStringLiteral("U32"), QStringLiteral("2"),
         QStringLiteral("R/W"), QStringLiteral("4000"),
         QStringLiteral("0"), scale},
        {QStringLiteral("0xA003"), QStringLiteral("状态字"),
         QStringLiteral("U16"), QStringLiteral("1"),
         QStringLiteral("R"), QStringLiteral("7"),
         QStringLiteral("0"), QStringLiteral("1")},
    };
}

} // namespace

void RegisterConfigImporterTests::importsWorkbookAndBuildsCompilableTestItem()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto registerDirectory = directory.filePath(QStringLiteral("Register"));
    QVERIFY(QDir().mkpath(registerDirectory));
    const auto workbook = QDir(registerDirectory).filePath(QStringLiteral("table.xlsx"));
    writeWorkbook(workbook, QStringLiteral("M2系统监控(0xA000-0xD7FF)"),
                  standardRows());

    RegisterImportOptions options;
    options.deviceId = QStringLiteral("MODBUS2");
    const auto result = importRegisterDirectory(registerDirectory, options);
    QVERIFY2(result.ok(), qPrintable(result.errors.isEmpty()
        ? QStringLiteral("Import failed") : result.errors.first().message));
    QCOMPARE(result.sheetCount, 1);
    QCOMPARE(result.parameterCount, 2);
    QCOMPARE(result.testItem.value(QStringLiteral("id")).toString(),
             QStringLiteral("register_config"));

    const auto sheet = result.testItem.value(QStringLiteral("steps"))
                           .toArray().first().toObject();
    QCOMPARE(sheet.value(QStringLiteral("id")).toString(),
             QStringLiteral("001"));
    const auto parameters = sheet.value(QStringLiteral("steps")).toArray();
    QCOMPARE(parameters.size(), 2);
    const auto writableSteps = parameters[0].toObject()
                                   .value(QStringLiteral("steps")).toArray();
    QCOMPARE(writableSteps.size(), 5);
    QCOMPARE(writableSteps[0].toObject().value(QStringLiteral("inputs"))
                 .toObject().value(QStringLiteral("values")).toString(),
             QStringLiteral("[0x0000,0x0190]"));
    QCOMPARE(writableSteps[0].toObject().value(QStringLiteral("inputs"))
                 .toObject().value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("MODBUS2"));
    QCOMPARE(writableSteps[3].toObject().value(QStringLiteral("inputs"))
                 .toObject().value(QStringLiteral("source")).toString(),
             QStringLiteral("${step:register_config.001.01.03.outputs.registers}"));
    QCOMPARE(writableSteps[4].toObject().value(QStringLiteral("inputs"))
                 .toObject().value(QStringLiteral("actual")).toString(),
             QStringLiteral("${step:register_config.001.01.04.outputs.value}"));

    QJsonObject sequence{
        {QStringLiteral("id"), QStringLiteral("register-import-test")},
        {QStringLiteral("name"), QStringLiteral("Register Import Test")},
        {QStringLiteral("version"), QStringLiteral("1.0")},
        {QStringLiteral("groups"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("setup")},
                        {QStringLiteral("kind"), QStringLiteral("setup")},
                        {QStringLiteral("steps"), QJsonArray{}}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("main")},
                        {QStringLiteral("kind"), QStringLiteral("main")},
                        {QStringLiteral("steps"), QJsonArray{result.testItem}}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("cleanup")},
                        {QStringLiteral("kind"), QStringLiteral("cleanup")},
                        {QStringLiteral("steps"), QJsonArray{}}},
        }},
    };
    PicoATE::Core::SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(sequence);
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
        ? QStringLiteral("Generated sequence did not compile")
        : compiled.errors.first().message));
}

void RegisterConfigImporterTests::usesStableThreeDigitIdsForKnownSheets()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QVector<QPair<QString, QString>> cases = {
        {QStringLiteral("M2系统监控(0xA000-0xD7FF)"),
         QStringLiteral("001")},
        {QStringLiteral("CCU终端共用(0x6000-0x9EFF)"),
         QStringLiteral("002")},
        {QStringLiteral("MCU主控板(0x0200-0x5EFF)"),
         QStringLiteral("003")},
    };

    for (int index = 0; index < cases.size(); ++index) {
        const auto workbook = directory.filePath(
            QStringLiteral("known-sheet-%1.xlsx").arg(index));
        writeWorkbook(workbook, cases[index].first, standardRows());

        const auto result = importRegisterWorkbook(workbook);
        QVERIFY2(result.ok(), qPrintable(result.errors.isEmpty()
            ? QStringLiteral("Import failed") : result.errors.first().message));
        const auto sheet = result.testItem.value(QStringLiteral("steps"))
                               .toArray().first().toObject();
        QCOMPARE(sheet.value(QStringLiteral("id")).toString(),
                 cases[index].second);
    }
}

void RegisterConfigImporterTests::rejectsDecimalRegisterAddress()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto workbook = directory.filePath(QStringLiteral("decimal-address.xlsx"));
    auto rows = standardRows();
    rows[1][0] = QStringLiteral("40961");
    writeWorkbook(workbook, QStringLiteral("M2系统监控"), rows);

    const auto result = importRegisterWorkbook(workbook);
    QVERIFY(!result.ok());
    QVERIFY(!result.errors.isEmpty());
    QVERIFY(result.errors.first().path.endsWith(QStringLiteral(".起始地址")));
    QVERIFY(result.errors.first().suggestion.contains(QStringLiteral("0x")));
    QVERIFY(result.testItem.isEmpty());
}

void RegisterConfigImporterTests::rejectsFractionalRawRegisterValue()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto workbook = directory.filePath(QStringLiteral("fraction.xlsx"));
    writeWorkbook(workbook, QStringLiteral("M2系统监控"),
                  standardRows(QStringLiteral("3")));

    const auto result = importRegisterWorkbook(workbook);
    QVERIFY(!result.ok());
    QVERIFY(!result.errors.isEmpty());
    QVERIFY(result.errors.first().message.contains(
        QStringLiteral("cannot be represented exactly")));
    QVERIFY(result.testItem.isEmpty());
}

void RegisterConfigImporterTests::rejectsAmbiguousRegisterDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile first(directory.filePath(QStringLiteral("first.xlsx")));
    QVERIFY(first.open(QIODevice::WriteOnly));
    first.close();
    QFile second(directory.filePath(QStringLiteral("second.xlsx")));
    QVERIFY(second.open(QIODevice::WriteOnly));
    second.close();

    const auto result = importRegisterDirectory(directory.path());
    QVERIFY(!result.ok());
    QCOMPARE(result.errors.size(), 1);
    QVERIFY(result.errors.first().message.contains(QStringLiteral("Multiple")));
}

QTEST_MAIN(RegisterConfigImporterTests)
