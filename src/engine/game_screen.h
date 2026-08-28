#ifndef TA_GAME_SCREEN_H
#define TA_GAME_SCREEN_H

#include "camera.h"
#include "character.h"
#include "controller.h"
#include "geometry.h"
#include "hud.h"
#include "links.h"
#include "network.h"
#include "object_set.h"
#include "screen.h"
#include "sea_fox.h"
#include "tilemap.h"

class TA_GameScreen : public TA_Screen {
private:
    TA_Tilemap tilemap;
    TA_Camera camera;
    TA_Character character;
    TA_SeaFox seaFox;
    TA_Controller controller;
    TA_ObjectSet objectSet;
    TA_Links links;
    TA_Hud hud;

    std::string mode;
    bool isSeaFox = false;
    bool isSeaFoxGround = false;
    bool isSeaFoxFly = false;
    float timer = 0;

    // Jogadores remotos (multiplayer) - visual apenas, sem fisica propria;
    // posicao/animacao vem direto do que a rede recebeu.
    std::array<TA_Sprite, network::MAX_PLAYERS> remoteSprites;
    std::array<int, network::MAX_PLAYERS> remoteSpriteCharacter{-1, -1, -1, -1};
    void updateNetwork();
    void drawRemotePlayers();

public:
    void init() override;
    TA_ScreenState update() override;
    void quit() override;
};

#endif // TA_GAME_SCREEN_H
