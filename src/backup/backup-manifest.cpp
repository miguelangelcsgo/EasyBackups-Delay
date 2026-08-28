#include "backup-manifest.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Serialización del BackupManifest
// ─────────────────────────────────────────────────────────────────────────────

static json itemToJson(const RestoreItem& item)
{
    return {{"name", item.name},
            {"remotePath", item.remotePath},
            {"originalLocal", item.originalLocal},
            {"size", item.size}};
}

// Robusto ante un manifest editado a mano / corrupto: chequea el tipo de cada
// campo en vez de j.value<int>("size", 0), que LANZA si "size" es, por ej., un
// string.
static RestoreItem itemFromJson(const json& j)
{
    RestoreItem i;
    if (!j.is_object()) return i;
    if (j.contains("name")          && j["name"].is_string())          i.name          = j["name"].get<std::string>();
    if (j.contains("remotePath")    && j["remotePath"].is_string())    i.remotePath    = j["remotePath"].get<std::string>();
    if (j.contains("originalLocal") && j["originalLocal"].is_string()) i.originalLocal = j["originalLocal"].get<std::string>();
    if (j.contains("size")          && j["size"].is_number_integer())  i.size          = j["size"].get<int64_t>();
    return i;
}

std::string BackupManifest::toJson() const
{
    json j;
    j["createdAt"]    = createdAt;
    j["obsVersion"]   = obsVersion;
    j["providerName"] = providerName;

    j["sceneCollections"] = json::array();
    for (auto& x : sceneCollections) j["sceneCollections"].push_back(itemToJson(x));
    j["profiles"]         = json::array();
    for (auto& x : profiles)         j["profiles"].push_back(itemToJson(x));
    j["mediaFiles"]       = json::array();
    for (auto& x : mediaFiles)       j["mediaFiles"].push_back(itemToJson(x));

    // error_handler_t::replace evita que dump() lance si alguna ruta no es UTF-8
    // válido (rutas ANSI de Windows, nombres raros, etc.).
    return j.dump(2, ' ', false, json::error_handler_t::replace);
}

BackupManifest BackupManifest::fromJson(const std::string& data)
{
    BackupManifest m;
    try {
        auto j = json::parse(data);
        m.createdAt    = j.value("createdAt",    "");
        m.obsVersion   = j.value("obsVersion",   "");
        m.providerName = j.value("providerName", "");
        auto loadArray = [](const json& j, const char* key, std::vector<RestoreItem>& out) {
            auto it = j.find(key);
            if (it != j.end() && it->is_array())
                for (auto& x : *it) out.push_back(itemFromJson(x));
        };
        loadArray(j, "sceneCollections", m.sceneCollections);
        loadArray(j, "profiles",         m.profiles);
        loadArray(j, "mediaFiles",       m.mediaFiles);
    } catch (...) {}
    return m;
}
