#pragma once

#include <QMainWindow>
#include <QList>

class ClientPanel;
class QCheckBox;
class QGridLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class ServerController;

// A LibChat server and any number of test clients in one window.
class DebugWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DebugWindow(QWidget* parent = nullptr);

    void startServer();
    void addClient();

private:
    void toggleServer();
    void removeClient(ClientPanel* panel);
    void relayout();
    void appendLog(const QString& message);
    void updateServerState();

    ServerController* m_server;
    QSpinBox* m_port;
    QCheckBox* m_allowRemote;
    QPushButton* m_serverButton;
    QLabel* m_status;
    QPlainTextEdit* m_log;
    QGridLayout* m_clientGrid;
    QList<ClientPanel*> m_clients;
    int m_nextClient = 1;
};
