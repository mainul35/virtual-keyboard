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
    , m_mainPage(Layout::mainPage(true))
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
    m_mainPage = Layout::mainPage(visible);
    rebuildSlots();
    update();
}

// ---------------------------------------------------------------- geometry --

void KeyboardWidget::rebuildSlots()
{
    // Let go of any plain key that is still down, but keep modifier state so a
    // latched Ctrl survives switching to the Fn page (Ctrl+Home etc.).
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_pressed[i]) {
            const KeyDef &def = *m_slots[i].def;
            if (def.kind == KeyKind::Char || def.kind == KeyKind::Special || def.kind == KeyKind::Lock) {
                inject(def.code, false);
            }
        }
    }
    m_slots.clear();
    m_touchToSlot.clear();
    m_mouseSlot = -1;

    const PageDef &page = m_onFnPage ? m_fnPage : m_mainPage;
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
    const PageDef &page = m_onFnPage ? m_fnPage : m_mainPage;
    const float totalH = page.totalHeight();
    if (totalH <= 0 || width() <= 0 || height() <= 0) {
        return;
    }
    const qreal unitH = height() / static_cast<qreal>(totalH);

    size_t idx = 0;
    qreal y = 0;
    for (const RowDef &row : page.rows) {
        float units = 0;
        for (const KeyDef &k : row.keys) {
            units += k.width;
        }
        const qreal unitW = width() / static_cast<qreal>(units);
        const qreal rowH = unitH * row.height;
        qreal x = 0;
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
    relayout();
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
    if (info.state == ModState::Idle) {
        inject(code, true);
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
    if (info.used) {
        // Held while another key was typed: behaves like a physical key.
        if (info.before == ModState::Locked) {
            info.state = ModState::Locked;
        } else {
            inject(code, false);
            info.state = ModState::Idle;
        }
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
            inject(code, false);
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

void KeyboardWidget::releaseLatchedModifiers()
{
    bool shiftChanged = false;
    for (auto &[code, info] : m_mods) {
        if (info.state == ModState::Latched) {
            inject(code, false);
            info.state = ModState::Idle;
            updateSlotsWithCode(code);
            if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
                shiftChanged = true;
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
        inject(def.code, true);
        for (auto &[code, info] : m_mods) {
            if (info.state == ModState::Held || info.state == ModState::Latched) {
                info.used = true;
            }
        }
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
        inject(def.code, false);
        if (def.code == KEY_CAPSLOCK) {
            m_capsLock = !m_capsLock;
            updateAllCharKeys();
        }
        releaseLatchedModifiers();
        break;
    case KeyKind::Char:
    case KeyKind::Special:
        inject(def.code, false);
        releaseLatchedModifiers();
        break;
    case KeyKind::Action:
        switch (def.action) {
        case KeyAction::Hide:
            releaseAll();
            Q_EMIT hideRequested();
            break;
        case KeyAction::PageFn:
            m_onFnPage = true;
            rebuildSlots();
            update();
            break;
        case KeyAction::PageMain:
            m_onFnPage = false;
            rebuildSlots();
            update();
            break;
        case KeyAction::Screenshot:
            releaseLatchedModifiers();
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
    inject(code, true);
    inject(code, false);
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
    for (auto &[code, info] : m_mods) {
        if (info.state != ModState::Idle) {
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
    if (def.kind == KeyKind::Char && m_keymap) {
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
    if (def.kind == KeyKind::Char && !Layout::isLetterCode(def.code)) {
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
