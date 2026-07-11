#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QTimer>
#include <QVariantList>
#include <QQmlContext>

/* DBus proxy: bridges org.uperflinux.Daemon DBus interface to QML.
 * Polls properties every 500ms and listens for DBus signals. */
class ModeProxy : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentMode READ currentMode NOTIFY currentModeChanged)
    Q_PROPERTY(QString currentScene READ currentScene NOTIFY currentSceneChanged)
    Q_PROPERTY(QVariantList frequencies READ frequencies NOTIFY frequenciesChanged)
    Q_PROPERTY(QVariantList loads READ loads NOTIFY loadsChanged)
    Q_PROPERTY(bool isHeavyLoad READ isHeavyLoad NOTIFY isHeavyLoadChanged)
    Q_PROPERTY(int maxTemp READ maxTemp NOTIFY maxTempChanged)
    Q_PROPERTY(QString thermalState READ thermalState NOTIFY thermalStateChanged)

public:
    explicit ModeProxy(QObject *parent = nullptr)
        : QObject(parent)
        , m_currentMode("balance")
        , m_currentScene("idle")
        , m_isHeavyLoad(false)
        , m_maxTemp(0)
        , m_thermalState("normal")
    {
        m_bus = QDBusConnection::systemBus();
        if (!m_bus.isConnected()) {
            qWarning() << "[ModeProxy] Cannot connect to system bus";
            return;
        }
        qDebug() << "[ModeProxy] Connected to system bus";

        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &ModeProxy::pollStats);
        m_timer->start(500);

        // Subscribe to DBus signals
        m_bus.connect("org.uperflinux.Daemon",
                      "/org/uperflinux/Daemon",
                      "org.uperflinux.Daemon",
                      "StatsUpdated",
                      this, SLOT(onStatsUpdated(QVariantList)));
        m_bus.connect("org.uperflinux.Daemon",
                      "/org/uperflinux/Daemon",
                      "org.uperflinux.Daemon",
                      "ModeChanged",
                      this, SLOT(onModeChanged(QVariantList)));
        m_bus.connect("org.uperflinux.Daemon",
                      "/org/uperflinux/Daemon",
                      "org.uperflinux.Daemon",
                      "SceneChanged",
                      this, SLOT(onSceneChanged(QVariantList)));
        m_bus.connect("org.uperflinux.Daemon",
                      "/org/uperflinux/Daemon",
                      "org.uperflinux.Daemon",
                      "HeavyLoadStateChanged",
                      this, SLOT(onHeavyLoadChanged(QVariantList)));
    }

    QString currentMode() const { return m_currentMode; }
    QString currentScene() const { return m_currentScene; }
    QVariantList frequencies() const { return m_frequencies; }
    QVariantList loads() const { return m_loads; }
    bool isHeavyLoad() const { return m_isHeavyLoad; }
    int maxTemp() const { return m_maxTemp; }
    QString thermalState() const { return m_thermalState; }

    Q_INVOKABLE void setMode(const QString &mode) {
        QDBusInterface iface("org.uperflinux.Daemon",
                             "/org/uperflinux/Daemon",
                             "org.uperflinux.Daemon",
                             QDBusConnection::systemBus());
        QDBusReply<bool> reply = iface.call("SetMode", mode);
        if (!reply.isValid()) {
            qWarning() << "[ModeProxy] SetMode failed:" << reply.error().message();
        } else {
            qDebug() << "[ModeProxy] Mode set to:" << mode;
        }
    }

public slots:
    void pollStats() {
        QDBusInterface iface("org.uperflinux.Daemon",
                             "/org/uperflinux/Daemon",
                             "org.uperflinux.Daemon",
                             QDBusConnection::systemBus());

        QDBusReply<QString> modeReply = iface.call("GetCurrentMode");
        if (modeReply.isValid() && modeReply.value() != m_currentMode) {
            m_currentMode = modeReply.value();
            emit currentModeChanged();
        }

        QDBusReply<QString> sceneReply = iface.call("GetCurrentScene");
        if (sceneReply.isValid() && sceneReply.value() != m_currentScene) {
            m_currentScene = sceneReply.value();
            emit currentSceneChanged();
        }

        QDBusReply<QVariantList> freqReply = iface.call("GetCurrentFrequencies");
        if (freqReply.isValid()) {
            m_frequencies = freqReply.value();
            emit frequenciesChanged();
        }

        QDBusReply<QVariantList> loadReply = iface.call("GetCurrentLoads");
        if (loadReply.isValid()) {
            m_loads = loadReply.value();
            emit loadsChanged();
        }

        QDBusReply<bool> heavyReply = iface.call("GetCurrentHeavyLoad");
        if (heavyReply.isValid() && heavyReply.value() != m_isHeavyLoad) {
            m_isHeavyLoad = heavyReply.value();
            emit isHeavyLoadChanged();
        }

        QDBusReply<int> tempReply = iface.call("GetCurrentMaxTemp");
        if (tempReply.isValid() && tempReply.value() != m_maxTemp) {
            m_maxTemp = tempReply.value();
            emit maxTempChanged();
        }

        QDBusReply<QString> thermalReply = iface.call("GetCurrentThermalState");
        if (thermalReply.isValid() && thermalReply.value() != m_thermalState) {
            m_thermalState = thermalReply.value();
            emit thermalStateChanged();
        }
    }

private slots:
    void onStatsUpdated(const QVariantList &args) {
        if (args.size() >= 2) {
            m_frequencies = args[0].toList();
            m_loads = args[1].toList();
            emit frequenciesChanged();
            emit loadsChanged();
        }
    }

    void onModeChanged(const QVariantList &args) {
        if (!args.isEmpty()) {
            m_currentMode = args[0].toString();
            emit currentModeChanged();
        }
    }

    void onSceneChanged(const QVariantList &args) {
        if (!args.isEmpty()) {
            m_currentScene = args[0].toString();
            emit currentSceneChanged();
        }
    }

    void onHeavyLoadChanged(const QVariantList &args) {
        if (!args.isEmpty()) {
            m_isHeavyLoad = args[0].toBool();
            emit isHeavyLoadChanged();
        }
    }

    void onThermalUpdated(const QVariantList &args) {
        if (args.size() >= 2) {
            m_maxTemp = args[0].toInt();
            m_thermalState = args[1].toString();
            emit maxTempChanged();
            emit thermalStateChanged();
        }
    }

    Q_INVOKABLE void setAppMode(const QString &app, int modeIndex) {
        QStringList modes{"balance", "powersave", "performance"};
        if (modeIndex < 0 || modeIndex >= modes.size()) return;
        QString mode = modes[modeIndex];

        QDBusInterface iface("org.uperflinux.Daemon",
                             "/org/uperflinux/Daemon",
                             "org.uperflinux.Daemon",
                             QDBusConnection::systemBus());
        QDBusReply<bool> reply = iface.call("SetGameMode", 0, app, mode);
        if (!reply.isValid()) {
            qWarning() << "[ModeProxy] SetGameMode failed:" << reply.error().message();
        } else {
            qDebug() << "[ModeProxy] Per-app mode set:" << app << "=" << mode;
        }
    }

    Q_INVOKABLE void applySettings(
        double heavyLoadThd, double idleLoadThd,
        double sampleTime, double burstSlack,
        double latencyTime, double margin, double burst,
        double slowLimit, double fastLimit, double fastLimitCap,
        double warnTemp, double throttleTemp,
        double criticalTemp, double recoveryTemp)
    {
        QDBusInterface iface("org.uperflinux.Daemon",
                             "/org/uperflinux/Daemon",
                             "org.uperflinux.Daemon",
                             QDBusConnection::systemBus());
        QDBusReply<bool> reply = iface.call(
            "ApplySettings",
            heavyLoadThd, idleLoadThd,
            sampleTime, burstSlack,
            latencyTime, margin, burst,
            slowLimit, fastLimit, fastLimitCap,
            warnTemp, throttleTemp,
            criticalTemp, recoveryTemp);
        if (!reply.isValid()) {
            qWarning() << "[ModeProxy] ApplySettings failed:" << reply.error().message();
        } else {
            qDebug() << "[ModeProxy] Settings applied";
        }
    }

signals:
    void currentModeChanged();
    void currentSceneChanged();
    void frequenciesChanged();
    void loadsChanged();
    void isHeavyLoadChanged();
    void maxTempChanged();
    void thermalStateChanged();

private:
    QDBusConnection m_bus;
    QTimer *m_timer;
    QString m_currentMode;
    QString m_currentScene;
    QVariantList m_frequencies;
    QVariantList m_loads;
    bool m_isHeavyLoad;
    int m_maxTemp;
    QString m_thermalState;
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("uperf-gui");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("uperf-linux");

    // Register ModeProxy as a singleton type for QML
    qmlRegisterType<ModeProxy>("org.uperflinux", 1, 0, "ModeProxy");

    QQmlApplicationEngine engine;

    // Make proxy available as context property
    ModeProxy *proxy = new ModeProxy(&app);
    engine.rootContext()->setContextProperty("modeProxy", proxy);

    // Load QML from resources or file
    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    connect(&engine, &QQmlApplicationEngine::objectCreated,
            &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    // Try loading from qrc first, then from filesystem
    engine.load(url);
    if (engine.rootObjects().isEmpty()) {
        // Fallback: load from filesystem (for development)
        engine.load(QUrl::fromLocalFile(QStringLiteral("gui/qml/Main.qml")));
    }

    return app.exec();
}
