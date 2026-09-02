#include "keymap.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#ifdef VKBD_HAVE_XKBCOMMON
#include <xkbcommon/xkbcommon.h>
#endif

Keymap::~Keymap()
{
#ifdef VKBD_HAVE_XKBCOMMON
    if (m_map) {
        xkb_keymap_unref(m_map);
    }
    if (m_ctx) {
        xkb_context_unref(m_ctx);
    }
#endif
}

#ifdef VKBD_HAVE_XKBCOMMON

std::shared_ptr<Keymap> Keymap::fromString(const QByteArray &text)
{
    std::shared_ptr<Keymap> km(new Keymap);
    km->m_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!km->m_ctx) {
        return nullptr;
    }
    km->m_map = xkb_keymap_new_from_string(km->m_ctx, text.constData(), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!km->m_map) {
        return nullptr;
    }
    km->resolveModifiers();
    return km;
}

std::shared_ptr<Keymap> Keymap::fromSystemConfig()
{
    QByteArray layout, variant, model, options;

    const QString kxkbrc = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kxkbrc");
    if (QFile::exists(kxkbrc)) {
        QSettings s(kxkbrc, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("Layout"));
        layout = s.value(QStringLiteral("LayoutList")).toString().toUtf8();
        variant = s.value(QStringLiteral("VariantList")).toString().toUtf8();
        model = s.value(QStringLiteral("Model")).toString().toUtf8();
        options = s.value(QStringLiteral("Options")).toString().toUtf8();
    }
    if (layout.isEmpty()) {
        layout = qgetenv("XKB_DEFAULT_LAYOUT");
        variant = qgetenv("XKB_DEFAULT_VARIANT");
        model = qgetenv("XKB_DEFAULT_MODEL");
        options = qgetenv("XKB_DEFAULT_OPTIONS");
    }
    if (layout.isEmpty()) {
        layout = "us";
    }

    std::shared_ptr<Keymap> km(new Keymap);
    km->m_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!km->m_ctx) {
        return nullptr;
    }
    xkb_rule_names names{};
    names.rules = nullptr;
    names.model = model.isEmpty() ? nullptr : model.constData();
    names.layout = layout.constData();
    names.variant = variant.isEmpty() ? nullptr : variant.constData();
    names.options = options.isEmpty() ? nullptr : options.constData();
    km->m_map = xkb_keymap_new_from_names(km->m_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!km->m_map) {
        return nullptr;
    }
    km->resolveModifiers();
    return km;
}

void Keymap::resolveModifiers()
{
    auto mask = [this](const char *name, uint32_t fallback) {
        const xkb_mod_index_t idx = xkb_keymap_mod_get_index(m_map, name);
        return idx == XKB_MOD_INVALID ? fallback : (1u << idx);
    };
    m_shift = mask(XKB_MOD_NAME_SHIFT, m_shift);
    m_caps = mask(XKB_MOD_NAME_CAPS, m_caps);
    m_ctrl = mask(XKB_MOD_NAME_CTRL, m_ctrl);
    m_alt = mask(XKB_MOD_NAME_ALT, m_alt);
    m_meta = mask(XKB_MOD_NAME_LOGO, m_meta);
    m_altGr = mask("Mod5", m_altGr);
}

QString Keymap::label(int evdevCode, int group, bool shifted) const
{
    if (!m_map) {
        return {};
    }
    const xkb_keycode_t key = static_cast<xkb_keycode_t>(evdevCode + 8);
    const xkb_layout_index_t layouts = xkb_keymap_num_layouts_for_key(m_map, key);
    if (layouts == 0) {
        return {};
    }
    const xkb_layout_index_t layout = static_cast<xkb_layout_index_t>(group) < layouts ? group : 0;
    const xkb_level_index_t levels = xkb_keymap_num_levels_for_key(m_map, key, layout);
    if (levels == 0) {
        return {};
    }
    const xkb_level_index_t level = (shifted && levels > 1) ? 1 : 0;

    const xkb_keysym_t *syms = nullptr;
    const int n = xkb_keymap_key_get_syms_by_level(m_map, key, layout, level, &syms);
    if (n < 1) {
        return {};
    }
    char buf[16];
    const int len = xkb_keysym_to_utf8(syms[0], buf, sizeof(buf));
    if (len <= 1) {
        return {};
    }
    const QString text = QString::fromUtf8(buf, len - 1);
    if (text.isEmpty() || text.at(0).unicode() < 0x20 || text.at(0) == QChar(0x7f)) {
        return {};
    }
    return text;
}

int Keymap::groupCount() const
{
    return m_map ? static_cast<int>(xkb_keymap_num_layouts(m_map)) : 1;
}

#else // !VKBD_HAVE_XKBCOMMON

std::shared_ptr<Keymap> Keymap::fromString(const QByteArray &)
{
    return nullptr;
}

std::shared_ptr<Keymap> Keymap::fromSystemConfig()
{
    return nullptr;
}

void Keymap::resolveModifiers()
{
}

QString Keymap::label(int, int, bool) const
{
    return {};
}

int Keymap::groupCount() const
{
    return 1;
}

#endif
