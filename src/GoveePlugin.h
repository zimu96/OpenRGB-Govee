#pragma once
#include <QObject>
#include <QPointer>
#include "OpenRGBPluginInterface.h"
#include "GoveeBle.h"

class GoveeBle; class GoveeWidget;

struct GoveeBridge { GoveeBle* ble = nullptr; RGBControllerInterface* ctl = nullptr; };

class GoveePlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "metadata.json")
    Q_INTERFACES(OpenRGBPluginInterface)
public:
    GoveePlugin() = default;
    ~GoveePlugin() override = default;

    OpenRGBPluginInfo   GetPluginInfo() override;
    unsigned int        GetPluginAPIVersion() override;
    void                Load(OpenRGBPluginAPIInterface* plugin_api_ptr) override;
    QWidget*            GetWidget() override;
    QMenu*              GetTrayMenu() override { return nullptr; }
    void                Unload() override;
    void                OnProfileAboutToLoad() override {}
    void                OnProfileLoad(nlohmann::json) override {}
    nlohmann::json      OnProfileSave() override { return nlohmann::json(); }
    unsigned char*      OnSDKCommand(unsigned int, unsigned char*, unsigned int*) override { return nullptr; }
    void                ProfileManagerUpdated(unsigned int) override {}
    void                ResourceManagerUpdated(unsigned int) override {}
    void                SettingsManagerUpdated(unsigned int) override {}

private:
    void saveSettings();
    OpenRGBPluginAPIInterface* api = nullptr;
    GoveeBle*                  ble = nullptr;
    GoveeBridge*               bridge = nullptr;
    RGBControllerInterface*    controller = nullptr;
    QPointer<GoveeWidget>      widget;
};
