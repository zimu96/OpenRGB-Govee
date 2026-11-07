#include "GoveeBle.h"
#include <QMetaObject>
#include <QColor>
#include <QMutexLocker>
#include <cmath>
#include <algorithm>
#include <QFile>
#include <QDateTime>
#include <QStandardPaths>

static const char* CHR_UUID = "00010203-0405-0607-0809-0a0b0c0d2b11";
static const char* NOTIFY_UUID = "00010203-0405-0607-0809-0a0b0c0d2b10";
static const char* BLUEZ = "org.bluez";

GoveeBle::GoveeBle(QObject* parent) : QObject(parent)
{
    retry = new QTimer(this);        retry->setSingleShot(true);
    flushTimer = new QTimer(this);   flushTimer->setSingleShot(true);
    settleTimer = new QTimer(this);  settleTimer->setSingleShot(true);
    idleTimer = new QTimer(this);    idleTimer->setSingleShot(true);
    effectTimer = new QTimer(this);  effectTimer->setInterval(45);
    pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, [this]{
        if(state == Looking) lookForDevice();
        else if(state == Resolving) checkResolved();
        else if(state == Ready) {            // controllo periodico che il collegamento sia vivo
            QDBusMessage m = call(devPath, "org.freedesktop.DBus.Properties", "Get");
            m << "org.bluez.Device1" << "Connected";
            quint64 sess = session;
            dbusCall(m, 2000, [this, sess](const QDBusMessage& r){
                if(sess != session || r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) return;
                if(!r.arguments().first().value<QDBusVariant>().variant().toBool()) onDisconnected();
            });
        }
    });
    keepTimer = new QTimer(this); keepTimer->setInterval(2000);
    // la striscia chiude il collegamento dopo ~11 s di silenzio: pacchetto "tienimi sveglio" come l'app Govee
    connect(keepTimer, &QTimer::timeout, this, [this]{
        if(ready && sinceWrite.elapsed() > 1500) sendPacket(packet({0xAA, 0x01}));
    });
    watchdog = new QTimer(this); watchdog->setSingleShot(true);
    connect(watchdog, &QTimer::timeout, this, [this]{ if(!ready && wantConn) fail("tempo scaduto"); });
    qDBusRegisterMetaType<IfaceMap>();
    qDBusRegisterMetaType<Managed>();
    connect(retry, &QTimer::timeout, this, [this]{ if(wantConn) ensureConnection(); });
    connect(flushTimer, &QTimer::timeout, this, &GoveeBle::flush);
    connect(effectTimer, &QTimer::timeout, this, &GoveeBle::tick);
    connect(idleTimer, &QTimer::timeout, this, &GoveeBle::onIdle);
    // secondo invio dello stesso colore: se un pacchetto si perde, il colore finale arriva lo stesso
    connect(settleTimer, &QTimer::timeout, this, [this]{
        if(ready && lastQueued >= 0 && !animated()) {
            settling = true;
            lastColorSent = -1; pendingColor = lastQueued; flush();
            settling = false;
            if(++settleCount < 3) settleTimer->start(settleCount == 1 ? 300 : 700);
        }
    });
    sinceWrite.start();
    sinceMode.start();
}

GoveeBle::~GoveeBle() { shutdown(); }

void GoveeBle::setAddress(const QString& a) { wantedAddr = a.trimmed().toUpper(); }
void GoveeBle::setKeepConnected(bool k)
{
    keepAlways = k;
    if(k) { idleTimer->stop(); wantConn = true; ensureConnection(); }
    else if(ready && !animated()) idleTimer->start(idleSec * 1000);
}
void GoveeBle::setOrder(const QString& o)
{
    static const QStringList ok = {"RGB","RBG","GRB","GBR","BRG","BGR"};
    chanOrder = ok.contains(o) ? o : QString("RGB");
    lastColorSent = -1;
}

void GoveeBle::logLine(const QString& l)
{
    // piccolo file di diagnostica: ~/.config/OpenRGB/govee-plugin.log (si svuota oltre 200 KB)
    static QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/OpenRGB/govee-plugin.log";
    QFile f(path);
    if(f.size() > 200000) f.remove();
    if(f.open(QIODevice::Append | QIODevice::Text))
        f.write((QDateTime::currentDateTime().toString("hh:mm:ss.zzz ") + l + "\n").toUtf8());
}

void GoveeBle::setStatus(const QString& s) { lastStatus = s; logLine("stato: " + s); emit statusChanged(s); }

bool GoveeBle::animated() const
{
    int m;
    { QMutexLocker l(const_cast<QMutex*>(&pm)); m = params.mode; }
    return m >= GM_BREATHING && m < GM_OFF && m != GM_DIRECT && m != GM_STATIC;
}

void GoveeBle::start()
{
    if(keepAlways) { wantConn = true; ensureConnection(); }
}

void GoveeBle::shutdown()
{
    wantConn = false;
    effectTimer->stop(); idleTimer->stop(); settleTimer->stop(); flushTimer->stop();
    teardown(true);
    setStatus("fermo");
}

void GoveeBle::reconnectNow()
{
    teardown(true);
    retryStep = 0;
    wantConn = true;
    ensureConnection();
}

// ---- parametri da OpenRGB ------------------------------------------------
void GoveeBle::applyParams(const GoveeParams& p)
{
    { QMutexLocker l(&pm); params = p; }
    QMetaObject::invokeMethod(this, [this]{ onParams(); }, Qt::QueuedConnection);
}

void GoveeBle::testColor(unsigned char r, unsigned char g, unsigned char b)
{
    GoveeParams p;
    p.mode = GM_STATIC; p.color = (unsigned(b) << 16) | (unsigned(g) << 8) | r;
    p.brightness = 100;
    applyParams(p);
}

void GoveeBle::onParams()
{
    if(qEnvironmentVariableIsSet("GOVEE_DEBUG")) qInfo("onParams ready=%d state=%d", ready, int(state));
    GoveeParams p;
    { QMutexLocker l(&pm); p = params; }
    if(p.mode != lastMode) { lastMode = p.mode; sinceMode.restart(); }
    wantConn = true;
    if(animated()) { idleTimer->stop(); if(!effectTimer->isActive()) effectTimer->start(); }
    else {
        effectTimer->stop();
        if(!keepAlways) idleTimer->start(idleSec * 1000);
    }
    if(p.mode == GM_OFF) { pendingPower = 0; lastQueued = -1; }
    else pendingPower = 1;
    if(p.mode != GM_OFF) tick();
    if(ready) flush(); else ensureConnection();
}

double GoveeBle::noise(double x)
{
    // rumore dolce e deterministico in 0..1
    return 0.5 + 0.25 * std::sin(x * 1.7) + 0.15 * std::sin(x * 3.1 + 1.3) + 0.10 * std::sin(x * 7.3 + 2.1);
}

void GoveeBle::tick()
{
    GoveeParams p;
    { QMutexLocker l(&pm); p = params; }
    if(p.mode == GM_OFF) return;
    int r = p.color & 0xFF, g = (p.color >> 8) & 0xFF, b = (p.color >> 16) & 0xFF;
    double t = sinceMode.elapsed() / 1000.0;
    double sp = std::clamp(p.speed, 1, 10);
    double f = 1.0;
    auto hsv = [&](double h, double sat, double v){ QColor c = QColor::fromHsvF(std::fmod(std::fmod(h, 1.0) + 1.0, 1.0), sat, v); r = c.red(); g = c.green(); b = c.blue(); };
    switch(p.mode) {
    case GM_BREATHING: {
        double period = 12.0 / sp;
        f = 0.04 + 0.96 * (1.0 - std::cos(2 * M_PI * t / period)) / 2.0;
        break; }
    case GM_FLASHING: {
        double period = 4.0 / sp;
        f = (std::fmod(t, period) < period / 2) ? 1.0 : 0.0;
        break; }
    case GM_STROBE: {
        double period = 1.0 / sp;
        f = (std::fmod(t, period) < std::max(0.05, 0.2 * period)) ? 1.0 : 0.0;
        break; }
    case GM_HEARTBEAT: {
        double period = 6.0 / sp;
        double x = std::fmod(t, period) / period;
        auto pulse = [](double x, double c, double w){ double d = (x - c) / w; return std::exp(-d * d); };
        f = 0.03 + 0.97 * std::max(pulse(x, 0.05, 0.035), 0.7 * pulse(x, 0.2, 0.04));
        break; }
    case GM_SPECTRUM:
        hsv(t / (24.0 / sp), 1.0, 1.0);
        break;
    case GM_FADE2: {
        double period = 16.0 / sp;
        double x = (1.0 - std::cos(2 * M_PI * t / period)) / 2.0;
        int r2 = p.color2 & 0xFF, g2 = (p.color2 >> 8) & 0xFF, b2 = (p.color2 >> 16) & 0xFF;
        r = std::lround(r + (r2 - r) * x); g = std::lround(g + (g2 - g) * x); b = std::lround(b + (b2 - b) * x);
        break; }
    case GM_RANDOM: {
        double period = 6.0 / sp, fade = period * 0.5;
        if(t >= rndNext || t < rndNext - period - 1) {
            rndFrom = rndTo;
            std::uniform_real_distribution<double> d(0.0, 1.0);
            QColor c = QColor::fromHsvF(d(rng), 1.0, 1.0);
            rndTo = (unsigned(c.blue()) << 16) | (unsigned(c.green()) << 8) | unsigned(c.red());
            rndNext = t + period;
        }
        double x = std::clamp((t - (rndNext - period)) / fade, 0.0, 1.0);
        auto mix = [&](int sh){ int a = (rndFrom >> sh) & 0xFF, c = (rndTo >> sh) & 0xFF; return int(std::lround(a + (c - a) * x)); };
        r = mix(0); g = mix(8); b = mix(16);
        break; }
    case GM_CANDLE: {
        double n = noise(t * (1.0 + sp * 0.8));
        f = std::clamp(0.35 + 0.65 * n, 0.0, 1.0);
        break; }
    case GM_FIRE: {
        double n = noise(t * (1.0 + sp * 0.8)), n2 = noise(t * (2.0 + sp * 1.2) + 5.0);
        hsv(0.015 + 0.07 * n, 1.0, 1.0);
        f = std::clamp(0.45 + 0.55 * n2, 0.0, 1.0);
        break; }
    case GM_POLICE: {
        double period = 1.2 / sp;
        double x = std::fmod(t, period) / period;
        bool red = x < 0.5;
        double ph = std::fmod(x, 0.5) / 0.5;
        f = (ph < 0.6) ? 1.0 : 0.0;           // due lampi rapidi per colore
        if(std::fmod(ph * 4.0, 1.0) > 0.6) f = 0.0;
        if(red) { r = 255; g = 0; b = 0; } else { r = 0; g = 0; b = 255; }
        break; }
    case GM_AURORA: {
        double period = 30.0 / sp;
        hsv(0.5 + 0.2 * (0.5 + 0.5 * std::sin(2 * M_PI * t / period)), 1.0, 1.0);
        f = 0.55 + 0.45 * noise(t * 0.8);
        break; }
    case GM_LIGHTNING: {
        double base = 0.08;
        f = base;
        if(t >= ltUntil && t >= rndNext) {
            std::uniform_real_distribution<double> d(0.0, 1.0);
            double gap = (0.5 + d(rng) * 4.0) * (5.0 / sp);
            ltStart = t; ltUntil = t + (d(rng) < 0.4 ? 0.22 : 0.09);
            rndNext = ltUntil + gap;
        }
        if(t < ltUntil) {
            double dt = t - ltStart;
            f = (ltUntil - ltStart > 0.15 && dt > 0.07 && dt < 0.11) ? 0.1 : 1.0;
        }
        break; }
    default: break;
    }
    f *= std::clamp(p.brightness, 0, 100) / 100.0;
    queueColor((unsigned char)std::lround(r * f), (unsigned char)std::lround(g * f), (unsigned char)std::lround(b * f));
    if(ready) flush();
}

void GoveeBle::queueColor(unsigned char r, unsigned char g, unsigned char b)
{
    unsigned char in[3] = {r, g, b}, out[3];
    for(int i = 0; i < 3; i++) {
        QChar ch = chanOrder.at(i);
        out[i] = in[ch == 'R' ? 0 : ch == 'G' ? 1 : 2];
    }
    pendingColor = (long(out[0]) << 16) | (long(out[1]) << 8) | out[2];
    lastQueued = pendingColor;
    settleCount = 0;
}

// ---- BlueZ via D-Bus -------------------------------------------------------
QDBusMessage GoveeBle::call(const QString& path, const QString& iface, const QString& method) const
{
    return QDBusMessage::createMethodCall(BLUEZ, path, iface, method);
}

void GoveeBle::dbusCall(const QDBusMessage& m, int timeoutMs, std::function<void(const QDBusMessage&)> cb)
{
    auto* w = new QDBusPendingCallWatcher(bus.asyncCall(m, timeoutMs), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [w, cb]{
        QDBusMessage reply = w->reply();
        w->deleteLater();
        if(cb) cb(reply);
    });
}

void GoveeBle::getManaged(std::function<void(const Managed&)> cb)
{
    QDBusMessage m = call("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    quint64 sess = session;
    dbusCall(m, 5000, [this, sess, cb](const QDBusMessage& r){
        if(sess != session) return;
        if(r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) { fail("BlueZ non risponde"); return; }
        Managed mo;
        r.arguments().first().value<QDBusArgument>() >> mo;
        cb(mo);
    });
}

bool GoveeBle::deviceMatches(const QVariantMap& p) const
{
    const QString addr = p.value("Address").toString().toUpper();
    if(!wantedAddr.isEmpty()) return addr == wantedAddr;
    const QString n = p.value("Name").toString();
    return n.startsWith("ihoment_", Qt::CaseInsensitive) || n.startsWith("Govee_", Qt::CaseInsensitive);
}

void GoveeBle::teardown(bool disconnectDevice)
{
    session++;                       // le risposte in arrivo dai tentativi vecchi vengono ignorate
    retry->stop(); watchdog->stop(); pollTimer->stop(); keepTimer->stop();
    if(discovering) stopDiscovery();
    if(disconnectDevice && !devPath.isEmpty() && (state == Connecting || state == Resolving || state == Ready))
        dbusCall(call(devPath, "org.bluez.Device1", "Disconnect"), 5000, nullptr);
    state = Idle; ready = false; charPath.clear();
    lastPowerSent = -1; lastColorSent = -1;
}

void GoveeBle::ensureConnection()
{
    if(state != Idle) return;
    connectClock.start();
    state = Looking;
    watchdog->start(30000);
    setStatus("collegamento...");
    lookForDevice();
    pollTimer->start(500);
}

void GoveeBle::lookForDevice()
{
    getManaged([this](const Managed& m){ onManaged(m); });
}

void GoveeBle::onManaged(const Managed& m)
{
    if(state != Looking) return;
    QString cand;
    for(auto it = m.constBegin(); it != m.constEnd(); ++it) {
        const IfaceMap& im = it.value();
        if(adapterPath.isEmpty() && im.contains("org.bluez.Adapter1")) adapterPath = it.key().path();
        if(!im.contains("org.bluez.Device1")) continue;
        const QVariantMap p = im.value("org.bluez.Device1");
        const QString n = p.value("Name").toString();
        if(n.startsWith("ihoment_", Qt::CaseInsensitive) || n.startsWith("Govee_", Qt::CaseInsensitive))
            emit deviceFound(p.value("Address").toString(), n);
        if(cand.isEmpty() && deviceMatches(p)) {
            cand = it.key().path();
            if(wantedAddr.isEmpty()) { wantedAddr = p.value("Address").toString().toUpper(); emit addressLearned(wantedAddr); }
        }
    }
    if(!cand.isEmpty()) { devPath = cand; connectDevice(); return; }
    if(!discovering) startDiscovery();      // sconosciuta a BlueZ: avvia la ricerca e riprova ogni 0,5 s
}

void GoveeBle::startDiscovery()
{
    if(adapterPath.isEmpty()) { fail("nessun adattatore Bluetooth"); return; }
    discovering = true;
    setStatus("ricerca della striscia...");
    QDBusMessage f = call(adapterPath, "org.bluez.Adapter1", "SetDiscoveryFilter");
    QVariantMap flt; flt["Transport"] = "le"; flt["DuplicateData"] = false;
    f << QVariant(flt);
    dbusCall(f, 3000, nullptr);
    dbusCall(call(adapterPath, "org.bluez.Adapter1", "StartDiscovery"), 3000, nullptr);
}

void GoveeBle::stopDiscovery()
{
    discovering = false;
    if(!adapterPath.isEmpty()) dbusCall(call(adapterPath, "org.bluez.Adapter1", "StopDiscovery"), 3000, nullptr);
}

void GoveeBle::connectDevice()
{
    if(discovering) stopDiscovery();
    state = Connecting;
    pollTimer->stop();
    setStatus("collegamento...");
    // "fidato": BlueZ non dimentica la striscia, cosi' i collegamenti successivi sono diretti (senza ricerca)
    QDBusMessage t = call(devPath, "org.freedesktop.DBus.Properties", "Set");
    t << "org.bluez.Device1" << "Trusted" << QVariant::fromValue(QDBusVariant(true));
    dbusCall(t, 3000, nullptr);
    quint64 sess = session;
    dbusCall(call(devPath, "org.bluez.Device1", "Connect"), 25000, [this, sess](const QDBusMessage& r){
        if(sess != session) return;
        if(r.type() == QDBusMessage::ErrorMessage && !r.errorName().endsWith("AlreadyConnected")) {
            if(r.errorName().endsWith("UnknownObject") || r.errorName().endsWith("DoesNotExist")) devPath.clear();
            fail("errore collegamento: " + r.errorMessage());
            return;
        }
        state = Resolving;
        checkResolved();
        pollTimer->start(100);
    });
}

void GoveeBle::checkResolved()
{
    QDBusMessage m = call(devPath, "org.freedesktop.DBus.Properties", "Get");
    m << "org.bluez.Device1" << "ServicesResolved";
    quint64 sess = session;
    dbusCall(m, 2000, [this, sess](const QDBusMessage& r){
        if(sess != session || state != Resolving) return;
        if(r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) return;
        if(r.arguments().first().value<QDBusVariant>().variant().toBool()) { pollTimer->stop(); findChar(); }
    });
}

void GoveeBle::findChar()
{
    getManaged([this](const Managed& m){
        if(state != Resolving) return;
        QString notifyPath;
        for(auto it = m.constBegin(); it != m.constEnd(); ++it) {
            const IfaceMap& im = it.value();
            if(!im.contains("org.bluez.GattCharacteristic1")) continue;
            if(!it.key().path().startsWith(devPath + "/")) continue;
            const QString u = im.value("org.bluez.GattCharacteristic1").value("UUID").toString();
            if(u.compare(CHR_UUID, Qt::CaseInsensitive) == 0) charPath = it.key().path();
            else if(u.compare(NOTIFY_UUID, Qt::CaseInsensitive) == 0) notifyPath = it.key().path();
        }
        if(charPath.isEmpty()) { fail("caratteristica di scrittura non trovata"); return; }
        // come l'app Govee: attiva le notifiche (la striscia altrimenti chiude il collegamento dopo ~12 s)
        if(!notifyPath.isEmpty()) dbusCall(call(notifyPath, "org.bluez.GattCharacteristic1", "StartNotify"), 3000, nullptr);
        onReady();
    });
}

void GoveeBle::onReady()
{
    state = Ready; ready = true; retryStep = 0; txKeep = 0; txOther = 0;
    watchdog->stop();
    pollTimer->start(2000);
    keepTimer->start();
    setStatus(QString("collegato (%1 ms)").arg(connectClock.elapsed()));
    lastColorSent = -1; lastPowerSent = -1;
    QMetaObject::invokeMethod(this, [this]{ onParams(); }, Qt::QueuedConnection);   // riapplica lo stato attuale
}

void GoveeBle::onDisconnected()
{
    if(state == Idle) return;
    bool was = ready;
    logLine(QString("caduto dopo %1 ms dalla connessione: pacchetti colore/comandi=%2 keepalive=%3").arg(connectClock.elapsed()).arg(txOther).arg(txKeep));
    teardown(false);
    if(wantConn) { setStatus(was ? "scollegato, riconnessione..." : "collegamento fallito, riprovo..."); scheduleRetry(); }
    else setStatus("scollegato (a riposo)");
}

void GoveeBle::fail(const QString& why)
{
    teardown(true);
    setStatus(why);
    scheduleRetry();
}

void GoveeBle::scheduleRetry()
{
    if(!wantConn || retry->isActive()) return;
    static const int steps[] = {800, 1500, 3000, 6000};
    retry->start(steps[std::min(retryStep, 3)]);
    retryStep++;
}

void GoveeBle::onIdle()
{
    if(keepAlways || animated()) return;
    wantConn = false;
    teardown(true);
    setStatus("a riposo: si ricollega al prossimo cambio");
}

// ---- invio -------------------------------------------------------------------
QByteArray GoveeBle::packet(std::initializer_list<unsigned char> head)
{
    QByteArray p(20, '\0');
    int i = 0;
    for(unsigned char b : head) p[i++] = char(b);
    unsigned char x = 0;
    for(int k = 0; k < 19; k++) x ^= (unsigned char)p[k];
    p[19] = char(x);
    return p;
}

void GoveeBle::sendPacket(const QByteArray& p)
{
    if(!ready || charPath.isEmpty()) return;
    if(qEnvironmentVariableIsSet("GOVEE_DEBUG")) qInfo("[%lld] tx %s", connectClock.elapsed(), p.left(6).toHex(' ').constData());
    if((unsigned char)p[0] == 0xAA) txKeep++; else txOther++;
    QDBusMessage m = QDBusMessage::createMethodCall("org.bluez", charPath, "org.bluez.GattCharacteristic1", "WriteValue");
    QVariantMap opts; opts["type"] = "command";          // scrittura senza risposta
    m << QVariant(p) << QVariant(opts);
    quint64 sess = session;
    dbusCall(m, 3000, [this, sess](const QDBusMessage& r){
        if(r.type() == QDBusMessage::ErrorMessage)
            qWarning("Govee: scrittura fallita: %s %s", qPrintable(r.errorName()), qPrintable(r.errorMessage()));
        if(sess == session && r.type() == QDBusMessage::ErrorMessage
           && (r.errorName().endsWith("NotConnected") || r.errorName().endsWith("Failed")))
            onDisconnected();
    });
    sinceWrite.restart();
}

void GoveeBle::flush()
{
    if(!ready) return;
    if(sinceWrite.elapsed() < 30) {
        if(!flushTimer->isActive()) flushTimer->start(int(30 - sinceWrite.elapsed()) + 1);
        return;
    }
    if(pendingPower >= 0 && pendingPower != lastPowerSent) {
        sendPacket(packet({0x33, 0x01, (unsigned char)pendingPower}));
        lastPowerSent = pendingPower;
        pendingPower = -1;
        if(pendingColor >= 0) flushTimer->start(35);
        return;
    }
    pendingPower = -1;
    if(pendingColor >= 0) {
        long c = pendingColor; pendingColor = -1;
        if(c != lastColorSent) {
            sendPacket(packet({0x33, 0x05, 0x02, (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c}));
            lastColorSent = c;
            if(!animated() && !settling) { settleCount = 0; settleTimer->start(120); }
        }
    }
}
