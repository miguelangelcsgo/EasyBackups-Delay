#pragma once
#include <string>
#include <vector>
#include <set>

// ─────────────────────────────────────────────────────────────────────────────
// SceneParser
//
// Lee los archivos JSON de colecciones de escenas de OBS y extrae todas las
// rutas de archivos locales referenciadas por las fuentes (medios, imágenes,
// archivos locales del browser, playlists de VLC, archivos de texto, etc.).
// Lo usa BackupManager para saber qué assets subir.
// ─────────────────────────────────────────────────────────────────────────────

struct SceneCollection {
    std::string name;       // Nombre visible
    std::string jsonPath;   // Ruta completa al .json en disco
};

struct OBSPaths {
    std::string appdata;        // %APPDATA%\obs-studio
    std::string sceneCollDir;   // …\scene-collections
    std::string profilesDir;    // …\basic\profiles
};

class SceneParser {
public:
    // Ubica los directorios de datos de OBS en Windows
    static OBSPaths findObsPaths();

    // Lista todos los archivos JSON de colecciones de escenas
    static std::vector<SceneCollection> listCollections(const OBSPaths& paths);

    // Parsea un archivo JSON y devuelve todas las rutas absolutas de archivos locales encontradas
    // (las rutas inexistentes se descartan silenciosamente salvo que includeAll = true)
    static std::set<std::string> extractMediaPaths(const std::string& jsonPath,
                                                   bool includeAll = false);

    // Recorre TODAS las colecciones y devuelve el conjunto combinado de rutas de medios
    static std::set<std::string> allMediaPaths(const OBSPaths& paths);

    // Devuelve la lista de directorios de perfiles (cada uno es un nombre de carpeta dentro de profilesDir)
    static std::vector<std::string> listProfiles(const OBSPaths& paths);
};
