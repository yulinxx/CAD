#include "Commands.h"

#include "UiDialogs/AboutDialogs.h"
#include "UiDialogs/SampleDialogs.h"

namespace GL
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
        add(QStringLiteral("design.new")).shortcut(QStringLiteral("Ctrl+N"));
        add(QStringLiteral("design.sketch"));
        add(QStringLiteral("manufacture.preview"));
        add(QStringLiteral("manufacture.run"));
        add(QStringLiteral("analysis.measure"));
        add(QStringLiteral("window.reset"));
        add(QString::fromLatin1(AA::samples::kSampleRunCmdId), runSample).shortcut(QStringLiteral("F5"));
        add(QString::fromLatin1(AA::about::kAboutCmdId));
    }
}  // namespace GL