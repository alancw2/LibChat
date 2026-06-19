#pragma once

#include <QDate>
#include <QMainWindow>
#include <QStringList>
#include <QTime>

class QScrollArea;
class QVBoxLayout;
class QLineEdit;
class QPushButton;
class QListWidget;
class QLabel;
class QFrame;

class ChatClient;
class SettingsDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onConnected();
    void onDisconnected();
    void onChatMessage(const QString& sender, const QString& text, bool isOwn);
    void onSystemMessage(const QString& text);
    void onUserListUpdated(const QStringList& users);
    void onRoomListUpdated(const QStringList& rooms);
    void onNicknameChanged(const QString& nick);
    void onRoomChanged(const QString& room);
    void onConnectionError(const QString& error);
    void onSendClicked();
    void onInputReturnPressed();
    void openSettings();
    void promptForNickname();

private:
    void buildSidebar();
    void buildChatArea();
    void buildMemberPanel();
    void connectClientSignals();
    void setConnectedState(bool connected);
    void updateHeader();
    void updateStatusBar();
    void addRoomItem(const QString& room);
    void scrollChatToBottom();

    void appendBubble(const QString& sender, const QString& text, bool isOwn);
    void appendSystem(const QString& text);
    void appendDayDivider(const QDate& date);

    ChatClient*      m_client;
    SettingsDialog*  m_settingsDialog{nullptr};

    // Sidebar
    QWidget*      m_sidebar;
    QLabel*       m_connDotLabel;
    QListWidget*  m_roomList;
    QLabel*       m_selfNickLabel;
    QLabel*       m_selfRoomLabel;

    // Chat
    QWidget*      m_chatArea;
    QLabel*       m_headerRoomLabel;
    QLabel*       m_headerSubLabel;
    QScrollArea*  m_chatScroll;
    QWidget*      m_chatContent;
    QVBoxLayout*  m_chatLayout;
    QLineEdit*    m_messageInput;
    QPushButton*  m_sendButton;

    // Member panel
    QWidget*      m_memberPanel;
    QLabel*       m_memberCountLabel;
    QListWidget*  m_memberList;

    // Grouped-message tracking
    QString m_lastSender;
    QTime   m_lastMsgTime;
    QDate   m_lastMsgDate;
};
