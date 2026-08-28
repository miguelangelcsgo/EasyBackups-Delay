#include "backup-dialog.hpp"
#include "delay-dock.hpp"
#include "web-dock.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QMessageBox>
#include <QTimer>
#include <exception>

OBS_DECLARE_MODULE()

// ─────────────────────────────────────────────────────────────────────────────
// Locale: hooks vacíos a propósito, sin OBS_MODULE_USE_DEFAULT_LOCALE.
//
// Todos los textos del plugin salen de i18n.hpp (tabla propia en 5 idiomas) y no
// hay una sola llamada a obs_module_text(). Con el macro por defecto, libobs
// buscaba en cada arranque un data/obs-plugins/EasyBackupandDelay/locale/en-US.ini
// que nunca empaquetamos (el ZIP y el instalador llevan solo la DLL + FFmpeg), y
// dejaba "Failed to load 'en-US' text for module" en el log al instalar una
// version nueva. Definimos las tres funciones vacías —libobs las busca por
// nombre— para que no intente cargar ningún archivo.
// ─────────────────────────────────────────────────────────────────────────────
const char* obs_module_text(const char* val)
{
    return val;
}

bool obs_module_get_string(const char*, const char**)
{
    return false;
}

void obs_module_set_locale(const char*) {}
void obs_module_free_locale(void) {}

MODULE_EXPORT const char* obs_module_description(void)
{
    return "EasyBackupandDelay: local backup/restore of scenes, profiles and media, plus per-source video/audio delay filters.";
}

// Definidas en delay-filters.cpp
void register_delay_filters(void);
void unregister_delay_filters(void);
void easyobs_sync_bypass_from_filters(void);

// Definida en delay-codec.cpp: ¿están las DLLs de FFmpeg que necesita el delay?
bool easyobs_ffmpeg_available();

// Definidas en delay-visibility.cpp: las fuentes que siguen al interruptor de delays.
void easyobs_load_delay_visibility(void);
void easyobs_apply_delay_visibility(void);

// Definidas en mic-mute.cpp: botón/atajo de silenciar el micrófono.
void easyobs_load_mic_settings(void);
void easyobs_register_mic_hotkey(void);
void easyobs_unregister_mic_hotkey(void);

// Definidas en delay-playing.cpp: luz roja/verde del audio demorado y su aviso.
void easyobs_load_playing_settings(void);
void easyobs_shutdown_alert(void);
void easyobs_apply_delay_monitor(void);

// ─────────────────────────────────────────────────────────────────────────────

// Después de cargar una colección de escenas, sus filtros de delay pueden haberse
// guardado DESHABILITADOS (el "ojo" está apagado). Adoptamos ese estado para que
// el botón del dock avise en vez de mostrar los delays como activos por error.
static void on_frontend_event(enum obs_frontend_event event, void* /*data*/)
{
    // Invocado por OBS como callback de C: nunca dejar que una excepción de C++ se
    // propague a través del marco de C (eso es comportamiento indefinido / std::terminate → OBS crashea).
    try {
        if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING ||
            event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
            easyobs_sync_bypass_from_filters();
            // Dejar las fuentes atadas al delay coherentes con el estado recién
            // adoptado (si no, la colección arranca con la pantalla equivocada).
            easyobs_apply_delay_visibility();
            // El monitor se fija por fuente, así que hay que reaplicarlo cuando
            // cambia la colección de escenas.
            easyobs_apply_delay_monitor();
        }

        // El plugin no habla con ningún servidor: no hay chequeo de
        // actualizaciones ni envío de reportes. Se actualiza a mano.
        if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
            // Si el delay no puede arrancar porque faltan/no coinciden las DLLs de
            // FFmpeg de OBS, avisarlo UNA vez con un diálogo (el resto del plugin sigue
            // funcionando). Así el usuario sabe por qué el delay "muestra el directo".
            static bool ffmpeg_checked = false;
            if (!ffmpeg_checked) {
                ffmpeg_checked = true;
                if (!easyobs_ffmpeg_available()) {
                    auto* mainWin = static_cast<QMainWindow*>(obs_frontend_get_main_window());
                    if (mainWin) {
                        QTimer::singleShot(4500, mainWin, [mainWin]() {
                            QMessageBox::warning(mainWin,
                                "EasyBackupandDelay — delay desactivado",
                                "El delay de video no puede iniciar: falta la carpeta "
                                "'EasyBackupandDelay-ffmpeg' con las librerías FFmpeg propias "
                                "del plugin. Suele pasar si actualizaste SOLO la DLL por el "
                                "auto-update.\n\n"
                                "Solución: reinstalá el plugin con el INSTALADOR completo "
                                "(trae FFmpeg incluido):\n"
                                "https://mavsoft.com.ar/easybackupanddelay/\n\n"
                                "El backup y el resto del plugin funcionan igual. Mientras "
                                "tanto, el filtro de delay se ve en ROJO en vez de pasar el "
                                "directo en vivo.");
                        });
                    }
                }
            }
        }
    } catch (const std::exception&) {
        // Sin telemetría: una excepción del callback no puede tumbar OBS.
    } catch (...) {
    }
}

static void openBackupDialog(void* /*data*/)
{
    try {
        auto* mainWin = static_cast<QMainWindow*>(obs_frontend_get_main_window());
        BackupDialog dlg(mainWin);
        dlg.exec();
    } catch (const std::exception&) {
    } catch (...) {
    }
}

bool obs_module_load(void)
{
    obs_frontend_add_tools_menu_item(
        "Cloud Backup / Restore…",
        openBackupDialog,
        nullptr);

    easyobs_load_delay_visibility();
    easyobs_load_mic_settings();
    easyobs_load_playing_settings();
    easyobs_register_mic_hotkey();
    register_delay_filters();
    register_delay_dock();
    register_web_dock();
    obs_frontend_add_event_callback(on_frontend_event, nullptr);

    return true;
}

void obs_module_unload(void)
{
    obs_frontend_remove_event_callback(on_frontend_event, nullptr);
    unregister_web_dock();
    unregister_delay_dock();
    easyobs_unregister_mic_hotkey();
    easyobs_shutdown_alert();
    unregister_delay_filters();
}
