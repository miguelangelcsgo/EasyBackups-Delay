#pragma once
#include "scene-parser.hpp"
#include "backup-manifest.hpp"

#include <string>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// BackupManager
//
// Copia y restaura la configuración de OBS (escenas, perfiles, media) hacia/desde
// una CARPETA LOCAL que elige el usuario. Si esa carpeta está dentro de un
// directorio de sincronización (OneDrive / Google Drive / Dropbox), el cliente de
// escritorio la sube solo — por eso no hace falta ningún proveedor en la nube ni
// OAuth: es simplemente copiar carpetas locales.
//
// Corre su trabajo en un hilo en segundo plano; emite callbacks de progreso / log.
// La ruta de la carpeta se guarda en la config del plugin (obs_module_config_path).
// ─────────────────────────────────────────────────────────────────────────────

// Se llama durante la copia: (nombre de archivo, porcentaje, actual, total).
using ProgressCb = std::function<void(const std::string& file, int percent,
                                      int current, int total)>;

struct BackupSummary {
    int  totalFiles      = 0;
    int  uploadedFiles   = 0;
    int  skippedFiles    = 0;
    bool success         = false;
    std::string errorMsg;
};

class BackupManager {
public:
    explicit BackupManager();
    ~BackupManager() = default;

    // ── Config (se guarda en la config de obs) ────────────────────────────────
    void loadConfig();      // lee de obs_module_config_path()
    void saveConfig() const;

    // La carpeta está lista si hay una ruta configurada que existe o se puede crear.
    // (La crea si falta.) Deja el motivo en lastError() cuando devuelve false.
    bool folderReady();

    // ── Backup ───────────────────────────────────────────────────────────────
    // Se llama de forma síncrona – desde un hilo worker en el diálogo.
    // progress se invoca con (nombre de archivo, porcentaje, actual, total).
    // log se invoca con líneas de log legibles.
    BackupSummary runBackup(ProgressCb progress,
                            std::function<void(const std::string&)> log);

    // ── Restore ──────────────────────────────────────────────────────────────
    // Lee y parsea el manifest de la carpeta, devuelve los ítems para la UI.
    bool loadRemoteManifest(BackupManifest& out,
                            std::function<void(const std::string&)> log);

    // Restaura los ítems seleccionados.
    BackupSummary runRestore(const BackupManifest& manifest,
                             ProgressCb progress,
                             std::function<void(const std::string&)> log);

    // Subcarpeta y manifest dentro de la carpeta elegida.
    static constexpr const char* kRemoteRoot   = "obs-cloud-backup";
    static constexpr const char* kManifestFile = "obs-cloud-backup/manifest.json";

    // Carpeta destino de las copias (pública para el binding del diálogo).
    std::string m_localPath;

    // Opcional: renombrar la colección de escenas restaurada (vacío = mantener su
    // nombre original). Tras un restore también se la deja como la activa en OBS.
    std::string m_restoreSceneName;

    // Último error (vacío = sin error).
    std::string lastError() const { return m_lastError; }

private:
    // Copia un archivo local HACIA / DESDE la carpeta de copias (ruta lógica
    // relativa, ej. "obs-cloud-backup/scenes/x.json"). Setea m_lastError si falla.
    bool copyIntoFolder(const std::string& localSrc, const std::string& remoteRel);
    bool copyFromFolder(const std::string& remoteRel, const std::string& localDest);

    std::string configPath() const;

    OBSPaths            m_obsPaths;
    mutable std::string m_lastError;
};
