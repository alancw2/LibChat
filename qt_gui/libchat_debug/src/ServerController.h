#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <thread>

class ChatHistory;
class Database;

// Runs the real LibChat server (runServer) on a background thread inside
// this process, and reports its activity through logMessage.
class ServerController : public QObject {
    Q_OBJECT
public:
    explicit ServerController(QObject* parent = nullptr);
    ~ServerController() override;

    // Listens on the given port (0 picks a free one). Returns false and sets
    // *error if the port can't be opened.
    bool start(quint16 port, bool allowRemote, QString* error);
    void stop();

    bool isRunning() const { return m_running; }
    quint16 port() const { return m_port; }

signals:
    // Emitted on the GUI thread.
    void logMessage(const QString& message);
    void runningChanged(bool running);

private:
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    int m_listenFd = -1;
    quint16 m_port = 0;
    bool m_running = false;
    // Chat history is kept in memory for as long as the app runs, so it
    // survives stopping and restarting the server but not quitting. Client
    // threads may still be finishing at exit, so these are never freed.
    Database* m_database;
    ChatHistory* m_history;
};
