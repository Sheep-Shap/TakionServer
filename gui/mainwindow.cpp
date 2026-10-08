#include "mainwindow.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <windows.h>

MainWindow::MainWindow() {
    setWindowTitle("PS Takion Server");
    resize(820, 620);
    auto* central = new QWidget;
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);

    auto* top = new QHBoxLayout;
    status_ = new QLabel("Остановлен");
    pin_ = new QLabel("PIN: —");
    btn_ = new QPushButton("Запустить");
    top->addWidget(status_, 1);
    top->addWidget(pin_);
    top->addWidget(btn_);
    root->addLayout(top);

    auto* box = new QGroupBox("Настройки (server.ini)");
    auto* form = new QFormLayout(box);
    maxMbps_ = new QDoubleSpinBox; maxMbps_->setRange(1, 100); maxMbps_->setDecimals(1); maxMbps_->setSuffix(" Мбит/с");
    minMbps_ = new QDoubleSpinBox; minMbps_->setRange(1, 100); minMbps_->setDecimals(1); minMbps_->setSuffix(" Мбит/с");
    adaptive_ = new QCheckBox("Адаптивный битрейт (по потерям)");
    respect_ = new QCheckBox("Не превышать битрейт, заявленный клиентом");
    auto* save = new QPushButton("Сохранить настройки");
    form->addRow("Макс. битрейт:", maxMbps_);
    form->addRow("Мин. битрейт:", minMbps_);
    form->addRow(adaptive_);
    form->addRow(respect_);
    form->addRow(save);
    root->addWidget(box);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    root->addWidget(log_, 1);

    proc_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= CREATE_NO_WINDOW;
    });
    proc_.setProcessChannelMode(QProcess::MergedChannels);

    connect(btn_, &QPushButton::clicked, this, &MainWindow::toggleServer);
    connect(save, &QPushButton::clicked, this, &MainWindow::saveSettings);
    connect(&proc_, &QProcess::readyRead, this, &MainWindow::onOutput);
    connect(&proc_, &QProcess::finished, this, &MainWindow::onFinished);
    connect(&proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            status_->setText("Не удалось запустить сервер");
            btn_->setText("Запустить");
        }
    });

    loadSettings();
}

QString MainWindow::serverExe() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath("takion-host.exe");
}

QString MainWindow::iniPath() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath("server.ini");
}

QString MainWindow::iniValue(const QString& key, const QString& def) const {
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

void MainWindow::setIniValue(const QString& key, const QString& value) {
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

void MainWindow::loadSettings() {
    maxMbps_->setValue(iniValue("bitrate_max_mbps", "20").toDouble());
    minMbps_->setValue(iniValue("bitrate_min_mbps", "6").toDouble());
    adaptive_->setChecked(isTrue(iniValue("adaptive_bitrate", "0")));
    respect_->setChecked(isTrue(iniValue("respect_client_bitrate", "1")));
}

void MainWindow::saveSettings() {
    if (minMbps_->value() > maxMbps_->value()) {
        QMessageBox::warning(this, "Настройки", "Минимальный битрейт не может быть больше максимального.");
        return;
    }
    setIniValue("bitrate_max_mbps", QString::number(maxMbps_->value(), 'f', 1));
    setIniValue("bitrate_min_mbps", QString::number(minMbps_->value(), 'f', 1));
    setIniValue("adaptive_bitrate", adaptive_->isChecked() ? "1" : "0");
    setIniValue("respect_client_bitrate", respect_->isChecked() ? "1" : "0");
    if (proc_.state() != QProcess::NotRunning)
        QMessageBox::information(this, "Настройки", "Сохранено. Чтобы применить, остановите и запустите сервер заново.");
}

void MainWindow::toggleServer() {
    if (proc_.state() != QProcess::NotRunning) {
        stopServer();
        return;
    }
    if (!QFileInfo::exists(serverExe())) {
        QMessageBox::critical(this, "Ошибка", "Не найден takion-host.exe рядом с программой.");
        return;
    }
    buf_.clear();
    log_->clear();
    proc_.setProgram(serverExe());
    proc_.setArguments({"--gui-control"});
    proc_.setWorkingDirectory(QFileInfo(serverExe()).absolutePath());
    proc_.start();
    btn_->setText("Остановить");
    status_->setText("Запуск…");
}

void MainWindow::stopServer() {
    if (proc_.state() == QProcess::NotRunning) return;
    proc_.write("quit\n");
    if (!proc_.waitForFinished(5000)) {
        proc_.kill();
        proc_.waitForFinished(2000);
    }
}

void MainWindow::onOutput() {
    buf_ += proc_.readAll();
    int nl;
    while ((nl = buf_.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(buf_.left(nl)).trimmed();
        buf_.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        log_->appendPlainText(line);
        parseLine(line);
    }
}

void MainWindow::parseLine(const QString& line) {
    static const QRegularExpression rxPin(R"(Registration PIN:\s*(\d+))");
    static const QRegularExpression rxVideo(R"(\[video\]\s+([\d.]+)\s+fps,\s+([\d.]+)\s+Mbit/s)");
    if (auto m = rxPin.match(line); m.hasMatch()) {
        pin_->setText("PIN: " + m.captured(1));
    } else if (auto v = rxVideo.match(line); v.hasMatch()) {
        status_->setText(QString("Клиент подключён: %1 fps, %2 Мбит/с").arg(v.captured(1), v.captured(2)));
    } else if (line.contains("STREAMINFO sent")) {
        status_->setText("Клиент подключён");
    } else if (line.contains("Waiting for PS Remote Play connections") ||
               line.contains("client disconnect") || line.contains("client timeout")) {
        status_->setText("Ожидание клиента");
    }
}

void MainWindow::onFinished(int code, QProcess::ExitStatus) {
    btn_->setText("Запустить");
    status_->setText(QString("Остановлен (код %1)").arg(code));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    stopServer();
    e->accept();
}