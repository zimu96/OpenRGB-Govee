#pragma once
#include <QObject>
#include <QtDBus>
#include <functional>
#include <QElapsedTimer>
#include <QMutex>
#include <QTimer>
#include <algorithm>
#include <random>

enum GoveeMode { GM_DIRECT = 0, GM_STATIC, GM_BREATHING, GM_FLASHING, GM_STROBE, GM_HEARTBEAT,
                 GM_SPECTRUM, GM_FADE2, GM_RANDOM, GM_CANDLE, GM_FIRE, GM_POLICE, GM_AURORA,
                 GM_LIGHTNING, GM_OFF,
                 // scene native della striscia (pacchetto 0x33 05 04)
                 GM_SC_SUNRISE, GM_SC_SUNSET, GM_SC_MOVIE, GM_SC_DATING, GM_SC_ROMANTIC,
                 GM_SC_BLINKING, GM_SC_CANDLE, GM_SC_SNOWFLAKE,
                 GM_COUNT };
static const int GOVEE_SEGMENTS = 15;

struct GoveeParams
{
    int          mode = GM_DIRECT;
    unsigned int color = 0x00FF0000;   // RGBColor di OpenRGB: 0x00BBGGRR
    unsigned int color2 = 0x0000FF00;  // secondo colore (Color Fade)
    int          speed = 5;            // 1..10
    int          brightness = 100;     // 0..100
    bool         useSeg = false;       // Direct a segmenti (opzione sperimentale)
    unsigned int seg[GOVEE_SEGMENTS] = {};   // colori dei segmenti (RGBColor di OpenRGB)
};

// Collegamento BLE a una striscia Govee + effetti generati dal plugin.
// Vive nel thread della GUI; applyParams() si puo' chiamare da qualsiasi thread.
class GoveeBle : public QObject
{
    Q_OBJECT
public:
    explicit GoveeBle(QObject* parent = nullptr);
    ~GoveeBle() override;

    void setAddress(const QString& addr);   // vuoto = cerca da solo
    QString address() const { return wantedAddr; }
    void setKeepConnected(bool k);
    bool keepConnected() const { return keepAlways; }
    void setIdleSeconds(int s) { idleSec = std::max(2, std::min(600, s)); }
    int idleSeconds() const { return idleSec; }
    void setOrder(const QString& o);        // RGB, RBG, GRB, GBR, BRG, BGR
    QString order() const { return chanOrder; }
    void testSegments();                    // primo segmento rosso, ultimo blu, resto verde

    void start();                           // collega subito se "resta sempre collegato"
    void shutdown();
    void reconnectNow();                    // forza una nuova ricerca/collegamento
    void applyParams(const GoveeParams& p); // thread-safe
    void testColor(unsigned char r, unsigned char g, unsigned char b);

    QString status() const { return lastStatus; }

signals:
    void statusChanged(QString text);
    void deviceFound(QString address, QString name);
    void addressLearned(QString address);

private slots:
    void onParams();
    void tick();
    void flush();
    void onIdle();

private:
    typedef QMap<QString, QVariantMap> IfaceMap;
    typedef QMap<QDBusObjectPath, IfaceMap> Managed;
    enum State { Idle, Looking, Connecting, Resolving, Ready };

    void dbusCall(const QDBusMessage& m, int timeoutMs, std::function<void(const QDBusMessage&)> cb);
    QDBusMessage call(const QString& path, const QString& iface, const QString& method) const;
    void getManaged(std::function<void(const Managed&)> cb);
    void lookForDevice();
    void onManaged(const Managed& m);
    void startDiscovery();
    void stopDiscovery();
    void connectDevice();
    void checkResolved();
    void findChar();
    void onReady();
    void onDisconnected();
    bool deviceMatches(const QVariantMap& props) const;
    void ensureConnection();
    void teardown(bool disconnectDevice);
    void scheduleRetry();
    void fail(const QString& why);

    void setStatus(const QString& s);
    void logLine(const QString& l);
    int txKeep = 0, txOther = 0;
    void queueColor(unsigned char r, unsigned char g, unsigned char b);
    double noise(double x);
    void sendPacket(const QByteArray& p20);
    static QByteArray packet(std::initializer_list<unsigned char> head);
    bool animated() const;

    QString wantedAddr;
    QString chanOrder = "RGB";
    bool keepAlways = true;
    int idleSec = 8;
    QElapsedTimer connectClock;
    std::mt19937 rng{std::random_device{}()};
    double rndNext = 0; unsigned rndFrom = 0, rndTo = 0, ltNext = 0;
    double ltUntil = 0, ltStart = 0;
    QString lastStatus = "in attesa";

    QMutex pm;
    GoveeParams params;

    bool wantConn = false;
    bool ready = false;
    int  retryStep = 0;
    State state = Idle;
    quint64 session = 0;
    bool discovering = false;
    QString adapterPath, devPath, charPath;
    QDBusConnection bus = QDBusConnection::systemBus();

    QTimer* retry = nullptr;
    QTimer* flushTimer = nullptr;
    QTimer* effectTimer = nullptr;
    QTimer* settleTimer = nullptr;
    QTimer* idleTimer = nullptr;
    QTimer* pollTimer = nullptr;
    QTimer* watchdog = nullptr;
    QTimer* keepTimer = nullptr;
    QElapsedTimer sinceWrite;
    QElapsedTimer sinceMode;
    int lastMode = -1;
    int settleCount = 0;
    bool settling = false;

    bool nativeActive = false;              // la striscia e' in una sua scena nativa
    int  burst = 0;                         // ripetizioni del colore dopo l'uscita da un modo nativo
    QByteArray lastRaw;                     // ultimo pacchetto nativo: viene ripetuto qualche volta
    QTimer* rawTimer = nullptr;
    int rawCount = 0;
    QByteArray pendingRaw;                  // pacchetto nativo (scena) da spedire una volta
    QList<QByteArray> segQueue;             // pacchetti a segmenti in coda
    void queueSegments(const unsigned* colors);
    int pendingPower = -1;
    long pendingColor = -1;                 // 0xRRGGBB (gia' riordinato) o -1
    long lastColorSent = -1;
    int lastPowerSent = -1;
    long lastQueued = -1;
};
