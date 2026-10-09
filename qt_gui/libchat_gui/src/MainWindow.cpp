#include "MainWindow.h"
#include "ChatClient.h"
#include "MessageBubble.h"
#include "SettingsDialog.h"

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

static const QStringList NICK_PALETTE = {
    "#7cb8f0", "#81d47e", "#f5c26b", "#f08ba0",
    "#c5a0f5", "#7edcf5", "#f07ab0", "#82d4ab",
    "#f0a07a", "#a0d47e",
};

static QString colorForNick(const QString& nick) {
    quint32 h = 5381;
    for (QChar c : nick)
        h = ((h << 5) + h) ^ static_cast<quint32>(c.unicode());
    return NICK_PALETTE[h % static_cast<quint32>(NICK_PALETTE.size())];
}

// ─────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_client(new ChatClient(this))
{
    setWindowTitle(QStringLiteral("LibChat"));
    setMinimumSize(860, 580);
    resize(1080, 680);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralWidget"));
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setCentralWidget(central);

    buildSidebar();
    buildChatArea();
    buildMemberPanel();

    root->addWidget(m_sidebar);
    root->addWidget(m_chatArea, 1);
    root->addWidget(m_memberPanel);

    connectClientSignals();
    setConnectedState(false);

    statusBar()->setObjectName(QStringLiteral("mainStatusBar"));
    statusBar()->showMessage(tr("Not connected — open Settings to connect"));

    QSettings s;
    QString host = s.value(QStringLiteral("server/host")).toString();
    if (!host.isEmpty()) {
        quint16 port = static_cast<quint16>(
            s.value(QStringLiteral("server/port"), 8080).toInt());
        QTimer::singleShot(400, this, [this, host, port]() {
            m_client->connectToServer(host, port);
        });
    }
}

// ── Left sidebar ─────────────────────────────────────────────────────────────

void MainWindow::buildSidebar() {
    m_sidebar = new QWidget;
    m_sidebar->setObjectName(QStringLiteral("sidebar"));
    m_sidebar->setFixedWidth(260);

    auto* lay = new QVBoxLayout(m_sidebar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ── App header ────────────────────────────────────────────────────────────
    auto* header = new QFrame;
    header->setObjectName(QStringLiteral("sidebarHeader"));
    header->setFixedHeight(56);
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(16, 0, 12, 0);

    m_connDotLabel = new QLabel(QStringLiteral("●"));
    m_connDotLabel->setObjectName(QStringLiteral("connDotOff"));
    m_connDotLabel->setFixedWidth(14);

    auto* appName = new QLabel(QStringLiteral("LibChat"));
    appName->setObjectName(QStringLiteral("appNameLabel"));

    auto* settingsBtn = new QPushButton(QStringLiteral("⚙"));
    settingsBtn->setObjectName(QStringLiteral("iconButton"));
    settingsBtn->setToolTip(tr("Settings"));
    settingsBtn->setFixedSize(32, 32);
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettings);

    hl->addWidget(m_connDotLabel);
    hl->addSpacing(6);
    hl->addWidget(appName, 1);
    hl->addWidget(settingsBtn);
    lay->addWidget(header);

    // ── Search bar (decorative) ───────────────────────────────────────────────
    auto* searchFrame = new QFrame;
    searchFrame->setObjectName(QStringLiteral("searchFrame"));
    auto* sl = new QHBoxLayout(searchFrame);
    sl->setContentsMargins(12, 8, 12, 8);

    auto* searchEdit = new QLineEdit;
    searchEdit->setPlaceholderText(tr("🔍  Search rooms…"));
    searchEdit->setObjectName(QStringLiteral("searchInput"));
    searchEdit->setReadOnly(true);
    connect(searchEdit, &QLineEdit::textChanged, this, [this, searchEdit](const QString&) {
        // Filter room list
        QString q = searchEdit->text().toLower();
        for (int i = 0; i < m_roomList->count(); ++i) {
            auto* item = m_roomList->item(i);
            item->setHidden(!item->text().toLower().contains(q));
        }
    });
    searchEdit->setReadOnly(false);
    sl->addWidget(searchEdit);
    lay->addWidget(searchFrame);

    // ── Room list label ───────────────────────────────────────────────────────
    auto* roomsHdr = new QHBoxLayout;
    roomsHdr->setContentsMargins(16, 8, 12, 4);

    auto* roomsLbl = new QLabel(tr("ROOMS"));
    roomsLbl->setObjectName(QStringLiteral("sectionLabel"));

    auto* addBtn = new QPushButton(QStringLiteral("+"));
    addBtn->setObjectName(QStringLiteral("iconButton"));
    addBtn->setFixedSize(22, 22);
    addBtn->setToolTip(tr("Join Room"));
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        bool ok;
        QString r = QInputDialog::getText(
            this, tr("Join Room"), tr("Room name:"),
            QLineEdit::Normal, QString(), &ok);
        if (ok && !r.trimmed().isEmpty())
            m_client->joinRoom(r.trimmed());
    });

    roomsHdr->addWidget(roomsLbl, 1);
    roomsHdr->addWidget(addBtn);

    auto* roomsHdrW = new QWidget;
    roomsHdrW->setLayout(roomsHdr);
    lay->addWidget(roomsHdrW);

    // ── Room list ─────────────────────────────────────────────────────────────
    m_roomList = new QListWidget;
    m_roomList->setObjectName(QStringLiteral("roomList"));
    m_roomList->setFocusPolicy(Qt::NoFocus);
    m_roomList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // One click switches rooms, like channels in Discord.
    connect(m_roomList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) {
                QString room = item->data(Qt::UserRole).toString();
                if (m_client->isConnected() && room != m_client->currentRoom())
                    m_client->joinRoom(room);
            });
    addRoomItem(QStringLiteral("lobby"));
    lay->addWidget(m_roomList, 1);

    // ── Refresh button ────────────────────────────────────────────────────────
    auto* refreshBtn = new QPushButton(tr("↻  Refresh rooms"));
    refreshBtn->setObjectName(QStringLiteral("sidebarActionButton"));
    connect(refreshBtn, &QPushButton::clicked, m_client, &ChatClient::requestRooms);
    lay->addWidget(refreshBtn);

    // ── Bottom user strip ─────────────────────────────────────────────────────
    auto* strip = new QFrame;
    strip->setObjectName(QStringLiteral("userStrip"));
    strip->setFixedHeight(60);
    auto* stl = new QHBoxLayout(strip);
    stl->setContentsMargins(12, 0, 12, 0);
    stl->setSpacing(8);

    auto* avatar = new QLabel(QStringLiteral(""));
    avatar->setObjectName(QStringLiteral("avatarLabel"));
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(36, 36);
    avatar->setText(QStringLiteral("?"));

    auto* nc = new QVBoxLayout;
    nc->setSpacing(1);
    m_selfNickLabel = new QLabel(tr("(no nickname)"));
    m_selfNickLabel->setObjectName(QStringLiteral("selfNickLabel"));
    m_selfRoomLabel = new QLabel(QStringLiteral("# lobby"));
    m_selfRoomLabel->setObjectName(QStringLiteral("selfRoomLabel"));
    nc->addWidget(m_selfNickLabel);
    nc->addWidget(m_selfRoomLabel);

    auto* gearBtn = new QPushButton(QStringLiteral("⚙"));
    gearBtn->setObjectName(QStringLiteral("iconButton"));
    gearBtn->setFixedSize(28, 28);
    connect(gearBtn, &QPushButton::clicked, this, &MainWindow::openSettings);

    stl->addWidget(avatar);
    stl->addLayout(nc, 1);
    stl->addWidget(gearBtn);
    lay->addWidget(strip);
}

// ── Chat area ────────────────────────────────────────────────────────────────

void MainWindow::buildChatArea() {
    m_chatArea = new QWidget;
    m_chatArea->setObjectName(QStringLiteral("chatArea"));

    auto* lay = new QVBoxLayout(m_chatArea);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ── Chat header ───────────────────────────────────────────────────────────
    auto* header = new QFrame;
    header->setObjectName(QStringLiteral("chatHeader"));
    header->setFixedHeight(56);
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(16, 0, 12, 0);
    hl->setSpacing(8);

    auto* hashLabel = new QLabel(QStringLiteral("#"));
    hashLabel->setObjectName(QStringLiteral("hashLabel"));

    m_headerRoomLabel = new QLabel(QStringLiteral("lobby"));
    m_headerRoomLabel->setObjectName(QStringLiteral("headerRoomLabel"));

    m_headerSubLabel = new QLabel(tr("Not connected"));
    m_headerSubLabel->setObjectName(QStringLiteral("headerSubLabel"));

    auto* membersBtn = new QPushButton(tr("👥"));
    membersBtn->setObjectName(QStringLiteral("headerIconButton"));
    membersBtn->setToolTip(tr("Toggle member list"));
    membersBtn->setFixedSize(36, 36);
    connect(membersBtn, &QPushButton::clicked, this, [this]() {
        m_memberPanel->setVisible(!m_memberPanel->isVisible());
        if (m_memberPanel->isVisible())
            m_client->requestWho();
    });

    auto* helpBtn = new QPushButton(QStringLiteral("?"));
    helpBtn->setObjectName(QStringLiteral("headerIconButton"));
    helpBtn->setToolTip(tr("Help"));
    helpBtn->setFixedSize(36, 36);
    connect(helpBtn, &QPushButton::clicked, m_client, &ChatClient::requestHelp);

    hl->addWidget(hashLabel);
    hl->addWidget(m_headerRoomLabel);
    hl->addSpacing(8);
    hl->addWidget(m_headerSubLabel, 1);
    hl->addWidget(membersBtn);
    hl->addWidget(helpBtn);
    lay->addWidget(header);

    // ── Scroll area with message bubbles ──────────────────────────────────────
    m_chatScroll = new QScrollArea;
    m_chatScroll->setObjectName(QStringLiteral("chatScroll"));
    m_chatScroll->setWidgetResizable(true);
    m_chatScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chatScroll->setFrameShape(QFrame::NoFrame);

    m_chatContent = new QWidget;
    m_chatContent->setObjectName(QStringLiteral("chatContent"));
    m_chatContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_chatLayout = new QVBoxLayout(m_chatContent);
    m_chatLayout->setContentsMargins(0, 8, 0, 8);
    m_chatLayout->setSpacing(0);
    // Stretch at top: pushes messages to bottom when fewer than viewport height
    m_chatLayout->addStretch(1);

    m_chatScroll->setWidget(m_chatContent);
    lay->addWidget(m_chatScroll, 1);

    // ── Input bar ─────────────────────────────────────────────────────────────
    auto* inputArea = new QFrame;
    inputArea->setObjectName(QStringLiteral("inputArea"));
    auto* il = new QHBoxLayout(inputArea);
    il->setContentsMargins(12, 10, 12, 10);
    il->setSpacing(8);

    // Rounded input pill
    auto* inputFrame = new QFrame;
    inputFrame->setObjectName(QStringLiteral("inputFrame"));
    auto* ifl = new QHBoxLayout(inputFrame);
    ifl->setContentsMargins(4, 0, 4, 0);
    ifl->setSpacing(0);

    m_messageInput = new QLineEdit;
    m_messageInput->setObjectName(QStringLiteral("messageInput"));
    m_messageInput->setMaxLength(1000);
    m_messageInput->setPlaceholderText(tr("Message #lobby"));
    connect(m_messageInput, &QLineEdit::returnPressed,
            this, &MainWindow::onInputReturnPressed);
    ifl->addWidget(m_messageInput, 1);

    // Send button (circle)
    m_sendButton = new QPushButton(QStringLiteral("➤"));
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_sendButton->setFixedSize(40, 40);
    connect(m_sendButton, &QPushButton::clicked,
            this, &MainWindow::onSendClicked);

    il->addWidget(inputFrame, 1);
    il->addWidget(m_sendButton);
    lay->addWidget(inputArea);
}

// ── Member panel ─────────────────────────────────────────────────────────────

void MainWindow::buildMemberPanel() {
    m_memberPanel = new QWidget;
    m_memberPanel->setObjectName(QStringLiteral("memberPanel"));
    m_memberPanel->setFixedWidth(200);
    m_memberPanel->hide(); // Hidden by default (Signal-style)

    auto* lay = new QVBoxLayout(m_memberPanel);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* hdr = new QFrame;
    hdr->setObjectName(QStringLiteral("memberPanelHeader"));
    hdr->setFixedHeight(56);
    auto* hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(16, 0, 16, 0);

    m_memberCountLabel = new QLabel(tr("MEMBERS"));
    m_memberCountLabel->setObjectName(QStringLiteral("sectionLabel"));
    hl->addWidget(m_memberCountLabel);
    lay->addWidget(hdr);

    m_memberList = new QListWidget;
    m_memberList->setObjectName(QStringLiteral("memberList"));
    m_memberList->setFocusPolicy(Qt::NoFocus);
    m_memberList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    lay->addWidget(m_memberList, 1);
}

// ── Signal wiring ─────────────────────────────────────────────────────────────

void MainWindow::connectClientSignals() {
    connect(m_client, &ChatClient::connectedToServer,
            this, &MainWindow::onConnected);
    connect(m_client, &ChatClient::disconnectedFromServer,
            this, &MainWindow::onDisconnected);
    connect(m_client, &ChatClient::chatMessage,
            this, &MainWindow::onChatMessage);
    connect(m_client, &ChatClient::systemMessage,
            this, &MainWindow::onSystemMessage);
    connect(m_client, &ChatClient::userListUpdated,
            this, &MainWindow::onUserListUpdated);
    connect(m_client, &ChatClient::roomListUpdated,
            this, &MainWindow::onRoomListUpdated);
    connect(m_client, &ChatClient::nicknameChanged,
            this, &MainWindow::onNicknameChanged);
    connect(m_client, &ChatClient::roomChanged,
            this, &MainWindow::onRoomChanged);
    connect(m_client, &ChatClient::connectionError,
            this, &MainWindow::onConnectionError);
    connect(m_client, &ChatClient::historyStarted,
            this, &MainWindow::onHistoryStarted);
    connect(m_client, &ChatClient::historyMessage,
            this, &MainWindow::onHistoryMessage);
    connect(m_client, &ChatClient::historyFinished,
            this, [this](const QString&) { scrollChatToBottom(); });
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void MainWindow::onConnected() {
    setConnectedState(true);
    updateHeader();
    updateStatusBar();
    appendSystem(tr("Connected to %1:%2")
                     .arg(m_client->serverHost())
                     .arg(m_client->serverPort()));

    // Prompt for a nickname so the server never sees "IP:port" as the label,
    // then load the room's history (after the prompt, so our own past
    // messages are recognised). Deferred so the connection signals finish first.
    QTimer::singleShot(150, this, [this]() {
        promptForNickname();
        m_client->requestHistory();
    });

    QTimer::singleShot(800, m_client, &ChatClient::requestRooms);
    QTimer::singleShot(1000, m_client, &ChatClient::requestWho);
}

void MainWindow::onDisconnected() {
    setConnectedState(false);
    updateHeader();
    updateStatusBar();
    appendSystem(tr("Disconnected from server."));
}

void MainWindow::onChatMessage(const QString& sender,
                                const QString& text, bool isOwn) {
    appendBubble(sender, text, isOwn);
}

void MainWindow::onSystemMessage(const QString& text) {
    appendSystem(text);
}

void MainWindow::onUserListUpdated(const QStringList& users) {
    m_memberList->clear();
    for (const QString& u : users) {
        auto* item = new QListWidgetItem(m_memberList);
        item->setText(u);
        item->setForeground(QColor(colorForNick(u)));
    }
    m_memberCountLabel->setText(tr("MEMBERS — %1").arg(users.size()));
    updateHeader();
}

void MainWindow::onRoomListUpdated(const QStringList& rooms) {
    QString cur = m_client->currentRoom();
    m_roomList->clear();
    for (const QString& r : rooms)
        addRoomItem(r);
    for (int i = 0; i < m_roomList->count(); ++i) {
        if (m_roomList->item(i)->data(Qt::UserRole).toString() == cur) {
            m_roomList->setCurrentRow(i);
            break;
        }
    }
}

void MainWindow::onNicknameChanged(const QString& nick) {
    m_selfNickLabel->setText(nick);
    if (auto* av = m_sidebar->findChild<QLabel*>(QStringLiteral("avatarLabel")))
        av->setText(nick.isEmpty() ? QStringLiteral("?")
                                   : nick.at(0).toUpper());
    // Persist so the next session can pre-fill the dialog
    QSettings().setValue(QStringLiteral("profile/nickname"), nick);
    appendSystem(tr("Nickname set to '%1'.").arg(nick));
    updateStatusBar();
}

void MainWindow::onRoomChanged(const QString& room) {
    m_selfRoomLabel->setText(QLatin1Char('#') + room);
    m_messageInput->setPlaceholderText(tr("Message #%1").arg(room));
    m_headerRoomLabel->setText(room);
    updateStatusBar();

    for (int i = 0; i < m_roomList->count(); ++i) {
        if (m_roomList->item(i)->data(Qt::UserRole).toString() == room) {
            m_roomList->setCurrentRow(i);
            return;
        }
    }
    addRoomItem(room);
    m_roomList->setCurrentRow(m_roomList->count() - 1);
}

void MainWindow::onHistoryStarted(const QString& room, bool more) {
    // Everything on screen belongs to the previous room (or a stale view),
    // so start over with just this room's messages.
    clearChat();
    // The server keeps messages for 24 hours (ChatHistory's default retention).
    appendSystem(more ? tr("Showing the most recent messages in #%1.").arg(room)
                      : tr("#%1 — messages from the last 24 hours.").arg(room));
}

void MainWindow::onHistoryMessage(const QString& sender, const QString& text,
                                  const QDateTime& sentAt, bool isOwn) {
    appendBubble(sender, text, isOwn, sentAt.toLocalTime());
}

void MainWindow::onConnectionError(const QString& error) {
    setConnectedState(false);
    appendSystem(tr("⚠ %1").arg(error));
    statusBar()->showMessage(tr("Error: %1").arg(error));
}

void MainWindow::onSendClicked() {
    QString text = m_messageInput->text().trimmed();
    if (text.isEmpty()) return;

    if (!m_client->isConnected()) {
        appendSystem(tr("Not connected — open Settings (⚙) to connect."));
        return;
    }

    m_messageInput->clear();

    if (text.startsWith(QLatin1Char('/'))) {
        if (text.startsWith(QLatin1String("/nick "))) {
            m_client->setNickname(text.mid(6).trimmed());
        } else if (text.startsWith(QLatin1String("/join "))) {
            m_client->joinRoom(text.mid(6).trimmed());
        } else if (text == QLatin1String("/who")) {
            m_client->requestWho();
        } else if (text == QLatin1String("/rooms")) {
            m_client->requestRooms();
        } else if (text == QLatin1String("/help")) {
            m_client->requestHelp();
        } else if (text == QLatin1String("/quit")) {
            m_client->disconnectFromServer();
        } else {
            m_client->sendChatMessage(text);
        }
    } else {
        // Show the message immediately in the local UI (optimistic display).
        // ChatClient will suppress the server echo so it doesn't appear twice.
        QString displayName = m_client->nickname().isEmpty()
                                  ? tr("You")
                                  : m_client->nickname();
        appendBubble(displayName, text, /*isOwn=*/true);
        m_client->sendChatMessage(text);
    }
}

void MainWindow::onInputReturnPressed() {
    onSendClicked();
}

void MainWindow::openSettings() {
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(m_client, this);
        connect(m_settingsDialog, &SettingsDialog::connectRequested,
                m_client, &ChatClient::connectToServer);
        connect(m_settingsDialog, &SettingsDialog::disconnectRequested,
                m_client, &ChatClient::disconnectFromServer);
        connect(m_settingsDialog, &SettingsDialog::nicknameChangeRequested,
                m_client, &ChatClient::setNickname);
        connect(m_settingsDialog, &SettingsDialog::joinRoomRequested,
                m_client, &ChatClient::joinRoom);
    }
    m_settingsDialog->exec();
}

// ── UI helpers ────────────────────────────────────────────────────────────────

void MainWindow::setConnectedState(bool on) {
    m_sendButton->setEnabled(on);
    m_messageInput->setEnabled(on);
    m_connDotLabel->setObjectName(on ? QStringLiteral("connDotOn")
                                     : QStringLiteral("connDotOff"));
    m_connDotLabel->style()->unpolish(m_connDotLabel);
    m_connDotLabel->style()->polish(m_connDotLabel);
}

void MainWindow::updateHeader() {
    QString room = m_client->currentRoom();
    m_headerRoomLabel->setText(room.isEmpty() ? QStringLiteral("lobby") : room);

    if (m_client->isConnected()) {
        m_headerSubLabel->setText(
            tr("%1:%2")
                .arg(m_client->serverHost())
                .arg(m_client->serverPort()));
    } else {
        m_headerSubLabel->setText(tr("Not connected"));
    }
}

void MainWindow::updateStatusBar() {
    if (m_client->isConnected()) {
        QString nick = m_client->nickname().isEmpty()
                           ? tr("(no nickname)")
                           : m_client->nickname();
        statusBar()->showMessage(
            tr("● Connected  ·  %1:%2  ·  %3  ·  #%4")
                .arg(m_client->serverHost())
                .arg(m_client->serverPort())
                .arg(nick, m_client->currentRoom()));
    } else {
        statusBar()->showMessage(tr("Not connected — open Settings (⚙) to connect"));
    }
}

void MainWindow::addRoomItem(const QString& room) {
    for (int i = 0; i < m_roomList->count(); ++i)
        if (m_roomList->item(i)->data(Qt::UserRole).toString() == room)
            return;

    auto* item = new QListWidgetItem(m_roomList);
    item->setText(QStringLiteral("# ") + room);
    item->setData(Qt::UserRole, room);
}

void MainWindow::scrollChatToBottom() {
    // Deferred so the new widget has been laid out first
    QTimer::singleShot(0, this, [this]() {
        m_chatScroll->verticalScrollBar()->setValue(
            m_chatScroll->verticalScrollBar()->maximum());
    });
}

// ── Message rendering ─────────────────────────────────────────────────────────

void MainWindow::appendBubble(const QString& sender, const QString& text,
                               bool isOwn, const QDateTime& sentAt) {
    QDate day = sentAt.date();

    if (day != m_lastMsgDate) {
        appendDayDivider(day);
        m_lastMsgDate = day;
        m_lastSender.clear();
    }

    // Always show the sender name — never suppress it between grouped messages.
    auto* bubble = new MessageBubble(
        sender, text, isOwn, sentAt.time(), /*showSender=*/true, m_chatContent);
    m_chatLayout->addWidget(bubble);

    m_lastSender  = sender;
    m_lastMsgTime = sentAt.time();

    scrollChatToBottom();
}

void MainWindow::appendSystem(const QString& text) {
    m_lastSender.clear();
    auto* w = new SystemMessageWidget(text, m_chatContent);
    m_chatLayout->addWidget(w);
    scrollChatToBottom();
}

void MainWindow::clearChat() {
    // Index 0 is the stretch that keeps messages at the bottom; keep it.
    while (m_chatLayout->count() > 1) {
        QLayoutItem* item = m_chatLayout->takeAt(1);
        if (QWidget* w = item->widget()) {
            w->hide();  // deleteLater only runs once control returns to the event loop
            w->deleteLater();
        }
        delete item;
    }
    m_lastSender.clear();
    m_lastMsgDate = QDate();
    m_lastMsgTime = QTime();
}

void MainWindow::appendDayDivider(const QDate& date) {
    QDate today = QDate::currentDate();
    QString label = date == today              ? tr("Today")
                  : date == today.addDays(-1)  ? tr("Yesterday")
                  : QLocale().toString(date, QLocale::LongFormat);
    auto* w = new DayDividerWidget(label, m_chatContent);
    m_chatLayout->addWidget(w);
}

void MainWindow::promptForNickname() {
    // Pre-fill with the last used nickname (saved in QSettings).
    QString saved = QSettings().value(QStringLiteral("profile/nickname")).toString();
    QString initial = m_client->nickname().isEmpty() ? saved : m_client->nickname();

    bool ok = false;
    QString nick = QInputDialog::getText(
        this,
        tr("Choose your nickname"),
        tr("Enter a nickname to appear as in chat.\n"
           "Allowed: letters, digits, space, . _ , @ # -\n"
           "Must not start or end with a space."),
        QLineEdit::Normal,
        initial,
        &ok);

    nick = nick.trimmed();
    if (ok && !nick.isEmpty()) {
        m_client->setNickname(nick);
    } else if (!ok) {
        // User cancelled — prompt again if still no nickname set
        // so the server doesn't use "IP:port" as their display name.
        if (m_client->nickname().isEmpty() && m_client->isConnected()) {
            appendSystem(tr("You have no nickname. "
                            "Use /nick <name> or open Settings → Profile."));
        }
    }
}
