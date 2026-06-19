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
}

void ChatClient::requestWho() {
    if (m_state == State::WhoList) return;
    m_state = State::WhoList;
    m_accumulator.clear();
    m_collectTimer->start();
    rawSend(QStringLiteral("/who"));
}

void ChatClient::requestRooms() {
    if (m_state == State::RoomList) return;
    m_state = State::RoomList;
    m_accumulator.clear();
    rawSend(QStringLiteral("/rooms"));
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

void ChatClient::processLine(const QString& line) {
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
}

void ChatClient::flushRoomList() {
    m_collectTimer->stop();
    emit roomListUpdated(m_accumulator);
    m_accumulator.clear();
    m_state = State::Normal;
}

void ChatClient::rawSend(const QString& text) {
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write((text + QLatin1Char('\n')).toUtf8());
}
