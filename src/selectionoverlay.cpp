#include "selectionoverlay.h"

#include <QDebug>
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

void SelectionOverlay::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    qInfo() << "vkbd: selection overlay shown, size" << size() << "screen" << m_screen;
    m_localPath.clear();
    m_active = false;
    m_touchId = -1;
}

void SelectionOverlay::begin(const QPointF &local)
{
    qInfo() << "vkbd: selection gesture began at" << local << "overlay" << geometry();
    m_active = true;
    m_localPath.clear();
    m_localPath.append(local);
    update();
}

void SelectionOverlay::move(const QPointF &local)
{
    if (!m_active) {
        return;
    }
    if (m_localPath.isEmpty() || (m_localPath.last() - local).manhattanLength() >= 2.0) {
        m_localPath.append(local);
        update();
    }
}

void SelectionOverlay::finish()
{
    if (!m_active) {
        return;
    }
    m_active = false;
    QVector<QPointF> path;
    path.reserve(m_localPath.size());
    for (const QPointF &p : std::as_const(m_localPath)) {
        path.append(normalise(p));
    }
    m_localPath.clear();
    update();
    qInfo() << "vkbd: selection gesture ended," << path.size() << "points, from" << (path.isEmpty() ? QPointF() : path.first()) << "to" << (path.isEmpty() ? QPointF() : path.last());
    if (m_onGesture) {
        m_onGesture(path);
    }
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
                begin(pt.position());
            } else if (pt.id() == m_touchId) {
                if (pt.state() == QEventPoint::Updated) {
                    move(pt.position());
                } else if (pt.state() == QEventPoint::Released) {
                    m_touchId = -1;
                    finish();
                }
            }
        }
        if (e->type() == QEvent::TouchCancel && m_active) {
            m_touchId = -1;
            finish();
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
        begin(e->position());
    }
    e->accept();
}

void SelectionOverlay::mouseMoveEvent(QMouseEvent *e)
{
    if (m_touchId < 0) {
        move(e->position());
    }
    e->accept();
}

void SelectionOverlay::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_touchId < 0) {
        finish();
    }
    e->accept();
}

void SelectionOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor hl = palette().color(QPalette::Highlight);

    // Faint tint plus a frame so the mode is recognisable.
    QColor tint = hl;
    tint.setAlphaF(0.04);
    p.fillRect(rect(), tint);
    QColor frame = hl;
    frame.setAlphaF(0.8);
    p.setPen(QPen(frame, 4));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(2, 2, -2, -2));

    // Rubber band: the path so far and both end points.
    if (m_localPath.size() >= 1) {
        QColor band = hl;
        band.setAlphaF(0.9);
        p.setPen(QPen(band, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(m_localPath.constData(), m_localPath.size());
        p.setBrush(band);
        p.setPen(Qt::NoPen);
        p.drawEllipse(m_localPath.first(), 7, 7);
        p.drawEllipse(m_localPath.last(), 7, 7);
    }
}
