#pragma once

#include <QWidget>

// A small always-on-top round button that shows/hides the keyboard. Only used
// when there is no compositor-driven way to bring the keyboard up (X11, or a
// Wayland session where vkbd was not configured as KWin's input method).
class ToggleButton : public QWidget
{
    Q_OBJECT
public:
    explicit ToggleButton(QWidget *parent = nullptr);

Q_SIGNALS:
    void clicked();

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    bool m_down = false;
};
