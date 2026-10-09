#pragma once

#include <QGroupBox>
#include <QTcpSocket>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

// Returns text with control characters spelled out (\r, \t, \x00, ...) so
// framing and encoding problems are visible.
QString showControlChars(const QString& text);

// One test client: a raw TCP connection to the server with a transcript of
// everything sent and received, shown exactly (control characters visible).
class ClientPanel : public QGroupBox {
    Q_OBJECT
public:
    ClientPanel(int number, QWidget* parent = nullptr);

    void connectTo(quint16 port);
    void disconnectFromServer();

signals:
    void removeRequested(ClientPanel* panel);
    void connectRequested(ClientPanel* panel);

private:
    void sendLine(const QString& text);
    void appendTranscript(const QString& prefix, const QString& text, const QString& color);
    void updateState();
    void onReadyRead();

    QTcpSocket m_socket;
    QString m_readBuffer;

    QLineEdit* m_nick;
    QLineEdit* m_room;
    QLineEdit* m_input;
    QCheckBox* m_appendNewline;
    QToolButton* m_connectButton;
    QPlainTextEdit* m_transcript;
};
