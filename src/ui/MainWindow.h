#pragma once

#include <QMainWindow>
#include <QString>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

namespace apdu {
class TraceWorker;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void openCapture(const QString& path);
    QString traceText() const;

signals:
    void sessionFinished(bool success);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void refreshDevices();
    void startLive();
    void startWorker(TraceWorker* worker);
    void drainLines();
    void setBusy(bool busy);
    void clearTrace();
    void saveTrace();

    QComboBox* devices_;
    QSpinBox* address_;
    QPushButton* refresh_;
    QPushButton* start_;
    QPushButton* stop_;
    QPushButton* open_;
    QPushButton* clear_;
    QPushButton* save_;
    QCheckBox* autoScroll_;
    QPlainTextEdit* trace_;
    QLabel* status_;
    QLabel* count_;
    QTimer* poll_;
    TraceWorker* worker_ = nullptr;
    quint64 total_ = 0;
    bool failed_ = false;
};
} // namespace apdu
