#include "backup-manager.hpp"
#include "restore-helpers.hpp"

#include <nlohmann/json.hpp>
#include <obs-module.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cctype>

namespace fs = std::filesystem;
using json   = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// BackupManager — orquesta la copia y restauración hacia/desde una carpeta local.
// El modelo de datos vive en backup-manifest.*, y la plomería de rutas/ini en
// restore-helpers.*; acá queda la lógica de alto nivel y la copia de archivos.
// ─────────────────────────────────────────────────────────────────────────────

// Resuelve una ruta lógica ("obs-cloud-backup/scenes/x.json") a una ruta absoluta
// dentro de la carpeta base. Todo se trata como UTF-8 (fs::u8path) para no romper
// los nombres con acentos en Windows. Tolera que el usuario haya apuntado la
// carpeta directamente al "obs-cloud-backup" en vez de a su padre.
static fs::path resolveInFolder(const std::string& basePath, const std::string& remote)
{
    fs::path base = fs::u8path(basePath);
    fs::path leaf = base.filename();
    if (leaf.empty()) { base = base.parent_path(); leaf = base.filename(); }  // '\' final
    if (leaf == fs::u8path("obs-cloud-backup"))
        base = base.parent_path();
    return base / fs::u8path(remote);
}

BackupManager::BackupManager()
{
    m_obsPaths = SceneParser::findObsPaths();
}

bool BackupManager::folderReady()
{
    if (m_localPath.empty()) {
        m_lastError = "No hay carpeta de copia configurada (elegila en Ajustes).";
        return false;
    }
    std::error_code ec;
    fs::path base = fs::u8path(m_localPath);
    fs::create_directories(base, ec);
    if (!fs::is_directory(base, ec)) {
        m_lastError = "No se puede usar la carpeta: " + m_localPath;
        return false;
    }
    m_lastError.clear();
    return true;
}

bool BackupManager::copyIntoFolder(const std::string& localSrc, const std::string& remoteRel)
{
    std::error_code ec;
    fs::path src  = fs::u8path(localSrc);
    fs::path dest = resolveInFolder(m_localPath, remoteRel);
    fs::create_directories(dest.parent_path(), ec);
    fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) { m_lastError = "Copia fallida (" + localSrc + "): " + ec.message(); return false; }
    return true;
}

bool BackupManager::copyFromFolder(const std::string& remoteRel, const std::string& localDest)
{
    std::error_code ec;
    fs::path src  = resolveInFolder(m_localPath, remoteRel);
    fs::path dest = fs::u8path(localDest);
    if (!fs::exists(src, ec)) { m_lastError = "No está en la carpeta de copia: " + remoteRel; return false; }
    fs::create_directories(dest.parent_path(), ec);
    fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) { m_lastError = "Copia fallida (" + remoteRel + "): " + ec.message(); return false; }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Persistencia de la configuración
// ─────────────────────────────────────────────────────────────────────────────

std::string BackupManager::configPath() const
{
    char* cp = obs_module_config_path("cloud-backup.json");
    std::string p(cp ? cp : "");
    bfree(cp);
    return p;
}

void BackupManager::loadConfig()
{
    std::string cp = configPath();
    if (cp.empty()) return;

    std::ifstream f(fs::u8path(cp));   // u8path: la carpeta de config puede tener no-ASCII
    if (!f.is_open()) return;

    try {
        auto j = json::parse(f);
        m_localPath = j.value("local_path", "");
    } catch (...) {}
}

void BackupManager::saveConfig() const
{
    std::string cp = configPath();
    if (cp.empty()) return;

    std::error_code ec;
    fs::create_directories(fs::u8path(cp).parent_path(), ec);
    if (ec) return;

    json j;
    j["local_path"] = m_localPath;

    std::ofstream f(fs::u8path(cp));
    f << j.dump(2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Backup
// ─────────────────────────────────────────────────────────────────────────────

static std::string nowIso()
{
    auto now   = std::chrono::system_clock::now();
    auto timet = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_s(&tm, &timet);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

BackupSummary BackupManager::runBackup(ProgressCb progress,
                                        std::function<void(const std::string&)> log)
{
    BackupSummary result;

    if (!folderReady()) {
        result.errorMsg = m_lastError;
        return result;
    }

    log("Building backup list…");

    BackupManifest manifest;
    manifest.createdAt    = nowIso();
    manifest.obsVersion   = obs_get_version_string();
    manifest.providerName = "Local Folder";

    // ── Colecciones de escenas ───────────────────────────────────────────────
    auto collections = SceneParser::listCollections(m_obsPaths);
    for (auto& sc : collections) {
        RestoreItem item;
        item.name          = fs::u8path(sc.jsonPath).filename().u8string();
        item.remotePath    = std::string(kRemoteRoot) + "/scenes/" + item.name;
        item.originalLocal = sc.jsonPath;
        std::error_code ec; item.size = (int64_t)fs::file_size(fs::u8path(sc.jsonPath), ec);
        manifest.sceneCollections.push_back(item);
    }

    // ── Perfiles (copiar cada archivo dentro de cada carpeta de perfil) ──────
    auto profiles = SceneParser::listProfiles(m_obsPaths);
    fs::path profilesRoot = fs::u8path(m_obsPaths.profilesDir);
    for (auto& profileName : profiles) {
        fs::path profileDir = profilesRoot / fs::u8path(profileName);
        std::error_code ec;
        // Iterar con la sobrecarga increment(ec) para que un error de E/S A MITAD
        // de la iteración (subcarpeta sin permiso, disco desconectado) corte el
        // recorrido con gracia en vez de lanzar un filesystem_error.
        fs::recursive_directory_iterator it(profileDir, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            const auto& entry = *it;
            std::error_code fec;
            if (!entry.is_regular_file(fec)) continue;
            RestoreItem item;
            item.name          = entry.path().filename().u8string();
            item.originalLocal = entry.path().u8string();

            // Ruta relativa desde profilesDir para restaurar en el lugar correcto.
            fs::path rel = fs::relative(entry.path(), profilesRoot, fec);
            item.remotePath = std::string(kRemoteRoot) + "/profiles/"
                            + rel.generic_u8string();
            item.size = (int64_t)fs::file_size(entry.path(), fec);
            manifest.profiles.push_back(item);
        }
    }

    // ── Archivos multimedia ──────────────────────────────────────────────────
    auto media = SceneParser::allMediaPaths(m_obsPaths);
    for (auto& p : media) {
        RestoreItem item;
        item.name          = fs::u8path(p).filename().u8string();
        item.originalLocal = p;

        // Aplanar nombres – usar un hash de la ruta para evitar colisiones.
        std::string hash = std::to_string(std::hash<std::string>{}(p));
        item.remotePath = std::string(kRemoteRoot) + "/media/"
                        + hash + "_" + item.name;
        std::error_code ec; item.size = (int64_t)fs::file_size(fs::u8path(p), ec);
        manifest.mediaFiles.push_back(item);
    }

    blog(LOG_INFO, "[EasyBackupandDelay] Backup set: %zu scene(s), %zu profile file(s), %zu media file(s)",
         manifest.sceneCollections.size(), manifest.profiles.size(), manifest.mediaFiles.size());
    log("Found " + std::to_string(manifest.sceneCollections.size()) + " scene(s), "
        + std::to_string(manifest.profiles.size()) + " profile file(s), "
        + std::to_string(manifest.mediaFiles.size()) + " media file(s).");
    for (auto& m : manifest.mediaFiles)
        blog(LOG_INFO, "[EasyBackupandDelay]   media: %s", m.originalLocal.c_str());

    // ── Copiar todos los archivos a la carpeta ────────────────────────────────
    std::vector<RestoreItem*> allItems;
    for (auto& x : manifest.sceneCollections) allItems.push_back(&x);
    for (auto& x : manifest.profiles)         allItems.push_back(&x);
    for (auto& x : manifest.mediaFiles)       allItems.push_back(&x);

    result.totalFiles = (int)allItems.size();
    int current = 0;

    for (auto* item : allItems) {
        ++current;
        log("Uploading: " + item->name + " (" + std::to_string(current)
            + "/" + std::to_string(result.totalFiles) + ")");

        if (copyIntoFolder(item->originalLocal, item->remotePath))
            ++result.uploadedFiles;
        else {
            ++result.skippedFiles;
            log("  Skipped: " + item->name + " — " + m_lastError);
            blog(LOG_WARNING, "[EasyBackupandDelay] Skipped %s (%s) — %s",
                 item->name.c_str(), item->originalLocal.c_str(), m_lastError.c_str());
        }
        if (progress) progress(item->name, 100, current, result.totalFiles);
    }

    // ── Binario del plugin + helper de instalación (para dejarlo en otra PC) ──
    {
        std::string dllPath = restore_helpers::selfModulePath();
        if (!dllPath.empty()) {
            std::string dllName   = fs::u8path(dllPath).filename().u8string();
            std::string pluginDir = std::string(kRemoteRoot) + "/plugin/";
            log("Backing up plugin: " + dllName);
            blog(LOG_INFO, "[EasyBackupandDelay] Backing up plugin binary: %s", dllPath.c_str());

            if (!copyIntoFolder(dllPath, pluginDir + dllName))
                log("  Warning: plugin backup failed — " + m_lastError);

            std::error_code pec;
            fs::path tmpDir = fs::temp_directory_path(pec);

            fs::path tmpTxt = tmpDir / "EasyBackupandDelay_INSTALL.txt";
            { std::ofstream f(tmpTxt); f << restore_helpers::kInstallReadme; }
            copyIntoFolder(tmpTxt.u8string(), pluginDir + "INSTALL.txt");
            fs::remove(tmpTxt, pec);

            fs::path tmpPs1 = tmpDir / "EasyBackupandDelay_install.ps1";
            { std::ofstream f(tmpPs1); f << restore_helpers::kInstallScript; }
            copyIntoFolder(tmpPs1.u8string(), pluginDir + "install.ps1");
            fs::remove(tmpPs1, pec);
        }
    }

    // ── Escribir el manifest ──────────────────────────────────────────────────
    log("Uploading manifest…");
    std::error_code mec;
    fs::path tmpManifest = fs::temp_directory_path(mec) / "obs_backup_manifest.json";
    { std::ofstream f(tmpManifest); f << manifest.toJson(); }
    if (!copyIntoFolder(tmpManifest.u8string(), kManifestFile))
        log("  Warning: manifest write failed — " + m_lastError);
    fs::remove(tmpManifest, mec);

    log("Backup complete. " + std::to_string(result.uploadedFiles)
        + " files copied, " + std::to_string(result.skippedFiles) + " skipped.");

    result.success = (result.uploadedFiles > 0 || result.totalFiles == 0);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Restore
// ─────────────────────────────────────────────────────────────────────────────

bool BackupManager::loadRemoteManifest(BackupManifest& out,
                                        std::function<void(const std::string&)> log)
{
    if (!folderReady()) {
        log(m_lastError);
        return false;
    }

    log("Reading manifest…");
    std::error_code tec;
    fs::path tmpPath = fs::temp_directory_path(tec) / "obs_manifest_dl.json";

    if (!copyFromFolder(kManifestFile, tmpPath.u8string())) {
        log("No se encontró ninguna copia en la carpeta: " + m_lastError);
        return false;
    }

    std::ifstream f(tmpPath);
    std::string data((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    fs::remove(tmpPath, tec);

    out = BackupManifest::fromJson(data);
    log("Manifest loaded. Backup from: " + out.createdAt);
    return true;
}

BackupSummary BackupManager::runRestore(const BackupManifest& manifest,
                                         ProgressCb progress,
                                         std::function<void(const std::string&)> log)
{
    BackupSummary result;

    if (!folderReady()) {
        result.errorMsg = m_lastError;
        log("Restore failed: " + m_lastError);
        return result;
    }

    std::vector<const RestoreItem*> all;
    for (auto& x : manifest.sceneCollections) all.push_back(&x);
    for (auto& x : manifest.profiles)         all.push_back(&x);
    for (auto& x : manifest.mediaFiles)       all.push_back(&x);

    result.totalFiles = (int)all.size();
    int current = 0;

    // Perfil de la máquina origen, para reescribir las rutas embebidas dentro de
    // los archivos de config restaurados (referencias a media, etc.).
    const std::string oldRoot = restore_helpers::oldProfileRoot(manifest);

    // La primera colección de escenas es la "primaria": se puede renombrar y se
    // deja como colección activa tras el restore.
    const size_t nScenes  = manifest.sceneCollections.size();
    const std::string want = m_restoreSceneName;   // vacío = mantener el nombre original
    std::string activeDisplay, activeFileBase;
    bool haveActive = false;

    blog(LOG_INFO, "[EasyBackupandDelay] Restore: %d file(s)", result.totalFiles);

    size_t idx = 0;
    for (auto* item : all) {
        const bool isPrimaryScene = (idx == 0 && nScenes > 0);
        ++idx;
        ++current;

        // Reapuntar la ruta del manifest al perfil de usuario de esta máquina.
        std::string dest = restore_helpers::remapUserPath(item->originalLocal);

        // Renombrar el archivo de la colección primaria si se dio un nombre custom.
        if (isPrimaryScene && !want.empty()) {
            std::string fileBase = restore_helpers::sanitizeCollectionFile(want);
            dest = (fs::u8path(dest).parent_path()
                    / fs::u8path(fileBase + ".json")).u8string();
        }

        log("Restoring: " + item->name);
        blog(LOG_INFO, "[EasyBackupandDelay] Restoring %s -> %s",
             item->remotePath.c_str(), dest.c_str());

        // Nunca escribir en un destino inseguro (no-absoluto / con traversal).
        std::string why;
        if (!restore_helpers::restoreDestSafe(dest, why)) {
            ++result.skippedFiles;
            log("  Skipped (unsafe path: " + why + "): " + dest);
            blog(LOG_WARNING, "[EasyBackupandDelay] Restore refused unsafe path (%s): %s",
                 why.c_str(), dest.c_str());
            continue;
        }

        // Respaldar el archivo existente antes de sobrescribirlo, para que un
        // restore malo/parcial no destruya los datos actuales del usuario. Se
        // conserva sólo el PRIMER respaldo (el original real previo); best-effort.
        {
            std::error_code bec;
            fs::path destP = fs::u8path(dest);
            if (fs::exists(destP, bec)) {
                fs::path bak = destP; bak += ".easybak";
                if (!fs::exists(bak, bec))
                    fs::copy_file(destP, bak, bec);
            }
        }

        if (copyFromFolder(item->remotePath, dest)) {
            ++result.uploadedFiles;

            // Los archivos de config llevan rutas absolutas de la máquina origen
            // adentro — reescribirlas para que las escenas restauradas encuentren
            // su media acá.
            std::string ext = fs::u8path(dest).extension().u8string();
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext == ".json" || ext == ".ini")
                restore_helpers::rewriteFileContents(dest, oldRoot, log);

            if (isPrimaryScene) {
                if (!want.empty())
                    restore_helpers::setSceneCollectionName(dest, want, log);
                std::string display = !want.empty() ? want
                                                     : restore_helpers::readSceneCollectionName(dest);
                if (display.empty())
                    display = fs::u8path(dest).stem().u8string();
                activeDisplay  = display;
                activeFileBase = fs::u8path(dest).stem().u8string();
                haveActive     = true;
            }
        }
        else {
            ++result.skippedFiles;
            log("  Skipped (error): " + m_lastError);
            blog(LOG_WARNING, "[EasyBackupandDelay] Restore skipped %s — %s",
                 item->name.c_str(), m_lastError.c_str());
        }

        if (progress) progress(item->name, 100, current, result.totalFiles);
    }

    // Dejar la colección restaurada como la activa, para que OBS la abra al reiniciar.
    if (haveActive)
        restore_helpers::activateSceneCollection(activeDisplay, activeFileBase, log);

    // Resultado honesto: un restore sólo "tuvo éxito" si no se saltó nada y al
    // menos un archivo volvió (un manifest vacío cuenta como éxito).
    result.success = (result.skippedFiles == 0)
                     && (result.uploadedFiles > 0 || result.totalFiles == 0);
    if (result.success)
        log("Restore complete. " + std::to_string(result.uploadedFiles)
            + " files restored.");
    else
        log("Restore finished with problems: " + std::to_string(result.uploadedFiles)
            + " restored, " + std::to_string(result.skippedFiles) + " skipped.");
    return result;
}
