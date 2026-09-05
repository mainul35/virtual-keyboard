#pragma once

#include "layout.h"

#include <QHash>
#include <QSet>
#include <QWidget>
#include <map>
#include <memory>
#include <vector>

class Feedback;
class Injector;
class Keymap;

// The whole keyboard is one custom-painted QWidget: no per-key widgets, no QML
// engine, no OpenGL. Repaints are limited to the keys whose state changed.
class KeyboardWidget : public QWidget
{
    Q_OBJECT
public:
    enum class LayoutMode {
        Auto,    // compact when the panel is taller than wide (portrait), full otherwise
        Compact, // phone-style 10-key rows with a ?123 symbols page
        Full,    // PC layout with 15-key rows
    };

    explicit KeyboardWidget(Injector *injector, QWidget *parent = nullptr);
    ~KeyboardWidget() override;

    void setKeymap(std::shared_ptr<Keymap> keymap);
    void setLayoutGroup(int group);
    void setFunctionRowVisible(bool visible);
    bool functionRowVisible() const { return m_fnRow; }
    void setLayoutMode(LayoutMode mode);
    void setFeedback(Feedback *feedback) { m_feedback = feedback; }
    // Enlarged copy of a character key's label shown above it while pressed.
    void setKeyPreview(bool enabled);
    // How long the bubble stays after the finger lifts (ms).
    void setKeyPreviewLinger(int ms) { m_previewLingerMs = ms; }
    // Mark a key as pressed for rendering only (layout previews); no key is sent.
    void debugPressCode(int code);

    // Release every key and modifier we are holding (called when the panel
    // is hidden or the compositor deactivates us).
    void releaseAll();

    // Press and release a keycode through the injector (used for fallbacks).
    void tapKey(int code);
    // Modifier + key chord, independent of the on-screen modifier state
    // (used for the tray's Copy/Paste actions).
    void tapChord(int modifierCode, int code);

Q_SIGNALS:
    void hideRequested();
    void screenshotRequested();

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    enum class Page { Main, Fn, Symbols, Symbols2 };

    // Held   = a finger is on the key (physically pressed for the duration)
    // Latched/Locked = sticky: the key is only pressed around the next key
    //                  press(es), never while touching other windows
    enum class ModState { Idle, Held, Latched, Locked };
    struct ModInfo {
        ModState state = ModState::Idle;
        ModState before = ModState::Idle;
        bool used = false; // another key was typed while this one was held
        bool down = false; // a press has been injected and not yet released
    };
    struct KeySlot {
        const KeyDef *def = nullptr;
        QRectF rect;
    };

    bool compactActive() const;
    const PageDef &currentPage() const;
    void switchPage(Page page);
    void rebuildSlots();
    void relayout();
    int slotAt(const QPointF &pos) const;
    void pressSlot(int idx);
    void releaseSlot(int idx);
    void pressModifier(int code);
    void releaseModifier(int code);
    void engageStickyModifiers();
    void releaseStickyModifiers();
    void inject(int code, bool pressed);
    bool shiftActive() const;
    bool modifierEngaged(int code) const;
    QString labelFor(const KeyDef &def, bool shifted) const;
    void paintKey(QPainter &p, int idx);
    void paintHideGlyph(QPainter &p, const QRectF &r, const QColor &color);
    bool hasPreview(int idx) const;
    QRectF previewRect(int idx) const;
    void paintPreview(QPainter &p, int idx);
    void updateSlot(int idx);
    void setSelectMode(bool on);
    void updateSlotsWithCode(int code);
    void updateAllCharKeys();

    Injector *m_injector;
    Feedback *m_feedback = nullptr;
    bool m_selectMode = false; // Shift held for the next tap in the application
    bool m_keyPreview = true;
    int m_previewLingerMs = 300;
    QSet<int> m_lingerSlots;   // released keys whose bubble is still showing
    int m_lingerGeneration = 0; // invalidates pending linger timers on relayout
    PageDef m_fullPage;
    PageDef m_compactPage;
    PageDef m_symbolsPage;
    PageDef m_symbols2Page;
    PageDef m_fnPage;
    bool m_fnRow = true;
    LayoutMode m_layoutMode = LayoutMode::Auto;
    bool m_compactNow = false;
    Page m_page = Page::Main;

    std::vector<KeySlot> m_slots;
    std::vector<bool> m_pressed;
    QHash<int, int> m_touchToSlot;
    int m_mouseSlot = -1;
    QSet<int> m_tempShiftSlots; // slots that pressed Shift on their own

    std::map<int, ModInfo> m_mods; // keyed by evdev code
    bool m_capsLock = false;

    std::shared_ptr<Keymap> m_keymap;
    int m_group = 0;
};
