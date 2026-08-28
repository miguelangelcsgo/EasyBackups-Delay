#include "delay-common.hpp"

#define S_PD_PRIORITY "pd_live_priority"
#define S_PD_DUCK_DB  "pd_duck_db"
#define S_PD_VK       "pd_vk"

struct PushDelay {
    obs_source_t*                  context = nullptr;
    std::vector<std::deque<float>> line;     // lane de delay por canal
    size_t   channels     = 0;
    uint32_t samplerate   = 48000;
    size_t   delaySamples = 0;
    uint64_t delayMs      = 0;   // el mismo delay, para agendar el aviso anticipado
    std::atomic<bool> held{false};
    std::atomic<int>  vk{'V'};   // virtual-key sondeada globalmente; 0 = solo hotkey de OBS
    obs_hotkey_id     hotkey = OBS_INVALID_HOTKEY_ID;

    // Prioridad de voz en vivo: mientras hablás en vivo (tecla suelta), duckea el
    // callout demorado para que los dos no se pisen y gane la voz en vivo.
    bool  priority = true;
    float duckGain = 1.0f;   // multiplicador aplicado a la lane demorada cuando hablás en vivo
    float thresh   = 0.0f;   // nivel en vivo por encima del cual se activa el ducking
    float atkCoef  = 1.0f;
    float relCoef  = 1.0f;
    float env      = 0.0f;   // envelope de la voz en vivo
    float g        = 1.0f;   // ganancia de duck suavizada sobre la lane demorada
};

const char* pd_get_name(void*) { return "Push-to-Delay (mic)"; }

void pd_hotkey(void* data, obs_hotkey_id, obs_hotkey_t*, bool pressed)
{
    static_cast<PushDelay*>(data)->held.store(pressed, std::memory_order_relaxed);
}

void pd_update(void* data, obs_data_t* s)
{
    auto* d = static_cast<PushDelay*>(data);
    uint64_t ms = (uint64_t)(obs_data_get_double(s, S_DELAY_SEC) * 1000.0);
    d->delaySamples = (size_t)(ms * (uint64_t)d->samplerate / 1000ULL);
    d->delayMs      = ms;

    d->vk.store((int)obs_data_get_int(s, S_PD_VK), std::memory_order_relaxed);
    d->priority = obs_data_get_bool(s, S_PD_PRIORITY);
    d->duckGain = db_to_mul((float)obs_data_get_double(s, S_PD_DUCK_DB));
    d->thresh   = db_to_mul(-45.0f);                 // ~inicio del habla
    d->atkCoef  = one_pole_coef(10.0f,  d->samplerate);
    d->relCoef  = one_pole_coef(150.0f, d->samplerate);
}

void pd_defaults(obs_data_t* s)
{
    obs_data_set_default_double(s, S_DELAY_SEC, DEFAULT_DELAY_SEC);
    obs_data_set_default_int   (s, S_PD_VK, 'V');
    obs_data_set_default_bool  (s, S_PD_PRIORITY, true);
    obs_data_set_default_double(s, S_PD_DUCK_DB, -15.0);
}

void* pd_create(obs_data_t* s, obs_source_t* ctx)
{
    auto* d = new PushDelay();
    d->context = ctx;

    struct obs_audio_info oai;
    if (obs_get_audio_info(&oai))
        d->samplerate = oai.samples_per_sec;

    pd_update(d, s);

    d->hotkey = obs_hotkey_register_source(
        ctx, "mav_push_to_delay",
        "Push-to-Delay (mantener para enviar la voz al delay)",
        pd_hotkey, d);

    return d;
}

void pd_destroy(void* data)
{
    auto* d = static_cast<PushDelay*>(data);
    if (d->hotkey != OBS_INVALID_HOTKEY_ID)
        obs_hotkey_unregister(d->hotkey);
    delete d;
}

obs_properties_t* pd_properties(void*)
{
    obs_properties_t* props = obs_properties_create();

    obs_property_t* p =
        obs_properties_add_float_slider(props, S_DELAY_SEC, "Delay", 0.0, 120.0, 0.1);
    obs_property_float_set_suffix(p, " s");

    // Tecla push-to-delay configurable, sondeada globalmente (funciona con el
    // juego en foco, sin configurar un atajo de OBS). Misma lista de teclas que
    // el resto del plugin (ver add_key_list).
    obs_property_t* keys = obs_properties_add_list(props, S_PD_VK,
        "Tecla push-to-delay", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    add_key_list(keys);

    obs_properties_add_bool(props, S_PD_PRIORITY,
        "Priorizar voz en vivo (baja la voz demorada mientras hablás)");

    obs_property_t* dk =
        obs_properties_add_float_slider(props, S_PD_DUCK_DB,
            "Cuánto baja la voz demorada", -60.0, 0.0, 0.5);
    obs_property_float_set_suffix(dk, " dB");

    obs_properties_add_text(props, "pd_info",
        "Aplicalo al micrófono y elegí arriba tu tecla de push-to-talk del juego "
        "(funciona global, con el juego en foco). Mientras la mantenés, tu voz se "
        "silencia en vivo y sale demorada. Igualá el delay al del video/audio.\n"
        "También podés asignar un atajo extra en OBS → Ajustes → Atajos.\n"
        "Con \"Priorizar voz en vivo\", si hablás en vivo mientras suena una "
        "llamada demorada, esa llamada baja de volumen para no pisarte.",
        OBS_TEXT_INFO);

    return props;
}

struct obs_audio_data* pd_filter_audio(void* data, struct obs_audio_data* audio)
{
    auto* d = static_cast<PushDelay*>(data);
    const uint32_t n = audio->frames;

    size_t ch = 0;
    for (size_t i = 0; i < MAX_AV_PLANES; ++i)
        if (audio->data[i]) ++ch;
    if (ch == 0) return audio;

    // Bypass global: el mic pasa en vivo, vaciar la lane de delay.
    if (delays_bypassed()) {
        for (auto& q : d->line) q.clear();
        return audio;
    }

    if (ch != d->channels) { d->channels = ch; d->line.assign(ch, {}); }

    // Estado de la tecla: la tecla configurable se sondea globalmente (funciona con el
    // juego en foco); el hotkey de OBS sigue funcionando como binding alternativo.
    bool held = d->held.load(std::memory_order_relaxed);
    const int vk = d->vk.load(std::memory_order_relaxed);
    if (vk)
        held = held || ((GetAsyncKeyState(vk) & 0x8000) != 0);

    float* buf[MAX_AV_PLANES] = {};
    for (size_t c = 0; c < ch; ++c)
        buf[c] = reinterpret_cast<float*>(audio->data[c]);

    float env = d->env, g = d->g;
    float delayedPeak = 0.0f;   // pico del callout demorado, para la luz del dock
    float heldPeak    = 0.0f;   // pico de lo que estas grabando ahora en la lane

    for (uint32_t i = 0; i < n; ++i) {
        // Señal en vivo que realmente sale al aire (solo cuando la tecla está suelta).
        // Seguimos su envelope para poder duckear el callout demorado mientras hablás.
        float livePeak = 0.0f;
        if (!held)
            for (size_t c = 0; c < ch; ++c) {
                float a = fabsf(buf[c][i]);
                if (a > livePeak) livePeak = a;
            }
        env += (livePeak - env) * (livePeak > env ? d->atkCoef : d->relCoef);

        // Duckear la lane demorada cuando hay voz en vivo (el vivo tiene prioridad).
        float target = (d->priority && env > d->thresh) ? d->duckGain : 1.0f;
        g += (target - g) * (target < g ? d->atkCoef : d->relCoef);

        for (size_t c = 0; c < ch; ++c) {
            const float in = buf[c][i];
            if (held) {
                const float ia = fabsf(in);
                if (ia > heldPeak) heldPeak = ia;
            }
            auto& q = d->line[c];

            // Alimentar la lane de delay: la voz mientras está apretada, silencio si no.
            q.push_back(held ? in : 0.0f);

            float delayed = 0.0f;
            if (q.size() > d->delaySamples) {
                delayed = q.front();
                q.pop_front();
            }
            const float da = fabsf(delayed);
            if (da > delayedPeak) delayedPeak = da;

            // El vivo pasa cuando está suelta; el callout demorado se mezcla,
            // atenuado por g cada vez que estás hablando en vivo.
            float out = (held ? 0.0f : in) + delayed * g;
            if (out >  1.0f) out =  1.0f;
            if (out < -1.0f) out = -1.0f;
            buf[c][i] = out;
        }
    }

    d->env = env;
    d->g   = g;

    // Igual que en el delay de audio: avisar si esta sonando el callout demorado.
    easyobs_note_delayed_peak(delayedPeak);

    // Y agendar el aviso anticipado con lo que se esta grabando AHORA en la lane:
    // eso va a salir al aire dentro de delayMs. Faltaba: este filtro solo alimentaba
    // la luz del dock, asi que en un setup con Push-to-Delay en el microfono el bip
    // no se agendaba nunca, por mas que el usuario lo tuviera tildado.
    if (heldPeak > 0.0f)
        easyobs_note_live_peak(heldPeak, d->delayMs);

    return audio;
}
