#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

class QFileSystemWatcher;

// Detects real (alphanumeric) keyboards among /dev/input/event* and follows
// hot-plug. Virtual devices (vkbd's own, keyd's, other uinput keyboards) and
// ACPI/hotkey devices are ignored. Needs read access to /dev/input/event*,
// which the 'input' group grants (same requirement as the uinput backend).
class PhysicalKeyboardWatcher : public QObject
{
    Q_OBJECT
public:
    explicit PhysicalKeyboardWatcher(QObject *parent = nullptr);

    bool present() const { return !m_names.isEmpty(); }
    QStringList names() const { return m_names; }

    // One-off scan (used by --doctor too). Entries look like
    // "Logitech K380 (bus 0x05, 123 keys)"; names in `ignore` are skipped.
    static QStringList scan(const QStringList &ignore = {});
    void setIgnoreList(const QStringList &names) { m_ignore = names; }

Q_SIGNALS:
    void presenceChanged(bool present);

private:
    void rescan();

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_debounce;
    QStringList m_names;
    QStringList m_ignore;
};
