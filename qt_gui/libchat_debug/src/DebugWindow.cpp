#include "DebugWindow.h"
#include "ClientPanel.h"
#include "ServerController.h"

#include <QCheckBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTime>
#include <QToolBar>
#include <QVBoxLayout>

DebugWindow::DebugWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("LibChat Debug"));
    resize(1400, 850);

    m_server = new ServerController(this);

    // ── Toolbar: server controls and "add client" ────────────────────
    auto* toolbar = addToolBar(QStringLiteral("Server"));
    toolbar->setMovable(false);
    toolbar->addWidget(new QLabel(QStringLiteral(" Port ")));
    m_port = new QSpinBox;
    m_port->setRange(0, 65535);
    m_port->setValue(8080);
    m_port->setToolTip(QStringLiteral("0 picks any free port"));
    toolbar->addWidget(m_port);
    m_allowRemote = new QCheckBox(QStringLiteral("Allow other machines"));
    m_allowRemote->setToolTip(QStringLiteral(
        "Listen on all network interfaces instead of only 127.0.0.1,\n"
        "so real clients on other computers can connect too."));
    toolbar->addWidget(m_allowRemote);
    m_serverButton = new QPushButton;
    toolbar->addWidget(m_serverButton);
    toolbar->addSeparator();
    auto* addButton = new QPushButton(QStringLiteral("+ Add client"));
    toolbar->addWidget(addButton);
    auto* connectAll = new QPushButton(QStringLiteral("Connect all"));
    toolbar->addWidget(connectAll);
    toolbar->addSeparator();
    m_status = new QLabel;
    toolbar->addWidget(m_status);

    // ── Left: server log ─────────────────────────────────────────────
    auto* logPane = new QWidget;
    auto* logLayout = new QVBoxLayout(logPane);
    auto* logHeader = new QHBoxLayout;
    logHeader->addWidget(new QLabel(QStringLiteral("<b>Server log</b>")));
    logHeader->addStretch();
    auto* clearLog = new QPushButton(QStringLiteral("Clear"));
    logHeader->addWidget(clearLog);
    logLayout->addLayout(logHeader);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setMaximumBlockCount(5000);
    logLayout->addWidget(m_log);

    // ── Right: client panels ─────────────────────────────────────────
    auto* clientsWidget = new QWidget;
    m_clientGrid = new QGridLayout(clientsWidget);
    m_clientGrid->setAlignment(Qt::AlignTop);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(clientsWidget);

    auto* splitter = new QSplitter;
    splitter->addWidget(logPane);
    splitter->addWidget(scroll);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({420, 980});
    setCentralWidget(splitter);

    connect(m_serverButton, &QPushButton::clicked, this, &DebugWindow::toggleServer);
    connect(addButton, &QPushButton::clicked, this, &DebugWindow::addClient);
    connect(connectAll, &QPushButton::clicked, this, [this] {
        if (!m_server->isRunning())
            return;
        for (auto* c : m_clients)
            c->connectTo(m_server->port());
    });
    connect(clearLog, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
    connect(m_server, &ServerController::logMessage, this, &DebugWindow::appendLog);
    connect(m_server, &ServerController::runningChanged, this, &DebugWindow::updateServerState);

    updateServerState();
}

void DebugWindow::startServer() {
    QString error;
    if (!m_server->start(static_cast<quint16>(m_port->value()), m_allowRemote->isChecked(), &error))
        appendLog(QStringLiteral("error: ") + error);
}

void DebugWindow::toggleServer() {
    if (m_server->isRunning())
        m_server->stop();
    else
        startServer();
}

void DebugWindow::addClient() {
    auto* panel = new ClientPanel(m_nextClient++);
    m_clients.append(panel);
    connect(panel, &ClientPanel::removeRequested, this, &DebugWindow::removeClient);
    connect(panel, &ClientPanel::connectRequested, this, [this](ClientPanel* p) {
        if (m_server->isRunning())
            p->connectTo(m_server->port());
        else
            appendLog(QStringLiteral("start the server before connecting a client"));
    });
    relayout();
    if (m_server->isRunning())
        panel->connectTo(m_server->port());
}

void DebugWindow::removeClient(ClientPanel* panel) {
    m_clients.removeOne(panel);
    panel->disconnectFromServer();
    panel->deleteLater();
    relayout();
}

void DebugWindow::relayout() {
    for (auto* c : m_clients)
        m_clientGrid->removeWidget(c);
    const int columns = 2;
    for (int i = 0; i < m_clients.size(); ++i)
        m_clientGrid->addWidget(m_clients[i], i / columns, i % columns);
}

void DebugWindow::appendLog(const QString& message) {
    m_log->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz  ")) +
                           showControlChars(message));
}

void DebugWindow::updateServerState() {
    bool running = m_server->isRunning();
    m_serverButton->setText(running ? QStringLiteral("Stop server") : QStringLiteral("Start server"));
    m_port->setEnabled(!running);
    m_allowRemote->setEnabled(!running);
    m_status->setText(running
        ? QStringLiteral("  ● running on port %1").arg(m_server->port())
        : QStringLiteral("  ○ stopped"));
}
