#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Modelo de datos de una copia de seguridad.
//
// El "manifest" es el índice de una copia: la lista de escenas, perfiles y
// archivos multimedia, con la ruta original de cada uno y su ruta remota. Se
// serializa a JSON para guardarlo junto a los archivos y poder restaurar.
// ─────────────────────────────────────────────────────────────────────────────

// Un archivo dentro de una copia: dónde vivía y dónde quedó guardado.
struct RestoreItem {
    std::string name;          // ej. "Gaming.json" u "overlay.png"
    std::string remotePath;    // ruta dentro del destino de la copia
    std::string originalLocal; // dónde restaurarlo
    int64_t     size = 0;
};

// Índice completo de una copia (escenas, perfiles y media).
struct BackupManifest {
    std::string  createdAt;
    std::string  obsVersion;
    std::string  providerName;
    std::vector<RestoreItem> sceneCollections;
    std::vector<RestoreItem> profiles;   // cada archivo del perfil, por separado
    std::vector<RestoreItem> mediaFiles;

    // Serializar / deserializar.
    std::string toJson() const;
    static BackupManifest fromJson(const std::string& j);
};
