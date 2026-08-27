#ifndef TA_TOOLS_H
#define TA_TOOLS_H

#include <set>
#include <string>
#include "SDL3/SDL.h"
#include "geometry.h"

class TA_Character;

namespace TA {
    extern SDL_Window* window;
    extern SDL_Renderer* renderer;

    extern int screenWidth, screenHeight, scaleFactor;
    extern float elapsedTime;

    // Personagem ativo na fase atual (Tails ou Sonic), ou nullptr fora do
    // gameplay (menus, tela de save etc). Setado por TA_GameScreen.
    // Usado pela API de scripting Lua pra expor posição/estado do player.
    extern TA_Character* activeCharacter;

    constexpr float pi = 3.14159265358979323846;

    extern std::string levelPath, previousLevelPath;
    extern std::set<std::string> arguments;

    void drawRect(TA_Point topLeft, TA_Point bottomRight, int r, int g, int b, int a);
    void drawScreenRect(int r, int g, int b, int a);
    void drawShadow(int factor);
    float linearInterpolation(float left, float right, float pos);
    int getBaseHeight(int index);

    template <typename T>
    T sign(T a) {
        return (a >= 0 ? 1 : -1);
    }

    namespace random {
        void init(unsigned long long seed);
        long long next();
        long long max();
    }
}

#endif // TA_TOOLS_H
