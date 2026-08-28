#include "restore-helpers.hpp"

#include <nlohmann/json.hpp>
#include <obs-module.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <cctype>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace restore_helpers {

// ── Remapeo de rutas entre máquinas ─────────────────────────────────────────
// El manifest guarda rutas absolutas de la máquina donde se hizo la copia
// (ej. C:\Users\OTRO\AppData\Roaming\obs-studio\...). Al restaurar hay que
// reapuntar lo que esté bajo un perfil de usuario al usuario actual de ESTA
// máquina; si no, la copia cae en un C:\Users\OTRO inexistente y falla. Las
// rutas fuera de un perfil (ej. C:\Video\clip.mp4) se dejan intactas. Una copia
// del mismo usuario es un no-op (el nombre viejo ya es el actual).
std::string remapUserPath(const std::string& original)
{
    if (original.empty()) return original;

    const char* profile = std::getenv("USERPROFILE");   // ej. C:\Users\usuario
    if (!profile || !*profile) return original;

    // Búsqueda case-insensitive de un segmento "\Users\<nombre>\" (o con /).
    std::string lower(original.size(), '\0');
    for (size_t i = 0; i < original.size(); ++i)
        lower[i] = (char)std::tolower((unsigned char)original[i]);

    size_t pos = lower.find("\\users\\");
    if (pos == std::string::npos) pos = lower.find("/users/");
    if (pos == std::string::npos) return original;       // no está bajo un perfil

    // Sólo una RAÍZ DE DISCO "<X>:\Users\" es un perfil real. Un segmento anidado
    // como "E:\Shared\Users\bob\clip.mp4" hay que dejarlo (remapearlo corrompería
    // la ruta). Una raíz de disco pone "\Users\" en el índice 2 ("C:").
    if (!(pos == 2 && original.size() > 1 && original[1] == ':'))
        return original;

    // Saltar "\Users\" (7 chars) hasta el nombre viejo, y luego hasta su fin.
    size_t nameStart = pos + 7;
    size_t nameEnd   = original.find_first_of("/\\", nameStart);

    std::string tail = (nameEnd == std::string::npos) ? std::string()
                                                      : original.substr(nameEnd);
    return std::string(profile) + tail;                  // <perfil>\AppData\...
}

// Un restore escribe en rutas tomadas del manifest, que se produjo en otra
// máquina y por lo tanto no es de fiar. Rechaza cualquier cosa que no sea una
// ruta absoluta simple, para que un manifest armado/corrupto no pueda salirse
// con ".." ni escribir relativo al directorio de trabajo. (La media se restaura
// legítimamente a carpetas arbitrarias del usuario, así que no restringimos más
// la ubicación.)
bool restoreDestSafe(const std::string& dest, std::string& why)
{
    if (dest.empty()) { why = "empty path"; return false; }
    fs::path p = fs::u8path(dest);
    if (!p.is_absolute()) { why = "not an absolute path"; return false; }
    for (const auto& part : p)
        if (part == "..") { why = "contains a '..' segment"; return false; }
    return true;
}

// Deriva el perfil de la máquina origen ("<disco>:\Users\<viejo>") del manifest
// — las escenas/perfiles siempre viven bajo el %APPDATA% de esa máquina.
// Devuelve vacío si no se puede determinar.
std::string oldProfileRoot(const BackupManifest& manifest)
{
    auto scan = [](const std::string& p) -> std::string {
        std::string lower(p.size(), '\0');
        for (size_t i = 0; i < p.size(); ++i)
            lower[i] = (char)std::tolower((unsigned char)p[i]);
        size_t pos = lower.find("\\users\\");
        if (pos == std::string::npos) pos = lower.find("/users/");
        if (pos == std::string::npos) return {};
        size_t nameEnd = p.find_first_of("/\\", pos + 7);
        if (nameEnd == std::string::npos) return {};
        return p.substr(0, nameEnd);                      // <disco>:\Users\<viejo>
    };
    for (auto& x : manifest.sceneCollections) { auto r = scan(x.originalLocal); if (!r.empty()) return r; }
    for (auto& x : manifest.profiles)         { auto r = scan(x.originalLocal); if (!r.empty()) return r; }
    return {};
}

// Reescribe las referencias de rutas embebidas dentro de un archivo de texto
// restaurado (colección .json, basic.ini, service.json) para que la media/las
// fuentes apunten al usuario de ESTA máquina en vez del de la copia. Cubre rutas
// con barra normal (JSON de OBS), backslash crudo (ini) y backslash escapado en
// JSON.
int rewriteFileContents(const std::string& destPath,
                        const std::string& oldRoot,
                        LogFn log)
{
    if (oldRoot.empty()) return 0;
    const char* up = std::getenv("USERPROFILE");
    if (!up || !*up) return 0;
    std::string newRoot = up;                             // ej. C:\Users\usuario

    std::ifstream in(fs::u8path(destPath), std::ios::binary);
    if (!in) return 0;
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    in.close();

    auto toFwd = [](std::string s) { for (auto& c : s) if (c == '\\') c = '/'; return s; };
    auto esc   = [](const std::string& s) { std::string o; for (char c : s) { if (c == '\\') o += "\\\\"; else o += c; } return o; };

    auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
        if (from.empty()) return 0;
        int n = 0; size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to); pos += to.size(); ++n;
        }
        return n;
    };

    int hits = 0;
    hits += replaceAll(data, esc(oldRoot), esc(newRoot));   // backslash escapado en JSON
    hits += replaceAll(data, toFwd(oldRoot), toFwd(newRoot)); // barra normal (JSON de OBS)
    hits += replaceAll(data, oldRoot, newRoot);             // backslash crudo (ini)

    if (hits > 0) {
        std::ofstream out(fs::u8path(destPath), std::ios::binary | std::ios::trunc);
        out.write(data.data(), (std::streamsize)data.size());
        out.close();
        if (log) log("  Remapeadas " + std::to_string(hits) + " ruta(s) dentro de "
                     + fs::u8path(destPath).filename().u8string());
        blog(LOG_INFO, "[EasyBackupandDelay]   reescritas %d ruta(s) embebida(s) en %s",
             hits, destPath.c_str());
    }
    return hits;
}

// OBS deriva el NOMBRE DE ARCHIVO de una colección a partir de su nombre visible
// reemplazando por '_' los caracteres inválidos en un nombre de archivo (y los
// espacios).
std::string sanitizeCollectionFile(const std::string& name)
{
    std::string out;
    for (char c : name) {
        switch (c) {
            case ' ': case '<': case '>': case ':': case '"':
            case '/': case '\\': case '|': case '?': case '*':
                out += '_'; break;
            default: out += c;
        }
    }
    if (out.empty()) out = "Untitled";
    return out;
}

// Lee el "name" visible del JSON de una colección de escenas.
std::string readSceneCollectionName(const std::string& jsonPath)
{
    std::ifstream in(fs::u8path(jsonPath), std::ios::binary);
    if (!in) return {};
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    try { return json::parse(data).value("name", std::string()); }
    catch (...) { return {}; }
}

// Escribe el "name" visible dentro del JSON de una colección (lo que muestra OBS).
void setSceneCollectionName(const std::string& jsonPath,
                            const std::string& name,
                            LogFn log)
{
    std::ifstream in(fs::u8path(jsonPath), std::ios::binary);
    if (!in) return;
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    in.close();
    try {
        json j = json::parse(data);
        j["name"] = name;
        std::string s = j.dump(4);
        std::ofstream out(fs::u8path(jsonPath), std::ios::binary | std::ios::trunc);
        out.write(s.data(), (std::streamsize)s.size());
        if (log) log("  Colección renombrada a \"" + name + "\"");
    } catch (const std::exception& e) {
        blog(LOG_WARNING, "[EasyBackupandDelay] no se pudo fijar el nombre de la colección: %s", e.what());
    }
}

// Reemplaza in-place el valor de una línea "clave=..." en un .ini de OBS.
// Devuelve false si la clave no estaba.
static bool setIniKey(const std::string& iniPath,
                      const std::string& key, const std::string& value)
{
    std::ifstream in(fs::u8path(iniPath), std::ios::binary);
    if (!in) return false;
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    in.close();

    const std::string prefix = key + "=";
    size_t pos = 0;
    while (pos <= data.size()) {
        if (data.compare(pos, prefix.size(), prefix) == 0 &&
            (pos == 0 || data[pos - 1] == '\n')) {
            size_t end = data.find('\n', pos);
            if (end == std::string::npos) end = data.size();
            data.replace(pos, end - pos, prefix + value);
            std::ofstream out(fs::u8path(iniPath), std::ios::binary | std::ios::trunc);
            out.write(data.data(), (std::streamsize)data.size());
            return true;
        }
        size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return false;
}

// Deja la colección restaurada como la activa, para que OBS la abra tras el
// reinicio. OBS lo guarda en user.ini (más nuevo) o global.ini (más viejo).
void activateSceneCollection(const std::string& display,
                             const std::string& fileBase,
                             LogFn log)
{
    const char* appdata = std::getenv("APPDATA");
    if (!appdata || !*appdata) return;
    const std::string dir = std::string(appdata) + "\\obs-studio\\";
    const std::string fileWithExt = fileBase + ".json";

    for (const char* f : { "user.ini", "global.ini" }) {
        const std::string path = dir + f;
        bool a = setIniKey(path, "SceneCollection",     display);
        bool b = setIniKey(path, "SceneCollectionFile", fileWithExt);
        if (a || b) {
            if (log) log("  Colección activa fijada en \"" + display + "\"");
            blog(LOG_INFO, "[EasyBackupandDelay]   activada '%s' (%s) en %s",
                 display.c_str(), fileWithExt.c_str(), f);
        }
    }
}

// Ruta completa a la propia DLL del plugin (para llevarla a otra PC en la copia).
std::string selfModulePath()
{
    HMODULE hm = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&selfModulePath), &hm))
        return {};

    // Agranda el buffer hasta que la ruta completa entre (una ruta de instalación
    // profunda puede pasar de 1024 wchars); GetModuleFileNameW trunca y devuelve
    // el tamaño del buffer cuando no entra, así que iteramos hasta que reporte
    // menos chars de los que le dimos.
    std::vector<wchar_t> buf(1024);
    DWORD n;
    for (;;) {
        n = GetModuleFileNameW(hm, buf.data(), (DWORD)buf.size());
        if (n == 0) return {};
        if (n < buf.size()) break;             // copiado completo
        if (buf.size() >= 32768) return {};    // techo de rutas de Windows — cortar
        buf.resize(buf.size() * 2);
    }

    int sz = WideCharToMultiByte(CP_UTF8, 0, buf.data(), (int)n, nullptr, 0, nullptr, nullptr);
    std::string out(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf.data(), (int)n, out.data(), sz, nullptr, nullptr);
    return out;
}

const char* kInstallReadme =
    "EasyBackupandDelay - install on another PC\r\n"
    "======================================\r\n"
    "\r\n"
    "1. Install OBS Studio 28+ (Qt6 build) if it isn't already.\r\n"
    "2. Close OBS completely.\r\n"
    "3. Copy EasyBackupandDelay.dll into:\r\n"
    "     C:\\Program Files\\obs-studio\\obs-plugins\\64bit\\EasyBackupandDelay.dll\r\n"
    "   (needs administrator - or just right-click install.ps1 > Run with PowerShell,\r\n"
    "    which does it for you.)\r\n"
    "4. Start OBS. You'll see Tools > Cloud Backup / Restore and the delay filters.\r\n"
    "   No other files are needed: OBS already ships the Qt6 and libcurl libraries\r\n"
    "   the plugin uses.\r\n"
    "5. To bring your scenes/media back: Tools > Cloud Backup / Restore > Settings,\r\n"
    "   choose 'Local folder' pointing at THIS backup folder, then Restore tab >\r\n"
    "   Refresh > Restore selected backup. Restart OBS afterwards.\r\n";

const char* kInstallScript =
    "# Installs EasyBackupandDelay.dll (sitting next to this script) into OBS.\r\n"
    "$ErrorActionPreference = 'Stop'\r\n"
    "$here = Split-Path -Parent $MyInvocation.MyCommand.Path\r\n"
    "$dll  = Join-Path $here 'EasyBackupandDelay.dll'\r\n"
    "$dst  = 'C:\\Program Files\\obs-studio\\obs-plugins\\64bit\\EasyBackupandDelay.dll'\r\n"
    "if (-not (Test-Path $dll)) { throw 'EasyBackupandDelay.dll not found next to this script' }\r\n"
    "if (Get-Process obs64 -ErrorAction SilentlyContinue) { throw 'Close OBS first (it locks the DLL)' }\r\n"
    "$copy = \"Copy-Item -LiteralPath '$dll' -Destination '$dst' -Force\"\r\n"
    "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-Command',$copy\r\n"
    "if ((Test-Path $dst) -and ((Get-Item $dst).Length -eq (Get-Item $dll).Length)) {\r\n"
    "  Write-Host 'Installed. Start OBS.' -ForegroundColor Green\r\n"
    "} else { throw 'Install failed (UAC cancelled or file locked)' }\r\n";

}  // namespace restore_helpers
