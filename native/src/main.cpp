#include "ui/MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("APDU Tracer Native"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("APDU Tracer"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Textual USB CCID APDU capture with USBPcap"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("capture"), QStringLiteral("Optional classic USBPcap .pcap file"),
                                 QStringLiteral("[capture]"));
    parser.process(app);
    apdu::MainWindow window;
    window.show();
    const auto args = parser.positionalArguments();
    if (!args.isEmpty())
        QTimer::singleShot(0, &window, [&window, args] { window.openCapture(args.first()); });
    return app.exec();
}
