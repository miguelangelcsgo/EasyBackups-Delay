#include "delay-common.hpp"

#define S_SIDECHAIN "duck_sidechain"
#define S_KEY_DELAY_SEC "duck_key_delay_s"
#undef  S_THRESHOLD   // windows.h también define este nombre
#define S_THRESHOLD "duck_threshold_db"
#define S_DUCK_DB   "duck_amount_db"
#define S_ATTACK    "duck_attack_ms"
#define S_RELEASE   "duck_release_ms"

// db_to_mul() y one_pole_coef() ahora viven en delay-common.hpp (los usa también
// delay-push.inc, antes dependía del orden de include).

struct DuckDelay {
    obs_source_t* context = nullptr;

    std::mutex data_mtx;   // protege los deques de la clave
    std::mutex upd_mtx;    // protege el nombre del sidechain / weak ref

    obs_weak_source_t* weak_side = nullptr;
    std::string        side_name;          // "" / "none" => deshabilitado
    uint64_t           check_time = 0;

    std::vector<std::deque<float>> key;    // samples de mic capturados por canal
    size_t   channels     = 0;
    uint32_t samplerate   = 48000;
    size_t   keySamples   = 0;             // delay aplicado a la clave

    float threshold = 0.0056f;             // lineal (~ -45 dB)
    float duckGain  = 0.0316f;             // lineal (~ -30 dB)
    float atkCoef   = 1.0f;
    float relCoef   = 0.02f;
    float gain      = 1.0f;                // ganancia suavizada actual
};

const char* dd_get_name(void*) { return "Screen Delay Ducking (audio)"; }

// Captura el audio del sidechain (mic) en FIFOs por canal.
void dd_capture(void* param, obs_source_t*, const struct audio_data* ad, bool muted)
{
    auto* d = static_cast<DuckDelay*>(param);

    size_t ch = 0;
    for (size_t i = 0; i < MAX_AV_PLANES; ++i)
        if (ad->data[i]) ++ch;
    if (ch == 0) return;

    std::lock_guard<std::mutex> lk(d->data_mtx);
    if (ch != d->channels) { d->channels = ch; d->key.assign(ch, {}); }

    const uint32_t n = ad->frames;
    for (size_t c = 0; c < ch; ++c) {
        if (muted || !ad->data[c])
            d->key[c].insert(d->key[c].end(), n, 0.0f);
        else {
            const float* s = reinterpret_cast<const float*>(ad->data[c]);
            d->key[c].insert(d->key[c].end(), s, s + n);
        }
    }

    // Tope de seguridad para que el FIFO no crezca sin límite si el bus demorado se traba.
    const size_t cap = d->keySamples + (size_t)d->samplerate * 2;
    for (auto& q : d->key)
        while (q.size() > cap) q.pop_front();
}

void dd_disconnect(DuckDelay* d)
{
    obs_weak_source_t* old = nullptr;
    {
        std::lock_guard<std::mutex> lk(d->upd_mtx);
        old = d->weak_side;
        d->weak_side = nullptr;
    }
    if (old) {
        obs_source_t* s = obs_weak_source_get_source(old);
        if (s) {
            obs_source_remove_audio_capture_callback(s, dd_capture, d);
            obs_source_release(s);
        }
        obs_weak_source_release(old);
    }
}

void dd_update(void* data, obs_data_t* s)
{
    auto* d = static_cast<DuckDelay*>(data);

    d->samplerate = audio_output_get_sample_rate(obs_get_audio());
    if (d->samplerate == 0) d->samplerate = 48000;

    uint64_t keyMs = (uint64_t)(obs_data_get_double(s, S_KEY_DELAY_SEC) * 1000.0);
    d->keySamples  = (size_t)(keyMs * (uint64_t)d->samplerate / 1000ULL);

    d->threshold = db_to_mul((float)obs_data_get_double(s, S_THRESHOLD));
    d->duckGain  = db_to_mul((float)obs_data_get_double(s, S_DUCK_DB));
    d->atkCoef   = one_pole_coef((float)obs_data_get_int(s, S_ATTACK),  d->samplerate);
    d->relCoef   = one_pole_coef((float)obs_data_get_int(s, S_RELEASE), d->samplerate);

    const char* name = obs_data_get_string(s, S_SIDECHAIN);
    bool valid = name && *name && strcmp(name, "none") != 0;

    if (!valid) {
        dd_disconnect(d);
        std::lock_guard<std::mutex> lk(d->upd_mtx);
        d->side_name.clear();
    } else if (d->side_name != name) {
        dd_disconnect(d);
        std::lock_guard<std::mutex> lk(d->upd_mtx);
        d->side_name  = name;
        d->check_time = os_gettime_ns() - 3000000000ULL;   // conectar en el próximo tick
    }
}

void dd_defaults(obs_data_t* s)
{
    obs_data_set_default_string(s, S_SIDECHAIN, "none");
    obs_data_set_default_double(s, S_KEY_DELAY_SEC, DEFAULT_DELAY_SEC);
    obs_data_set_default_double(s, S_THRESHOLD, -45.0);
    obs_data_set_default_double(s, S_DUCK_DB,   -30.0);
    obs_data_set_default_int   (s, S_ATTACK,    10);
    obs_data_set_default_int   (s, S_RELEASE,   150);
}

void* dd_create(obs_data_t* s, obs_source_t* ctx)
{
    auto* d = new DuckDelay();
    d->context = ctx;
    dd_update(d, s);
    return d;
}

void dd_destroy(void* data)
{
    auto* d = static_cast<DuckDelay*>(data);
    dd_disconnect(d);
    delete d;
}

// Conectar el callback de captura de audio de forma perezosa una vez que la fuente nombrada existe.
void dd_tick(void* data, float)
{
    auto* d = static_cast<DuckDelay*>(data);

    std::string want;
    {
        std::lock_guard<std::mutex> lk(d->upd_mtx);
        if (d->side_name.empty() || d->weak_side) return;
        uint64_t t = os_gettime_ns();
        if (t - d->check_time <= 3000000000ULL) return;
        d->check_time = t;
        want = d->side_name;
    }

    obs_source_t* src = obs_get_source_by_name(want.c_str());
    if (!src) return;

    obs_weak_source_t* weak = obs_source_get_weak_source(src);
    {
        std::lock_guard<std::mutex> lk(d->upd_mtx);
        if (d->side_name == want && !d->weak_side) {
            d->weak_side = weak;
            weak = nullptr;
        }
    }
    if (weak) obs_weak_source_release(weak);
    else      obs_source_add_audio_capture_callback(src, dd_capture, d);

    obs_source_release(src);
}

struct obs_audio_data* dd_filter_audio(void* data, struct obs_audio_data* audio)
{
    auto* d = static_cast<DuckDelay*>(data);
    const uint32_t n = audio->frames;
    if (n == 0) return audio;

    // Bypass global: no hay nada demorado, así que no hay nada que duckear.
    if (delays_bypassed()) { d->gain = 1.0f; return audio; }

    bool haveSide;
    { std::lock_guard<std::mutex> lk(d->upd_mtx); haveSide = d->weak_side != nullptr; }

    float target = 1.0f;
    if (haveSide) {
        std::lock_guard<std::mutex> lk(d->data_mtx);
        if (!d->key.empty() && d->key[0].size() >= d->keySamples + n) {
            const size_t ch = d->key.size();
            float peak = 0.0f;
            for (uint32_t i = 0; i < n; ++i)
                for (size_t c = 0; c < ch; ++c) {
                    float v = d->key[c].front();
                    d->key[c].pop_front();
                    float a = fabsf(v);
                    if (a > peak) peak = a;
                }
            target = (peak > d->threshold) ? d->duckGain : 1.0f;
        }
    }

    size_t ch = 0;
    for (size_t i = 0; i < MAX_AV_PLANES; ++i)
        if (audio->data[i]) ++ch;

    float g = d->gain;
    for (uint32_t i = 0; i < n; ++i) {
        float coef = (target < g) ? d->atkCoef : d->relCoef;
        g += (target - g) * coef;
        for (size_t c = 0; c < ch; ++c)
            reinterpret_cast<float*>(audio->data[c])[i] *= g;
    }
    d->gain = g;

    return audio;
}

struct dd_src_list { obs_property_t* list; obs_source_t* parent; };

bool dd_add_source(void* param, obs_source_t* source)
{
    auto* info = static_cast<dd_src_list*>(param);
    if (source == info->parent) return true;
    if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0) return true;
    const char* name = obs_source_get_name(source);
    obs_property_list_add_string(info->list, name, name);
    return true;
}

obs_properties_t* dd_properties(void* data)
{
    auto* d = static_cast<DuckDelay*>(data);
    obs_properties_t* props = obs_properties_create();

    obs_property_t* list = obs_properties_add_list(
        props, S_SIDECHAIN, "Micrófono (clave)",
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(list, "None", "none");
    dd_src_list info{ list, d ? obs_filter_get_parent(d->context) : nullptr };
    obs_enum_sources(dd_add_source, &info);

    obs_property_t* p =
        obs_properties_add_float_slider(props, S_KEY_DELAY_SEC,
            "Delay de la clave (= delay del audio)", 0.0, 120.0, 0.1);
    obs_property_float_set_suffix(p, " s");

    obs_properties_add_float_slider(props, S_THRESHOLD, "Umbral", -60.0, 0.0, 0.5);
    obs_properties_add_float_slider(props, S_DUCK_DB,   "Atenuación", -60.0, 0.0, 0.5);
    obs_properties_add_int_slider  (props, S_ATTACK,    "Ataque",  0, 500, 1);
    obs_properties_add_int_slider  (props, S_RELEASE,   "Liberación", 0, 2000, 10);

    obs_properties_add_text(props, "duck_info",
        "Poné este filtro DESPUÉS de \"Screen Delay (audio)\" y elegí tu micrófono "
        "como clave. Igualá el delay de la clave al delay del audio.",
        OBS_TEXT_INFO);

    return props;
}
