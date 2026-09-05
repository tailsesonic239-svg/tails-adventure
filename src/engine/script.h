#ifndef TA_SCRIPT_H
#define TA_SCRIPT_H

#include <string>

namespace TA {
    namespace script {
        // Scans enabled mods for a mods/<name>/Mod.lua file, creates one
        // Lua state per mod found, exposes the "ta" API table and detects
        // which optional event functions each script defined.
        void load();

        // Calls each loaded mod's on_update(dt), if it defined one.
        void update(float dt);

        // Calls each loaded mod's on_level_load(levelPath), if it defined one.
        // Fired once whenever a level finishes loading (TA_GameScreen::init()).
        void notifyLevelLoad(const std::string& levelPath);

        // Calls each loaded mod's on_player_hurt(), if it defined one.
        // Fired from TA_Character::setHurt(), right when the hit registers
        // (before rings scatter / invincibility starts).
        void notifyPlayerHurt();

        // Closes every Lua state. Safe to call even if load() was never called.
        void quit();
    }
}

#endif // TA_SCRIPT_H
