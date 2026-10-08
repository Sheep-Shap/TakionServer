#pragma once
#include <QMainWindow>
#include <QProcess>

class QPlainTextEdit; class QPushButton; class QLabel; class QDoubleSpinBox; class QCheckBox;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
protected:
    void closeEvent(QCloseEvent* e) override;
private slots:
    void toggleServer();
    void onOutput();
    void onFinished(int code, QProcess::ExitStatus st);
    void saveSettings();
private:
    QString serverExe() const;
    QString iniPath() const;
    QString iniValue(const QString& key, const QString& def) const;
    void setIniValue(const QString& key, const QString& value);
    void loadSettings();
    void parseLine(const QString& line);
    void stopServer();

    QProcess proc_;
    QByteArray buf_;
    QPlainTextEdit* log_;
    QPushButton* btn_;
    QLabel* status_;
    QLabel* pin_;
    QDoubleSpinBox* maxMbps_;
    QDoubleSpinBox* minMbps_;
    QCheckBox* adaptive_;
    QCheckBox* respect_;
};