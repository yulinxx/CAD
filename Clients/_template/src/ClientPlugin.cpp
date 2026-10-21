#include "UI/ClientConfig/IClientUiPlugin.h"

#include <QLabel>
#include <QWidget>

namespace
{
class ClientUiPlugin final : public IClientUiPlugin
{
public:
    QString id() const override
    {
        return QStringLiteral("__CLIENT_ID__.ui");
    }

    QString clientId() const override
    {
        return QStringLiteral("__CLIENT_ID__");
    }

    void registerPanels(UiPanelRegisterSink& sink) override
    {
        sink.registerPanel(QStringLiteral("__CLIENT_ID__.welcome"), [](QWidget* parent) {
            auto* label = new QLabel(QObject::tr("__PRODUCT_NAME__ customer panel"), parent);
            label->setAlignment(Qt::AlignCenter);
            return static_cast<QWidget*>(label);
        });
    }
};
}  // namespace

SANYI_CLIENT_PLUGIN_EXPORT IClientUiPlugin* createClientUiPlugin()
{
    return new ClientUiPlugin();
}

SANYI_CLIENT_PLUGIN_EXPORT void destroyClientUiPlugin(IClientUiPlugin* plugin)
{
    delete plugin;
}
