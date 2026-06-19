#include "SettingsDialog.h"
#include "ChatClient.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(ChatClient* client, QWidget* parent)
    : QDialog(parent)
    , m_client(client)
{
    setWindowTitle(tr("LibChat Settings"));
    setMinimumWidth(400);
    setModal(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 16);
    root->setSpacing(0);

    auto* tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    root->addWidget(tabs);

    buildConnectionTab(tabs);
    buildProfileTab(tabs);
    buildRoomsTab(tabs);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    btnBox->setContentsMargins(16, 8, 16, 0);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(btnBox);

    updateConnectionUI();
}

void SettingsDialog::buildConnectionTab(QTabWidget* tabs) {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(20, 20, 20, 20);
    lay->setSpacing(16);

    auto* form = new QFormLayout;
    form->setSpacing(12);

    m_hostEdit = new QLineEdit;
    m_hostEdit->setPlaceholderText(tr("e.g. 127.0.0.1"));
    m_hostEdit->setObjectName(QStringLiteral("settingsInput"));

    QSettings s;
    m_hostEdit->setText(s.value(QStringLiteral("server/host"),
                                QStringLiteral("127.0.0.1")).toString());

    m_portSpin = new QSpinBox;
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(
        s.value(QStringLiteral("server/port"), 8080).toInt());
    m_portSpin->setObjectName(QStringLiteral("settingsInput"));

    form->addRow(tr("Host"), m_hostEdit);
    form->addRow(tr("Port"), m_portSpin);
    lay->addLayout(form);

    m_statusLabel = new QLabel;
    m_statusLabel->setObjectName(QStringLiteral("connectionStatus"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_statusLabel);

    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    auto* connectBtn = new QPushButton(tr("Connect"));
    connectBtn->setObjectName(QStringLiteral("primaryButton"));
    connect(connectBtn, &QPushButton::clicked, this, &SettingsDialog::onConnectClicked);
    btnRow->addWidget(connectBtn);

    auto* disconnectBtn = new QPushButton(tr("Disconnect"));
    disconnectBtn->setObjectName(QStringLiteral("dangerButton"));
    connect(disconnectBtn, &QPushButton::clicked, this, [this]() {
        emit disconnectRequested();
        updateConnectionUI();
    });
    btnRow->addWidget(disconnectBtn);
    lay->addLayout(btnRow);
    lay->addStretch();

    // Reflect connection state changes live
    connect(m_client, &ChatClient::connectedToServer,
            this, &SettingsDialog::updateConnectionUI);
    connect(m_client, &ChatClient::disconnectedFromServer,
            this, &SettingsDialog::updateConnectionUI);

    tabs->addTab(w, tr("Connection"));
}

void SettingsDialog::buildProfileTab(QTabWidget* tabs) {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(20, 20, 20, 20);
    lay->setSpacing(16);

    auto* form = new QFormLayout;
    form->setSpacing(12);

    m_nickEdit = new QLineEdit;
    m_nickEdit->setPlaceholderText(tr("Letters, digits, ._,@#- allowed"));
    m_nickEdit->setObjectName(QStringLiteral("settingsInput"));
    m_nickEdit->setText(m_client->nickname());
    m_nickEdit->setMaxLength(64);

    form->addRow(tr("Nickname"), m_nickEdit);
    lay->addLayout(form);

    auto* hint = new QLabel(
        tr("Valid characters: a–z A–Z 0–9 space . _ , @ # -\n"
           "Must not start or end with a space."));
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* applyBtn = new QPushButton(tr("Apply Nickname"));
    applyBtn->setObjectName(QStringLiteral("primaryButton"));
    connect(applyBtn, &QPushButton::clicked,
            this, &SettingsDialog::onApplyNickClicked);
    lay->addWidget(applyBtn);
    lay->addStretch();

    tabs->addTab(w, tr("Profile"));
}

void SettingsDialog::buildRoomsTab(QTabWidget* tabs) {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(20, 20, 20, 20);
    lay->setSpacing(16);

    auto* form = new QFormLayout;
    form->setSpacing(12);

    m_roomEdit = new QLineEdit;
    m_roomEdit->setPlaceholderText(tr("e.g. general"));
    m_roomEdit->setObjectName(QStringLiteral("settingsInput"));
    m_roomEdit->setText(m_client->currentRoom());

    form->addRow(tr("Room name"), m_roomEdit);
    lay->addLayout(form);

    auto* hint = new QLabel(
        tr("You are currently in: <b>%1</b>\n"
           "Enter a room name and press Join to switch rooms.")
            .arg(m_client->currentRoom()));
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    lay->addWidget(hint);

    auto* joinBtn = new QPushButton(tr("Join Room"));
    joinBtn->setObjectName(QStringLiteral("primaryButton"));
    connect(joinBtn, &QPushButton::clicked,
            this, &SettingsDialog::onApplyRoomClicked);
    lay->addWidget(joinBtn);
    lay->addStretch();

    tabs->addTab(w, tr("Rooms"));
}

void SettingsDialog::onConnectClicked() {
    QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) return;
    quint16 port = static_cast<quint16>(m_portSpin->value());

    QSettings s;
    s.setValue(QStringLiteral("server/host"), host);
    s.setValue(QStringLiteral("server/port"), port);

    emit connectRequested(host, port);
    updateConnectionUI();
}

void SettingsDialog::onApplyNickClicked() {
    QString nick = m_nickEdit->text().trimmed();
    if (!nick.isEmpty())
        emit nicknameChangeRequested(nick);
}

void SettingsDialog::onApplyRoomClicked() {
    QString room = m_roomEdit->text().trimmed();
    if (!room.isEmpty())
        emit joinRoomRequested(room);
}

void SettingsDialog::updateConnectionUI() {
    if (m_client->isConnected()) {
        m_statusLabel->setText(
            tr("● Connected to %1:%2")
                .arg(m_client->serverHost())
                .arg(m_client->serverPort()));
        m_statusLabel->setObjectName(QStringLiteral("connectedStatus"));
    } else {
        m_statusLabel->setText(tr("● Disconnected"));
        m_statusLabel->setObjectName(QStringLiteral("disconnectedStatus"));
    }
    // Force style refresh
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}
