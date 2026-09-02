#pragma once

#include <QString>
#include <unordered_map>
#include <vector>

// Something that can press and release evdev keycodes on behalf of the panel.
class Injector
{
public:
    virtual ~Injector() = default;
    virtual QString name() const = 0;
    virtual bool isReady() const = 0;
    virtual void key(int code, bool pressed) = 0;
};

// Picks the first ready backend for every press and makes sure the matching
// release goes through the same backend, even if availability changed in between.
class InjectorRouter : public Injector
{
public:
    void addBackend(Injector *backend);
    bool hasBackends() const { return !m_backends.empty(); }

    QString name() const override;
    bool isReady() const override;
    void key(int code, bool pressed) override;

    Injector *current() const;

private:
    std::vector<Injector *> m_backends;
    std::unordered_map<int, Injector *> m_pressedBy;
};
