#pragma once

#include <QMainWindow>
#include <QString>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

namespace apdu {
class TraceView;
class TraceWorker;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void openCapture(const QString& path);
    // All rows in the exported line format, oldest first.
    QString traceText() const;
    const TraceView& traceView() const { return *trace_; }

signals:
    void sessionFinished(bool success);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void refreshDevices();
    void startLive();
    void startWorker(TraceWorker* worker);
    void drainRows();
    void updateShownCount();
    void setBusy(bool busy);
    void clearTrace();
    void saveTrace();

    QComboBox* devices_;
    QComboBox* transport_;
    QComboBox* buffer_;
    QSpinBox* address_;
    QPushButton* refresh_;
    QPushButton* start_;
    QPushButton* stop_;
    QPushButton* open_;
    QPushButton* clear_;
    QPushButton* save_;
    QCheckBox* saveRaw_;
    QCheckBox* autoScroll_;
    QLineEdit* filter_;
    TraceView* trace_;
    QLabel* status_;
    QLabel* count_;
    QLabel* shown_;
    QTimer* poll_;
    TraceWorker* worker_ = nullptr;
    QString rawPath_;
    QString sessionRawPath_;
    quint64 total_ = 0;
    bool failed_ = false;
    bool clearedDuringSession_ = false;
};
} // namespace apdu
