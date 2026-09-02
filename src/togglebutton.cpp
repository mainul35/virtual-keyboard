#include "togglebutton.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTouchEvent>

ToggleButton::ToggleButton(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(52, 52);
}

bool ToggleButton::event(QEvent *e)
{
    switch (e->type()) {
    case QEvent::TouchBegin:
        m_down = true;
        update();
        e->accept();
        return true;
    case QEvent::TouchEnd:
        if (m_down) {
            m_down = false;
            update();
            Q_EMIT clicked();
        }
        e->accept();
        return true;
    case QEvent::TouchCancel:
        m_down = false;
        update();
        e->accept();
        return true;
    default:
        return QWidget::event(e);
    }
}

void ToggleButton::mousePressEvent(QMouseEvent *e)
{
    m_down = true;
    update();
    e->accept();
}

void ToggleButton::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_down) {
        m_down = false;
        update();
        if (rect().contains(e->position().toPoint())) {
            Q_EMIT clicked();
        }
    }
    e->accept();
}

void ToggleButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = palette();
    QColor fill = m_down ? pal.color(QPalette::Highlight) : pal.color(QPalette::Button);
    fill.setAlphaF(0.92);
    p.setPen(QPen(pal.color(QPalette::Mid), 1));
    p.setBrush(fill);
    p.drawEllipse(rect().adjusted(2, 2, -2, -2));

    // Simple keyboard glyph: three rows of dashes.
    p.setPen(QPen(m_down ? pal.color(QPalette::HighlightedText) : pal.color(QPalette::ButtonText), 2.5, Qt::SolidLine, Qt::RoundCap));
    const int cx = width() / 2;
    const int cy = height() / 2;
    for (int row = -1; row <= 1; ++row) {
        const int y = cy + row * 7;
        const int half = row == 1 ? 8 : 12;
        for (int x = -half; x <= half; x += 6) {
            p.drawPoint(cx + x, y);
        }
    }
}
