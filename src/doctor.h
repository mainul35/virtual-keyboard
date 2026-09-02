#pragma once

// `vkbd --doctor`: prints everything needed to understand why the keyboard
// is (not) appearing: KWin configuration, D-Bus state, running instance,
// backend availability and the tail of the last log. Returns an exit code.
int runDoctor();
