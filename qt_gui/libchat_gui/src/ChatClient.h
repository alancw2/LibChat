#pragma once

#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

class ChatClient : public QObject {
    Q_OBJECT

public:
    explicit ChatClient(QObject* parent = nullptr);

    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();
    void sendChatMessage(const QString& text);
    void setNickname(const QString& nick);
    void joinRoom(const QString& room);
    void requestWho();
    void requestRooms();
    void requestHelp();

    bool isConnected() const;
    QString nickname() const { return m_nickname; }
    QString currentRoom() const { return m_room; }
    QString serverHost() const { return m_host; }
    quint16 serverPort() const { return m_port; }

signals:
    void connectedToServer();
    void disconnectedFromServer();
    void chatMessage(const QString& sender, const QString& text, bool isOwn);
    void systemMessage(const QString& text);
    void userListUpdated(const QStringList& users);
    void roomListUpdated(const QStringList& rooms);
    void nicknameChanged(const QString& nick);
    void roomChanged(const QString& room);
    void connectionError(const QString& error);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onCollectTimeout();

private:
    enum class State { Normal, WhoList, RoomList };

    void processLine(const QString& line);
    void flushWhoList();
    void flushRoomList();
    void rawSend(const QString& text);

    QTcpSocket* m_socket;
    QTimer* m_collectTimer;
    QString m_readBuffer;
    QString m_nickname;
    QString m_room{"lobby"};
    QString m_host;
    quint16 m_port{8080};
    QString m_pendingNick;
    QString m_pendingRoom;
    State m_state{State::Normal};
    QStringList m_accumulator;

    // FIFO of texts we sent ourselves; used to suppress the server echo
    // so the GUI can show messages immediately without a duplicate.
    QQueue<QString> m_echoQueue;
};
