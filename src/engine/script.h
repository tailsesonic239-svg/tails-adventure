#ifndef TA_SCRIPT_H
#define TA_SCRIPT_H

namespace TA {
    namespace script {
        // Scans enabled mods for a mods/<name>/Mod.lua file, creates one
        // Lua state per mod found, exposes the minimal "ta" API table and
        // detects an optional global on_update(dt) function.
        void load();

        // Calls each loaded mod's on_update(dt), if it defined one.
        void update(float dt);

        // Closes every Lua state. Safe to call even if load() was never called.
        void quit();
    }
}

#endif // TA_SCRIPT_H
