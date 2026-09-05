#pragma once

#include <QString>
#include <cstdint>

struct ca_context;

// Key-press feedback. Plays a short click through libcanberra (the event-sound
// library Plasma itself uses), which is loaded at run time so it is not a
// build dependency. Vibration is not offered because typical x86 tablets expose
// no force-feedback device; see README.
class Feedback
{
public:
    Feedback();
    ~Feedback();

    void setSoundEnabled(bool enabled);
    void setVolumeDb(double db) { m_volumeDb = db; }
    bool soundAvailable() const { return m_ctx != nullptr; }

    void keyPressed();

private:
    bool load();
    QString ensureClickFile();

    void *m_lib = nullptr;
    ca_context *m_ctx = nullptr;
    int (*m_play)(ca_context *, uint32_t, ...) = nullptr;
    int (*m_cache)(ca_context *, ...) = nullptr;
    int (*m_destroy)(ca_context *) = nullptr;
    QByteArray m_clickPath;
    bool m_enabled = true;
    double m_volumeDb = -8.0;
};
