#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <utility>

#include "UiShell/Contract/HostContext.h"

namespace AA
{
    // 语言切换刷新
    //
    // 使用：using RetranslatableLabel = Retranslatable<QLabel>;
    //       auto* label = new RetranslatableLabel(parent);
    //       label->onRetranslate([label]() { label->setText(tr(...)); });
    template<typename Base>
    class Retranslatable : public Base, public IRetranslatable
    {
    public:
        using Base::Base;

        void retranslate() override
        {
            for (const auto& refresher : m_refreshers)
                refresher();
        }

        // 注册一个刷新回调,retranslate() 时按注册顺序执行
        void onRetranslate(std::function<void()> refresher)
        {
            m_refreshers.append(std::move(refresher));
        }

    private:
        QList<std::function<void()>> m_refreshers;
    };

    // 标记面板
    template<typename Widget>
    void markPanel(Widget* widget, const QString& role, const QString& series)
    {
        widget->setProperty("msuiRole", role);
        widget->setProperty("msuiSeries", series);
    }
}  // namespace AA
