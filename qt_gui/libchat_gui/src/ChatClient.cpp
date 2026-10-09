#include "ChatClient.h"
#include <QHostAddress>
#include <QRegularExpression>

static const QRegularExpression NICK_RE(
    QStringLiteral(R"(^[^\s](?:[a-zA-Z0-9 ._,@#\-]*[^\s])?$)"));

ChatClient::ChatClient(QObject* parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_collectTimer(new QTimer(this))
{
    m_collectTimer->setSingleShot(true);
    m_collectTimer->setInterval(400);

    connect(m_socket, &QTcpSocket::connected, this, &ChatClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ChatClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &ChatClient::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &ChatClient::onSocketError);
    connect(m_collectTimer, &QTimer::timeout, this, &ChatClient::onCollectTimeout);
}

void ChatClient::connectToServer(const QString& host, quint16 port) {
    m_host = host;
    m_port = port;
    m_room = QStringLiteral("lobby");
    m_state = State::Normal;
    m_accumulator.clear();
    m_queuedLists.clear();
    m_readBuffer.clear();
    m_socket->connectToHost(host, port);
}

void ChatClient::disconnectFromServer() {
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        rawSend(QStringLiteral("/quit"));
        m_socket->disconnectFromHost();
    }
}

bool ChatClient::isConnected() const {
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void ChatClient::sendChatMessage(const QString& text) {
    if (!text.isEmpty() && isConnected()) {
        // Queue the text so processLine can suppress the server echo.
        // The GUI already displays the message locally on send.
        m_echoQueue.enqueue(text);
        rawSend(text);
    }
}

void ChatClient::setNickname(const QString& nick) {
    if (!NICK_RE.match(nick).hasMatch()) {
        emit systemMessage(QStringLiteral("⚠ Invalid nickname format."));
        return;
    }
    m_pendingNick = nick;
    rawSend(QStringLiteral("/nick ") + nick);
    // Server sends no confirmation, apply optimistically after short delay
    QTimer::singleShot(100, this, [this]() {
        if (!m_pendingNick.isEmpty()) {
            m_nickname = m_pendingNick;
            m_pendingNick.clear();
            emit nicknameChanged(m_nickname);
        }
    });
}

void ChatClient::joinRoom(const QString& room) {
    m_pendingRoom = room.trimmed();
    rawSend(QStringLiteral("/join ") + m_pendingRoom);
    // The server handles lines in order, so this returns the new room's history.
    requestHistory();
}

void ChatClient::requestHistory() {
    rawSend(QStringLiteral("/history"));
}

void ChatClient::requestWho() {
    startListRequest(State::WhoList);
}

void ChatClient::requestRooms() {
    startListRequest(State::RoomList);
}

// /who and /rooms replies are plain lists with no end marker, so only one can
// be collected at a time. Starting a second while the first is in flight used
// to throw the first reply away; now it waits its turn.
void ChatClient::startListRequest(State list) {
    if (m_state != State::Normal) {
        if (m_state != list && !m_queuedLists.contains(list))
            m_queuedLists.enqueue(list);
        return;
    }
    m_state = list;
    m_accumulator.clear();
    m_collectTimer->start();
    rawSend(list == State::WhoList ? QStringLiteral("/who") : QStringLiteral("/rooms"));
}

void ChatClient::sendNextQueuedList() {
    if (!m_queuedLists.isEmpty())
        startListRequest(m_queuedLists.dequeue());
}

void ChatClient::requestHelp() {
    rawSend(QStringLiteral("/help"));
}

void ChatClient::onConnected() {
    emit connectedToServer();
}

void ChatClient::onDisconnected() {
    m_collectTimer->stop();
    m_state = State::Normal;
    m_accumulator.clear();
    m_queuedLists.clear();
    emit disconnectedFromServer();
}

void ChatClient::onReadyRead() {
    m_readBuffer += QString::fromUtf8(m_socket->readAll());

    int nl;
    while ((nl = m_readBuffer.indexOf(QLatin1Char('\n'))) != -1) {
        QString line = m_readBuffer.left(nl).trimmed();
        m_readBuffer.remove(0, nl + 1);
        if (!line.isEmpty())
            processLine(line);
    }
}

void ChatClient::onSocketError(QAbstractSocket::SocketError /*error*/) {
    emit connectionError(m_socket->errorString());
}

void ChatClient::onCollectTimeout() {
    if (m_state == State::WhoList)
        flushWhoList();
    else if (m_state == State::RoomList)
        flushRoomList();
}

// Handles the server's reply to /history:
//   ~history-begin <more> <room>
//   ~history <unix seconds> <sender>: <body>
//   ~history-end <room>
// Returns false if the line isn't part of a history reply.
bool ChatClient::processHistoryLine(const QString& line) {
    if (line.startsWith(QLatin1String("~history-begin "))) {
        QString rest = line.mid(15);
        bool more = rest.startsWith(QLatin1Char('1'));
        QString room = rest.mid(2);
        // The history reply is the server confirming which room we're in.
        m_echoQueue.clear();
        if (room != m_room) {
            m_room = room;
            emit roomChanged(m_room);
        }
        if (room == m_pendingRoom)
            m_pendingRoom.clear();
        emit historyStarted(room, more);
        return true;
    }
    if (line.startsWith(QLatin1String("~history-end "))) {
        emit historyFinished(line.mid(13));
        return true;
    }
    if (line.startsWith(QLatin1String("~history "))) {
        QString rest = line.mid(9);
        qsizetype space = rest.indexOf(QLatin1Char(' '));
        qsizetype colon = rest.indexOf(QLatin1String(": "), space + 1);
        if (space <= 0 || colon <= space)
            return true;  // malformed; ignore it rather than show protocol text
        QDateTime sentAt = QDateTime::fromSecsSinceEpoch(rest.left(space).toLongLong());
        QString sender = rest.mid(space + 1, colon - space - 1);
        QString text = rest.mid(colon + 2);
        // The nickname may still be pending right after connecting.
        bool isOwn = (!m_nickname.isEmpty() && sender == m_nickname) ||
                     (!m_pendingNick.isEmpty() && sender == m_pendingNick);
        emit historyMessage(sender, text, sentAt, isOwn);
        return true;
    }
    return false;
}

void ChatClient::processLine(const QString& line) {
    if (line.startsWith(QLatin1Char('~'))) {
        // A history reply means any /who or /rooms listing has finished.
        if (m_state == State::WhoList) flushWhoList();
        else if (m_state == State::RoomList) flushRoomList();
        if (processHistoryLine(line))
            return;
    }

    // ── Room list: header triggers collection ──────────────────────────────
    if (line.startsWith(QLatin1String("active rooms:"))) {
        if (m_state == State::WhoList) flushWhoList();
        m_state = State::RoomList;
        m_accumulator.clear();
        m_collectTimer->start();
        return;
    }

    // ── Collecting room list ───────────────────────────────────────────────
    if (m_state == State::RoomList) {
        m_collectTimer->start(); // reset
        if (line.contains(QLatin1String(": ")) || line.contains(QLatin1String(" has "))) {
            flushRoomList();
            // fall through
        } else {
            m_accumulator.append(line.trimmed());
            return;
        }
    }

    // ── Collecting who list ────────────────────────────────────────────────
    if (m_state == State::WhoList) {
        m_collectTimer->start(); // reset
        if (line.contains(QLatin1String(": ")) ||
            line.contains(QLatin1String(" has ")) ||
            line.startsWith(QLatin1String("active rooms:")) ||
            line.startsWith(QLatin1String("available commands:"))) {
            flushWhoList();
            // fall through
        } else {
            m_accumulator.append(line);
            return;
        }
    }

    // ── Help text ──────────────────────────────────────────────────────────
    if (line.startsWith(QLatin1String("available commands:"))) {
        emit systemMessage(QStringLiteral("📋 ") + line);
        return;
    }
    // Help continuation lines start with "/"
    if (line.startsWith(QLatin1Char('/')) &&
        !line.startsWith(QLatin1String("/nick ")) &&
        !line.startsWith(QLatin1String("/join "))) {
        emit systemMessage(line);
        return;
    }

    // ── Room transition messages ───────────────────────────────────────────
    if (line.contains(QLatin1String(" has entered room "))) {
        // Detect our own room join
        if (!m_pendingRoom.isEmpty() &&
            line.endsWith(QLatin1String(" has entered room ") + m_pendingRoom)) {
            m_room = m_pendingRoom;
            m_pendingRoom.clear();
            emit roomChanged(m_room);
        }
        emit systemMessage(line);
        return;
    }
    if (line.contains(QLatin1String(" has moved to room "))) {
        emit systemMessage(line);
        return;
    }

    // ── Disconnect notices ─────────────────────────────────────────────────
    if (line.endsWith(QLatin1String(" disconnected")) ||
        line.contains(QLatin1String(" has disconnected"))) {
        emit systemMessage(line);
        return;
    }

    // ── Nick / validation errors ───────────────────────────────────────────
    if (line.startsWith(QLatin1String("Invalid")) ||
        line.startsWith(QLatin1String("Nickname"))) {
        // Server rejected nick; roll back
        m_pendingNick.clear();
        emit systemMessage(QStringLiteral("⚠ ") + line);
        return;
    }

    // ── Chat message  "sender: text" ──────────────────────────────────────
    int colon = line.indexOf(QLatin1String(": "));
    if (colon > 0 && colon <= 64) {
        QString sender = line.left(colon);
        QString text   = line.mid(colon + 2);
        bool isOwn = (!m_nickname.isEmpty() && sender == m_nickname);

        // Suppress the server echo for messages we sent ourselves.
        // The GUI shows them immediately on send so no duplicate is wanted.
        if (isOwn && !m_echoQueue.isEmpty() && m_echoQueue.head() == text) {
            m_echoQueue.dequeue();
            return;
        }

        emit chatMessage(sender, text, isOwn);
        return;
    }

    // ── Fallback ───────────────────────────────────────────────────────────
    emit systemMessage(line);
}

void ChatClient::flushWhoList() {
    m_collectTimer->stop();
    emit userListUpdated(m_accumulator);
    m_accumulator.clear();
    m_state = State::Normal;
    sendNextQueuedList();
}

void ChatClient::flushRoomList() {
    m_collectTimer->stop();
    emit roomListUpdated(m_accumulator);
    m_accumulator.clear();
    m_state = State::Normal;
    sendNextQueuedList();
}

void ChatClient::rawSend(const QString& text) {
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write((text + QLatin1Char('\n')).toUtf8());
}
