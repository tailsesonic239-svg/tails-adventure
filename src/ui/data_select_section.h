#ifndef DATA_SELECT_SECTION_H
#define DATA_SELECT_SECTION_H

#include "character.h"
#include "font.h"
#include "main_menu_section.h"
#include "resource_manager.h"
#include "sound.h"

class TA_DataSelectSection : public TA_MainMenuSection {
public:
    using TA_MainMenuSection::TA_MainMenuSection;
    void load() override;
    TA_MainMenuState update() override;
    void setAlpha(int alpha) override { this->alpha = alpha; }
    void draw() override;

private:
    const float menuStart = 16;
    const float menuOffset = 64;
    const float scrollSpeed = 12;
    const float selectorBlinkTime = 15;
    const float loadTime = 60;
    const float scrollSlowdown = 0.125;

    void updateScroll();
    void updateSelection();
    void updateCharacterSelect();
    void drawCharacterSelect();
    void updateModsBrowse();
    void drawModsBrowse();
    void updateMultiplayerBrowse();
    void drawMultiplayerBrowse();
    void activateMultiplayerAction(int index);
    bool updateTouchscreenSelection();
    int getSavePercent(int save);
    std::string getSaveTime(int save);
    TA_MainMenuState processSelection();
    void confirmSaveAndEnter();
    void drawCustomEntries();
    void drawMultiplayerEntry();
    void drawModsEntry();
    void drawSaveEntries();
    void drawDeleteSaveEntry();
    void drawModCount();
    void drawSplash();
    void drawSelector();

    std::vector<std::string> generateSplashSequence();
    std::string generateSplash();

    TA_Sprite entrySprite, selectorRedSprite, selectorWhiteSprite, previewSprite, previewSeafoxSprite, optionsSprite;
    TA_Sprite characterSelectTailsSprite, characterSelectSonicSprite, characterSelectFrameSprite;
    TA_Sound switchSound, selectSound, loadSaveSound;
    TA_Font font, splashFont;
    float timer = 0, selectorTimer = 0;

    std::vector<std::string> splashSequence;
    float splashTimer = 0;

    std::array<TA_OnscreenButton, 12> buttons;
    TA_OnscreenButton characterSelectTailsButton, characterSelectSonicButton, characterSelectBackButton;
    float position = 0, scrollVelocity = 0;
    int selection = 3, createdSave = -1, alpha = 255;
    bool locked = false;

    // Tela de escolha de personagem, mostrada depois de escolher o save
    // e antes de entrar no jogo de fato.
    bool choosingCharacter = false;
    int pendingSave = -1;
    TA_CharacterID pendingCharacter = TA_CHARACTER_TAILS;

    // Tela de listar/ativar mods
    bool browsingMods = false;
    int modsBrowseSelection = 0;
    std::vector<TA::resmgr::ModInfo> modsBrowseList;
    TA_OnscreenButton modsBrowseBackButton;
    std::array<TA_OnscreenButton, 8> modsBrowseRowButtons;

    // Tela de hospedar/conectar multiplayer. IP/porta/senha vem de um
    // arquivo que o jogador edita por fora (user/multiplayer.toml), ja
    // que o jogo ainda nao tem teclado na tela pra digitar isso.
    bool browsingMultiplayer = false;
    int multiplayerSelection = 0;
    TA_OnscreenButton multiplayerBackButton;
    std::array<TA_OnscreenButton, 3> multiplayerRowButtons;

    // Modo "apagar save": ativado clicando na pilula "delete save" do
    // carrossel. Enquanto ativo, os nomes dos saves ficam vermelhos e
    // clicar num save existente apaga na hora (sem popup, sem confirmar
    // 2x) — clicar num slot vazio nao faz nada e continua armado; clicar
    // em qualquer outra coisa (ou apertar B) cancela o modo.
    bool deletingSave = false;
};

#endif // DATA_SELECT_SECTION_H
