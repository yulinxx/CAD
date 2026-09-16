/**
 * @file TextLayoutUtf8.h
 * @brief UTF-8 码点解码（宿主排版的公共零件）
 *
 * 排版归应用层，于是「按码点遍历字符串」这件事在屏幕文字与世界文字两条
 * 路径上都要做。解码本身有若干易错细节（非法序列不能死循环、截断的多字节
 * 序列不能越界读），所以只保留这一份实现；两个构建器各抄一份的代价是
 * 其中一份迟早会漏改。
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace Render
{
    /**
     * @brief 解码一个 UTF-8 码点，返回消耗的字节数
     *
     * 非法序列按 1 字节吞掉并返回 U+FFFD：图纸里出现半截多字节序列时
     * 应当画个替换字符继续排版，而不是丢掉整行或死循环。
     *
     * @param text      待解码位置（非空）
     * @param remaining 从该位置起剩余的字节数，用于拒绝截断序列
     */
    inline size_t decodeUtf8(const char* text, size_t remaining, uint32_t& outCodepoint)
    {
        const auto* p = reinterpret_cast<const unsigned char*>(text);
        const unsigned char b0 = p[0];

        if (b0 < 0x80)
        {
            outCodepoint = b0;
            return 1;
        }

        size_t length = 0;
        uint32_t cp = 0;
        if ((b0 & 0xE0) == 0xC0)
        {
            length = 2;
            cp = b0 & 0x1Fu;
        }
        else if ((b0 & 0xF0) == 0xE0)
        {
            length = 3;
            cp = b0 & 0x0Fu;
        }
        else if ((b0 & 0xF8) == 0xF0)
        {
            length = 4;
            cp = b0 & 0x07u;
        }
        else
        {
            outCodepoint = 0xFFFDu;
            return 1;
        }

        if (remaining < length)
        {
            outCodepoint = 0xFFFDu;
            return 1;
        }
        for (size_t i = 1; i < length; ++i)
        {
            if ((p[i] & 0xC0) != 0x80)
            {
                outCodepoint = 0xFFFDu;
                return 1;
            }
            cp = (cp << 6) | (p[i] & 0x3Fu);
        }
        outCodepoint = cp;
        return length;
    }
}  // namespace Render
