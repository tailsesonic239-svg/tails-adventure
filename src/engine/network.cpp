#include "network.h"
#include <cstdio>
#include <cstring>
#include "tools.h"

namespace network {

enum class PacketType : uint8_t { ASSIGN_ID = 0, PLAYER_STATE = 1, PASSWORD = 2, REJECTED = 3 };

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
struct PasswordPacket {
    uint8_t type;
    char password[32];
};
#pragma pack(pop)

// event.peer->data marca o estado de autenticacao de cada peer no HOST:
// -1 = aguardando pacote de senha, >=1 = slot ja atribuido.
constexpr intptr_t PENDING_AUTH = -1;

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

bool NetworkManager::startHost(uint16_t port, const std::string& password) {
    if (!initialized_ && !init()) return false;
    disconnect();
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    host_ = enet_host_create(&address, MAX_PLAYERS - 1, 2, 0, 0);
    if (!host_) {
        lastError_ = "falha ao criar host";
        std::fprintf(stderr, "[network] falha ao criar host na porta %d\n", port);
        status_ = ConnectionStatus::FAILED;
        return false;
    }
    mode_ = Mode::HOST;
    localPlayerId_ = 0;
    remoteStates_[0].valid = true;
    hostPassword_ = password;
    status_ = ConnectionStatus::CONNECTED;
    lastError_.clear();
    return true;
}

bool NetworkManager::connect(const std::string& ip, uint16_t port, const std::string& password) {
    if (!initialized_ && !init()) return false;
    disconnect();
    host_ = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!host_) {
        lastError_ = "falha ao criar host local";
        std::fprintf(stderr, "[network] falha ao criar host local (cliente)\n");
        status_ = ConnectionStatus::FAILED;
        return false;
    }
    ENetAddress address{};
    if (enet_address_set_host(&address, ip.c_str()) != 0) {
        lastError_ = "endereco invalido";
        enet_host_destroy(host_);
        host_ = nullptr;
        status_ = ConnectionStatus::FAILED;
        return false;
    }
    address.port = port;
    serverPeer_ = enet_host_connect(host_, &address, 2, 0);
    if (!serverPeer_) {
        lastError_ = "falha ao iniciar conexao";
        enet_host_destroy(host_);
        host_ = nullptr;
        status_ = ConnectionStatus::FAILED;
        return false;
    }

    // Nao bloqueia: so registra a intencao. O resultado (sucesso, senha
    // errada, timeout) e resolvido aos poucos dentro de update().
    mode_ = Mode::CLIENT;
    pendingPassword_ = password;
    status_ = ConnectionStatus::CONNECTING;
    lastError_.clear();
    connectTimer_ = 0;
    localPlayerId_ = -1;
    return true;
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

    if (mode_ == Mode::CLIENT && status_ == ConnectionStatus::CONNECTING) {
        connectTimer_ += TA::elapsedTime;
        if (connectTimer_ > connectTimeoutFrames) {
            lastError_ = "tempo esgotado conectando";
            status_ = ConnectionStatus::FAILED;
            disconnect();
            return;
        }
    }

    ENetEvent event;
    while (enet_host_service(host_, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                if (mode_ == Mode::HOST) {
                    if (!hostPassword_.empty()) {
                        // so libera um slot depois de validar a senha
                        event.peer->data = reinterpret_cast<void*>(PENDING_AUTH);
                        break;
                    }
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
                } else if (mode_ == Mode::CLIENT) {
                    // conexao de transporte ok; ainda falta autenticar com a senha
                    // (mandada mesmo vazia, o host decide se precisa validar)
                    PasswordPacket pkt{};
                    pkt.type = static_cast<uint8_t>(PacketType::PASSWORD);
                    std::strncpy(pkt.password, pendingPassword_.c_str(), sizeof(pkt.password) - 1);
                    ENetPacket* packet = enet_packet_create(&pkt, sizeof(pkt), ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, packet);
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
                } else if (mode_ == Mode::CLIENT) {
                    if (lastError_.empty()) {
                        lastError_ = (status_ == ConnectionStatus::CONNECTING) ? "conexao recusada pelo host"
                                                                                : "desconectado do host";
                    }
                    status_ = ConnectionStatus::FAILED;
                    disconnect();
                    return;
                }
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
        status_ = ConnectionStatus::CONNECTED;
        lastError_.clear();
    } else if (type == PacketType::REJECTED && mode_ == Mode::CLIENT) {
        lastError_ = "senha incorreta";
        status_ = ConnectionStatus::FAILED;
        disconnect();
    } else if (type == PacketType::PASSWORD && mode_ == Mode::HOST) {
        intptr_t peerState = reinterpret_cast<intptr_t>(event.peer->data);
        if (peerState != PENDING_AUTH) return; // ja autenticado (ou pacote fora de hora), ignora

        if (event.packet->dataLength < sizeof(PasswordPacket)) return;
        PasswordPacket pkt;
        std::memcpy(&pkt, event.packet->data, sizeof(pkt));
        pkt.password[sizeof(pkt.password) - 1] = '\0';

        if (hostPassword_ != pkt.password) {
            uint8_t rejected = static_cast<uint8_t>(PacketType::REJECTED);
            ENetPacket* out = enet_packet_create(&rejected, sizeof(rejected), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(event.peer, 0, out);
            enet_host_flush(host_);
            enet_peer_disconnect(event.peer, 0);
            return;
        }

        int slot = -1;
        for (int i = 1; i < MAX_PLAYERS; i++) if (!peers_[i]) { slot = i; break; }
        if (slot == -1) { enet_peer_disconnect(event.peer, 0); return; }
        peers_[slot] = event.peer;
        event.peer->data = reinterpret_cast<void*>(static_cast<intptr_t>(slot));
        remoteStates_[slot].valid = true;
        AssignIdPacket idPkt{static_cast<uint8_t>(PacketType::ASSIGN_ID), static_cast<uint8_t>(slot)};
        ENetPacket* idPacket = enet_packet_create(&idPkt, sizeof(idPkt), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(event.peer, 0, idPacket);
        if (onPlayerJoined) onPlayerJoined(slot);
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

NetworkManager& instance() {
    static NetworkManager manager;
    return manager;
}

} // namespace network
