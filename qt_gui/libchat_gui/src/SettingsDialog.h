#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QSpinBox;
class QLabel;
class QTabWidget;
class ChatClient;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(ChatClient* client, QWidget* parent = nullptr);

signals:
    void connectRequested(const QString& host, quint16 port);
    void disconnectRequested();
    void nicknameChangeRequested(const QString& nick);
    void joinRoomRequested(const QString& room);

private slots:
    void onConnectClicked();
    void onApplyNickClicked();
    void onApplyRoomClicked();
    void updateConnectionUI();

private:
    void buildConnectionTab(QTabWidget* tabs);
    void buildProfileTab(QTabWidget* tabs);
    void buildRoomsTab(QTabWidget* tabs);

    ChatClient* m_client;

    // Connection tab
    QLineEdit* m_hostEdit;
    QSpinBox*  m_portSpin;
    QLabel*    m_statusLabel;

    // Profile tab
    QLineEdit* m_nickEdit;

    // Rooms tab
    QLineEdit* m_roomEdit;
};
