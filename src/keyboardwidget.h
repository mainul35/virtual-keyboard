#pragma once

#include "layout.h"

#include <QHash>
#include <QWidget>
#include <map>
#include <memory>
#include <vector>

class Injector;
class Keymap;

// The whole keyboard is one custom-painted QWidget: no per-key widgets, no QML
// engine, no OpenGL. Repaints are limited to the keys whose state changed.
class KeyboardWidget : public QWidget
{
    Q_OBJECT
public:
    explicit KeyboardWidget(Injector *injector, QWidget *parent = nullptr);
    ~KeyboardWidget() override;

    void setKeymap(std::shared_ptr<Keymap> keymap);
    void setLayoutGroup(int group);
    void setFunctionRowVisible(bool visible);
    bool functionRowVisible() const { return m_fnRow; }

    // Release every key and modifier we are holding (called when the panel
    // is hidden or the compositor deactivates us).
    void releaseAll();

Q_SIGNALS:
    void hideRequested();

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    enum class ModState { Idle, Held, Latched, Locked };
    struct ModInfo {
        ModState state = ModState::Idle;
        ModState before = ModState::Idle;
        bool used = false;
    };
    struct KeySlot {
        const KeyDef *def = nullptr;
        QRectF rect;
    };

    void rebuildSlots();
    void relayout();
    int slotAt(const QPointF &pos) const;
    void pressSlot(int idx);
    void releaseSlot(int idx);
    void pressModifier(int code);
    void releaseModifier(int code);
    void releaseLatchedModifiers();
    void inject(int code, bool pressed);
    bool shiftActive() const;
    bool modifierEngaged(int code) const;
    QString labelFor(const KeyDef &def, bool shifted) const;
    void paintKey(QPainter &p, int idx);
    void updateSlotsWithCode(int code);
    void updateAllCharKeys();

    Injector *m_injector;
    PageDef m_mainPage;
    PageDef m_fnPage;
    bool m_fnRow = true;
    bool m_onFnPage = false;

    std::vector<KeySlot> m_slots;
    std::vector<bool> m_pressed;
    QHash<int, int> m_touchToSlot;
    int m_mouseSlot = -1;

    std::map<int, ModInfo> m_mods; // keyed by evdev code
    bool m_capsLock = false;

    std::shared_ptr<Keymap> m_keymap;
    int m_group = 0;
};
