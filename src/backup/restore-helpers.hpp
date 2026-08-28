#pragma once
#include "backup-manifest.hpp"

#include <string>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers de restauración
//
// Toda la plomería de rutas y de configuración que necesita el restore, separada
// de BackupManager para que el orquestador quede claro:
//   • remapeo de rutas entre máquinas (perfil de usuario distinto),
//   • reescritura de rutas embebidas en los .json/.ini restaurados,
//   • edición de los .ini de OBS para activar la colección restaurada,
//   • ruta de la propia DLL + textos del instalador que viajan en la copia.
// ─────────────────────────────────────────────────────────────────────────────

namespace restore_helpers {

using LogFn = std::function<void(const std::string&)>;

// Reapunta una ruta bajo un perfil de usuario (C:\Users\OTRO\...) al usuario
// actual de ESTA máquina. Rutas fuera de un perfil se dejan igual.
std::string remapUserPath(const std::string& original);

// Valida que un destino de restore sea una ruta absoluta sin ".." (el manifest
// viene de otra máquina y no es de fiar). why = motivo del rechazo.
bool restoreDestSafe(const std::string& dest, std::string& why);

// Deriva el perfil de la máquina origen ("<disco>:\Users\<viejo>") del manifest.
std::string oldProfileRoot(const BackupManifest& manifest);

// Reescribe las rutas embebidas dentro de un archivo de texto restaurado
// (.json/.ini) para que apunten al usuario de esta máquina. Devuelve nº de hits.
int rewriteFileContents(const std::string& destPath, const std::string& oldRoot, LogFn log);

// OBS deriva el NOMBRE DE ARCHIVO de una colección a partir de su nombre visible
// reemplazando caracteres inválidos (y espacios) por '_'.
std::string sanitizeCollectionFile(const std::string& name);

// Lee / escribe el "name" visible dentro del JSON de una colección de escenas.
std::string readSceneCollectionName(const std::string& jsonPath);
void        setSceneCollectionName(const std::string& jsonPath, const std::string& name, LogFn log);

// Deja la colección restaurada como la activa (editando los .ini de OBS).
void activateSceneCollection(const std::string& display, const std::string& fileBase, LogFn log);

// Ruta completa a la propia DLL del plugin (para llevarla a otra PC en la copia).
std::string selfModulePath();

// Textos que se suben junto a la DLL para instalarla en otra PC.
extern const char* kInstallReadme;
extern const char* kInstallScript;

}  // namespace restore_helpers
