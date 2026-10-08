#include "backend.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QLocale>
#include <QHash>
#include <QRegularExpression>
#include <windows.h>

static QString guiIniPath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath("gui.ini");
}

static const QHash<QString, QStringList>& dict() {
    static const QHash<QString, QStringList> d = {
        {"menu_server",   {"Сервер", "Server"}},
        {"menu_settings", {"Настройки", "Settings"}},
        {"menu_log",      {"Журнал", "Log"}},
        {"start",         {"Запустить сервер", "Start server"}},
        {"stop",          {"Остановить сервер", "Stop server"}},
        {"max_bitrate",   {"Макс. битрейт", "Max bitrate"}},
        {"min_bitrate",   {"Мин. битрейт", "Min bitrate"}},
        {"unit_mbps",     {"Мбит/с", "Mbit/s"}},
        {"adaptive",      {"Адаптивный битрейт", "Adaptive bitrate"}},
        {"adaptive_sub",  {"Подстраивать битрейт по потерям", "Adjust bitrate to packet loss"}},
        {"respect",       {"Не превышать битрейт клиента", "Do not exceed client bitrate"}},
        {"respect_sub",   {"Учитывать битрейт, заявленный клиентом", "Respect the bitrate requested by the client"}},
        {"save",          {"Сохранить настройки", "Save settings"}},
        {"save_sub",      {"Запись в server.ini", "Written to server.ini"}},
        {"controller",    {"Эмуляция геймпада", "Gamepad emulation"}},
        {"controller_sub",{"Применяется при следующем запуске сервера", "Applied on next server start"}},
        {"language",      {"Язык", "Language"}},
        {"language_sub",  {"Применяется сразу", "Applied immediately"}},
        {"st_stopped",    {"Остановлен", "Stopped"}},
        {"st_starting",   {"Запуск…", "Starting…"}},
        {"st_failed",     {"Не удалось запустить сервер", "Failed to start the server"}},
        {"st_connected_video", {"Клиент подключён: %1 fps, %2 Мбит/с", "Client connected: %1 fps, %2 Mbit/s"}},
        {"st_connected",  {"Клиент подключён", "Client connected"}},
        {"st_waiting",    {"Ожидание клиента", "Waiting for client"}},
        {"st_stopped_code", {"Остановлен (код %1)", "Stopped (code %1)"}},
        {"n_settings",    {"Настройки", "Settings"}},
        {"n_minmax",      {"Минимальный битрейт не может быть больше максимального.", "Minimum bitrate cannot exceed the maximum."}},
        {"n_saved_running", {"Сохранено. Чтобы применить, остановите и запустите сервер заново.", "Saved. Stop and start the server again to apply."}},
        {"n_saved",       {"Сохранено.", "Saved."}},
        {"n_error",       {"Ошибка", "Error"}},
        {"n_no_exe",      {"Не найден takion-host.exe рядом с программой.", "takion-host.exe was not found next to the application."}},
    };
    return d;
}

QString ServerBackend::t(const QString& key) const {
    const auto it = dict().constFind(key);
    if (it == dict().constEnd()) return key;
    return it->value(lang_ == "en" ? 1 : 0);
}

QString ServerBackend::status() const {
    QString s = t(statusKey_);
    if (!statusA_.isEmpty()) s = s.arg(statusA_);
    if (!statusB_.isEmpty()) s = s.arg(statusB_);
    return s;
}

void ServerBackend::setStatus(const QString& key, const QString& a, const QString& b) {
    statusKey_ = key; statusA_ = a; statusB_ = b;
    emit statusChanged();
}

void ServerBackend::cycleLanguage() {
    lang_ = (lang_ == "ru") ? "en" : "ru";
    QSettings(guiIniPath(), QSettings::IniFormat).setValue("language", lang_);
    emit languageChanged();
    emit statusChanged();
}

ServerBackend::ServerBackend(QObject* parent) : QObject(parent) {
    proc_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= CREATE_NO_WINDOW;
    });
    proc_.setProcessChannelMode(QProcess::MergedChannels);

    connect(&proc_, &QProcess::readyRead, this, &ServerBackend::onOutput);
    connect(&proc_, &QProcess::finished, this, &ServerBackend::onFinished);
    connect(&proc_, &QProcess::stateChanged, this, [this] { emit runningChanged(); });
    connect(&proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            setStatus("st_failed");
    });
    {
        QSettings s(guiIniPath(), QSettings::IniFormat);
        const QString def = (QLocale::system().language() == QLocale::Russian) ? "ru" : "en";
        lang_ = s.value("language", def).toString();
        if (lang_ != "en") lang_ = "ru";
    }
    loadSettings();
}

QString ServerBackend::serverExe() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath("takion-host.exe");
}
QString ServerBackend::iniPath() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath("server.ini");
}

QString ServerBackend::iniValue(const QString& key, const QString& def) const {
    QFile f(iniPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return def;
    const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith('#') || line.startsWith(';')) continue;
        const int eq = line.indexOf('=');
        if (eq > 0 && line.left(eq).trimmed() == key) return line.mid(eq + 1).trimmed();
    }
    return def;
}

void ServerBackend::setIniValue(const QString& key, const QString& value) {
    QFile f(iniPath());
    QStringList lines;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lines = QString::fromUtf8(f.readAll()).split('\n');
        f.close();
    }
    bool done = false;
    for (QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith('#') || line.startsWith(';')) continue;
        const int eq = line.indexOf('=');
        if (eq > 0 && line.left(eq).trimmed() == key) {
            raw = key + " = " + value;
            done = true;
            break;
        }
    }
    if (!done) lines << (key + " = " + value);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        f.write(lines.join("\n").toUtf8());
}

static bool isTrue(const QString& v) {
    const QString s = v.trimmed().toLower();
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

static const QStringList kControllerIds   = {"ds4", "x360"};
static const QStringList kControllerNames = {"DualShock 4", "Xbox 360"};

QString ServerBackend::controllerName() const {
    const int i = kControllerIds.indexOf(controller_);
    return kControllerNames.value(i < 0 ? 0 : i);
}

void ServerBackend::cycleController(int dir) {
    int i = kControllerIds.indexOf(controller_);
    if (i < 0) i = 0;
    i = (i + dir + kControllerIds.size()) % kControllerIds.size();
    controller_ = kControllerIds[i];
    emit settingsChanged();
}

void ServerBackend::loadSettings() {
    maxMbps_ = iniValue("bitrate_max_mbps", "20").toDouble();
    minMbps_ = iniValue("bitrate_min_mbps", "6").toDouble();
    adaptive_ = isTrue(iniValue("adaptive_bitrate", "0"));
    respect_ = isTrue(iniValue("respect_client_bitrate", "1"));
    controller_ = iniValue("controller_type", "ds4");
    emit settingsChanged();
}

void ServerBackend::saveSettings() {
    if (minMbps_ > maxMbps_) {
        emit notice(t("n_settings"), t("n_minmax"));
        return;
    }
    setIniValue("controller_type", controller_);
    setIniValue("bitrate_max_mbps", QString::number(maxMbps_, 'f', 1));
    setIniValue("bitrate_min_mbps", QString::number(minMbps_, 'f', 1));
    setIniValue("adaptive_bitrate", adaptive_ ? "1" : "0");
    setIniValue("respect_client_bitrate", respect_ ? "1" : "0");
    emit notice(t("n_settings"), running() ? t("n_saved_running") : t("n_saved"));
}

void ServerBackend::toggleServer() {
    if (running()) { stopServer(); return; }
    if (!QFileInfo::exists(serverExe())) {
        emit notice(t("n_error"), t("n_no_exe"));
        return;
    }
    buf_.clear();
    emit logCleared();
    proc_.setProgram(serverExe());
    proc_.setArguments({"--gui-control", "--random-pin"});
    proc_.setWorkingDirectory(QFileInfo(serverExe()).absolutePath());
    proc_.start();
    setStatus("Запуск…");
}

void ServerBackend::stopServer() {
    if (proc_.state() == QProcess::NotRunning) return;
    proc_.write("quit\n");
    if (!proc_.waitForFinished(5000)) {
        proc_.kill();
        proc_.waitForFinished(2000);
    }
}

void ServerBackend::onOutput() {
    buf_ += proc_.readAll();
    int nl;
    while ((nl = buf_.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(buf_.left(nl)).trimmed();
        buf_.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        emit logLine(line);
        parseLine(line);
    }
}

void ServerBackend::parseLine(const QString& line) {
    static const QRegularExpression rxPin(R"(Registration PIN:\s*(\d+))");
    static const QRegularExpression rxVideo(R"(\[video\]\s+([\d.]+)\s+fps,\s+([\d.]+)\s+Mbit/s)");
    if (auto m = rxPin.match(line); m.hasMatch()) {
        pin_ = m.captured(1);
        emit pinChanged();
    } else if (auto v = rxVideo.match(line); v.hasMatch()) {
        setStatus("st_connected_video", v.captured(1), v.captured(2));
    } else if (line.contains("STREAMINFO sent")) {
        setStatus("st_connected");
    } else if (line.contains("Waiting for client connections") ||
               line.contains("client disconnect") || line.contains("client timeout")) {
        setStatus("st_waiting");
    }
}

void ServerBackend::onFinished(int exitCode, QProcess::ExitStatus) {
    setStatus("st_stopped_code", QString::number(exitCode));
}