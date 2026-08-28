#pragma once

#include <enet/enet.h>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

// Fase 1+2 do multiplayer: host cria sala (com senha opcional), cliente
// conecta por IP sem travar o jogo, posicao/animacao/personagem de cada
// jogador e replicada. Fogo amigo e so um flag combinado entre host e
// clientes por enquanto -- ainda nao ha dano real entre jogadores, isso
// precisa entrar na colisao do character.cpp numa proxima etapa.

namespace network {

constexpr int MAX_PLAYERS = 4;
constexpr uint16_t DEFAULT_PORT = 7777;

enum class Mode { NONE, HOST, CLIENT };
enum class ConnectionStatus { DISCONNECTED, CONNECTING, CONNECTED, FAILED };

struct PlayerState {
    float x = 0.0f;
    float y = 0.0f;
    uint8_t characterId = 0;
    uint8_t animation = 0;
    uint8_t facing = 0; // 0 = direita, 1 = esquerda
    bool valid = false;
};

// Converte o nome da animacao (string) pra um numero pequeno, e vice-versa,
// pra nao ter que mandar a string inteira em todo pacote de rede.
uint8_t encodeAnimation(const std::string& name);
std::string decodeAnimation(uint8_t code);

class NetworkManager {
public:
    NetworkManager() = default;
    ~NetworkManager();

    bool init();
    void shutdown();

    // Cria uma sala. O host tambem eh o jogador 0. senha vazia = sem senha.
    bool startHost(uint16_t port = DEFAULT_PORT, const std::string& password = "");

    // Comeca a conectar numa sala existente. NAO bloqueia o jogo -- volta
    // na hora, e o resultado (sucesso/falha/senha errada) aparece depois
    // via getStatus()/getLastError() conforme update() eh chamado.
    bool connect(const std::string& ip, uint16_t port = DEFAULT_PORT, const std::string& password = "");

    void disconnect();

    // Deve ser chamado a cada frame do jogo (processa pacotes, timeout de conexao, etc)
    void update();

    // Envia o estado do jogador local pros outros peers
    void sendLocalState(const PlayerState& state);

    void setFriendlyFire(bool enabled) { friendlyFire_ = enabled; }
    bool isFriendlyFireEnabled() const { return friendlyFire_; }

    Mode getMode() const { return mode_; }
    bool isActive() const { return mode_ != Mode::NONE; }
    ConnectionStatus getStatus() const { return status_; }
    const std::string& getLastError() const { return lastError_; }
    int getLocalPlayerId() const { return localPlayerId_; }
    int getConnectedPlayerCount() const;

    // Estado recebido dos outros jogadores (indice = player id, 0 = host)
    const std::array<PlayerState, MAX_PLAYERS>& getRemoteStates() const { return remoteStates_; }

    std::function<void(int playerId)> onPlayerJoined;
    std::function<void(int playerId)> onPlayerLeft;

private:
    void handlePacket(ENetEvent& event);

    ENetHost* host_ = nullptr;
    ENetPeer* serverPeer_ = nullptr; // usado so no modo CLIENT
    std::array<ENetPeer*, MAX_PLAYERS> peers_{}; // usado so no modo HOST

    Mode mode_ = Mode::NONE;
    ConnectionStatus status_ = ConnectionStatus::DISCONNECTED;
    std::string lastError_;
    int localPlayerId_ = -1;
    bool initialized_ = false;

    std::string hostPassword_;
    std::string pendingPassword_; // senha que o CLIENTE vai mandar assim que conectar
    bool friendlyFire_ = false;

    float connectTimer_ = 0;
    static constexpr float connectTimeoutFrames = 300; // ~5s a 60fps

    std::array<PlayerState, MAX_PLAYERS> remoteStates_{};
};

// Instancia unica compartilhada pelo jogo inteiro.
NetworkManager& instance();

} // namespace network
