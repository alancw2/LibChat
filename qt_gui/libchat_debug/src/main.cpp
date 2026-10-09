#include "DebugWindow.h"

#include <QApplication>
#include <csignal>

int main(int argc, char* argv[]) {
    // A client vanishing must never take the embedded server down with it.
    std::signal(SIGPIPE, SIG_IGN);

    QApplication app(argc, argv);
    DebugWindow window;
    window.show();
    // Ready to go on launch: server running and two clients connected.
    window.startServer();
    window.addClient();
    window.addClient();
    return app.exec();
}
