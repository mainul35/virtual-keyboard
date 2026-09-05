#include "selectionoverlay.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTouchEvent>

SelectionOverlay::SelectionOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
}

QPointF SelectionOverlay::normalise(const QPointF &local) const
{
    const QPointF onScreen = local + QPointF(x(), y());
    const qreal w = m_screen.width() > 0 ? m_screen.width() : width();
    const qreal h = m_screen.height() > 0 ? m_screen.height() : height();
    return QPointF(onScreen.x() / w, onScreen.y() / h);
}

bool SelectionOverlay::event(QEvent *e)
{
    switch (e->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        auto *te = static_cast<QTouchEvent *>(e);
        for (const QEventPoint &pt : te->points()) {
            if (pt.state() == QEventPoint::Pressed && m_touchId < 0) {
                m_touchId = pt.id();
                m_active = true;
                Q_EMIT gestureBegan(normalise(pt.position()));
            } else if (pt.id() == m_touchId) {
                if (pt.state() == QEventPoint::Updated) {
                    Q_EMIT gestureMoved(normalise(pt.position()));
                } else if (pt.state() == QEventPoint::Released) {
                    m_touchId = -1;
                    m_active = false;
                    Q_EMIT gestureEnded();
                }
            }
        }
        if (e->type() == QEvent::TouchCancel && m_active) {
            m_touchId = -1;
            m_active = false;
            Q_EMIT gestureEnded();
        }
        e->accept();
        return true;
    }
    default:
        return QWidget::event(e);
    }
}

void SelectionOverlay::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && !m_active) {
        m_active = true;
        Q_EMIT gestureBegan(normalise(e->position()));
    }
    e->accept();
}

void SelectionOverlay::mouseMoveEvent(QMouseEvent *e)
{
    if (m_active && m_touchId < 0) {
        Q_EMIT gestureMoved(normalise(e->position()));
    }
    e->accept();
}

void SelectionOverlay::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_active && m_touchId < 0) {
        m_active = false;
        Q_EMIT gestureEnded();
    }
    e->accept();
}

void SelectionOverlay::paintEvent(QPaintEvent *)
{
    // Almost invisible tint plus a thin frame so the mode is recognisable.
    QPainter p(this);
    QColor tint = palette().color(QPalette::Highlight);
    tint.setAlphaF(0.04);
    p.fillRect(rect(), tint);
    QColor frame = palette().color(QPalette::Highlight);
    frame.setAlphaF(0.8);
    p.setPen(QPen(frame, 4));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(2, 2, -2, -2));
}
