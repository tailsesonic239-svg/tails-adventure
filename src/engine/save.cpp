#include "save.h"
#include <filesystem>
#include <map>
#include <sstream>
#include "error.h"
#include "filesystem.h"
#include "resource_manager.h"

namespace TA {
    namespace save {
        void addOptionsFromFile(std::filesystem::path path);
        std::filesystem::path getSaveFileName();
        std::map<std::string, long long> saveMap;
        std::string currentSave = "";
    }
}

void TA::save::load() {
    std::filesystem::path defaultConfigPath = TA::filesystem::getAssetsPath() / "default_config";
    addOptionsFromFile(defaultConfigPath);
    addOptionsFromFile(getSaveFileName());
}

void TA::save::addOptionsFromFile(std::filesystem::path path) {
    if(!TA::filesystem::fileExists(path)) {
        TA::printWarning("save file %s was not found, skipping", path.c_str());
        return;
    }

    std::string options = TA::filesystem::readFile(path);
    std::stringstream stream;
    stream << options;

    std::string name;
    long long value;
    while(stream >> name >> value) {
        if(name.starts_with("default_save/") && saveMap.contains(name)) {
            continue;
        }
        saveMap[name] = value;
    }
}

void TA::save::writeToFile() {
    std::stringstream output;
    for(auto [key, value] : saveMap) {
        output << key << ' ' << value << std::endl;
    }

    std::filesystem::path name = getSaveFileName();
    TA::filesystem::writeFile(name, output.str());
}

std::filesystem::path TA::save::getSaveFileName() {
    std::filesystem::path dataRoot = TA::resmgr::getDataRoot();
    if(dataRoot.empty()) {
#ifdef __ANDROID__
        return std::filesystem::path(SDL_GetAndroidInternalStoragePath()) / "config";
#else
        return TA::filesystem::getExecutableDirectory() / "config";
#endif
    }

    std::filesystem::path newPath = dataRoot / "user" / "config";

    if(!std::filesystem::exists(newPath)) {
        std::filesystem::path oldPath;
#ifdef __ANDROID__
        oldPath = std::filesystem::path(SDL_GetAndroidExternalStoragePath()) / "config";
#elifdef TA_UNIX_INSTALL
        oldPath = std::filesystem::path(getenv("HOME")) / ".local/share/tails-adventure" / "config";
#else
        oldPath = TA::filesystem::getExecutableDirectory() / "config";
#endif
        if(std::filesystem::exists(oldPath)) {
            try {
                std::filesystem::copy_file(oldPath, newPath);
                TA::printLog("%s", "migrated old save file into Tails-adventure-extra-story/user");
            } catch(std::exception& e) {
                TA::printWarning("failed to migrate old save file: %s", e.what());
            }
        }
    }

    return newPath;
}

long long TA::save::getParameter(std::string name) {
    if(!saveMap.contains(name)) {
        TA::handleError("unknown parameter %s", name.c_str());
    }
    return saveMap[name];
}

bool TA::save::hasParameter(std::string name) {
    return saveMap.contains(name);
}

void TA::save::setParameter(std::string name, long long value) {
    saveMap[name] = value;
}

void TA::save::setCurrentSave(std::string name) {
    currentSave = name;
}

long long TA::save::getSaveParameter(std::string name, std::string saveName) {
    if(saveName == "") {
        saveName = currentSave;
    }
    return getParameter(saveName + "/" + name);
}

void TA::save::setSaveParameter(std::string name, long long value, std::string saveName) {
    if(saveName == "") {
        saveName = currentSave;
    }
    setParameter(saveName + "/" + name, value);
}

bool TA::save::hasSaveParameter(std::string name, std::string saveName) {
    if(saveName == "") {
        saveName = currentSave;
    }
    return hasParameter(saveName + "/" + name);
}

void TA::save::createSave(std::string saveName) {
    std::map<std::string, long long> newSaveMap = saveMap;
    const std::string defaultSaveName = "default_save/";

    for(auto item : saveMap) {
        if(item.first.length() >= defaultSaveName.length() &&
            item.first.substr(0, defaultSaveName.length()) == defaultSaveName) {
            std::string itemName =
                saveName + "/" +
                item.first.substr(defaultSaveName.length(), item.first.length() - defaultSaveName.length());
            newSaveMap[itemName] = item.second;
        }
    }

    saveMap = newSaveMap;
}

void TA::save::repairSave(std::string saveName) {
    std::map<std::string, long long> newSaveMap = saveMap;
    const std::string defaultSaveName = "default_save/";

    for(auto item : saveMap) {
        if(item.first.length() >= defaultSaveName.length() &&
            item.first.substr(0, defaultSaveName.length()) == defaultSaveName) {
            std::string itemName =
                saveName + "/" +
                item.first.substr(defaultSaveName.length(), item.first.length() - defaultSaveName.length());
            if(!newSaveMap.count(itemName)) {
                newSaveMap[itemName] = item.second;
            }
        }
    }

    saveMap = newSaveMap;
}

bool TA::save::saveExists(int save) {
    std::string saveName = "save_" + std::to_string(save);
    return saveMap.count(saveName + "/item_mask");
}

void TA::save::deleteSave(int save) {
    std::string prefix = "save_" + std::to_string(save) + "/";
    for(auto it = saveMap.begin(); it != saveMap.end();) {
        if(it->first.starts_with(prefix)) {
            it = saveMap.erase(it);
        } else {
            ++it;
        }
    }
    if(currentSave == "save_" + std::to_string(save)) {
        currentSave = "";
    }
    writeToFile();
}
