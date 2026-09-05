#include "feedback.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <cmath>
#include <dlfcn.h>
#include <vector>

namespace {

// Minimal libcanberra ABI we use (see canberra.h).
using ca_create_fn = int (*)(ca_context **);
using ca_change_props_fn = int (*)(ca_context *, ...);

// A short, soft "tick": a damped 1.9 kHz tone over ~28 ms, 44.1 kHz mono 16-bit.
QByteArray synthesizeClick()
{
    const int rate = 44100;
    const int samples = rate * 28 / 1000;
    std::vector<int16_t> pcm(samples);
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double env = std::exp(-t / 0.0045);
        const double tone = std::sin(2 * M_PI * 1900.0 * t) * 0.75 + std::sin(2 * M_PI * 3800.0 * t) * 0.25;
        const double attack = std::min(1.0, t / 0.0008);
        pcm[i] = static_cast<int16_t>(std::clamp(tone * env * attack * 0.6, -1.0, 1.0) * 32767);
    }

    QByteArray wav;
    auto put32 = [&](uint32_t v) { wav.append(reinterpret_cast<const char *>(&v), 4); };
    auto put16 = [&](uint16_t v) { wav.append(reinterpret_cast<const char *>(&v), 2); };
    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * 2);
    wav.append("RIFF");
    put32(36 + dataBytes);
    wav.append("WAVE");
    wav.append("fmt ");
    put32(16);
    put16(1); // PCM
    put16(1); // mono
    put32(rate);
    put32(rate * 2);
    put16(2);
    put16(16);
    wav.append("data");
    put32(dataBytes);
    wav.append(reinterpret_cast<const char *>(pcm.data()), dataBytes);
    return wav;
}

} // namespace

Feedback::Feedback()
{
    load();
}

Feedback::~Feedback()
{
    if (m_ctx && m_destroy) {
        m_destroy(m_ctx);
    }
    if (m_lib) {
        dlclose(m_lib);
    }
}

bool Feedback::load()
{
    m_lib = dlopen("libcanberra.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!m_lib) {
        qInfo() << "vkbd: libcanberra not available, key sounds disabled";
        return false;
    }
    auto create = reinterpret_cast<ca_create_fn>(dlsym(m_lib, "ca_context_create"));
    auto changeProps = reinterpret_cast<ca_change_props_fn>(dlsym(m_lib, "ca_context_change_props"));
    m_play = reinterpret_cast<decltype(m_play)>(dlsym(m_lib, "ca_context_play"));
    m_cache = reinterpret_cast<decltype(m_cache)>(dlsym(m_lib, "ca_context_cache"));
    m_destroy = reinterpret_cast<decltype(m_destroy)>(dlsym(m_lib, "ca_context_destroy"));
    if (!create || !m_play || !m_destroy) {
        qWarning() << "vkbd: libcanberra symbols missing, key sounds disabled";
        return false;
    }
    if (create(&m_ctx) != 0 || !m_ctx) {
        m_ctx = nullptr;
        return false;
    }
    if (changeProps) {
        changeProps(m_ctx, "application.name", "vkbd", "application.id", "org.vkbd.Keyboard", "media.role", "event", nullptr);
    }

    m_clickPath = ensureClickFile().toLocal8Bit();
    if (m_clickPath.isEmpty()) {
        return false;
    }
    if (m_cache) {
        // Keep the sample resident in the sound server for low latency.
        m_cache(m_ctx, "media.filename", m_clickPath.constData(), "canberra.cache-control", "permanent", nullptr);
    }
    return true;
}

QString Feedback::ensureClickFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/vkbd");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/click.wav");
    QFile f(path);
    if (f.exists() && f.size() > 44) {
        return path;
    }
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "vkbd: cannot write" << path;
        return {};
    }
    f.write(synthesizeClick());
    return path;
}

void Feedback::setSoundEnabled(bool enabled)
{
    m_enabled = enabled;
}

void Feedback::keyPressed()
{
    if (!m_enabled || !m_ctx || m_clickPath.isEmpty()) {
        return;
    }
    const QByteArray volume = QByteArray::number(m_volumeDb, 'f', 1);
    m_play(m_ctx, 0,
           "media.filename", m_clickPath.constData(),
           "canberra.cache-control", "permanent",
           "canberra.volume", volume.constData(),
           "event.description", "Key press",
           nullptr);
}
