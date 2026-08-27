#include "script.h"
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "error.h"
#include "filesystem.h"
#include "resource_manager.h"

namespace {
    struct TA_ModScript {
        std::string modName;
        lua_State* state = nullptr;
        bool hasOnUpdate = false;
    };

    std::vector<TA_ModScript> scripts;

    // ta.log(msg) -> TA::printLog, prefixed with the owning mod's name.
    // The mod name is passed as upvalue 1 (a Lua string) so each mod's
    // closure logs under its own tag without any global state.
    int lua_ta_log(lua_State* L) {
        const char* msg = luaL_checkstring(L, 1);
        const char* modName = lua_tostring(L, lua_upvalueindex(1));
        TA::printLog("[lua:%s] %s", modName, msg);
        return 0;
    }

    void registerApi(lua_State* L, const std::string& modName) {
        lua_newtable(L);

        lua_pushstring(L, modName.c_str());
        lua_pushcclosure(L, lua_ta_log, 1);
        lua_setfield(L, -2, "log");

        lua_setglobal(L, "ta");
    }
}

void TA::script::load() {
    quit();

    std::filesystem::path dataRoot = TA::resmgr::getDataRoot();
    std::filesystem::path modsPath = dataRoot / "mods";

    for(const TA::resmgr::ModInfo& mod : TA::resmgr::getModList()) {
        if(!mod.enabled) {
            continue;
        }

        std::filesystem::path scriptPath = modsPath / mod.name / "Mod.lua";
        if(!TA::filesystem::fileExists(scriptPath)) {
            continue;
        }

        TA_ModScript script;
        script.modName = mod.name;
        script.state = luaL_newstate();
        if(script.state == nullptr) {
            TA::printWarning("failed to create Lua state for mod %s", mod.name.c_str());
            continue;
        }

        luaL_openlibs(script.state);
        registerApi(script.state, mod.name);

        if(luaL_dofile(script.state, scriptPath.string().c_str()) != LUA_OK) {
            const char* err = lua_tostring(script.state, -1);
            TA::printWarning("failed to load script for mod %s: %s", mod.name.c_str(), err ? err : "unknown error");
            lua_close(script.state);
            continue;
        }

        lua_getglobal(script.state, "on_update");
        script.hasOnUpdate = lua_isfunction(script.state, -1);
        lua_pop(script.state, 1);

        TA::printLog("loaded script for mod %s (on_update: %s)", mod.name.c_str(), script.hasOnUpdate ? "yes" : "no");
        scripts.push_back(std::move(script));
    }
}

void TA::script::update(float dt) {
    for(TA_ModScript& script : scripts) {
        if(!script.hasOnUpdate) {
            continue;
        }

        lua_getglobal(script.state, "on_update");
        lua_pushnumber(script.state, dt);
        if(lua_pcall(script.state, 1, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(script.state, -1);
            TA::printWarning(
                "error in on_update for mod %s: %s", script.modName.c_str(), err ? err : "unknown error");
            lua_pop(script.state, 1);
            script.hasOnUpdate = false;
        }
    }
}

void TA::script::quit() {
    for(TA_ModScript& script : scripts) {
        if(script.state != nullptr) {
            lua_close(script.state);
        }
    }
    scripts.clear();
}
