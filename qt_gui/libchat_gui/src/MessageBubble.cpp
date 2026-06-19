#include "MessageBubble.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSizePolicy>
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

// ── MessageBubble ────────────────────────────────────────────────────────────

MessageBubble::MessageBubble(const QString& sender, const QString& text,
                             bool isOwn, const QTime& time,
                             bool showSender, QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(12, 2, 12, 2);
    outer->setSpacing(0);

    m_bubble = new QFrame;
    m_bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // Inline stylesheet gives us full control over the bubble appearance
    if (isOwn) {
        m_bubble->setStyleSheet(
            "QFrame {"
            "  background-color: #2c6bed;"
            "  border-radius: 16px;"
            "  border-bottom-right-radius: 4px;"
            "}"
            "QLabel { background: transparent; color: #ffffff; }");
    } else {
        m_bubble->setStyleSheet(
            "QFrame {"
            "  background-color: #2d2f31;"
            "  border-radius: 16px;"
            "  border-bottom-left-radius: 4px;"
            "}"
            "QLabel { background: transparent; color: #e8e8e8; }");
    }

    auto* bl = new QVBoxLayout(m_bubble);
    bl->setContentsMargins(12, 8, 12, 8);
    bl->setSpacing(2);

    // Sender name — only for others, only when not grouped
    if (!isOwn && showSender && !sender.isEmpty()) {
        auto* snl = new QLabel(sender);
        snl->setStyleSheet(
            QString("QLabel { color: %1; font-size: 12px; font-weight: 700; "
                    "background: transparent; }")
                .arg(colorForNick(sender)));
        bl->addWidget(snl);
    }

    // Message text
    auto* tl = new QLabel(text);
    tl->setWordWrap(true);
    tl->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                Qt::TextSelectableByKeyboard);
    tl->setStyleSheet("QLabel { font-size: 15px; background: transparent; "
                      "line-height: 140%; }");
    tl->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    bl->addWidget(tl);

    // Timestamp row
    auto* foot = new QHBoxLayout;
    foot->setContentsMargins(0, 0, 0, 0);
    foot->setSpacing(0);
    foot->addStretch();

    auto* tsl = new QLabel(time.toString(QStringLiteral("hh:mm")));
    tsl->setStyleSheet("QLabel { font-size: 11px; "
                       "color: rgba(255,255,255,0.5); "
                       "background: transparent; }");
    foot->addWidget(tsl);
    bl->addLayout(foot);

    if (isOwn) {
        outer->addStretch();
        outer->addWidget(m_bubble);
    } else {
        outer->addWidget(m_bubble);
        outer->addStretch();
    }
}

void MessageBubble::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_bubble) {
        // Limit bubble width to 68 % of available width
        int maxW = static_cast<int>(width() * 0.68);
        m_bubble->setMaximumWidth(qMax(180, maxW));
    }
}

// ── SystemMessageWidget ──────────────────────────────────────────────────────

SystemMessageWidget::SystemMessageWidget(const QString& text, QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(24, 3, 24, 3);
    lay->addStretch();

    auto* lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet(
        "QLabel {"
        "  color: #6e6e73;"
        "  font-size: 12px;"
        "  font-style: italic;"
        "  background: transparent;"
        "}");
    lay->addWidget(lbl);
    lay->addStretch();
}

// ── DayDividerWidget ─────────────────────────────────────────────────────────

DayDividerWidget::DayDividerWidget(const QString& label, QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(20, 12, 20, 8);
    lay->setSpacing(10);

    auto makeLine = []() {
        auto* l = new QFrame;
        l->setFrameShape(QFrame::HLine);
        l->setStyleSheet("QFrame { color: rgba(255,255,255,0.08); }");
        return l;
    };

    auto* pill = new QLabel(label);
    pill->setAlignment(Qt::AlignCenter);
    pill->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    pill->setStyleSheet(
        "QLabel {"
        "  color: #6e6e73;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "  background: #1e2024;"
        "  border-radius: 10px;"
        "  padding: 2px 12px;"
        "}");

    lay->addWidget(makeLine(), 1);
    lay->addWidget(pill);
    lay->addWidget(makeLine(), 1);
}
