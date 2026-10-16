#include "ui/MainWindow.h"
#include <QApplication>
#include <QTimer>
#include <iostream>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    if (argc < 2 || argc > 3) return 2;
    apdu::MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    QObject::connect(&window, &apdu::MainWindow::sessionFinished, &app, [&](bool success) {
        const auto text = window.traceText();
        if (!success || !text.contains(QStringLiteral(">>")) || !text.contains(QStringLiteral("<<"))
            || !text.contains(QStringLiteral("90 00"))) {
            std::cerr << "Replay did not display the expected APDU exchange\n";
            app.exit(1);
        } else {
            if (argc == 3 && !window.grab().save(QString::fromLocal8Bit(argv[2]))) {
                std::cerr << "Could not save UI snapshot\n";
                app.exit(1);
                return;
            }
            std::cout << "Qt window and asynchronous PCAP replay passed\n";
            app.exit(0);
        }
    });
    QTimer::singleShot(0, &window, [&] { window.openCapture(QString::fromLocal8Bit(argv[1])); });
    QTimer::singleShot(10000, &app, [&] { std::cerr << "Replay timed out\n"; app.exit(1); });
    return app.exec();
}
