#include "ClientPanel.h"

#include <QCheckBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

QString showControlChars(const QString& text) {
    QString out;
    for (QChar c : text) {
        if (c == QLatin1Char('\r'))
            out += QStringLiteral("\\r");
        else if (c == QLatin1Char('\t'))
            out += QStringLiteral("\\t");
        else if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            out += QStringLiteral("\\x%1").arg(static_cast<uint>(c.unicode()), 2, 16, QLatin1Char('0'));
        else
            out += c;
    }
    return out;
}

ClientPanel::ClientPanel(int number, QWidget* parent)
    : QGroupBox(QStringLiteral("Client %1").arg(number), parent) {
    m_nick = new QLineEdit(QStringLiteral("user%1").arg(number));
    m_nick->setPlaceholderText(QStringLiteral("nickname"));
    auto* nickButton = new QPushButton(QStringLiteral("Set nick"));
    m_room = new QLineEdit(QStringLiteral("lobby"));
    m_room->setPlaceholderText(QStringLiteral("room"));
    auto* joinButton = new QPushButton(QStringLiteral("Join"));
    m_connectButton = new QToolButton;
    auto* removeButton = new QToolButton;
    removeButton->setText(QStringLiteral("Remove"));

    m_nick->setMinimumWidth(90);
    m_room->setMinimumWidth(90);
    auto* top = new QHBoxLayout;
    top->addWidget(m_nick, 1);
    top->addWidget(nickButton);
    top->addSpacing(8);
    top->addWidget(m_room, 1);
    top->addWidget(joinButton);

    m_transcript = new QPlainTextEdit;
    m_transcript->setReadOnly(true);
    m_transcript->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_transcript->setMinimumHeight(160);
    setMinimumWidth(440);

    auto* quick = new QHBoxLayout;
    for (const char* cmd : {"/who", "/rooms", "/history", "/help", "/quit"}) {
        auto* b = new QToolButton;
        b->setText(QString::fromLatin1(cmd));
        connect(b, &QToolButton::clicked, this, [this, cmd] { sendLine(QString::fromLatin1(cmd)); });
        quick->addWidget(b);
    }
    auto* clear = new QToolButton;
    clear->setText(QStringLiteral("Clear"));
    connect(clear, &QToolButton::clicked, m_transcript, &QPlainTextEdit::clear);
    quick->addStretch();
    quick->addWidget(clear);
    quick->addWidget(m_connectButton);
    quick->addWidget(removeButton);

    m_input = new QLineEdit;
    m_input->setPlaceholderText(QStringLiteral("message or /command — Enter to send"));
    m_appendNewline = new QCheckBox(QStringLiteral("Add \\n"));
    m_appendNewline->setChecked(true);
    m_appendNewline->setToolTip(QStringLiteral(
        "The server only handles a message once it sees a newline.\n"
        "Untick to send partial data and test message framing."));
    auto* sendButton = new QPushButton(QStringLiteral("Send"));
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(m_input, 1);
    bottom->addWidget(m_appendNewline);
    bottom->addWidget(sendButton);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_transcript, 1);
    layout->addLayout(quick);
    layout->addLayout(bottom);

    auto send = [this] {
        if (m_input->text().isEmpty() && m_appendNewline->isChecked())
            return;
        sendLine(m_input->text());
        m_input->clear();
    };
    connect(sendButton, &QPushButton::clicked, this, send);
    connect(m_input, &QLineEdit::returnPressed, this, send);
    connect(nickButton, &QPushButton::clicked, this, [this] { sendLine(QStringLiteral("/nick ") + m_nick->text()); });
    connect(m_nick, &QLineEdit::returnPressed, nickButton, &QPushButton::click);
    connect(joinButton, &QPushButton::clicked, this, [this] { sendLine(QStringLiteral("/join ") + m_room->text()); });
    connect(m_room, &QLineEdit::returnPressed, joinButton, &QPushButton::click);
    connect(removeButton, &QToolButton::clicked, this, [this] { emit removeRequested(this); });
    connect(m_connectButton, &QToolButton::clicked, this, [this] {
        if (m_socket.state() == QAbstractSocket::UnconnectedState)
            emit connectRequested(this);
        else
            disconnectFromServer();
    });

    connect(&m_socket, &QTcpSocket::readyRead, this, &ClientPanel::onReadyRead);
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        appendTranscript(QStringLiteral("●"), QStringLiteral("connected"), QStringLiteral("#43a047"));
        // Use the nickname field straight away so messages are easy to tell apart.
        if (!m_nick->text().trimmed().isEmpty())
            sendLine(QStringLiteral("/nick ") + m_nick->text().trimmed());
        updateState();
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        appendTranscript(QStringLiteral("●"), QStringLiteral("disconnected"), QStringLiteral("#e53935"));
        updateState();
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError err) {
        if (err != QAbstractSocket::RemoteHostClosedError)
            appendTranscript(QStringLiteral("!"), m_socket.errorString(), QStringLiteral("#e53935"));
        updateState();
    });
    connect(&m_socket, &QTcpSocket::stateChanged, this, &ClientPanel::updateState);

    updateState();
}

void ClientPanel::connectTo(quint16 port) {
    m_readBuffer.clear();
    m_socket.connectToHost(QStringLiteral("127.0.0.1"), port);
}

void ClientPanel::disconnectFromServer() {
    m_socket.disconnectFromHost();
}

void ClientPanel::sendLine(const QString& text) {
    if (m_socket.state() != QAbstractSocket::ConnectedState) {
        appendTranscript(QStringLiteral("!"), QStringLiteral("not connected"), QStringLiteral("#e53935"));
        return;
    }
    QString data = text;
    if (m_appendNewline->isChecked())
        data += QLatin1Char('\n');
    m_socket.write(data.toUtf8());
    appendTranscript(QStringLiteral("→"), data, QStringLiteral("#42a5f5"));
}

void ClientPanel::onReadyRead() {
    m_readBuffer += QString::fromUtf8(m_socket.readAll());
    qsizetype nl;
    while ((nl = m_readBuffer.indexOf(QLatin1Char('\n'))) != -1) {
        QString line = m_readBuffer.left(nl);
        m_readBuffer.remove(0, nl + 1);
        appendTranscript(QStringLiteral("←"), line, QString());
    }
}

void ClientPanel::appendTranscript(const QString& prefix, const QString& text, const QString& color) {
    QString shown = text;
    if (prefix == QStringLiteral("→") && shown.endsWith(QLatin1Char('\n')))
        shown.chop(1);  // the newline is implied for sent lines
    else if (prefix == QStringLiteral("→"))
        shown += QStringLiteral(" (no newline)");
    // An empty color means "use the theme's normal text color".
    QString style = color.isEmpty() ? QString() : QStringLiteral(" style='color:%1'").arg(color);
    m_transcript->appendHtml(QStringLiteral("<span style='color:#9e9e9e'>%1</span> <span%2>%3 %4</span>")
                                 .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                      style, prefix, showControlChars(shown).toHtmlEscaped()));
}

void ClientPanel::updateState() {
    auto state = m_socket.state();
    if (state == QAbstractSocket::ConnectedState)
        m_connectButton->setText(QStringLiteral("Disconnect"));
    else if (state == QAbstractSocket::UnconnectedState)
        m_connectButton->setText(QStringLiteral("Connect"));
    else
        m_connectButton->setText(QStringLiteral("Cancel"));
}
