#include "injector.h"

#include <QDebug>

void InjectorRouter::addBackend(Injector *backend)
{
    m_backends.push_back(backend);
}

Injector *InjectorRouter::current() const
{
    for (Injector *b : m_backends) {
        if (b->isReady()) {
            return b;
        }
    }
    return nullptr;
}

QString InjectorRouter::name() const
{
    Injector *b = current();
    return b ? b->name() : QStringLiteral("none");
}

bool InjectorRouter::isReady() const
{
    return current() != nullptr;
}

void InjectorRouter::key(int code, bool pressed)
{
    if (pressed) {
        Injector *b = current();
        if (!b) {
            qWarning() << "vkbd: no input backend can deliver key" << code
                       << "(not activated by KWin and /dev/uinput not writable)";
            return;
        }
        m_pressedBy[code] = b;
        b->key(code, true);
        return;
    }

    auto it = m_pressedBy.find(code);
    if (it == m_pressedBy.end()) {
        return;
    }
    Injector *b = it->second;
    m_pressedBy.erase(it);
    b->key(code, false);
}
