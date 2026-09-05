#pragma once

#include <QPointF>
#include <QWidget>

// Transparent layer over the application area, shown while Sel is active.
// It captures one finger gesture and reports it in normalised screen
// coordinates so it can be replayed as a pointer drag.
class SelectionOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit SelectionOverlay(QWidget *parent = nullptr);

    // Geometry of this overlay within the screen (for normalising positions).
    void setScreenSize(const QSize &screen) { m_screen = screen; }

Q_SIGNALS:
    void gestureBegan(const QPointF &normalised);
    void gestureMoved(const QPointF &normalised);
    void gestureEnded();

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QPointF normalise(const QPointF &local) const;

    QSize m_screen;
    int m_touchId = -1;
    bool m_active = false;
};
