#include "script.h"
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "character.h"
#include "error.h"
#include "filesystem.h"
#include "object_set.h"
#include "resource_manager.h"
#include "save.h"
#include "sound.h"
#include "tools.h"

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

    // ta.get_player_x() / ta.get_player_y() -> number, or nil if there's no
    // active character right now (menus, sea fox stages, etc).
    int lua_ta_get_player_x(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushnumber(L, TA::activeCharacter->getPosition().x);
        return 1;
    }

    int lua_ta_get_player_y(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushnumber(L, TA::activeCharacter->getPosition().y);
        return 1;
    }

    // ta.get_character() -> "tails" | "sonic" | nil
    int lua_ta_get_character(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushstring(L, TA::activeCharacter->getCharacter() == TA_CHARACTER_SONIC ? "sonic" : "tails");
        return 1;
    }

    // ta.get_player_vx() / ta.get_player_vy() -> number, ou nil sem personagem ativo
    int lua_ta_get_player_vx(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushnumber(L, TA::activeCharacter->getVelocity().x);
        return 1;
    }

    int lua_ta_get_player_vy(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushnumber(L, TA::activeCharacter->getVelocity().y);
        return 1;
    }

    // ta.set_player_position(x, y) -> teleporta o personagem ativo (sem
    // efeito se nao houver personagem ativo no momento)
    int lua_ta_set_player_position(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            return 0;
        }
        double x = luaL_checknumber(L, 1);
        double y = luaL_checknumber(L, 2);
        TA::activeCharacter->setCharacterPosition(TA_Point(static_cast<float>(x), static_cast<float>(y)));
        return 0;
    }

    // ta.set_player_velocity(vx, vy) -> ajusta a velocidade do personagem
    // ativo diretamente (sem efeito se nao houver personagem ativo)
    int lua_ta_set_player_velocity(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            return 0;
        }
        double vx = luaL_checknumber(L, 1);
        double vy = luaL_checknumber(L, 2);
        TA::activeCharacter->setVelocity(TA_Point(static_cast<float>(vx), static_cast<float>(vy)));
        return 0;
    }

    // ta.is_on_ground() / ta.is_flying() / ta.is_in_water() /
    // ta.is_invincible() / ta.is_attacking() -> bool, ou nil sem personagem ativo
    int lua_ta_is_on_ground(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushboolean(L, TA::activeCharacter->isOnGround());
        return 1;
    }

    int lua_ta_is_flying(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushboolean(L, TA::activeCharacter->isFlying());
        return 1;
    }

    int lua_ta_is_in_water(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushboolean(L, TA::activeCharacter->isInWater());
        return 1;
    }

    int lua_ta_is_invincible(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushboolean(L, TA::activeCharacter->isInvincible());
        return 1;
    }

    int lua_ta_is_attacking(lua_State* L) {
        if(TA::activeCharacter == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushboolean(L, TA::activeCharacter->isAttacking());
        return 1;
    }

    // ta.hurt_player() -> aplica dano/hit-stun no personagem ativo, igual
    // encostar num inimigo. Sem efeito se nao houver personagem ativo.
    int lua_ta_hurt_player(lua_State* L) {
        if(TA::activeCharacter != nullptr) {
            TA::activeCharacter->setHurt();
        }
        return 0;
    }

    // ta.get_screen_width() / ta.get_screen_height() -> number
    int lua_ta_get_screen_width(lua_State* L) {
        lua_pushnumber(L, TA::screenWidth);
        return 1;
    }

    int lua_ta_get_screen_height(lua_State* L) {
        lua_pushnumber(L, TA::screenHeight);
        return 1;
    }

    // ---- rings / emeralds ----
    // "rings" no save fica guardado com prefixo do save atual
    // (ex: "save_0/rings"), por isso usamos as funcoes *SaveParameter.

    int lua_ta_get_rings(lua_State* L) {
        if(TA::activeObjectSet == nullptr || !TA::save::hasSaveParameter("rings")) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushinteger(L, TA::save::getSaveParameter("rings"));
        return 1;
    }

    // ta.add_rings(n) -- positivo da rings, negativo tira (mesma chamada
    // que o jogo usa pra pegar ring / tomar dano).
    int lua_ta_add_rings(lua_State* L) {
        if(TA::activeObjectSet == nullptr) {
            return 0;
        }
        int amount = static_cast<int>(luaL_checkinteger(L, 1));
        TA::activeObjectSet->addRings(amount);
        return 0;
    }

    int lua_ta_get_max_rings(lua_State* L) {
        if(TA::activeObjectSet == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushinteger(L, TA::activeObjectSet->getMaxRings());
        return 1;
    }

    int lua_ta_get_emeralds(lua_State* L) {
        if(TA::activeObjectSet == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushinteger(L, TA::activeObjectSet->getEmeraldsCount());
        return 1;
    }

    // ---- nivel / tempo ----

    int lua_ta_get_level(lua_State* L) {
        lua_pushstring(L, TA::levelPath.c_str());
        return 1;
    }

    int lua_ta_get_elapsed_time(lua_State* L) {
        lua_pushnumber(L, TA::elapsedTime);
        return 1;
    }

    // ---- save de dados do mod ----
    // Mods usam o mesmo arquivo de save do jogo (valores long long por
    // chave string). Prefixar sua propria chave (ex: "meumod_moedas") e
    // por sua conta -- nao ha namespace automatico por mod.

    // ta.get_save(nome) -> numero | nil (nil se a chave nunca foi setada)
    int lua_ta_get_save(lua_State* L) {
        std::string name = luaL_checkstring(L, 1);
        if(!TA::save::hasParameter(name)) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushinteger(L, TA::save::getParameter(name));
        return 1;
    }

    // ta.set_save(nome, valor) -- valor e truncado pra inteiro. Nao grava
    // no disco sozinho; o jogo escreve o save no proprio ritmo dele.
    int lua_ta_set_save(lua_State* L) {
        std::string name = luaL_checkstring(L, 1);
        long long value = static_cast<long long>(luaL_checknumber(L, 2));
        TA::save::setParameter(name, value);
        return 0;
    }

    // ---- audio ----

    // ta.play_music(caminho, [loop]) -- caminho relativo a assets/, ex:
    // "sound/title.vgm". loop e true por padrao; passe false pra tocar 1x.
    int lua_ta_play_music(lua_State* L) {
        std::string path = luaL_checkstring(L, 1);
        bool loop = !lua_isboolean(L, 2) || lua_toboolean(L, 2);
        TA::sound::playMusic(path, loop ? -1 : 0);
        return 0;
    }

    void registerApi(lua_State* L, const std::string& modName) {
        lua_newtable(L);

        auto setFn = [&](const char* name, lua_CFunction fn) {
            lua_pushcfunction(L, fn);
            lua_setfield(L, -2, name);
        };

        lua_pushstring(L, modName.c_str());
        lua_pushcclosure(L, lua_ta_log, 1);
        lua_setfield(L, -2, "log");

        setFn("get_player_x", lua_ta_get_player_x);
        setFn("get_player_y", lua_ta_get_player_y);
        setFn("get_player_vx", lua_ta_get_player_vx);
        setFn("get_player_vy", lua_ta_get_player_vy);
        setFn("set_player_position", lua_ta_set_player_position);
        setFn("set_player_velocity", lua_ta_set_player_velocity);
        setFn("get_character", lua_ta_get_character);

        setFn("is_on_ground", lua_ta_is_on_ground);
        setFn("is_flying", lua_ta_is_flying);
        setFn("is_in_water", lua_ta_is_in_water);
        setFn("is_invincible", lua_ta_is_invincible);
        setFn("is_attacking", lua_ta_is_attacking);
        setFn("hurt_player", lua_ta_hurt_player);

        setFn("get_screen_width", lua_ta_get_screen_width);
        setFn("get_screen_height", lua_ta_get_screen_height);

        setFn("get_rings", lua_ta_get_rings);
        setFn("add_rings", lua_ta_add_rings);
        setFn("get_max_rings", lua_ta_get_max_rings);
        setFn("get_emeralds", lua_ta_get_emeralds);

        setFn("get_level", lua_ta_get_level);
        setFn("get_elapsed_time", lua_ta_get_elapsed_time);

        setFn("get_save", lua_ta_get_save);
        setFn("set_save", lua_ta_set_save);

        setFn("play_music", lua_ta_play_music);

        lua_setglobal(L, "ta");
    }

    // Chama a funcao global `name(...)` de cada script carregado que a
    // definiu, se houver. `pushArgs` empilha os argumentos (pode ser um
    // lambda vazio pra eventos sem argumento); `argCount` precisa bater
    // com o que ela empilha. Erros sao logados e nao travam outros mods.
    template <typename PushArgs>
    void callEvent(const char* name, int argCount, PushArgs pushArgs) {
        for(TA_ModScript& script : scripts) {
            lua_getglobal(script.state, name);
            if(!lua_isfunction(script.state, -1)) {
                lua_pop(script.state, 1);
                continue;
            }

            pushArgs(script.state);
            if(lua_pcall(script.state, argCount, 0, 0) != LUA_OK) {
                const char* err = lua_tostring(script.state, -1);
                TA::printWarning("error in %s for mod %s: %s", name, script.modName.c_str(), err ? err : "unknown error");
                lua_pop(script.state, 1);
            }
        }
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

void TA::script::notifyLevelLoad(const std::string& levelPath) {
    callEvent("on_level_load", 1, [&](lua_State* L) { lua_pushstring(L, levelPath.c_str()); });
}

void TA::script::notifyPlayerHurt() {
    callEvent("on_player_hurt", 0, [](lua_State*) {});
}

void TA::script::quit() {
    for(TA_ModScript& script : scripts) {
        if(script.state != nullptr) {
            lua_close(script.state);
        }
    }
    scripts.clear();
}
