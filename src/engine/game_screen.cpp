#include "game_screen.h"
#include "resource_manager.h"
#include "save.h"
#include "tools.h"

void TA_GameScreen::init() {
    const toml::value& table = TA::resmgr::loadToml(TA::levelPath + ".toml");
    if(table.contains("level") && table.at("level").contains("mode") && table.at("level").at("mode").is_string()) {
        mode = table.at("level").at("mode").as_string();
    } else {
        mode = "ground";
    }

    isSeaFox = (mode != "ground");
    isSeaFoxGround = (mode == "seafox_ground");
    isSeaFoxFly = (mode == "seafox_fly");

    if(isSeaFox) {
        links.seaFox = &seaFox;
    } else {
        links.character = &character;
    }

    links.tilemap = &tilemap;
    links.camera = &camera;
    links.objectSet = &objectSet;
    links.hud = &hud;
    links.controller = &controller;

    controller.load();
    controller.setMode(TA_ONSCREEN_CONTROLLER_GAME);

    if(isSeaFox) {
        seaFox.load(links);
        if(mode == "seafox_ground") {
            seaFox.setGroundMode();
        } else if(mode == "seafox_fly") {
            seaFox.setFlyMode();
        }
    } else {
        character.setCharacter(static_cast<TA_CharacterID>(TA::save::getSaveParameter("character")));
        character.load(links);
        TA::activeCharacter = &character;
    }

    objectSet.setLinks(links);
    tilemap.load(TA::levelPath + ".tmx");
    tilemap.setCamera(&camera);
    hud.load(links);
    objectSet.load(TA::levelPath + ".toml");

    if(isSeaFox) {
        seaFox.setSpawnPoint(objectSet.getCharacterSpawnPoint(), objectSet.getCharacterSpawnFlip());
    } else {
        character.setSpawnPoint(objectSet.getCharacterSpawnPoint(), objectSet.getCharacterSpawnFlip());
    }

    TA::previousLevelPath = TA::levelPath;
    timer = TA::save::getSaveParameter("time");
}

TA_ScreenState TA_GameScreen::update() {
    timer += TA::elapsedTime;
    TA::save::setSaveParameter("time", timer);

    controller.update();
    hud.update();
    updateNetwork();

    if(!hud.isPaused()) {
        if(!isSeaFox) {
            character.handleInput();
        }
        objectSet.update();

        if(isSeaFox) {
            seaFox.update();
            camera.update(!isSeaFoxGround && !isSeaFoxFly, seaFox.isFastCamera(), true);
        } else {
            character.update();
            camera.update(character.isOnGround(), character.isFastCamera(), !character.isRemoteRobot());
        }
    }

    if(isSeaFox) {
        seaFox.setUpdateAnimation(!hud.isPaused());
    } else {
        character.setPaused(hud.isPaused());
    }

    tilemap.setUpdateAnimation(!hud.isPaused());
    objectSet.setPaused(hud.isPaused());

    tilemap.draw(0);
    objectSet.draw(0);

    if(isSeaFox) {
        seaFox.draw();
    } else if(!objectSet.isNight()) {
        character.draw();
    }

    objectSet.draw(1);
    tilemap.draw(1);

    if(!isSeaFox && objectSet.isNight()) {
        character.draw();
    }

    objectSet.draw(2);
    drawRemotePlayers();
    hud.draw();
    controller.draw();

    if(hud.getTransition() != TA_SCREENSTATE_CURRENT) {
        return hud.getTransition();
    }
    if((!isSeaFox && character.gameOver()) || (isSeaFox && seaFox.gameOver())) {
        return TA_SCREENSTATE_GAMEOVER;
    }
    if(!isSeaFox && character.isTeleported()) {
        return TA_SCREENSTATE_HOUSE;
    }
    if(objectSet.getTransition() != TA_SCREENSTATE_CURRENT) {
        return objectSet.getTransition();
    }
    return TA_SCREENSTATE_CURRENT;
}

void TA_GameScreen::updateNetwork() {
    if(!network::instance().isActive()) {
        return;
    }
    network::instance().update();

    if(!isSeaFox && network::instance().getStatus() == network::ConnectionStatus::CONNECTED) {
        network::PlayerState state;
        state.x = character.getPosition().x;
        state.y = character.getPosition().y;
        state.facing = character.getFlip() ? 1 : 0;
        state.characterId = static_cast<uint8_t>(character.getCharacter());
        state.animation = network::encodeAnimation(character.getAnimationName());
        network::instance().sendLocalState(state);
    }
}

void TA_GameScreen::drawRemotePlayers() {
    if(!network::instance().isActive()) {
        return;
    }

    const auto& states = network::instance().getRemoteStates();
    int localId = network::instance().getLocalPlayerId();

    for(int i = 0; i < network::MAX_PLAYERS; i++) {
        if(i == localId || !states[i].valid) {
            continue;
        }

        if(remoteSpriteCharacter[i] != states[i].characterId) {
            remoteSprites[i].loadFromToml(states[i].characterId == 1 ? "sonic/sonic.toml" : "tails/tails.toml");
            remoteSprites[i].setCamera(&camera);
            remoteSpriteCharacter[i] = states[i].characterId;
        }

        remoteSprites[i].setPosition(states[i].x, states[i].y);
        remoteSprites[i].setFlip(states[i].facing != 0);
        remoteSprites[i].setAnimation(network::decodeAnimation(states[i].animation));
        remoteSprites[i].draw();
    }
}

void TA_GameScreen::quit() {
    TA::activeCharacter = nullptr;
}
