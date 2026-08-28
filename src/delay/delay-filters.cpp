// ─────────────────────────────────────────────────────────────────────────────
// delay-filters.cpp
//
// Dos filtros de OBS que agregan un "delay de transmisión" a una sola fuente sin
// tocar el resto de la escena:
//
//   • "Screen Delay (video)"  – aplicalo a tu captura de pantalla / juego / ventana.
//   • "Screen Delay (audio)"  – aplicalo al audio de escritorio / juego que corresponde.
//
// Poné ambos con el mismo delay (ej. 20 s): el plugin bufferea esos segundos y
// después los reproduce demorados, en sync, mientras tu cámara / mic siguen en vivo.
//
// Notas del MVP
// ─────────────
//   • Los frames de video se bufferean como texturas de GPU (VRAM). El tamaño del
//     buffer está acotado por el delay configurable, el buffer FPS y la escala de
//     resolución para que los delays largos sigan siendo accesibles. Una fase futura
//     pasará a un buffer comprimido para que 1080p60 completo por los 2 minutos entre
//     en poca RAM/disco.
//   • Mientras el buffer todavía se está llenando (los primeros <delay> segundos) el
//     video pasa en vivo y el audio queda muteado; una vez lleno, ambos corren
//     demorados y en sync.
// ─────────────────────────────────────────────────────────────────────────────

#include "delay-common.hpp"   // S_*, is_ts_jump, db_to_mul, delays_bypassed, add_key_list, g_delay_bypass

#include <util/platform.h>   // os_gettime_ns
#include "delay-codec.hpp"

#include <deque>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <algorithm>
#include <map>
#include <memory>
#include <chrono>
#include <cstring>
#include <exception>

// ─────────────────────────────────────────────────────────────────────────────
// Estado global de "bypassear todos los delays" (un solo hotkey del frontend)
//
// Un único hotkey de OBS pasa TODOS los filtros de delay (video, audio, ducking y
// push-to-delay) a pass-through en vivo de una; volver a apretarlo los reactiva
// y cada uno vuelve a bufferear desde cero. Asigná la tecla en
// OBS → Settings → Hotkeys ("EasyBackupandDelay: activar/desactivar todos los delays").
// La definición vive acá; se declara extern en delay-common.hpp para los motores.
// ─────────────────────────────────────────────────────────────────────────────
std::atomic<bool> g_delay_bypass{false};

// ─────────────────────────────────────────────────────────────────────────────
// Motores de delay — se incluyen textualmente (una sola unidad de traducción).
// Cada uno vive en su propio archivo; ver delay-*.inc. El orden importa poco: el
// prólogo compartido (delay-common.hpp) provee todo lo que necesitan.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

#include "delay-video.inc"

#include "delay-audio.inc"
#include "delay-duck.inc"
#include "delay-push.inc"
#include "delay-status.inc"

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Hotkey de toggle global: prender/apagar TODOS los delays de una
// ─────────────────────────────────────────────────────────────────────────────

static obs_hotkey_id g_bypass_hotkey = OBS_INVALID_HOTKEY_ID;

// Definida en delay-visibility.cpp: prende/apaga las fuentes atadas al estado del
// delay. Se declara acá suelta para no arrastrar Qt a esta unidad de traducción.
void easyobs_apply_delay_visibility(void);

// Accessors de estado compartido para que el botón del dock de la toolbar y el hotkey
// global manejen el MISMO flag de bypass (definido acá; declarado en la unidad de traducción del dock).
bool easyobs_delays_bypassed()
{
    return g_delay_bypass.load(std::memory_order_relaxed);
}

// True para cada filtro de delay que registramos. Se usa para manejar el ícono del
// "ojo" (enabled) nativo de los filtros de OBS: cuando los delays están bypasseados los
// filtros están deshabilitados, así el ojo aparece tachado en la lista de Filtros; reactivar los habilita.
static inline bool is_our_delay_filter(const char* id)
{
    return id && (
        strcmp(id, "mav_screen_delay_video") == 0 ||
        strcmp(id, "mav_screen_delay_audio") == 0 ||
        strcmp(id, "mav_screen_delay_duck")  == 0 ||
        strcmp(id, "mav_push_to_delay")      == 0 ||
        strcmp(id, "mav_push_to_delay_video") == 0);
}

static void enum_filter_set_enabled(obs_source_t* /*parent*/, obs_source_t* filter, void* param)
{
    const bool enable = *static_cast<bool*>(param);
    if (is_our_delay_filter(obs_source_get_id(filter)) &&
        obs_source_enabled(filter) != enable) {
        obs_source_set_enabled(filter, enable);   // un filtro es una fuente; maneja el ojo
    }
}

static bool enum_source_set_filters(void* param, obs_source_t* src)
{
    obs_source_enum_filters(src, enum_filter_set_enabled, param);
    return true;   // seguir enumerando
}

// Cambiar el estado enabled nativo de OBS (el ícono del ojo) de cada filtro de delay
// para que coincida con el estado de bypass: bypasseado → deshabilitado (ojo tachado), activo → habilitado.
static void apply_filter_eye(bool bypass)
{
    bool enable = !bypass;
    obs_enum_all_sources(enum_source_set_filters, &enable);
}

void easyobs_set_delays_bypassed(bool bypass)
{
    g_delay_bypass.store(bypass, std::memory_order_relaxed);
    apply_filter_eye(bypass);            // mostrar el ojo tachado en la lista de Filtros de OBS
    easyobs_apply_delay_visibility();    // intercambiar las fuentes atadas al estado
    blog(LOG_INFO, "[EasyBackupandDelay] delays %s",
         bypass ? "DESACTIVADOS (todo en vivo)" : "REACTIVADOS");
}

// ── Sync de arranque: adoptar el estado guardado del "ojo" del filtro ─────────
// El estado enabled de un filtro de delay se guarda con la colección de escenas, así
// que puede volver DESHABILITADO después de un reinicio. No lo forzamos a prenderse de
// nuevo — solo lo adoptamos en el flag interno para que el botón del dock avise (se pone
// rojo) que los delays arrancan deshabilitados. Solo lee los filtros; nunca los escribe.
struct delay_scan_ctx { bool anyFound; bool anyDisabled; };

static void enum_filter_scan(obs_source_t* /*parent*/, obs_source_t* filter, void* param)
{
    auto* c = static_cast<delay_scan_ctx*>(param);
    if (is_our_delay_filter(obs_source_get_id(filter))) {
        c->anyFound = true;
        if (!obs_source_enabled(filter)) c->anyDisabled = true;
    }
}

static bool enum_source_scan(void* param, obs_source_t* src)
{
    obs_source_enum_filters(src, enum_filter_scan, param);
    return true;
}

void easyobs_sync_bypass_from_filters(void)
{
    delay_scan_ctx c{false, false};
    obs_enum_all_sources(enum_source_scan, &c);
    if (!c.anyFound) return;   // no hay filtros de delay en esta colección de escenas

    g_delay_bypass.store(c.anyDisabled, std::memory_order_relaxed);
    if (c.anyDisabled)
        blog(LOG_WARNING,
             "[EasyBackupandDelay] delays start DISABLED (filter eye off) — press the key to enable");
}

// Cambiar el estado de bypass, con DEBOUNCE: si llegan dos toggles dentro de 200 ms
// cuentan como uno. Esto evita que una sola apretada de tecla se cancele a sí misma cuando
// está bindeada en DOS lugares a la vez (la tecla del plugin + un hotkey de OBS), que dejaba
// el toggle "trabado en off". Compartido por el hotkey de OBS, el botón del dock y el sondeo.
void easyobs_toggle_delays()
{
    static std::atomic<uint64_t> lastToggle{0};
    const uint64_t now  = os_gettime_ns();
    const uint64_t prev = lastToggle.load(std::memory_order_relaxed);
    if (now - prev < 200000000ULL) return;   // guarda de 200 ms
    lastToggle.store(now, std::memory_order_relaxed);
    easyobs_set_delays_bypassed(!easyobs_delays_bypassed());
}

static void toggle_all_delays(void*, obs_hotkey_id, obs_hotkey_t*, bool pressed)
{
    if (!pressed) return;   // togglear solo en key-down, ignorar el release
    easyobs_toggle_delays();
}

// ─────────────────────────────────────────────────────────────────────────────
// Registro (llamado desde obs_module_load)
// ─────────────────────────────────────────────────────────────────────────────

void register_delay_filters(void)
{
    static struct obs_source_info video_delay = {};
    video_delay.id             = "mav_screen_delay_video";
    video_delay.type           = OBS_SOURCE_TYPE_FILTER;
    video_delay.output_flags   = OBS_SOURCE_VIDEO;
    video_delay.get_name       = vd_get_name;
    video_delay.create         = vd_create;
    video_delay.destroy        = vd_destroy;
    video_delay.update         = vd_update;
    video_delay.get_defaults   = vd_defaults;
    video_delay.get_properties = vd_properties;
    video_delay.video_tick     = vd_tick;
    video_delay.video_render   = vd_render;
    video_delay.video_get_color_space = vd_get_color_space;
    obs_register_source(&video_delay);

    static struct obs_source_info audio_delay = {};
    audio_delay.id             = "mav_screen_delay_audio";
    audio_delay.type           = OBS_SOURCE_TYPE_FILTER;
    audio_delay.output_flags   = OBS_SOURCE_AUDIO;
    audio_delay.get_name       = ad_get_name;
    audio_delay.create         = ad_create;
    audio_delay.destroy        = ad_destroy;
    audio_delay.update         = ad_update;
    audio_delay.get_defaults   = ad_defaults;
    audio_delay.get_properties = ad_properties;
    audio_delay.filter_audio   = ad_filter_audio;
    obs_register_source(&audio_delay);

    static struct obs_source_info duck_delay = {};
    duck_delay.id             = "mav_screen_delay_duck";
    duck_delay.type           = OBS_SOURCE_TYPE_FILTER;
    duck_delay.output_flags   = OBS_SOURCE_AUDIO;
    duck_delay.get_name       = dd_get_name;
    duck_delay.create         = dd_create;
    duck_delay.destroy        = dd_destroy;
    duck_delay.update         = dd_update;
    duck_delay.get_defaults   = dd_defaults;
    duck_delay.get_properties = dd_properties;
    duck_delay.filter_audio   = dd_filter_audio;
    duck_delay.video_tick     = dd_tick;
    obs_register_source(&duck_delay);

    static struct obs_source_info push_delay = {};
    push_delay.id             = "mav_push_to_delay";
    push_delay.type           = OBS_SOURCE_TYPE_FILTER;
    push_delay.output_flags   = OBS_SOURCE_AUDIO;
    push_delay.get_name       = pd_get_name;
    push_delay.create         = pd_create;
    push_delay.destroy        = pd_destroy;
    push_delay.update         = pd_update;
    push_delay.get_defaults   = pd_defaults;
    push_delay.get_properties = pd_properties;
    push_delay.filter_audio   = pd_filter_audio;
    obs_register_source(&push_delay);

    // Push-to-Delay (cámara): el mismo motor de delay de video, pero el frame demorado
    // solo se muestra mientras la tecla está apretada (oculto si no).
    static struct obs_source_info push_delay_video = {};
    push_delay_video.id             = "mav_push_to_delay_video";
    push_delay_video.type           = OBS_SOURCE_TYPE_FILTER;
    push_delay_video.output_flags   = OBS_SOURCE_VIDEO;
    push_delay_video.get_name       = vdg_get_name;
    push_delay_video.create         = vd_create_gated;
    push_delay_video.destroy        = vd_destroy;
    push_delay_video.update         = vd_update;
    push_delay_video.get_defaults   = vd_defaults;
    push_delay_video.get_properties = vd_properties;
    push_delay_video.video_tick     = vd_tick;
    push_delay_video.video_render   = vd_render;
    push_delay_video.video_get_color_space = vd_get_color_space;
    obs_register_source(&push_delay_video);

    // Fuente Delay Playback: elegí una cámara/fuente; esto la clona demorada. Agregala
    // en cualquier parte de la pantalla — autocontenida (sin necesidad de filtro ni canal).
    static struct obs_source_info delay_playback = {};
    delay_playback.id             = "mav_delay_playback";
    delay_playback.type           = OBS_SOURCE_TYPE_INPUT;
    delay_playback.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    delay_playback.get_name       = dpv_get_name;
    delay_playback.create         = dpv_create;
    delay_playback.destroy        = dpv_destroy;
    delay_playback.update         = dpv_update;
    delay_playback.get_defaults   = dpv_defaults;
    delay_playback.get_properties = dpv_properties;
    delay_playback.get_width      = dpv_get_width;
    delay_playback.get_height     = dpv_get_height;
    delay_playback.video_tick     = vd_tick;
    delay_playback.video_render   = vd_render;
    delay_playback.video_get_color_space = vd_get_color_space;
    obs_register_source(&delay_playback);

    // Estado del delay (texto): fuente que muestra ON/OFF de los delays en la escena.
    static struct obs_source_info delay_status = {};
    delay_status.id             = "mav_delay_status";
    delay_status.type           = OBS_SOURCE_TYPE_INPUT;
    delay_status.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    delay_status.get_name       = ds_get_name;
    delay_status.create         = ds_create;
    delay_status.destroy        = ds_destroy;
    delay_status.update         = ds_update;
    delay_status.get_defaults   = ds_defaults;
    delay_status.get_properties = ds_properties;
    delay_status.get_width      = ds_width;
    delay_status.get_height     = ds_height;
    delay_status.video_tick     = ds_tick;
    delay_status.video_render   = ds_render;
    obs_register_source(&delay_status);

    // Un hotkey global para bypassear / restaurar todos los delays de una. Aparece en
    // OBS → Settings → Hotkeys; OBS persiste el binding entre reinicios.
    g_bypass_hotkey = obs_hotkey_register_frontend(
        "mav_toggle_all_delays",
        "EasyBackupandDelay: activar/desactivar todos los delays",
        toggle_all_delays, nullptr);
}

void unregister_delay_filters(void)
{
    if (g_bypass_hotkey != OBS_INVALID_HOTKEY_ID) {
        obs_hotkey_unregister(g_bypass_hotkey);
        g_bypass_hotkey = OBS_INVALID_HOTKEY_ID;
    }
}
