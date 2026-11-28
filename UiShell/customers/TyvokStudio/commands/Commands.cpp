#include "Commands.h"

#include "UiDialogs/AboutDialogs.h"
#include "UiDialogs/SampleDialogs.h"

namespace TS
{
    namespace
    {
        void runSample(const QJsonObject& args)
        {
            AA::samples::runSampleCommand(QString::fromLatin1(AA::samples::kSampleRunCmdId), args);
        }
    }  // namespace

    Commands::Commands()
    {
        add(QStringLiteral("project.new")).shortcut(QStringLiteral("Ctrl+N"));
        add(QStringLiteral("project.open")).shortcut(QStringLiteral("Ctrl+O"));

        add(QStringLiteral("machining.start"), [this](const QJsonObject&) {
            m_running = true;
            m_paused = false;
            })
            .shortcut(QStringLiteral("F5"))
            .enabledWhen([this]() { return !m_running || m_paused; });

        add(QStringLiteral("machining.pause"), [this](const QJsonObject&) { m_paused = !m_paused; })
            .shortcut(QStringLiteral("F6"))
            .enabledWhen([this]() { return m_running; })
            .checkedWhen([this]() { return m_paused; });

        add(QStringLiteral("tools.settings"));
        add(QString::fromLatin1(AA::samples::kSampleRunCmdId), runSample);
        add(QString::fromLatin1(AA::about::kAboutCmdId));
    }
}  // namespace TS