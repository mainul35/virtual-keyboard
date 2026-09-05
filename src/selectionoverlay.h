#pragma once

#include <QPointF>
#include <QVector>
#include <QWidget>
#include <functional>

// Transparent layer over the application area, shown while Sel is active.
// It captures one finger gesture, shows a rubber band while the finger moves,
// and hands the recorded path over (normalised screen coordinates) once the
// finger lifts, so it can be replayed as a pointer drag after the overlay is
// gone. The overlay must be gone during the replay: a pointer click on the
// overlay would make the compositor flip focus and deactivate the keyboard.
class SelectionOverlay : public QWidget
{
public:
    explicit SelectionOverlay(QWidget *parent = nullptr);

    void setScreenSize(const QSize &screen) { m_screen = screen; }
    void setOnGesture(std::function<void(const QVector<QPointF> &)> cb) { m_onGesture = std::move(cb); }

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    QPointF normalise(const QPointF &local) const;
    void begin(const QPointF &local);
    void move(const QPointF &local);
    void finish();

    std::function<void(const QVector<QPointF> &)> m_onGesture;
    QSize m_screen;
    int m_touchId = -1;
    bool m_active = false;
    QVector<QPointF> m_localPath; // in overlay coordinates, for drawing
};
