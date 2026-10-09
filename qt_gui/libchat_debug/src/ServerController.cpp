#include "ServerController.h"
#include "Server.hpp"
#include "ChatHistory.hpp"
#include "Database.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
// Forwards log lines from server threads to the GUI thread. Client threads can
// still log after the window is gone, so the bridge is never deleted.
class LogBridge : public QObject {
    Q_OBJECT
signals:
    void message(const QString& text);
};
}

ServerController::ServerController(QObject* parent)
    : QObject(parent)
    , m_database(new Database(":memory:"))
    , m_history(new ChatHistory(*m_database)) {}

ServerController::~ServerController() {
    stop();
}

bool ServerController::start(quint16 port, bool allowRemote, QString* error) {
    if (m_running)
        return true;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        *error = QStringLiteral("socket() failed: %1").arg(std::strerror(errno));
        return false;
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(allowRemote ? INADDR_ANY : INADDR_LOOPBACK);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1 || listen(fd, 16) == -1) {
        *error = QStringLiteral("could not listen on port %1: %2").arg(port).arg(std::strerror(errno));
        close(fd);
        return false;
    }
    socklen_t len = sizeof(addr);
    getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);

    m_listenFd = fd;
    m_port = ntohs(addr.sin_port);
    m_stop = false;

    auto* bridge = new LogBridge;
    connect(bridge, &LogBridge::message, this, &ServerController::logMessage, Qt::QueuedConnection);

    ServerOptions options;
    options.stop = &m_stop;
    options.history = m_history;
    options.log = [bridge](const std::string& text) {
        emit bridge->message(QString::fromStdString(text));
    };
    m_thread = std::thread([fd, options] { runServer(fd, options); });

    m_running = true;
    emit logMessage(QStringLiteral("server listening on %1:%2")
                        .arg(allowRemote ? QStringLiteral("0.0.0.0") : QStringLiteral("127.0.0.1"))
                        .arg(m_port));
    emit runningChanged(true);
    return true;
}

void ServerController::stop() {
    if (!m_running)
        return;
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();  // runServer checks the flag every 200 ms
    close(m_listenFd);
    m_listenFd = -1;
    m_running = false;
    emit logMessage(QStringLiteral("server stopped"));
    emit runningChanged(false);
}

#include "ServerController.moc"
