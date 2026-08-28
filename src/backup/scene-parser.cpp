#include "scene-parser.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json   = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Descubrimiento de rutas
// ─────────────────────────────────────────────────────────────────────────────

OBSPaths SceneParser::findObsPaths()
{
    OBSPaths p;

    // Obtiene %APPDATA% mediante la API de Windows
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) {
        // Convierte la wide string a UTF-8
        int sz = WideCharToMultiByte(CP_UTF8, 0, roaming, -1,
                                     nullptr, 0, nullptr, nullptr);
        std::string appdata(sz - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, roaming, -1,
                            appdata.data(), sz, nullptr, nullptr);
        CoTaskMemFree(roaming);

        p.appdata       = appdata + "\\obs-studio";
        p.sceneCollDir  = p.appdata + "\\basic\\scenes";   // OBS guarda las colecciones de escenas acá
        p.profilesDir   = p.appdata + "\\basic\\profiles";
    }
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Listado de colecciones
// ─────────────────────────────────────────────────────────────────────────────

std::vector<SceneCollection> SceneParser::listCollections(const OBSPaths& paths)
{
    std::vector<SceneCollection> result;
    std::error_code ec;

    // increment(ec): un error de E/S a mitad de la iteración termina el recorrido en vez de lanzar excepción.
    fs::directory_iterator it(paths.sceneCollDir, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const auto& entry = *it;
        if (entry.path().extension() != ".json") continue;

        SceneCollection sc;
        sc.jsonPath = entry.path().u8string();   // UTF-8 (las rutas pueden tener acentos)

        // Intenta leer el campo "name" del JSON
        try {
            std::ifstream f(fs::u8path(sc.jsonPath));   // u8path para que se abran los nombres con acentos
            json j = json::parse(f, nullptr, false);
            if (j.contains("name") && j["name"].is_string())
                sc.name = j["name"].get<std::string>();
            else
                sc.name = entry.path().stem().string();
        } catch (...) {
            sc.name = entry.path().stem().string();
        }

        result.push_back(std::move(sc));
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Recolecta TODOS los valores string del árbol JSON. OBS guarda las rutas de
// medios bajo muchas claves distintas según el tipo de fuente (image_source→"file",
// ffmpeg_source/browser→"local_file", slideshow→"files"[].value,
// vlc_source→"playlist"[].value, text→"text_file", fonts→"font".path, …), así que
// en vez de adivinar los nombres de las claves tomamos todos los strings y después
// nos quedamos con los que parecen rutas absolutas a archivos existentes. Esto
// garantiza que no se pierda nada de lo usado en una escena.
// ─────────────────────────────────────────────────────────────────────────────

static void collectFromValue(const json& val, std::set<std::string>& out)
{
    if (val.is_string()) {
        std::string s = val.get<std::string>();
        if (!s.empty()) out.insert(std::move(s));
    } else if (val.is_array()) {
        for (auto& elem : val)
            collectFromValue(elem, out);
    } else if (val.is_object()) {
        for (auto& [k, v] : val.items())
            collectFromValue(v, out);
    }
}

std::set<std::string> SceneParser::extractMediaPaths(const std::string& jsonPath,
                                                      bool includeAll)
{
    std::set<std::string> raw;

    try {
        std::ifstream f(fs::u8path(jsonPath));   // u8path para que se abran los nombres con acentos
        if (!f.is_open()) return {};
        json j = json::parse(f, nullptr, false);
        if (j.is_discarded()) return {};
        collectFromValue(j, raw);
    } catch (...) {
        return {};
    }

    // Filtro: se queda solo con las rutas que parecen rutas absolutas de Windows y existen
    std::set<std::string> result;
    for (auto& p : raw) {
        if (p.size() < 3) continue;
        // Heurística simple: tiene letra de unidad (C:\...) o es una ruta UNC (\\...)
        bool looksAbsolute = (p[1] == ':' && (p[2] == '\\' || p[2] == '/'))
                          || (p[0] == '\\' && p[1] == '\\');
        if (!looksAbsolute) continue;

        std::error_code ec;
        bool exists = fs::exists(fs::u8path(p), ec);   // p viene en UTF-8 del JSON de la escena
        if (includeAll || exists)
            result.insert(p);
    }
    return result;
}

std::set<std::string> SceneParser::allMediaPaths(const OBSPaths& paths)
{
    std::set<std::string> all;
    for (auto& sc : listCollections(paths)) {
        auto m = extractMediaPaths(sc.jsonPath);
        all.insert(m.begin(), m.end());
    }
    return all;
}

std::vector<std::string> SceneParser::listProfiles(const OBSPaths& paths)
{
    std::vector<std::string> result;
    std::error_code ec;
    fs::directory_iterator it(paths.profilesDir, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const auto& entry = *it;
        std::error_code dec;
        if (entry.is_directory(dec))
            result.push_back(entry.path().filename().u8string());
    }
    return result;
}
