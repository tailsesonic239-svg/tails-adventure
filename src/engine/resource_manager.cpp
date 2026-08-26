#include "resource_manager.h"
#include <algorithm>
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
    Mod loadMod(std::filesystem::path root);
    std::filesystem::path getAssetPath(std::filesystem::path asset);
    std::filesystem::path getExternalStorageRoot();
    void migrateOldMods(std::filesystem::path oldMods, std::filesystem::path newMods);

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

void TA::resmgr::loadMods() {
    std::filesystem::path dataRoot = getDataRoot();

    if(dataRoot.empty()) {
        return;
    }
    std::filesystem::path modsPath = dataRoot / "mods";

    std::vector<Mod> mods;
    for(const auto& mod : std::filesystem::directory_iterator(modsPath)) {
        if(std::filesystem::exists(mod.path()) && std::filesystem::is_directory(mod.path())) {
            mods.push_back(loadMod(mod.path()));
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

TA::resmgr::Mod TA::resmgr::loadMod(std::filesystem::path root) {
    Mod mod = Mod();
    mod.root = root;

    if(!std::filesystem::is_regular_file(root / "enabled") ||
        TA::filesystem::readFile(root / "enabled").front() != '1') {
        mod.enabled = false;
        return mod;
    }

    mod.enabled = true;
    for(const auto& file : std::filesystem::recursive_directory_iterator(root)) {
        if(std::filesystem::is_regular_file(file.path())) {
            mod.files.emplace_back(file.path());
        }
    }
    if(std::filesystem::is_regular_file(root / "priority")) {
        mod.priority = std::stoi(TA::filesystem::readFile(root / "priority"));
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

    for(const auto& mod : std::filesystem::directory_iterator(modsPath)) {
        if(!std::filesystem::is_directory(mod.path())) {
            continue;
        }

        ModInfo info;
        info.name = mod.path().filename().generic_string();
        info.enabled = std::filesystem::is_regular_file(mod.path() / "enabled") &&
            TA::filesystem::readFile(mod.path() / "enabled").front() == '1';
        result.push_back(info);
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

    std::filesystem::path modPath = modsPath / name;
    if(!std::filesystem::is_directory(modPath)) {
        return;
    }

    TA::filesystem::writeFile(modPath / "enabled", (enabled ? "1" : "0"));
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
