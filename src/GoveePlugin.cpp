#include "GoveePlugin.h"
#include "GoveeWidget.h"
#include <QStandardPaths>
#include <QDir>
#include <algorithm>

static const char* KEY = "GoveePlugin";

// Chiamata da OpenRGB ogni volta che colori o modo cambiano.
static void GoveeApply(void* obj)
{
    GoveeBridge* b = static_cast<GoveeBridge*>(obj);
    if(!b || !b->ble || !b->ctl) return;
    int m = b->ctl->GetActiveMode();
    if(m < 0 || m >= GM_COUNT) return;
    GoveeParams p;
    p.mode = m;
    unsigned int fl = b->ctl->GetModeFlags(m);
    if(m == GM_DIRECT) {
        RGBColor* c = b->ctl->GetColorsPointer();
        if(c) {
            p.color = c[0];
            if(b->seg) { p.useSeg = true; for(int i = 0; i < GOVEE_SEGMENTS; i++) p.seg[i] = c[i]; }
        }
    }
    else if(fl & MODE_FLAG_HAS_MODE_SPECIFIC_COLOR) {
        p.color = b->ctl->GetModeColor(m, 0);
        if(b->ctl->GetModeColorsMax(m) >= 2) p.color2 = b->ctl->GetModeColor(m, 1);
    }
    if(fl & MODE_FLAG_HAS_SPEED) p.speed = (int)b->ctl->GetModeSpeed(m);
    if(fl & MODE_FLAG_HAS_BRIGHTNESS) p.brightness = (int)b->ctl->GetModeBrightness(m);
    b->ble->applyParams(p);
}

static void GoveeApplyZone(void* obj, int) { GoveeApply(obj); }
static void GoveeNoopInt(void*, int) {}
static void GoveeNoop(void*) {}

OpenRGBPluginInfo GoveePlugin::GetPluginInfo()
{
    OpenRGBPluginInfo i;
    i.Name        = "Govee";
    i.Description = "Controlla via Bluetooth LE le strisce LED Govee (H613A) come dispositivo OpenRGB.";
    i.Version     = "1.1.0";
    i.Commit      = "";
    i.URL         = "";
    i.Location    = OPENRGB_PLUGIN_LOCATION_TOP;
    i.Label       = "Govee";
    i.TabIconString = "";
    i.ProtocolVersion = 5;
    return i;
}

unsigned int GoveePlugin::GetPluginAPIVersion() { return OPENRGB_PLUGIN_API_VERSION; }

RGBControllerInterface* GoveePlugin::registerDevice(RGBController_Setup& setup, GoveeBridge* bridge, int nLeds)
{
    zone z;
    z.name = "Striscia"; z.type = ZONE_TYPE_LINEAR;
    z.leds_min = nLeds; z.leds_max = nLeds; z.leds_count = nLeds;
    setup.zones.push_back(z);
    for(int i = 0; i < nLeds; i++) {
        led l; l.name = nLeds > 1 ? "Segmento " + std::to_string(i + 1) : std::string("Striscia"); l.value = i;
        setup.leds.push_back(l);
    }

    setup.object_ptr = bridge;
    setup.DeviceConfigureZone = GoveeNoopInt;
    setup.DeviceSaveMode = GoveeNoop;
    setup.DeviceUpdateDeviceSpecificConfiguration = nullptr;
    setup.DeviceUpdateDeviceSpecificZoneConfiguration = nullptr;

    RGBControllerInterface* c = api->CreateVirtualRGBController(&setup);
    if(!c) return nullptr;
    bridge->ctl = c;
    api->RegisterVirtualRGBController(c);
    return c;
}

void GoveePlugin::loadBleDevice()
{
    RGBController_Setup setup;
    setup.name        = "Govee H613A";
    setup.vendor      = "Govee";
    setup.description = "Striscia LED Govee H613A (Bluetooth LE)";
    setup.location    = "BLE";
    setup.serial      = ble->address().toStdString();
    setup.version     = "";
    setup.configuration = "";
    setup.type        = DEVICE_TYPE_LEDSTRIP;
    setup.flags       = 0;
    setup.active_mode = GM_STATIC;   // con luminosita: le dissolvenze dello schermo funzionano subito

    const unsigned C1 = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR, SPD = MODE_FLAG_HAS_SPEED, BR = MODE_FLAG_HAS_BRIGHTNESS;
    struct MD { const char* name; int val; unsigned flags; int colorMode; int ncolors; RGBColor def1, def2; } defs[] = {
        {"Direct",         GM_DIRECT,    MODE_FLAG_HAS_PER_LED_COLOR | BR, MODE_COLORS_PER_LED, 0, 0, 0},
        {"Static",         GM_STATIC,    C1 | BR,       MODE_COLORS_MODE_SPECIFIC, 1, ToRGBColor(0,0,255), 0},
        {"Breathing",      GM_BREATHING, C1 | SPD | BR, MODE_COLORS_MODE_SPECIFIC, 1, ToRGBColor(0,0,255), 0},
        {"Flashing",       GM_FLASHING,  C1 | SPD | BR, MODE_COLORS_MODE_SPECIFIC, 1, ToRGBColor(255,0,0), 0},
        {"Strobe",         GM_STROBE,    C1 | SPD | BR, MODE_COLORS_MODE_SPECIFIC, 1, ToRGBColor(255,255,255), 0},
        {"Heartbeat",      GM_HEARTBEAT, C1 | SPD | BR, MODE_COLORS_MODE_SPECIFIC, 1, ToRGBColor(255,0,30), 0},
        {"Spectrum Cycle", GM_SPECTRUM,  SPD | BR,      MODE_COLORS_NONE, 0, 0, 0},
        {"Color Fade",     GM_FADE2,     C1 | SPD | BR, MODE_COLORS_MODE_SPECIFIC, 2, ToRGBColor(0,0,255), ToRGBColor(255,0,255)},
        {"Random",         GM_RANDOM,    SPD | BR,      MODE_COLORS_NONE, 0, 0, 0},
        {"Candle",         GM_CANDLE,    C1 | SPD | BR, MODE_COLORS_MODE_SPECIFIC, 1, ToRGBColor(255,110,20), 0},
        {"Fire",           GM_FIRE,      SPD | BR,      MODE_COLORS_NONE, 0, 0, 0},
        {"Police",         GM_POLICE,    SPD | BR,      MODE_COLORS_NONE, 0, 0, 0},
        {"Aurora",         GM_AURORA,    SPD | BR,      MODE_COLORS_NONE, 0, 0, 0},
        {"Lightning",      GM_LIGHTNING, C1 | SPD | BR, MODE_COLORS_MODE_SPECIFIC, 1, ToRGBColor(200,220,255), 0},
        {"Off",            GM_OFF,       0,             MODE_COLORS_NONE, 0, 0, 0},
        {"Scena: Sunrise",    GM_SC_SUNRISE,   0, MODE_COLORS_NONE, 0, 0, 0},
        {"Scena: Sunset",     GM_SC_SUNSET,    0, MODE_COLORS_NONE, 0, 0, 0},
        {"Scena: Movie",      GM_SC_MOVIE,     0, MODE_COLORS_NONE, 0, 0, 0},
        {"Scena: Dating",     GM_SC_DATING,    0, MODE_COLORS_NONE, 0, 0, 0},
        {"Scena: Romantic",   GM_SC_ROMANTIC,  0, MODE_COLORS_NONE, 0, 0, 0},
        {"Scena: Blinking",   GM_SC_BLINKING,  0, MODE_COLORS_NONE, 0, 0, 0},
        {"Scena: Candlelight",GM_SC_CANDLE,    0, MODE_COLORS_NONE, 0, 0, 0},
        {"Scena: Snowflake",  GM_SC_SNOWFLAKE, 0, MODE_COLORS_NONE, 0, 0, 0},
    };
    for(auto& d : defs) {
        mode m;
        m.name = d.name; m.value = d.val; m.flags = d.flags; m.color_mode = d.colorMode;
        if(d.flags & BR)  { m.brightness_min = 0; m.brightness_max = 100; m.brightness = 100; }
        if(d.flags & SPD) { m.speed_min = 1; m.speed_max = 10; m.speed = 5; }
        if(d.ncolors > 0) {
            m.colors_min = d.ncolors; m.colors_max = d.ncolors; m.colors.resize(d.ncolors);
            m.colors[0] = d.def1; if(d.ncolors > 1) m.colors[1] = d.def2;
        }
        setup.modes.push_back(m);
    }

    bleBridge = new GoveeBridge;
    bleBridge->ble = ble;
    bleBridge->seg = segEnabled;
    setup.DeviceUpdateLEDs = GoveeApply;
    setup.DeviceUpdateZoneLEDs = GoveeApplyZone;
    setup.DeviceUpdateSingleLED = GoveeApplyZone;
    setup.DeviceUpdateMode = GoveeApply;
    setup.DeviceUpdateZoneMode = GoveeApplyZone;
    bleCtl = registerDevice(setup, bleBridge, segEnabled ? GOVEE_SEGMENTS : 1);
    if(!bleCtl) { delete bleBridge; bleBridge = nullptr; return; }
    ble->start();
}

void GoveePlugin::Load(OpenRGBPluginAPIInterface* a)
{
    api = a;
    ble = new GoveeBle(this);
    nlohmann::json j = api->GetSettings(KEY);
    if(j.is_object()) {
        ble->setAddress(QString::fromStdString(j.value("address", std::string())));
        ble->setKeepConnected(j.value("stay_connected", true));
        ble->setIdleSeconds(j.value("idle_seconds", 8));
        ble->setOrder(QString::fromStdString(j.value("order", std::string("RGB"))));
        segEnabled = j.value("segments_enabled", false);
    }
    // La striscia accetta un solo collegamento Bluetooth: se un'altra istanza di OpenRGB ha gia' il plugin attivo,
    // questa non comanda le luci (altrimenti le due istanze si pestano i piedi e i modi restano fissi).
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if(dir.isEmpty()) dir = QDir::tempPath();
    instanceLock = new QLockFile(QDir(dir).filePath("openrgb-govee-plugin.lock"));
    instanceLock->setStaleLockTime(0);
    primary = instanceLock->tryLock(0);
    if(primary) {
        loadBleDevice();
    }

    connect(ble, &GoveeBle::statusChanged, this, [this](QString s){ if(widget) widget->setStatus(s); });
    connect(ble, &GoveeBle::addressLearned, this, [this](QString){ saveSettings(); });
}

void GoveePlugin::saveSettings()
{
    nlohmann::json j;
    j["address"] = ble->address().toStdString();
    j["stay_connected"] = ble->keepConnected();
    j["idle_seconds"] = ble->idleSeconds();
    j["order"] = ble->order().toStdString();
    j["segments_enabled"] = segEnabled;
    api->SetSettings(KEY, j);
    api->SaveSettings();
}

QWidget* GoveePlugin::GetWidget()
{
    widget = new GoveeWidget(ble, segEnabled);
    if(!primary) widget->setNotice("Un'altra istanza di OpenRGB sta gia' comandando le luci Govee. Chiudi le altre istanze (dall'icona nella barra) e riavvia OpenRGB: in questa istanza il plugin Govee e' fermo.");
    widget->setStatus(ble->status());
    connect(widget, &GoveeWidget::settingsChanged, this, [this](QString addr, bool keep, int idle, QString order, bool reconnect){
        ble->setAddress(addr);
        ble->setIdleSeconds(idle);
        ble->setKeepConnected(keep);
        ble->setOrder(order);
        saveSettings();
        if(reconnect) ble->reconnectNow();
    });
    connect(widget, &GoveeWidget::segmentsEnabledChanged, this, [this](bool on){ segEnabled = on; saveSettings(); });
    return widget;
}

void GoveePlugin::Unload()
{
    if(ble) ble->shutdown();
    if(api && bleCtl) {
        api->UnregisterVirtualRGBController(bleCtl);
        api->DeleteVirtualRGBController(bleCtl);
        bleCtl = nullptr;
    }
    delete bleBridge; bleBridge = nullptr;
    if(instanceLock) { instanceLock->unlock(); delete instanceLock; instanceLock = nullptr; }
    delete ble; ble = nullptr;
    widget = nullptr;
}
