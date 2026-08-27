#include "resource_manager.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include "SDL3_image/SDL_image.h"
#include "error.h"
#include "filesystem.h"
#include "sound.h"
#include "tools.h"

namespace TA::resmgr {
    struct Mod {
        std::filesystem::path root;
        std::vector<std::filesystem::path> files;
        int priority = 0;
        bool enabled = false;
    };

    void loadMods();
    Mod loadMod(std::filesystem::path root, const std::unordered_map<std::string, bool>& active);
    std::filesystem::path getAssetPath(std::filesystem::path asset);
    std::filesystem::path getExternalStorageRoot();
    void migrateOldMods(std::filesystem::path oldMods, std::filesystem::path newMods);

    // arquivo central que guarda "nome_do_mod = true/false"
    std::filesystem::path getModsActiveFilePath();
    std::unordered_map<std::string, bool> readModsActive();
    void writeModsActive(const std::unordered_map<std::string, bool>& active);

    void preloadTextures();
    void preloadChunks();

    std::unordered_map<std::string, std::filesystem::path> overrides;
    int totalMods = 0;
    int loadedMods = 0;

    std::unordered_map<std::string, SDL_Texture*> textureMap;
    std::unordered_map<std::string, MIX_Audio*> musicMap;
    std::unordered_map<std::string, MIX_Audio*> chunkMap;
    std::unordered_map<std::string, std::string> assetMap;
    std::unordered_map<std::string, toml::value> tomlMap;
}

void TA::resmgr::load() {
    loadMods();
    preloadTextures();
    preloadChunks();
}

std::filesystem::path TA::resmgr::getExternalStorageRoot() {
#ifdef __ANDROID__
    if(SDL_GetAndroidExternalStorageState() !=
        (SDL_ANDROID_EXTERNAL_STORAGE_READ | SDL_ANDROID_EXTERNAL_STORAGE_WRITE)) {
        return "";
    }
    return SDL_GetAndroidExternalStoragePath();
#elifdef TA_UNIX_INSTALL
    return "~/.local/share/tails-adventure";
#else
    return TA::filesystem::getExecutableDirectory();
#endif
}

void TA::resmgr::migrateOldMods(std::filesystem::path oldMods, std::filesystem::path newMods) {
    if(!std::filesystem::is_directory(oldMods) || oldMods == newMods) {
        return;
    }
    if(!std::filesystem::is_directory(newMods) || !std::filesystem::is_empty(newMods)) {
        return;
    }

    try {
        for(const auto& entry : std::filesystem::directory_iterator(oldMods)) {
            std::filesystem::rename(entry.path(), newMods / entry.path().filename());
        }
        TA::printLog("%s", "migrated old mods/ folder into Tails-adventure-extra-story/mods");
    } catch(std::exception& e) {
        TA::printWarning("failed to migrate old mods folder: %s", e.what());
    }
}

std::filesystem::path TA::resmgr::getDataRoot() {
    std::filesystem::path root = getExternalStorageRoot();
    if(root.empty()) {
        return "";
    }

    std::filesystem::path dataRoot = root / "Tails-adventure-extra-story";
    std::filesystem::create_directories(dataRoot / "mods");
    std::filesystem::create_directories(dataRoot / "lang");
    std::filesystem::create_directories(dataRoot / "user");

    migrateOldMods(root / "mods", dataRoot / "mods");

    return dataRoot;
}

std::filesystem::path TA::resmgr::getModsActiveFilePath() {
    std::filesystem::path dataRoot = getDataRoot();
    if(dataRoot.empty()) {
        return "";
    }
    return dataRoot / "mods" / "mods-active.toml";
}

std::unordered_map<std::string, bool> TA::resmgr::readModsActive() {
    std::unordered_map<std::string, bool> result;

    std::filesystem::path path = getModsActiveFilePath();
    if(path.empty() || !std::filesystem::is_regular_file(path)) {
        return result;
    }

    auto trim = [](std::string& s) {
        while(!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while(!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    };

    std::istringstream stream(TA::filesystem::readFile(path));
    std::string line;
    while(std::getline(stream, line)) {
        size_t eq = line.find('=');
        if(eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        trim(key);
        trim(value);
        if(key.empty()) continue;

        result[key] = (value == "true");
    }

    return result;
}

void TA::resmgr::writeModsActive(const std::unordered_map<std::string, bool>& active) {
    std::filesystem::path path = getModsActiveFilePath();
    if(path.empty()) {
        return;
    }

    std::vector<std::string> keys;
    keys.reserve(active.size());
    for(const auto& entry : active) {
        keys.push_back(entry.first);
    }
    std::sort(keys.begin(), keys.end());

    std::string content;
    for(const std::string& key : keys) {
        content += key + " = " + (active.at(key) ? "true" : "false") + "\n";
    }

    TA::filesystem::writeFile(path, content);
}

void TA::resmgr::loadMods() {
    std::filesystem::path dataRoot = getDataRoot();

    if(dataRoot.empty()) {
        return;
    }
    std::filesystem::path modsPath = dataRoot / "mods";

    std::unordered_map<std::string, bool> active = readModsActive();

    std::vector<Mod> mods;
    std::error_code dirEc;
    if(std::filesystem::is_directory(modsPath, dirEc) && !dirEc) {
        std::error_code iterEc;
        for(auto it = std::filesystem::directory_iterator(modsPath, iterEc);
            !iterEc && it != std::filesystem::directory_iterator();
            it.increment(iterEc)) {
            if(std::filesystem::exists(it->path()) && std::filesystem::is_directory(it->path())) {
                mods.push_back(loadMod(it->path(), active));
            }
        }
    }

    std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) { return a.priority < b.priority; });

    std::vector<std::string> loaded;
    for(const Mod& mod : mods) {
        totalMods++;
        if(!mod.enabled) continue;
        loaded.push_back(mod.root.filename().generic_string());
        loadedMods++;
        for(const std::filesystem::path& path : mod.files) {
            std::string rel = std::filesystem::relative(path, mod.root).generic_string();
            overrides[rel] = path;
        }
    }

    if(!loaded.empty()) {
        std::string log = "loaded mods (highest priority last): ";
        for(const std::string name : loaded) {
            log += name;
            log += ' ';
        }
        TA::printLog("%s", log.c_str());
    }
}

TA::resmgr::Mod TA::resmgr::loadMod(std::filesystem::path root, const std::unordered_map<std::string, bool>& active) {
    Mod mod = Mod();
    mod.root = root;

    std::string name = root.filename().generic_string();
    auto it = active.find(name);
    mod.enabled = (it != active.end()) && it->second;

    if(std::filesystem::is_regular_file(root / "priority")) {
        mod.priority = std::stoi(TA::filesystem::readFile(root / "priority"));
    }

    if(!mod.enabled) {
        return mod;
    }

    for(const auto& file : std::filesystem::recursive_directory_iterator(root)) {
        if(!std::filesystem::is_regular_file(file.path())) {
            continue;
        }
        std::string rel = std::filesystem::relative(file.path(), root).generic_string();
        // mod.toml (manifesto), icon.png e a pasta scripts/ nao sao assets do jogo
        if(rel == "mod.toml" || rel == "icon.png" || rel.starts_with("scripts/")) {
            continue;
        }
        mod.files.emplace_back(file.path());
    }

    return mod;
}

std::filesystem::path TA::resmgr::getAssetPath(std::filesystem::path asset) {
    if(overrides.contains(asset.generic_string())) {
        return overrides.at(asset.generic_string());
    }
    return TA::filesystem::getAssetsPath() / asset;
}

void TA::resmgr::preloadTextures() {
    const std::vector<std::string> names{"bomb", "enemy_bomb", "enemy_rock", "explosion", "leaf", "nezu_bomb", "ring",
        "rock", "splash", "walker_bullet"};

    for(std::string name : names) {
        loadTexture("objects/" + name + ".png");
    }
}

void TA::resmgr::preloadChunks() {
    const std::vector<std::string> names{"break", "damage", "enter", "explosion", "fall", "find_item", "fly", "hammer",
        "hit", "item_switch", "jump", "land", "open", "remote_robot_fly", "remote_robot_step", "ring", "select_item",
        "select", "shoot", "switch", "teleport"};

    for(std::string name : names) {
        loadChunk("sound/" + name + ".ogg");
    }
}

SDL_Texture* TA::resmgr::loadTexture(std::filesystem::path path) {
    path = getAssetPath(path);

    if(!textureMap.count(path.generic_string())) {
        std::string pathStr = path.generic_string();
        SDL_Surface* surface = IMG_Load(pathStr.c_str());
        if(surface == nullptr) {
            TA::handleSDLError("%s", "failed to load image");
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(TA::renderer, surface);
        if(texture == nullptr) {
            TA::handleSDLError("%s", "failed to create texture from surface");
        }
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        textureMap[path.generic_string()] = texture;
        SDL_DestroySurface(surface);
    }

    return textureMap[path.generic_string()];
}

MIX_Audio* TA::resmgr::loadMusic(std::filesystem::path path) {
    path = getAssetPath(path);

    if(!musicMap.count(path.generic_string())) {
        std::string pathStr = path.generic_string();
        musicMap[path.generic_string()] = MIX_LoadAudio(TA::sound::getMixer(), pathStr.c_str(), false);
        if(musicMap[path.generic_string()] == nullptr) {
            TA::handleSDLError("%s load failed", pathStr.c_str());
        }
    }

    return musicMap[path.generic_string()];
}

MIX_Audio* TA::resmgr::loadChunk(std::filesystem::path path) {
    path = getAssetPath(path);

    if(!chunkMap.contains(path.generic_string())) {
        std::string pathStr = path.generic_string();
        chunkMap[path.generic_string()] = MIX_LoadAudio(TA::sound::getMixer(), pathStr.c_str(), true);
        if(chunkMap[path.generic_string()] == nullptr) {
            TA::handleSDLError("%s load failed", pathStr.c_str());
        }
    }

    return chunkMap[path.generic_string()];
}

const std::string& TA::resmgr::loadAsset(std::filesystem::path path) {
    path = getAssetPath(path);
    if(!assetMap.contains(path.generic_string())) {
        assetMap[path.generic_string()] = TA::filesystem::readFile(path);
    }
    return assetMap[path.generic_string()];
}

const toml::value& TA::resmgr::loadToml(std::filesystem::path path) {
    path = getAssetPath(path);
    if(!tomlMap.contains(path.generic_string())) {
        try {
            tomlMap[path.generic_string()] = toml::parse_str(TA::filesystem::readFile(path));
        } catch(std::exception& e) {
            TA::handleError("failed to load %s\n%s", path.c_str(), e.what());
        }
    }
    return tomlMap[path.generic_string()];
}

int TA::resmgr::getLoadedMods() {
    return loadedMods;
}

int TA::resmgr::getTotalMods() {
    return totalMods;
}

std::vector<TA::resmgr::ModInfo> TA::resmgr::getModList() {
    std::vector<ModInfo> result;

    std::filesystem::path dataRoot = getDataRoot();
    if(dataRoot.empty()) {
        return result;
    }
    std::filesystem::path modsPath = dataRoot / "mods";

    std::error_code dirEc;
    if(!std::filesystem::is_directory(modsPath, dirEc) || dirEc) {
        // Pasta ainda não existe/não acessível nesse momento (ex: timing de
        // permissão de storage no Android). Sem isso o directory_iterator
        // abaixo lançaria uma exceção e derrubaria o jogo.
        return result;
    }

    std::unordered_map<std::string, bool> active = readModsActive();

    std::error_code iterEc;
    for(auto it = std::filesystem::directory_iterator(modsPath, iterEc);
        !iterEc && it != std::filesystem::directory_iterator();
        it.increment(iterEc)) {
        const auto& mod = *it;
        if(!std::filesystem::is_directory(mod.path())) {
            continue;
        }

        ModInfo info;
        info.name = mod.path().filename().generic_string();

        auto activeIt = active.find(info.name);
        info.enabled = (activeIt != active.end()) && activeIt->second;

        info.hasIcon = std::filesystem::is_regular_file(mod.path() / "icon.png");

        std::filesystem::path manifestPath = mod.path() / "mod.toml";
        if(std::filesystem::is_regular_file(manifestPath)) {
            try {
                toml::value manifest = toml::parse_str(TA::filesystem::readFile(manifestPath));
                info.description = toml::find_or<std::string>(manifest, "description", "");
            } catch(std::exception& e) {
                TA::printWarning("failed to read mod.toml for %s: %s", info.name.c_str(), e.what());
            }
        }

        result.push_back(info);
    }
    if(iterEc) {
        TA::printWarning("failed to list mods folder: %s", iterEc.message().c_str());
    }

    std::sort(result.begin(), result.end(), [](const ModInfo& a, const ModInfo& b) { return a.name < b.name; });
    return result;
}

void TA::resmgr::setModEnabled(const std::string& name, bool enabled) {
    std::filesystem::path dataRoot = getDataRoot();
    if(dataRoot.empty()) {
        return;
    }
    std::filesystem::path modsPath = dataRoot / "mods";
    if(!std::filesystem::is_directory(modsPath / name)) {
        return;
    }

    std::unordered_map<std::string, bool> active = readModsActive();
    active[name] = enabled;
    writeModsActive(active);
}

void TA::resmgr::quit() {
    for(std::pair<std::string, SDL_Texture*> element : textureMap) {
        SDL_DestroyTexture(element.second);
    }
    for(std::pair<std::string, MIX_Audio*> element : musicMap) {
        MIX_DestroyAudio(element.second);
    }
    for(std::pair<std::string, MIX_Audio*> element : chunkMap) {
        MIX_DestroyAudio(element.second);
    }
}
