#include "network.h"
#include <cstdio>
#include <cstring>

namespace network {

enum class PacketType : uint8_t { ASSIGN_ID = 0, PLAYER_STATE = 1 };

#pragma pack(push, 1)
struct PlayerStatePacket {
    uint8_t type;
    uint8_t playerId;
    float x;
    float y;
    uint8_t animation;
    uint8_t facing;
};
struct AssignIdPacket {
    uint8_t type;
    uint8_t assignedId;
};
#pragma pack(pop)

NetworkManager::~NetworkManager() { shutdown(); }

bool NetworkManager::init() {
    if (initialized_) return true;
    if (enet_initialize() != 0) {
        std::fprintf(stderr, "[network] falha ao inicializar ENet\n");
        return false;
    }
    initialized_ = true;
    return true;
}

void NetworkManager::shutdown() {
    disconnect();
    if (initialized_) { enet_deinitialize(); initialized_ = false; }
}

bool NetworkManager::startHost(uint16_t port) {
    if (!initialized_ && !init()) return false;
    disconnect();
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    host_ = enet_host_create(&address, MAX_PLAYERS - 1, 2, 0, 0);
    if (!host_) {
        std::fprintf(stderr, "[network] falha ao criar host na porta %d\n", port);
        return false;
    }
    mode_ = Mode::HOST;
    localPlayerId_ = 0;
    remoteStates_[0].valid = true;
    return true;
}

bool NetworkManager::connect(const std::string& ip, uint16_t port) {
    if (!initialized_ && !init()) return false;
    disconnect();
    host_ = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!host_) {
        std::fprintf(stderr, "[network] falha ao criar host local (cliente)\n");
        return false;
    }
    ENetAddress address{};
    enet_address_set_host(&address, ip.c_str());
    address.port = port;
    serverPeer_ = enet_host_connect(host_, &address, 2, 0);
    if (!serverPeer_) {
        std::fprintf(stderr, "[network] falha ao conectar em %s:%d\n", ip.c_str(), port);
        enet_host_destroy(host_);
        host_ = nullptr;
        return false;
    }
    ENetEvent event;
    if (enet_host_service(host_, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
        mode_ = Mode::CLIENT;
        return true;
    }
    std::fprintf(stderr, "[network] timeout conectando em %s:%d\n", ip.c_str(), port);
    enet_host_destroy(host_);
    host_ = nullptr;
    serverPeer_ = nullptr;
    return false;
}

void NetworkManager::disconnect() {
    if (!host_) {
        mode_ = Mode::NONE;
        localPlayerId_ = -1;
        peers_.fill(nullptr);
        remoteStates_.fill(PlayerState{});
        return;
    }
    if (mode_ == Mode::CLIENT && serverPeer_) {
        enet_peer_disconnect(serverPeer_, 0);
        enet_host_flush(host_);
    } else if (mode_ == Mode::HOST) {
        for (auto* peer : peers_) if (peer) enet_peer_disconnect(peer, 0);
        enet_host_flush(host_);
    }
    enet_host_destroy(host_);
    host_ = nullptr;
    serverPeer_ = nullptr;
    peers_.fill(nullptr);
    remoteStates_.fill(PlayerState{});
    mode_ = Mode::NONE;
    localPlayerId_ = -1;
}

int NetworkManager::getConnectedPlayerCount() const {
    int count = 0;
    for (const auto& state : remoteStates_) if (state.valid) count++;
    return count;
}

void NetworkManager::update() {
    if (!host_) return;
    ENetEvent event;
    while (enet_host_service(host_, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                if (mode_ == Mode::HOST) {
                    int slot = -1;
                    for (int i = 1; i < MAX_PLAYERS; i++) if (!peers_[i]) { slot = i; break; }
                    if (slot == -1) { enet_peer_disconnect(event.peer, 0); break; }
                    peers_[slot] = event.peer;
                    event.peer->data = reinterpret_cast<void*>(static_cast<intptr_t>(slot));
                    remoteStates_[slot].valid = true;
                    AssignIdPacket pkt{static_cast<uint8_t>(PacketType::ASSIGN_ID), static_cast<uint8_t>(slot)};
                    ENetPacket* packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, packet);
                    if (onPlayerJoined) onPlayerJoined(slot);
                }
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                if (mode_ == Mode::HOST) {
                    int slot = static_cast<int>(reinterpret_cast<intptr_t>(event.peer->data));
                    if (slot > 0 && slot < MAX_PLAYERS) {
                        peers_[slot] = nullptr;
                        remoteStates_[slot] = PlayerState{};
                        if (onPlayerLeft) onPlayerLeft(slot);
                    }
                } else if (mode_ == Mode::CLIENT) { disconnect(); return; }
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                handlePacket(event);
                enet_packet_destroy(event.packet);
                break;
            }
            default: break;
        }
    }
}

void NetworkManager::handlePacket(ENetEvent& event) {
    if (event.packet->dataLength < 1) return;
    auto type = static_cast<PacketType>(event.packet->data[0]);

    if (type == PacketType::ASSIGN_ID && mode_ == Mode::CLIENT) {
        if (event.packet->dataLength < sizeof(AssignIdPacket)) return;
        AssignIdPacket pkt;
        std::memcpy(&pkt, event.packet->data, sizeof(pkt));
        localPlayerId_ = pkt.assignedId;
        remoteStates_[0].valid = true;
        remoteStates_[localPlayerId_].valid = true;
    } else if (type == PacketType::PLAYER_STATE) {
        if (event.packet->dataLength < sizeof(PlayerStatePacket)) return;
        PlayerStatePacket pkt;
        std::memcpy(&pkt, event.packet->data, sizeof(pkt));
        if (pkt.playerId >= MAX_PLAYERS || pkt.playerId == localPlayerId_) return;
        remoteStates_[pkt.playerId].x = pkt.x;
        remoteStates_[pkt.playerId].y = pkt.y;
        remoteStates_[pkt.playerId].animation = pkt.animation;
        remoteStates_[pkt.playerId].facing = pkt.facing;
        remoteStates_[pkt.playerId].valid = true;

        if (mode_ == Mode::HOST) {
            for (int i = 1; i < MAX_PLAYERS; i++) {
                if (peers_[i] && peers_[i] != event.peer) {
                    ENetPacket* out = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_UNSEQUENCED);
                    enet_peer_send(peers_[i], 1, out);
                }
            }
        }
    }
}

void NetworkManager::sendLocalState(const PlayerState& state) {
    if (!host_ || localPlayerId_ < 0) return;
    PlayerStatePacket pkt{};
    pkt.type = static_cast<uint8_t>(PacketType::PLAYER_STATE);
    pkt.playerId = static_cast<uint8_t>(localPlayerId_);
    pkt.x = state.x;
    pkt.y = state.y;
    pkt.animation = state.animation;
    pkt.facing = state.facing;

    ENetPacket* packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_UNSEQUENCED);
    if (mode_ == Mode::CLIENT && serverPeer_) {
        enet_peer_send(serverPeer_, 1, packet);
    } else if (mode_ == Mode::HOST) {
        for (int i = 1; i < MAX_PLAYERS; i++) {
            if (peers_[i]) {
                ENetPacket* out = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_UNSEQUENCED);
                enet_peer_send(peers_[i], 1, out);
            }
        }
        enet_packet_destroy(packet);
    }
}

} // namespace network
