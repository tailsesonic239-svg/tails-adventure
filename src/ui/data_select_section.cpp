#include "data_select_section.h"
#include <bit>
#include <numeric>
#include "character.h"
#include "error.h"
#include "filesystem.h"
#include "network.h"
#include "resource_manager.h"
#include "save.h"
#include "tools.h"

void TA_DataSelectSection::load() {
    entrySprite.load("data_select/entry.png");
    selectorRedSprite.load("data_select/selector.png", 48, 72);
    selectorWhiteSprite.load("data_select/selector.png", 48, 72);
    previewSprite.load("data_select/preview.png", 46, 46);
    previewSeafoxSprite.load("data_select/preview_seafox.png", 46, 46);
    optionsSprite.load("data_select/options.png");
    characterSelectTailsSprite.loadFromToml("tails/tails.toml");
    characterSelectSonicSprite.loadFromToml("sonic/sonic.toml");
    characterSelectFrameSprite.load("hud/pause_menu_frame.png");

    font.loadFont("fonts/pause_menu.toml");
    splashFont.loadFont("fonts/splash.toml");

    switchSound.load("sound/switch.ogg", TA_SOUND_CHANNEL_SFX1);
    selectSound.load("sound/select_item.ogg", TA_SOUND_CHANNEL_SFX1);
    loadSaveSound.load("sound/load_save.ogg", TA_SOUND_CHANNEL_SFX1);

    for(int pos = 0; pos < 11; pos++) {
        TA_Point topLeft{menuStart + menuOffset * pos, (float)TA::screenHeight / 2 - entrySprite.getHeight() / 2};
        TA_Point bottomRight = topLeft + TA_Point(48, (pos == 0 ? 48 : 72));
        buttons[pos].setRectangle(topLeft, bottomRight);
    }

    // Metade esquerda da tela escolhe Tails, metade direita escolhe Sonic,
    // uma faixa fina no topo cancela e volta pra seleção de save.
    characterSelectBackButton.setRectangle(TA_Point(0, 0), TA_Point((float)TA::screenWidth, 24));
    characterSelectTailsButton.setRectangle(TA_Point(0, 24), TA_Point((float)TA::screenWidth / 2, (float)TA::screenHeight));
    characterSelectSonicButton.setRectangle(
        TA_Point((float)TA::screenWidth / 2, 24), TA_Point((float)TA::screenWidth, (float)TA::screenHeight));

    // Layout dos modais (Mods / Multiplayer) — mesma faixa de "voltar" no
    // topo, e uma linha tocável por item da lista logo abaixo do título.
    float frameTop = (float)TA::screenHeight / 2 - characterSelectFrameSprite.getHeight() / 2;
    float modsListY = frameTop + 22;
    float multiplayerListY = frameTop + 28;

    modsBrowseBackButton.setRectangle(TA_Point(0, 0), TA_Point((float)TA::screenWidth, 24));
    for(int i = 0; i < static_cast<int>(modsBrowseRowButtons.size()); i++) {
        float y = modsListY + i * 10;
        modsBrowseRowButtons[i].setRectangle(TA_Point(0, y), TA_Point((float)TA::screenWidth, y + 10));
    }

    multiplayerBackButton.setRectangle(TA_Point(0, 0), TA_Point((float)TA::screenWidth, 24));
    for(int i = 0; i < static_cast<int>(multiplayerRowButtons.size()); i++) {
        float y = multiplayerListY + i * 12;
        multiplayerRowButtons[i].setRectangle(TA_Point(0, y), TA_Point((float)TA::screenWidth, y + 12));
    }

    splashSequence = generateSplashSequence();
}

TA_MainMenuState TA_DataSelectSection::update() {
    if(locked) {
        timer += TA::elapsedTime;
        if(timer > loadTime) {
            return TA_MAIN_MENU_EXIT;
        }
        return TA_MAIN_MENU_DATA_SELECT;
    }

    if(choosingCharacter) {
        updateCharacterSelect();
        return TA_MAIN_MENU_DATA_SELECT;
    }

    if(browsingMods) {
        updateModsBrowse();
        return TA_MAIN_MENU_DATA_SELECT;
    }

    if(browsingMultiplayer) {
        updateMultiplayerBrowse();
        return TA_MAIN_MENU_DATA_SELECT;
    }

    if(controller->isTouchscreen()) {
        updateScroll();
        if(updateTouchscreenSelection()) {
            return processSelection();
        }
    } else {
        updateSelection();
        if(controller->isJustPressed(TA_BUTTON_A) || controller->isJustPressed(TA_BUTTON_PAUSE)) {
            return processSelection();
        }
    }

    return TA_MAIN_MENU_DATA_SELECT;
}

void TA_DataSelectSection::updateCharacterSelect() {
    if(controller->isTouchscreen()) {
        characterSelectTailsButton.update();
        characterSelectSonicButton.update();
        characterSelectBackButton.update();

        if(characterSelectBackButton.isReleased()) {
            choosingCharacter = false;
            pendingSave = -1;
        } else if(characterSelectTailsButton.isReleased()) {
            pendingCharacter = TA_CHARACTER_TAILS;
            confirmSaveAndEnter();
        } else if(characterSelectSonicButton.isReleased()) {
            pendingCharacter = TA_CHARACTER_SONIC;
            confirmSaveAndEnter();
        }
        return;
    }

    if(controller->isJustChangedDirection() &&
        (controller->getDirection() == TA_DIRECTION_LEFT || controller->getDirection() == TA_DIRECTION_RIGHT)) {
        pendingCharacter = (pendingCharacter == TA_CHARACTER_TAILS) ? TA_CHARACTER_SONIC : TA_CHARACTER_TAILS;
        switchSound.play();
    }

    if(controller->isJustPressed(TA_BUTTON_A)) {
        confirmSaveAndEnter();
    } else if(controller->isJustPressed(TA_BUTTON_B)) {
        // Cancela e volta pra tela de seleção de save, sem entrar no jogo.
        choosingCharacter = false;
        pendingSave = -1;
    }
}

void TA_DataSelectSection::updateModsBrowse() {
    if(controller->isTouchscreen()) {
        modsBrowseBackButton.update();
        if(modsBrowseBackButton.isReleased()) {
            browsingMods = false;
            return;
        }

        for(int i = 0; i < static_cast<int>(modsBrowseRowButtons.size()); i++) {
            modsBrowseRowButtons[i].update();
            if(i < static_cast<int>(modsBrowseList.size()) && modsBrowseRowButtons[i].isReleased()) {
                modsBrowseSelection = i;
                TA::resmgr::ModInfo& mod = modsBrowseList[i];
                mod.enabled = !mod.enabled;
                TA::resmgr::setModEnabled(mod.name, mod.enabled);
                selectSound.play();
            }
        }
        return;
    }

    if(controller->isJustPressed(TA_BUTTON_B)) {
        browsingMods = false;
        return;
    }

    if(controller->isJustChangedDirection() &&
        (controller->getDirection() == TA_DIRECTION_UP || controller->getDirection() == TA_DIRECTION_DOWN)) {
        if(!modsBrowseList.empty()) {
            int delta = (controller->getDirection() == TA_DIRECTION_DOWN) ? 1 : -1;
            int count = static_cast<int>(modsBrowseList.size());
            modsBrowseSelection = (modsBrowseSelection + delta + count) % count;
            switchSound.play();
        }
    }

    if(controller->isJustPressed(TA_BUTTON_A) && !modsBrowseList.empty()) {
        TA::resmgr::ModInfo& mod = modsBrowseList[modsBrowseSelection];
        mod.enabled = !mod.enabled;
        TA::resmgr::setModEnabled(mod.name, mod.enabled);
        selectSound.play();
    }
}

void TA_DataSelectSection::updateMultiplayerBrowse() {
    if(controller->isTouchscreen()) {
        multiplayerBackButton.update();
        if(multiplayerBackButton.isReleased()) {
            browsingMultiplayer = false;
            return;
        }

        for(int i = 0; i < static_cast<int>(multiplayerRowButtons.size()); i++) {
            multiplayerRowButtons[i].update();
            if(multiplayerRowButtons[i].isReleased()) {
                multiplayerSelection = i;
                activateMultiplayerAction(i);
                selectSound.play();
            }
        }
        return;
    }

    if(controller->isJustPressed(TA_BUTTON_B)) {
        browsingMultiplayer = false;
        return;
    }

    if(controller->isJustChangedDirection() &&
        (controller->getDirection() == TA_DIRECTION_UP || controller->getDirection() == TA_DIRECTION_DOWN)) {
        int delta = (controller->getDirection() == TA_DIRECTION_DOWN) ? 1 : -1;
        multiplayerSelection = (multiplayerSelection + delta + 3) % 3;
        switchSound.play();
    }

    if(controller->isJustPressed(TA_BUTTON_A)) {
        activateMultiplayerAction(multiplayerSelection);
        selectSound.play();
    }
}

void TA_DataSelectSection::activateMultiplayerAction(int index) {
    std::filesystem::path dataRoot = TA::resmgr::getDataRoot();
    if(dataRoot.empty()) {
        return;
    }
    std::filesystem::path configPath = dataRoot / "user" / "multiplayer.toml";

    std::string ip = "127.0.0.1";
    uint16_t port = network::DEFAULT_PORT;
    std::string password;
    bool friendlyFire = false;

    if(std::filesystem::is_regular_file(configPath)) {
        try {
            toml::value config = toml::parse_str(TA::filesystem::readFile(configPath));
            ip = toml::find_or<std::string>(config, "ip", ip);
            port = static_cast<uint16_t>(toml::find_or<int>(config, "port", static_cast<int>(port)));
            password = toml::find_or<std::string>(config, "password", "");
            friendlyFire = toml::find_or<bool>(config, "friendly_fire", false);
        } catch(std::exception& e) {
            TA::printWarning("failed to read multiplayer.toml: %s", e.what());
        }
    }

    if(index == 0) {
        // Hospedar
        if(network::instance().startHost(port, password)) {
            network::instance().setFriendlyFire(friendlyFire);
        }
    } else if(index == 1) {
        // Conectar
        network::instance().connect(ip, port, password);
    } else if(index == 2) {
        // Desconectar
        network::instance().disconnect();
    }
}

TA_MainMenuState TA_DataSelectSection::processSelection() {
    if(selection == 0) {
        selectSound.play();
        return TA_MAIN_MENU_OPTIONS;
    }

    if(selection == 1) {
        selectSound.play();
        modsBrowseList = TA::resmgr::getModList();
        modsBrowseSelection = 0;
        browsingMods = true;
        return TA_MAIN_MENU_DATA_SELECT;
    }

    if(selection == 2) {
        selectSound.play();
        multiplayerSelection = 0;
        browsingMultiplayer = true;
        return TA_MAIN_MENU_DATA_SELECT;
    }

    selectSound.play();
    choosingCharacter = true;
    pendingSave = selection - 3;
    if(TA::save::saveExists(pendingSave)) {
        pendingCharacter = static_cast<TA_CharacterID>(
            TA::save::getSaveParameter("character", "save_" + std::to_string(pendingSave)));
    } else {
        pendingCharacter = TA_CHARACTER_TAILS;
    }
    return TA_MAIN_MENU_DATA_SELECT;
}

void TA_DataSelectSection::confirmSaveAndEnter() {
    if(!TA::save::saveExists(pendingSave)) {
        createdSave = pendingSave;
    }
    std::string saveName = "save_" + std::to_string(pendingSave);
    TA::save::repairSave(saveName);
    TA::save::setCurrentSave(saveName);
    TA::save::setSaveParameter("character", pendingCharacter, saveName);

    choosingCharacter = false;
    loadSaveSound.play();
    locked = true;
}

void TA_DataSelectSection::updateScroll() {
    if(TA::touchscreen::isScrolling()) {
        scrollVelocity = TA::touchscreen::getScrollVector().x;
    } else if(!TA::equal(scrollVelocity, 0)) {
        if(scrollVelocity > 0) {
            scrollVelocity = std::max((float)0, scrollVelocity - scrollSlowdown * TA::elapsedTime);
        } else {
            scrollVelocity = std::min((float)0, scrollVelocity + scrollSlowdown * TA::elapsedTime);
        }
    }

    position += scrollVelocity;
    position = std::max(position, float(0));
    position = std::min(position, menuStart + 11 * menuOffset - TA::screenWidth);
}

void TA_DataSelectSection::updateSelection() {
    // menuStart + selection * menuOffset + 24 - need = TA::screenWidth / 2
    float need = menuStart + selection * menuOffset + 24 - (float)TA::screenWidth / 2;
    need = std::max(need, float(0));
    need = std::min(need, menuStart + 11 * menuOffset - TA::screenWidth);

    if(!TA::equal(position, need)) {
        if(position > need) {
            position = std::max(need, position - scrollSpeed * TA::elapsedTime);
        }
        if(position < need) {
            position = std::min(need, position + scrollSpeed * TA::elapsedTime);
        }
    } else if(controller->isJustChangedDirection()) {
        if(selection - 1 >= 0 && controller->getDirection() == TA_DIRECTION_LEFT) {
            selection--;
            switchSound.play();
        }
        if(selection + 1 < 11 && controller->getDirection() == TA_DIRECTION_RIGHT) {
            selection++;
            switchSound.play();
        }
    }
}

bool TA_DataSelectSection::updateTouchscreenSelection() {
    for(int pos = 0; pos < 11; pos++) {
        buttons[pos].setPosition({-position, 0});
        buttons[pos].update();

        if(buttons[pos].isJustPressed() && !TA::touchscreen::isScrolling()) {
            scrollVelocity = 0;
        } else if(buttons[pos].isReleased()) {
            selection = pos;
            return true;
        }
    }

    return false;
}

void TA_DataSelectSection::draw() {
    drawCustomEntries();
    drawModsEntry();
    drawMultiplayerEntry();
    drawSaveEntries();
    drawModCount();
    drawSplash();
    drawSelector();
    drawCharacterSelect();
    drawModsBrowse();
    drawMultiplayerBrowse();
}

void TA_DataSelectSection::drawCharacterSelect() {
    if(!choosingCharacter) {
        return;
    }

    font.setAlpha(255);
    splashFont.setAlpha(255);
    characterSelectTailsSprite.setAlpha(255);
    characterSelectSonicSprite.setAlpha(255);
    characterSelectFrameSprite.setAlpha(255);

    characterSelectFrameSprite.setPosition(
        (float)TA::screenWidth / 2 - characterSelectFrameSprite.getWidth() / 2,
        (float)TA::screenHeight / 2 - characterSelectFrameSprite.getHeight() / 2);
    characterSelectFrameSprite.draw();

    float centerX = (float)TA::screenWidth / 2;
    float centerY = (float)TA::screenHeight / 2;
    float frameTop = centerY - characterSelectFrameSprite.getHeight() / 2;

    std::string title = "CHOOSE CHARACTER";
    splashFont.drawTextCentered(frameTop + 6, title, TA_Point(-1, 0));
    font.drawText(TA_Point(8, 8), "< back");

    characterSelectTailsSprite.setAnimation("idle");
    characterSelectSonicSprite.setAnimation("idle");

    const float spriteOffset = 32;
    float spriteY = frameTop + 18;
    TA_Point tailsPos{centerX - spriteOffset - characterSelectTailsSprite.getWidth() / 2, spriteY};
    TA_Point sonicPos{centerX + spriteOffset - characterSelectSonicSprite.getWidth() / 2, spriteY};

    characterSelectTailsSprite.setPosition(tailsPos);
    characterSelectSonicSprite.setPosition(sonicPos);
    characterSelectTailsSprite.draw();
    characterSelectSonicSprite.draw();

    std::string tailsLabel = (pendingCharacter == TA_CHARACTER_TAILS) ? "> tails <" : "  tails  ";
    std::string sonicLabel = (pendingCharacter == TA_CHARACTER_SONIC) ? "> sonic <" : "  sonic  ";
    font.drawText(TA_Point(centerX - spriteOffset - font.getTextWidth(tailsLabel) / 2, spriteY + characterSelectTailsSprite.getHeight() + 4), tailsLabel);
    font.drawText(TA_Point(centerX + spriteOffset - font.getTextWidth(sonicLabel) / 2, spriteY + characterSelectSonicSprite.getHeight() + 4), sonicLabel);
}

void TA_DataSelectSection::drawModsBrowse() {
    if(!browsingMods) {
        return;
    }

    font.setAlpha(255);
    splashFont.setAlpha(255);
    characterSelectFrameSprite.setAlpha(255);

    characterSelectFrameSprite.setPosition(
        (float)TA::screenWidth / 2 - characterSelectFrameSprite.getWidth() / 2,
        (float)TA::screenHeight / 2 - characterSelectFrameSprite.getHeight() / 2);
    characterSelectFrameSprite.draw();

    float centerX = (float)TA::screenWidth / 2;
    float centerY = (float)TA::screenHeight / 2;
    float frameTop = centerY - characterSelectFrameSprite.getHeight() / 2;

    splashFont.drawTextCentered(frameTop + 6, "MODS", TA_Point(-1, 0));
    font.drawText(TA_Point(8, 8), "< back");

    if(modsBrowseList.empty()) {
        std::string line1 = "sem mods";
        std::string line2 = "instalados";
        font.drawText(TA_Point(centerX - font.getTextWidth(line1) / 2, centerY - 6), line1);
        font.drawText(TA_Point(centerX - font.getTextWidth(line2) / 2, centerY + 4), line2);
        return;
    }

    float listY = frameTop + 22;
    for(int i = 0; i < static_cast<int>(modsBrowseList.size()) && i < 8; i++) {
        const TA::resmgr::ModInfo& mod = modsBrowseList[i];
        std::string marker = (i == modsBrowseSelection) ? "> " : "  ";
        std::string state = mod.enabled ? "[on] " : "[off] ";
        std::string icon = mod.hasIcon ? " (icon)" : "";
        font.drawText(TA_Point(centerX - 60, listY + i * 10), marker + state + mod.name + icon);
    }

    if(modsBrowseSelection < static_cast<int>(modsBrowseList.size())) {
        const std::string& desc = modsBrowseList[modsBrowseSelection].description;
        if(!desc.empty()) {
            font.drawText(TA_Point(centerX - 60, listY + std::min<size_t>(modsBrowseList.size(), 8) * 10 + 6), desc);
        }
    }
}

void TA_DataSelectSection::drawMultiplayerBrowse() {
    if(!browsingMultiplayer) {
        return;
    }

    font.setAlpha(255);
    splashFont.setAlpha(255);
    characterSelectFrameSprite.setAlpha(255);

    characterSelectFrameSprite.setPosition(
        (float)TA::screenWidth / 2 - characterSelectFrameSprite.getWidth() / 2,
        (float)TA::screenHeight / 2 - characterSelectFrameSprite.getHeight() / 2);
    characterSelectFrameSprite.draw();

    float centerX = (float)TA::screenWidth / 2;
    float centerY = (float)TA::screenHeight / 2;
    float frameTop = centerY - characterSelectFrameSprite.getHeight() / 2;

    splashFont.drawTextCentered(frameTop + 6, "MULTIPLAYER", TA_Point(-1, 0));
    font.drawText(TA_Point(8, 8), "< back");
    font.drawText(TA_Point(centerX - 70, frameTop + 16), "config: user/multiplayer.toml");

    const char* rows[3] = {"hospedar", "conectar", "desconectar"};
    float listY = frameTop + 28;
    for(int i = 0; i < 3; i++) {
        std::string marker = (i == multiplayerSelection) ? "> " : "  ";
        font.drawText(TA_Point(centerX - 70, listY + i * 12), marker + rows[i]);
    }

    std::string status;
    switch(network::instance().getStatus()) {
        case network::ConnectionStatus::DISCONNECTED: status = "status: desconectado"; break;
        case network::ConnectionStatus::CONNECTING: status = "status: conectando..."; break;
        case network::ConnectionStatus::CONNECTED: status = "status: conectado"; break;
        case network::ConnectionStatus::FAILED: status = "status: falhou (" + network::instance().getLastError() + ")"; break;
    }
    font.drawText(TA_Point(centerX - 70, listY + 42), status);

    if(network::instance().isActive()) {
        std::string info = "jogadores: " + std::to_string(network::instance().getConnectedPlayerCount() + 1) + "/" +
            std::to_string(network::MAX_PLAYERS);
        font.drawText(TA_Point(centerX - 70, listY + 52), info);
    }
}

void TA_DataSelectSection::drawCustomEntries() {
    optionsSprite.setAlpha(alpha);
    optionsSprite.setPosition(menuStart - position, (float)TA::screenHeight / 2 - entrySprite.getHeight() / 2);
    optionsSprite.draw();
}

void TA_DataSelectSection::drawModsEntry() {
    TA_Point entryPosition{menuStart + menuOffset - position, (float)TA::screenHeight / 2 - entrySprite.getHeight() / 2};

    entrySprite.setAlpha(alpha);
    entrySprite.setPosition(entryPosition);
    entrySprite.draw();

    font.setAlpha(255 * ((float)alpha / 255) * ((float)alpha / 255));
    std::string label = "mods";
    font.drawText(entryPosition + TA_Point(24 - font.getTextWidth(label) / 2, 30), label);
}

void TA_DataSelectSection::drawMultiplayerEntry() {
    TA_Point entryPosition{menuStart + 2 * menuOffset - position, (float)TA::screenHeight / 2 - entrySprite.getHeight() / 2};

    entrySprite.setAlpha(alpha);
    entrySprite.setPosition(entryPosition);
    entrySprite.draw();

    font.setAlpha(255 * ((float)alpha / 255) * ((float)alpha / 255));
    std::string label1 = "multi";
    std::string label2 = "player";
    font.drawText(entryPosition + TA_Point(24 - font.getTextWidth(label1) / 2, 26), label1);
    font.drawText(entryPosition + TA_Point(24 - font.getTextWidth(label2) / 2, 36), label2);
}

void TA_DataSelectSection::drawSaveEntries() {
    entrySprite.setAlpha(alpha);
    previewSprite.setAlpha(alpha);
    previewSeafoxSprite.setAlpha(alpha);
    font.setAlpha(255 * ((float)alpha / 255) * ((float)alpha / 255));

    for(int num = 0; num < 8; num++) {
        TA_Point entryPosition{
            menuStart + (num + 3) * menuOffset - position, (float)TA::screenHeight / 2 - entrySprite.getHeight() / 2};
        entrySprite.setPosition(entryPosition);
        entrySprite.draw();

        previewSprite.setPosition(entryPosition + TA_Point(1, 1));
        previewSeafoxSprite.setPosition(entryPosition + TA_Point(1, 1));
        if(TA::save::saveExists(num) && num != createdSave) {
            int lastUnlocked =
                static_cast<int>(TA::save::getParameter("save_" + std::to_string(num) + "/last_unlocked"));
            if(lastUnlocked >= 41) {
                previewSeafoxSprite.setFrame(lastUnlocked - 41);
                previewSeafoxSprite.draw();
            } else {
                previewSprite.setFrame(lastUnlocked);
                previewSprite.draw();
            }
        } else {
            previewSprite.setFrame(0);
            previewSprite.draw();
        }

        font.drawText(entryPosition + TA_Point(11, 50), std::to_string(getSavePercent(num)) + "%");
        font.drawText(entryPosition + TA_Point(11, 60), getSaveTime(num), TA_Point(-2, 0));
    }
}

void TA_DataSelectSection::drawModCount() {
    int total = TA::resmgr::getTotalMods();
    int loaded = TA::resmgr::getLoadedMods();
    if(total >= 1) {
        std::string text = "M " + std::to_string(loaded) + "/" + std::to_string(total);
        font.setAlpha(alpha);
        font.drawText(TA_Point(TA::screenWidth - font.getTextWidth(text) - 8, 9), text);
    }
}

void TA_DataSelectSection::drawSplash() {
    static constexpr float spashInterval = 4;

    splashTimer += TA::elapsedTime;
    int pos = static_cast<int>(splashTimer / spashInterval);
    pos = std::min(pos, static_cast<int>(splashSequence.size()) - 1);

    splashFont.setAlpha(alpha);
    splashFont.drawTextCentered(TA::screenHeight - 24, splashSequence[pos], TA_Point(-1, 0));
}

void TA_DataSelectSection::drawSelector() {
    selectorTimer += TA::elapsedTime;
    selectorRedSprite.setAlpha(alpha);
    selectorWhiteSprite.setAlpha(TA::linearInterpolation(0, alpha, selectorTimer / selectorBlinkTime));

    int selectorPosition = -1;
    if(controller->isTouchscreen() && !locked) {
        selectorRedSprite.setAlpha(255);
        selectorWhiteSprite.setAlpha(0);
        for(int pos = 0; pos < 11; pos++) {
            if(buttons[pos].isPressed()) {
                selectorPosition = pos;
            }
        }
    } else {
        selectorPosition = selection;
    }
    if(selectorPosition == -1) {
        return;
    }

    if(selectorPosition >= 1) {
        selectorRedSprite.setFrame(0);
        selectorWhiteSprite.setFrame(1);
    } else {
        selectorRedSprite.setFrame(2);
        selectorWhiteSprite.setFrame(3);
    }

    selectorRedSprite.setPosition(
        menuStart + selectorPosition * menuOffset - position, TA::screenHeight / 2 - selectorRedSprite.getHeight() / 2);
    selectorWhiteSprite.setPosition(selectorRedSprite.getPosition());
    selectorRedSprite.draw();
    selectorWhiteSprite.draw();
}

int TA_DataSelectSection::getSavePercent(int save) {
    if(!TA::save::saveExists(save) || save == createdSave) {
        return 0;
    }

    std::string saveName = "save_" + std::to_string(save);
    int areaCount = std::popcount(static_cast<uint64_t>(TA::save::getParameter(saveName + "/area_mask")));
    int itemCount = std::popcount(static_cast<uint64_t>(TA::save::getParameter(saveName + "/item_mask")));
    int bossCount = std::popcount(static_cast<uint64_t>(TA::save::getParameter(saveName + "/boss_mask")));
    return std::min(100, (30 * std::min(11, areaCount - 3) / 11) + (30 * bossCount / 8) + (40 * (itemCount - 2) / 30));
}

std::string TA_DataSelectSection::getSaveTime(int save) {
    if(!TA::save::saveExists(save) || save == createdSave) {
        return "--:--";
    }

    std::string saveName = "save_" + std::to_string(save);
    int minutes = TA::save::getParameter(saveName + "/time") / 3600;
    int hours = minutes / 60;
    minutes %= 60;
    hours %= 100;

    if(minutes <= 9) {
        return std::to_string(hours) + ":0" + std::to_string(minutes);
    }
    return std::to_string(hours) + ":" + std::to_string(minutes);
}

std::vector<std::string> TA_DataSelectSection::generateSplashSequence() {
    static constexpr int sequenceSize = 8;

    std::string splash = generateSplash();
    std::vector<int> permutation(splash.size());
    std::iota(permutation.begin(), permutation.end(), 0);
    for(int i = 0; i < splash.size(); i++) {
        std::swap(permutation[i], permutation[TA::random::next() % splash.size()]);
    }

    std::vector<int> positions(sequenceSize + 1);
    positions[0] = 0;
    positions[sequenceSize] = static_cast<int>(splash.size());
    for(int i = 1; i < sequenceSize; i++) {
        positions[i] = static_cast<int>(TA::random::next() % splash.size());
    }
    std::ranges::sort(positions);

    std::vector<std::string> sequence(sequenceSize);
    std::set<int> stable;

    for(int i = 0; i < sequenceSize; i++) {
        for(int j = positions[i]; j < positions[i + 1]; j++) {
            stable.insert(permutation[j]);
        }

        sequence[i] = splash;
        for(int j = 0; j < splash.size(); j++) {
            if(splash[j] != ' ' && !stable.contains(j)) {
                if(TA::random::next() % 5 != 0) {
                    sequence[i][j] = static_cast<char>('a' + (TA::random::next() % 26));
                } else {
                    sequence[i][j] = static_cast<char>('A' + (TA::random::next() % 26));
                }
            }
        }
    }

    return sequence;
}

std::string TA_DataSelectSection::generateSplash() {
    const std::vector<std::string> splashes{
        "Free as in freedom",
        "GNU/Tails Adventure",
        "Bleeding edge SDL3 o_o",
        "One Piece wa jitsuzai suru!",
        "Row Row Fight The Power",
        "Giga Drill Break!",
        "Blazingly fast!",
        "Memory unsafe :(",
        "C++23!",
        "Mods supported!",
        "Only 3% can pass!",
        "You have (not) tried",
        "Nanomachines, son!",
        "Flint and Steel!",
        "Usecase?",
        "peak fiction",
        "Absolute cinema!",
        "Fun is infinite",
        "The cake is a lie",
        "Every copy is personalized",
    };

    return splashes[TA::random::next() % static_cast<int>(splashes.size())];
}
