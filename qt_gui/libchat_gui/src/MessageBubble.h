#pragma once

#include <QWidget>
#include <QString>
#include <QTime>

class QFrame;

class MessageBubble : public QWidget {
    Q_OBJECT
public:
    explicit MessageBubble(const QString& sender, const QString& text,
                           bool isOwn, const QTime& time,
                           bool showSender, QWidget* parent = nullptr);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    QFrame* m_bubble{nullptr};
};

class SystemMessageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SystemMessageWidget(const QString& text, QWidget* parent = nullptr);
};

class DayDividerWidget : public QWidget {
    Q_OBJECT
public:
    explicit DayDividerWidget(const QString& label, QWidget* parent = nullptr);
};
