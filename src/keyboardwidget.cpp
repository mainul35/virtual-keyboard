#include "keyboardwidget.h"

#include "injector.h"
#include "keymap.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QTouchEvent>

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cmath>

KeyboardWidget::KeyboardWidget(Injector *injector, QWidget *parent)
    : QWidget(parent)
    , m_injector(injector)
    , m_fullPage(Layout::fullPage(true))
    , m_compactPage(Layout::compactPage())
    , m_symbolsPage(Layout::symbolsPage())
    , m_symbols2Page(Layout::symbols2Page())
    , m_fnPage(Layout::fnPage())
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    rebuildSlots();
}

KeyboardWidget::~KeyboardWidget()
{
    releaseAll();
}

void KeyboardWidget::setKeymap(std::shared_ptr<Keymap> keymap)
{
    m_keymap = std::move(keymap);
    update();
}

void KeyboardWidget::setLayoutGroup(int group)
{
    if (m_group == group) {
        return;
    }
    m_group = group;
    update();
}

void KeyboardWidget::setFunctionRowVisible(bool visible)
{
    if (m_fnRow == visible) {
        return;
    }
    m_fnRow = visible;
    m_fullPage = Layout::fullPage(visible);
    rebuildSlots();
    update();
}

void KeyboardWidget::setLayoutMode(LayoutMode mode)
{
    if (m_layoutMode == mode) {
        return;
    }
    m_layoutMode = mode;
    rebuildSlots();
    update();
}

// ---------------------------------------------------------------- pages --

bool KeyboardWidget::compactActive() const
{
    switch (m_layoutMode) {
    case LayoutMode::Compact:
        return true;
    case LayoutMode::Full:
        return false;
    case LayoutMode::Auto:
        break;
    }
    // The panel is a fixed fraction of the screen, so its aspect ratio tells
    // us the orientation: landscape panels are far wider than tall.
    return width() > 0 && height() > 0 && width() < height() * 2.2;
}

const PageDef &KeyboardWidget::currentPage() const
{
    switch (m_page) {
    case Page::Fn:
        return m_fnPage;
    case Page::Symbols:
        return m_symbolsPage;
    case Page::Symbols2:
        return m_symbols2Page;
    case Page::Main:
        break;
    }
    return m_compactNow ? m_compactPage : m_fullPage;
}

void KeyboardWidget::switchPage(Page page)
{
    m_page = page;
    rebuildSlots();
    update();
}

// ---------------------------------------------------------------- geometry --

void KeyboardWidget::rebuildSlots()
{
    // Let go of any plain key that is still down, but keep modifier state so a
    // latched Ctrl survives switching pages (Ctrl+Home etc.).
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_pressed[i]) {
            const KeyDef &def = *m_slots[i].def;
            if (def.kind == KeyKind::Char || def.kind == KeyKind::Special || def.kind == KeyKind::Lock) {
                inject(def.code, false);
            }
            if (m_tempShiftSlots.contains(static_cast<int>(i))) {
                inject(KEY_LEFTSHIFT, false);
            }
        }
    }
    m_tempShiftSlots.clear();
    m_slots.clear();
    m_touchToSlot.clear();
    m_mouseSlot = -1;

    m_compactNow = compactActive();
    // The symbol pages only exist in the compact layout.
    if (!m_compactNow && (m_page == Page::Symbols || m_page == Page::Symbols2)) {
        m_page = Page::Main;
    }

    const PageDef &page = currentPage();
    for (const RowDef &row : page.rows) {
        for (const KeyDef &key : row.keys) {
            m_slots.push_back(KeySlot{&key, QRectF()});
        }
    }
    m_pressed.assign(m_slots.size(), false);
    relayout();
}

void KeyboardWidget::relayout()
{
    const PageDef &page = currentPage();
    const float totalH = page.totalHeight();
    if (totalH <= 0 || width() <= 0 || height() <= 0) {
        return;
    }
    const qreal unitH = height() / static_cast<qreal>(totalH);

    size_t idx = 0;
    qreal y = 0;
    for (const RowDef &row : page.rows) {
        float units = row.leftPad + row.rightPad;
        for (const KeyDef &k : row.keys) {
            units += k.width;
        }
        const qreal unitW = width() / static_cast<qreal>(units);
        const qreal rowH = unitH * row.height;
        qreal x = unitW * row.leftPad;
        for (const KeyDef &k : row.keys) {
            const qreal w = unitW * k.width;
            m_slots[idx++].rect = QRectF(x, y, w, rowH);
            x += w;
        }
        y += rowH;
    }
}

void KeyboardWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (m_layoutMode == LayoutMode::Auto && compactActive() != m_compactNow) {
        rebuildSlots(); // orientation changed
    } else {
        relayout();
    }
}

void KeyboardWidget::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::FontChange) {
        update();
    }
}

int KeyboardWidget::slotAt(const QPointF &pos) const
{
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].rect.contains(pos)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ------------------------------------------------------------------- input --

bool KeyboardWidget::event(QEvent *e)
{
    switch (e->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        auto *te = static_cast<QTouchEvent *>(e);
        for (const QEventPoint &pt : te->points()) {
            if (pt.state() == QEventPoint::Pressed) {
                const int idx = slotAt(pt.position());
                if (idx >= 0 && !m_touchToSlot.contains(pt.id())) {
                    m_touchToSlot.insert(pt.id(), idx);
                    pressSlot(idx);
                }
            } else if (pt.state() == QEventPoint::Released) {
                auto it = m_touchToSlot.find(pt.id());
                if (it != m_touchToSlot.end()) {
                    const int idx = it.value();
                    m_touchToSlot.erase(it);
                    releaseSlot(idx);
                }
            }
        }
        if (e->type() == QEvent::TouchCancel) {
            const auto pending = m_touchToSlot;
            m_touchToSlot.clear();
            for (int idx : pending) {
                releaseSlot(idx);
            }
        }
        e->accept();
        return true;
    }
    default:
        break;
    }
    return QWidget::event(e);
}

void KeyboardWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        return;
    }
    const int idx = slotAt(e->position());
    if (idx >= 0) {
        m_mouseSlot = idx;
        pressSlot(idx);
    }
    e->accept();
}

void KeyboardWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        return;
    }
    if (m_mouseSlot >= 0) {
        const int idx = m_mouseSlot;
        m_mouseSlot = -1;
        releaseSlot(idx);
    }
    e->accept();
}

// --------------------------------------------------------------- key logic --

void KeyboardWidget::inject(int code, bool pressed)
{
    if (m_injector && code > 0) {
        m_injector->key(code, pressed);
    }
}

bool KeyboardWidget::modifierEngaged(int code) const
{
    auto it = m_mods.find(code);
    return it != m_mods.end() && it->second.state != ModState::Idle;
}

bool KeyboardWidget::shiftActive() const
{
    return modifierEngaged(KEY_LEFTSHIFT) || modifierEngaged(KEY_RIGHTSHIFT);
}

void KeyboardWidget::pressModifier(int code)
{
    ModInfo &info = m_mods[code];
    info.before = info.state;
    info.used = false;
    if (!info.down) {
        inject(code, true); // finger down = physically held
        info.down = true;
    }
    info.state = ModState::Held;
    updateSlotsWithCode(code);
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
        updateAllCharKeys();
    }
}

void KeyboardWidget::releaseModifier(int code)
{
    ModInfo &info = m_mods[code];
    // The physical press always ends with the finger; sticky states are
    // re-applied around the next key press by engageStickyModifiers().
    if (info.down) {
        inject(code, false);
        info.down = false;
    }
    if (info.used) {
        // Held while another key was typed: behaves like a physical key.
        info.state = info.before == ModState::Locked ? ModState::Locked : ModState::Idle;
    } else {
        switch (info.before) {
        case ModState::Idle:
            info.state = ModState::Latched; // one-shot
            break;
        case ModState::Latched:
            info.state = ModState::Locked; // second tap locks
            break;
        case ModState::Locked:
        case ModState::Held:
            info.state = ModState::Idle;
            break;
        }
    }
    info.used = false;
    updateSlotsWithCode(code);
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
        updateAllCharKeys();
    }
}

void KeyboardWidget::engageStickyModifiers()
{
    for (auto &[code, info] : m_mods) {
        if ((info.state == ModState::Latched || info.state == ModState::Locked) && !info.down) {
            inject(code, true);
            info.down = true;
        }
        if (info.state == ModState::Held || info.state == ModState::Latched) {
            info.used = true;
        }
    }
}

void KeyboardWidget::releaseStickyModifiers()
{
    bool shiftChanged = false;
    for (auto &[code, info] : m_mods) {
        if (info.state == ModState::Latched || info.state == ModState::Locked) {
            if (info.down) {
                inject(code, false);
                info.down = false;
            }
            if (info.state == ModState::Latched) {
                info.state = ModState::Idle;
                updateSlotsWithCode(code);
                if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
                    shiftChanged = true;
                }
            }
        }
    }
    if (shiftChanged) {
        updateAllCharKeys();
    }
}

void KeyboardWidget::pressSlot(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(m_slots.size())) {
        return;
    }
    const KeyDef &def = *m_slots[idx].def;
    m_pressed[idx] = true;
    update(m_slots[idx].rect.toAlignedRect());

    switch (def.kind) {
    case KeyKind::Modifier:
        pressModifier(def.code);
        break;
    case KeyKind::Action:
        break;
    case KeyKind::Lock:
    case KeyKind::Char:
    case KeyKind::Special:
        engageStickyModifiers();
        if (def.withShift && !shiftActive()) {
            // Symbol on the shifted level: hold Shift just for this key.
            inject(KEY_LEFTSHIFT, true);
            m_tempShiftSlots.insert(idx);
        }
        inject(def.code, true);
        break;
    }
}

void KeyboardWidget::releaseSlot(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(m_slots.size())) {
        return;
    }
    const KeyDef &def = *m_slots[idx].def;
    m_pressed[idx] = false;
    update(m_slots[idx].rect.toAlignedRect());

    switch (def.kind) {
    case KeyKind::Modifier:
        releaseModifier(def.code);
        break;
    case KeyKind::Lock:
    case KeyKind::Char:
    case KeyKind::Special:
        inject(def.code, false);
        if (m_tempShiftSlots.remove(idx)) {
            inject(KEY_LEFTSHIFT, false);
        }
        if (def.kind == KeyKind::Lock && def.code == KEY_CAPSLOCK) {
            m_capsLock = !m_capsLock;
            updateAllCharKeys();
        }
        releaseStickyModifiers();
        break;
    case KeyKind::Action:
        switch (def.action) {
        case KeyAction::Hide:
            releaseAll();
            Q_EMIT hideRequested();
            break;
        case KeyAction::PageFn:
            switchPage(Page::Fn);
            break;
        case KeyAction::PageMain:
            switchPage(Page::Main);
            break;
        case KeyAction::PageSymbols:
            switchPage(Page::Symbols);
            break;
        case KeyAction::PageSymbols2:
            switchPage(Page::Symbols2);
            break;
        case KeyAction::Screenshot:
            releaseStickyModifiers();
            Q_EMIT screenshotRequested();
            break;
        case KeyAction::None:
            break;
        }
        break;
    }
}

void KeyboardWidget::tapKey(int code)
{
    engageStickyModifiers();
    inject(code, true);
    inject(code, false);
    releaseStickyModifiers();
}

void KeyboardWidget::releaseAll()
{
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_pressed[i]) {
            m_pressed[i] = false;
            const KeyDef &def = *m_slots[i].def;
            if (def.kind == KeyKind::Char || def.kind == KeyKind::Special || def.kind == KeyKind::Lock) {
                inject(def.code, false);
            }
        }
    }
    for (int idx : std::as_const(m_tempShiftSlots)) {
        Q_UNUSED(idx);
        inject(KEY_LEFTSHIFT, false);
    }
    m_tempShiftSlots.clear();
    for (auto &[code, info] : m_mods) {
        if (info.down) {
            inject(code, false);
        }
        info = ModInfo{};
    }
    m_touchToSlot.clear();
    m_mouseSlot = -1;
    update();
}

// ---------------------------------------------------------------- painting --

QString KeyboardWidget::labelFor(const KeyDef &def, bool shifted) const
{
    if (def.kind == KeyKind::Char && def.keymapLabel && m_keymap) {
        const QString s = m_keymap->label(def.code, m_group, shifted);
        if (!s.isEmpty()) {
            return s;
        }
    }
    if (def.kind == KeyKind::Char && shifted && !def.shiftLabel.isEmpty()) {
        return def.shiftLabel;
    }
    return def.label;
}

void KeyboardWidget::updateSlotsWithCode(int code)
{
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].def->code == code) {
            update(m_slots[i].rect.toAlignedRect());
        }
    }
}

void KeyboardWidget::updateAllCharKeys()
{
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].def->kind == KeyKind::Char) {
            update(m_slots[i].rect.toAlignedRect());
        }
    }
}

void KeyboardWidget::paintEvent(QPaintEvent *e)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = palette();
    p.fillRect(e->rect(), pal.color(QPalette::Window).darker(108));

    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].rect.toAlignedRect().intersects(e->rect())) {
            paintKey(p, static_cast<int>(i));
        }
    }
}

// A small keyboard outline with a chevron underneath: "put the keyboard away".
void KeyboardWidget::paintHideGlyph(QPainter &p, const QRectF &r, const QColor &color)
{
    const qreal size = std::min(r.width() * 0.55, r.height() * 0.6);
    const qreal kbW = size;
    const qreal kbH = size * 0.5;
    const qreal cx = r.center().x();
    const qreal top = r.center().y() - size * 0.42;
    const QRectF kb(cx - kbW / 2, top, kbW, kbH);
    const qreal stroke = std::max(1.5, size * 0.07);

    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(kb, size * 0.08, size * 0.08);

    // Two rows of "keys" and a space bar.
    const qreal dot = std::max(1.5, size * 0.07);
    p.setPen(QPen(color, dot, Qt::SolidLine, Qt::RoundCap));
    for (int row = 0; row < 2; ++row) {
        const qreal y = kb.top() + kbH * (0.3 + row * 0.25);
        for (int i = 0; i < 5; ++i) {
            const qreal x = kb.left() + kbW * (0.18 + i * 0.16);
            p.drawPoint(QPointF(x, y));
        }
    }
    p.drawLine(QPointF(kb.left() + kbW * 0.3, kb.bottom() - kbH * 0.2), QPointF(kb.right() - kbW * 0.3, kb.bottom() - kbH * 0.2));

    // Chevron pointing down.
    const qreal chevY = kb.bottom() + size * 0.14;
    const qreal chevW = size * 0.22;
    const qreal chevH = size * 0.14;
    p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF({QPointF(cx - chevW, chevY), QPointF(cx, chevY + chevH), QPointF(cx + chevW, chevY)}));
}

void KeyboardWidget::paintKey(QPainter &p, int idx)
{
    const KeySlot &slot = m_slots[idx];
    const KeyDef &def = *slot.def;
    const QPalette pal = palette();

    const qreal gap = std::clamp(height() * 0.012, 2.0, 6.0);
    const QRectF r = slot.rect.adjusted(gap, gap, -gap, -gap);
    const qreal radius = std::min(8.0, r.height() * 0.18);

    const QColor base = pal.color(QPalette::Button);
    const bool dark = base.lightness() < 128;
    const QColor special = dark ? base.lighter(118) : base.darker(112);
    const QColor highlight = pal.color(QPalette::Highlight);

    QColor fill = base;
    QColor text = pal.color(QPalette::ButtonText);
    QPen border(Qt::NoPen);

    if (def.kind != KeyKind::Char) {
        fill = special;
    }

    bool engaged = false;
    bool locked = false;
    if (def.kind == KeyKind::Modifier) {
        auto it = m_mods.find(def.code);
        if (it != m_mods.end()) {
            engaged = it->second.state != ModState::Idle;
            locked = it->second.state == ModState::Locked;
        }
    } else if (def.kind == KeyKind::Lock && def.code == KEY_CAPSLOCK) {
        engaged = m_capsLock;
        locked = m_capsLock;
    }

    if (m_pressed[idx] || locked) {
        fill = highlight;
        text = pal.color(QPalette::HighlightedText);
    } else if (engaged) {
        border = QPen(highlight, std::max(2.0, gap));
        text = highlight;
    }

    p.setPen(border);
    p.setBrush(fill);
    p.drawRoundedRect(r, radius, radius);

    if (def.kind == KeyKind::Action && def.action == KeyAction::Hide) {
        paintHideGlyph(p, r, text);
        return;
    }

    const bool shifted = shiftActive() || (m_capsLock && Layout::isLetterCode(def.code));
    const QString label = labelFor(def, shifted);
    if (label.isEmpty()) {
        return;
    }

    QFont f = font();
    const int len = label.size();
    qreal px = std::min(r.height() * 0.42, r.width() * 0.62);
    if (len > 1) {
        px = std::min(px, r.width() * 1.5 / len);
    }
    px = std::max(px, 9.0);
    f.setPixelSize(static_cast<int>(px));
    f.setBold(def.kind != KeyKind::Char);
    p.setFont(f);
    p.setPen(text);
    p.drawText(r, Qt::AlignCenter, label);

    // Small hint of the other shift level in the top-right corner.
    if (def.kind == KeyKind::Char && def.keymapLabel && !Layout::isLetterCode(def.code)) {
        const QString other = labelFor(def, !shifted);
        if (!other.isEmpty() && other != label) {
            QFont sf = f;
            sf.setPixelSize(std::max(8, static_cast<int>(px * 0.55)));
            sf.setBold(false);
            p.setFont(sf);
            QColor dim = text;
            dim.setAlphaF(0.55);
            p.setPen(dim);
            p.drawText(r.adjusted(0, gap, -gap * 1.5, 0), Qt::AlignTop | Qt::AlignRight, other);
        }
    }
}
