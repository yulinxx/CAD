#include <QtTest/QtTest>

#include <QString>
#include <QJsonDocument>
#include <QJsonParseError>

#include "Model/Json/JsoncInternal.h"
#include "Model/Profile/ProfileParser.h"
#include "Model/Json/Jsonc.h"
#include "Model/Profile/ProfileLoadResult.h"

using namespace RD;

namespace
{

    QByteArray diagnosticText(const ProfileLoadResult& result)
    {
        if (result.diagnostics.isEmpty())
            return QByteArrayLiteral("no diagnostics");
        return result.diagnostics.first().strMsg.toUtf8();
    }
} // namespace

class JsoncTest : public QObject
{
    Q_OBJECT

private slots:
    void stripsLineAndBlockComments();
    void keepsCommentLikeSequencesInsideStrings();
    void handlesEscapedBackslashBeforeQuote();
    void multilineBlockCommentKeepsLineNumbers();
    void reportsUnterminatedBlockCommentWithStartLine();
    void profileLoaderAcceptsCommentsAndChinese();
    void profileLoaderAcceptsUtf8Bom();
    void profileLoaderReportsLineForParseError();
};

void JsoncTest::stripsLineAndBlockComments()
{
    const QByteArray input = R"json({
      // 行注释
      "a": 1, /* 行内块注释 */
      "b": 2
    })json";

    const JsoncStripResult result = stripJsoncComments(input);
    QVERIFY(result.ok);
    // 注释被替换成空格而不是删除：长度不变，报错偏移才能继续对应原文件。
    QCOMPARE(result.json.size(), input.size());
    QVERIFY(!result.json.contains("行注释"));
    QVERIFY(!result.json.contains("行内块注释"));
    QVERIFY(result.json.contains("\"a\": 1"));
    QVERIFY(result.json.contains("\"b\": 2"));

    // 剥离后仍是合法 JSON。
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(result.json, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
}

// 字符串里的 // 与 /* */ 是内容而不是注释，剥离后必须逐字节保持不变。
void JsoncTest::keepsCommentLikeSequencesInsideStrings()
{
    const QByteArray input = R"json({
      "url": "https://example.com/a/b",
      "pattern": "a/*b*/c",
      "escaped": "quote \" // not a comment",
      "path": "C:\\dir\\file"
    })json";

    const JsoncStripResult result = stripJsoncComments(input);
    QVERIFY(result.ok);
    QCOMPARE(result.json, input);
}

void JsoncTest::handlesEscapedBackslashBeforeQuote()
{
    // "\\" 之后紧跟的引号才是字符串结束，不能被 // 误判。
    const QByteArray input = R"json({ "v": "\\", "x": "// keep" })json";
    const JsoncStripResult result = stripJsoncComments(input);
    QVERIFY(result.ok);
    QCOMPARE(result.json, input);
}

// 块注释占用的行要原样保留，报错行号才能指向用户看到的行。
void JsoncTest::multilineBlockCommentKeepsLineNumbers()
{
    const QByteArray input = R"json({
      "a": 1,
      /*
         "b": 2,
         "c": 3
      */
      "d": 4
    })json";

    const JsoncStripResult result = stripJsoncComments(input);
    QVERIFY(result.ok);

    int line = 0;
    int column = 0;
    const qsizetype offset = result.json.indexOf("\"d\"");
    QVERIFY(offset >= 0);
    jsoncLineColumn(result.json, offset, &line, &column);
    QCOMPARE(line, 7); // "d" 仍在原文件的第 7 行
}

// 未闭合的块注释要报“开始行”而不是文件末尾，方便用户直接定位。
void JsoncTest::reportsUnterminatedBlockCommentWithStartLine()
{
    const QByteArray input = R"json({
      "a": 1,
      /* 这里没有闭合
      "b": 2
    })json";

    const JsoncStripResult result = stripJsoncComments(input);
    QVERIFY(!result.ok);
    QCOMPARE(result.errorLine, 3);
    QVERIFY(result.error.toStdString().find("unterminated") != std::string::npos);
}

// 客户 profile 允许写注释和 UTF-8 中文，加载器需要端到端接受。
void JsoncTest::profileLoaderAcceptsCommentsAndChinese()
{
    const QByteArray profile = R"json(
// 客户名（中文 + 注释）
{
  "version": 2,
  "profileId": "注释测试",
  "titleBar": {
    "title": "中文标题", // 行尾注释
    "icon": "◆"
  },
  "regions": [ { "id": "titlebar", "type": "TitleBar" } ]
}
)json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(profile);
    QVERIFY2(result.isOk(), diagnosticText(result).constData());
    QVERIFY(result.profileModel.has_value());
    QCOMPARE(result.profileModel->strProfileId.toStdString(), std::string("注释测试"));
    QCOMPARE(result.profileModel->titleBar.strTitle.toStdString(), std::string("中文标题"));
}

void JsoncTest::profileLoaderAcceptsUtf8Bom()
{
    QByteArray withBom = "\xEF\xBB\xBF";
    withBom += R"json({
      "version": 2,
      "profileId": "bom",
      "titleBar": { "title": "带 BOM 的中文" },
      "regions": [ { "id": "titlebar", "type": "TitleBar" } ]
    })json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(withBom);
    // BOM 不是 JSON 内容，必须在解析前剥离，否则首个字符就会导致解析失败。
    QVERIFY(result.isOk());
    QVERIFY(result.profileModel.has_value());
    QCOMPARE(result.profileModel->titleBar.strTitle.toStdString(), std::string("带 BOM 的中文"));
}

void JsoncTest::profileLoaderReportsLineForParseError()
{
    const QByteArray profile = R"json({
  "version": 2,
  // 第 3 行是注释
  "profileId": "broken",
  "regions": [ { "id": "x", "type": } ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(profile);
    QVERIFY(!result.isOk());
    QVERIFY(!result.diagnostics.isEmpty());
    // 注释行仍占位，所以错误落在原文件第 5 行，而不是剥离后的行号。
    QVERIFY2(result.diagnostics.first().strMsg.contains(QStringLiteral("line 5")),
        qPrintable(result.diagnostics.first().strMsg));
}

QTEST_GUILESS_MAIN(JsoncTest)

#include "tstJsonc.moc"