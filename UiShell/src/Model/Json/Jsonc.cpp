#include "JsoncInternal.h"

#include <algorithm>

namespace AA
{
    namespace
    {
        char blank(char c)
        {
            // 保留换行，其余注释内容一律替换为空格，保证长度与行号不变。
            return (c == '\n' || c == '\r') ? c : ' ';
        }
    }  // namespace

    JsoncStripResult stripJsoncComments(const QByteArray& input)
    {
        JsoncStripResult result;
        // 复制一份再原地挖空，调用方传入的缓冲区保持不变。
        result.json = input;

        // BOM 不是 JSON 内容，先剥离；BOM 不含换行，不影响后续行号计算。
        if (result.json.startsWith("\xEF\xBB\xBF"))
            result.json.remove(0, 3);

        // 用一个最小状态机区分四种上下文，只有 Normal 状态才把 // 和 /* 当注释起点。
        enum class State
        {
            Normal,
            String,
            LineComment,
            BlockComment
        };
        State state = State::Normal;

        qsizetype blockStart = -1;
        const qsizetype sz = result.json.size();

        qsizetype i = 0;
        while (i < sz)
        {
            const char c = result.json.at(i);
            const char next = (i + 1 < sz) ? result.json.at(i + 1) : '\0';

            switch (state)
            {
            case State::Normal:
                if (c == '"')
                {
                    state = State::String;
                    ++i;
                }
                else if (c == '/' && next == '/')
                {
                    state = State::LineComment;
                    result.json.data()[i] = ' ';
                    result.json.data()[i + 1] = ' ';
                    i += 2;
                }
                else if (c == '/' && next == '*')
                {
                    state = State::BlockComment;
                    blockStart = i;
                    result.json.data()[i] = ' ';
                    result.json.data()[i + 1] = ' ';
                    i += 2;
                }
                else
                {
                    ++i;
                }
                break;

            case State::String:
                if (c == '\\')
                {
                    // 跳过被转义的字符（含 \" 与 \\），避免把转义引号当成字符串结束。
                    i += (i + 1 < sz) ? 2 : 1;
                }
                else if (c == '"')
                {
                    state = State::Normal;
                    ++i;
                }
                else
                {
                    ++i;
                }
                break;

            case State::LineComment:
                if (c == '\n')
                {
                    state = State::Normal;
                }
                else
                {
                    result.json.data()[i] = blank(c);
                }
                ++i;
                break;

            case State::BlockComment:
                if (c == '*' && next == '/')
                {
                    result.json.data()[i] = ' ';
                    result.json.data()[i + 1] = ' ';
                    state = State::Normal;
                    i += 2;
                }
                else
                {
                    result.json.data()[i] = blank(c);
                    ++i;
                }
                break;
            }
        }

        if (state == State::BlockComment)
        {
            // 走到结尾仍在块注释里说明没有闭合；从文件头数换行才能给出起始位置的真实行号。
            int line = 1;
            for (int k = 0; k < blockStart && k < sz; ++k)
            {
                if (result.json.at(k) == '\n')
                    ++line;
            }
            result.ok = false;
            result.errorLine = line;
            result.error = QStringLiteral("unterminated block comment starting at line %1").arg(line);
        }

        return result;
    }

    void jsoncLineColumn(const QByteArray& utf8, qsizetype offset, int* line, int* column)
    {
        int l = 1;
        int c = 1;
        // 偏移可能为负或越界（解析器偶发返回边界值），先夹取到合法区间再遍历。
        const qsizetype limit = std::min<qsizetype>(std::max<qsizetype>(offset, qsizetype(0)), utf8.size());
        for (qsizetype i = 0; i < limit; ++i)
        {
            if (utf8.at(i) == '\n')
            {
                ++l;
                c = 1;
            }
            else
            {
                ++c;
            }
        }
        if (line)
            *line = l;
        if (column)
            *column = c;
    }
}  // namespace AA