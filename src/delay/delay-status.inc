#include "delay-common.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Estado del delay (texto): una fuente que muestra automáticamente si TODOS los
// delays están ACTIVOS o EN VIVO (bypass), leyendo el mismo flag global que el
// dock y el hotkey. Por dentro envuelve una fuente de texto de OBS (GDI+) y le
// cambia el texto cuando el estado cambia — así hereda gratis el render de texto.
// ─────────────────────────────────────────────────────────────────────────────

#define S_DS_TEXT_ON  "ds_text_on"    // texto cuando el delay está ACTIVO
#define S_DS_TEXT_OFF "ds_text_off"   // texto cuando estás EN VIVO (sin delay)
#define S_DS_SIZE     "ds_size"       // tamaño de fuente

struct DelayStatus {
    obs_source_t* context = nullptr;
    obs_source_t* text    = nullptr;   // fuente de texto hija (privada)
    std::string   onText, offText;
    int           size      = 48;
    int           lastState = -1;      // -1 sin setear, 0 = con delay, 1 = bypass
};

// Aplica al texto hijo el string que corresponde al estado actual.
static void ds_apply(DelayStatus* d, bool bypassed)
{
    if (!d->text) return;
    obs_data_t* st = obs_data_create();
    obs_data_set_string(st, "text", (bypassed ? d->offText : d->onText).c_str());
    obs_data_t* font = obs_data_create();
    obs_data_set_string(font, "face", "Arial");
    obs_data_set_int(font, "size", d->size);
    obs_data_set_obj(st, "font", font);
    obs_data_release(font);
    obs_source_update(d->text, st);
    obs_data_release(st);
    d->lastState = bypassed ? 1 : 0;
}

const char* ds_get_name(void*) { return "Estado del delay (texto)"; }

void ds_update(void* data, obs_data_t* s)
{
    auto* d = static_cast<DelayStatus*>(data);
    const char* on  = obs_data_get_string(s, S_DS_TEXT_ON);
    const char* off = obs_data_get_string(s, S_DS_TEXT_OFF);
    d->onText  = (on  && *on ) ? on  : "🟢 CON DELAY";
    d->offText = (off && *off) ? off : "🔴 EN VIVO";
    d->size    = (int)obs_data_get_int(s, S_DS_SIZE);
    if (d->size < 8) d->size = 8;
    d->lastState = -1;                 // forzar reaplicar
    ds_apply(d, delays_bypassed());
}

void ds_defaults(obs_data_t* s)
{
    obs_data_set_default_string(s, S_DS_TEXT_ON,  "🟢 CON DELAY");
    obs_data_set_default_string(s, S_DS_TEXT_OFF, "🔴 EN VIVO (sin delay)");
    obs_data_set_default_int(s, S_DS_SIZE, 48);
}

void* ds_create(obs_data_t* s, obs_source_t* ctx)
{
    auto* d = new DelayStatus();
    d->context = ctx;
    // Fuente de texto GDI+ privada (no aparece en la escena; la renderizamos nosotros).
    d->text = obs_source_create_private("text_gdiplus_v2", "mav_delay_status_text", nullptr);
    if (!d->text)   // fallback a la versión anterior del plugin de texto
        d->text = obs_source_create_private("text_gdiplus", "mav_delay_status_text", nullptr);
    ds_update(d, s);
    blog(LOG_INFO, "[EasyBackupandDelay] fuente 'Estado del delay' creada (%s)",
         d->text ? "texto OK" : "sin fuente de texto!");
    return d;
}

void ds_destroy(void* data)
{
    auto* d = static_cast<DelayStatus*>(data);
    if (d->text) obs_source_release(d->text);
    delete d;
}

// Cada tick chequea el flag global y, si cambió, actualiza el texto.
void ds_tick(void* data, float)
{
    auto* d = static_cast<DelayStatus*>(data);
    const int cur = delays_bypassed() ? 1 : 0;
    if (cur != d->lastState) ds_apply(d, cur == 1);
}

void ds_render(void* data, gs_effect_t*)
{
    auto* d = static_cast<DelayStatus*>(data);
    if (d->text) obs_source_video_render(d->text);
}

uint32_t ds_width(void* data)
{
    auto* d = static_cast<DelayStatus*>(data);
    return d->text ? obs_source_get_width(d->text) : 0;
}
uint32_t ds_height(void* data)
{
    auto* d = static_cast<DelayStatus*>(data);
    return d->text ? obs_source_get_height(d->text) : 0;
}

obs_properties_t* ds_properties(void*)
{
    obs_properties_t* p = obs_properties_create();
    obs_properties_add_text(p, S_DS_TEXT_ON,  "Texto cuando el delay está ACTIVO", OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, S_DS_TEXT_OFF, "Texto cuando estás EN VIVO (sin delay)", OBS_TEXT_DEFAULT);
    obs_properties_add_int(p, S_DS_SIZE, "Tamaño de fuente", 8, 512, 1);
    obs_properties_add_text(p, "ds_info",
        "Muestra solo si tus delays están ON u OFF (el mismo estado que el dock y el "
        "hotkey global), y se actualiza al instante cuando los prendés/apagás. Usá un "
        "emoji de color (🟢/🔴) para que se note de un vistazo.",
        OBS_TEXT_INFO);
    return p;
}
