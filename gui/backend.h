#pragma once
#include <QObject>
#include <QProcess>

class ServerBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString pin READ pin NOTIFY pinChanged)
    Q_PROPERTY(QString controllerName READ controllerName NOTIFY settingsChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double maxMbps READ maxMbps WRITE setMaxMbps NOTIFY settingsChanged)
    Q_PROPERTY(double minMbps READ minMbps WRITE setMinMbps NOTIFY settingsChanged)
    Q_PROPERTY(bool adaptive READ adaptive WRITE setAdaptive NOTIFY settingsChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(bool respectClient READ respectClient WRITE setRespectClient NOTIFY settingsChanged)
public:
    explicit ServerBackend(QObject* parent = nullptr);
    ~ServerBackend() override { stopServer(); }

    QString pin() const { return pin_; }
    QString controllerName() const;
    QString language() const { return lang_; }
    QString status() const;                              // вместо прежнего inline-геттера
    bool running() const { return proc_.state() != QProcess::NotRunning; }
    double maxMbps() const { return maxMbps_; }
    double minMbps() const { return minMbps_; }
    bool adaptive() const { return adaptive_; }
    bool respectClient() const { return respect_; }
    void setMaxMbps(double v) { maxMbps_ = v; emit settingsChanged(); }
    void setMinMbps(double v) { minMbps_ = v; emit settingsChanged(); }
    void setAdaptive(bool v) { adaptive_ = v; emit settingsChanged(); }
    void setRespectClient(bool v) { respect_ = v; emit settingsChanged(); }

    Q_INVOKABLE QString t(const QString& key) const;
    Q_INVOKABLE void cycleLanguage();
    Q_INVOKABLE void cycleController(int dir);
    Q_INVOKABLE void toggleServer();
    Q_INVOKABLE void saveSettings();
    Q_INVOKABLE void stopServer();

signals:
    void languageChanged();
    void statusChanged();
    void pinChanged();
    void runningChanged();
    void settingsChanged();
    void logLine(const QString& line);
    void logCleared();
    void notice(const QString& title, const QString& text);

private slots:
    void onOutput();
    void onFinished(int code, QProcess::ExitStatus st);

private:
    QString serverExe() const;
    QString iniPath() const;
    QString iniValue(const QString& key, const QString& def) const;
    QString controller_ = "ds4";
    QString lang_ = "ru";
    QString statusKey_ = "st_stopped";
    QString statusA_, statusB_;
    void setStatus(const QString& key, const QString& a = QString(), const QString& b = QString());
    void setIniValue(const QString& key, const QString& value);
    void loadSettings();
    void parseLine(const QString& line);

    QProcess proc_;
    QByteArray buf_;
    QString pin_ = "—";
    double maxMbps_ = 20, minMbps_ = 6;
    bool adaptive_ = false, respect_ = true;
};