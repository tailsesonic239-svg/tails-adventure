#pragma once

#include <enet/enet.h>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace network {

constexpr int MAX_PLAYERS = 4;
constexpr uint16_t DEFAULT_PORT = 7777;

enum class Mode { NONE, HOST, CLIENT };

struct PlayerState {
    float x = 0.0f;
    float y = 0.0f;
    uint8_t animation = 0;
    uint8_t facing = 0;
    bool valid = false;
};

class NetworkManager {
public:
    NetworkManager() = default;
    ~NetworkManager();

    bool init();
    void shutdown();
    bool startHost(uint16_t port = DEFAULT_PORT);
    bool connect(const std::string& ip, uint16_t port = DEFAULT_PORT);
    void disconnect();
    void update();
    void sendLocalState(const PlayerState& state);

    Mode getMode() const { return mode_; }
    bool isActive() const { return mode_ != Mode::NONE; }
    int getLocalPlayerId() const { return localPlayerId_; }
    int getConnectedPlayerCount() const;
    const std::array<PlayerState, MAX_PLAYERS>& getRemoteStates() const { return remoteStates_; }

    std::function<void(int playerId)> onPlayerJoined;
    std::function<void(int playerId)> onPlayerLeft;

private:
    void handlePacket(ENetEvent& event);

    ENetHost* host_ = nullptr;
    ENetPeer* serverPeer_ = nullptr;
    std::array<ENetPeer*, MAX_PLAYERS> peers_{};

    Mode mode_ = Mode::NONE;
    int localPlayerId_ = -1;
    bool initialized_ = false;

    std::array<PlayerState, MAX_PLAYERS> remoteStates_{};
};

} // namespace network
