#include "delay-common.hpp"

struct AudioDelay {
    obs_source_t*                    context = nullptr;
    std::vector<std::deque<float>>   fifo;   // por canal
    std::vector<std::vector<float>>  out;    // backing de salida por canal
    struct obs_audio_data            output{};

    size_t   channels     = 0;
    uint32_t samplerate   = 48000;
    uint64_t delay_ms     = DEFAULT_DELAY_MS;
    size_t   delaySamples = 0;
    uint64_t last_ts      = 0;
    bool     reset        = true;
};

const char* ad_get_name(void*) { return "Screen Delay (audio)"; }

void ad_recalc(AudioDelay* f)
{
    f->delaySamples = (size_t)(f->delay_ms * (uint64_t)f->samplerate / 1000ULL);
}

void ad_update(void* data, obs_data_t* s)
{
    auto* f = static_cast<AudioDelay*>(data);
    f->delay_ms = (uint64_t)(obs_data_get_double(s, S_DELAY_SEC) * 1000.0);
    ad_recalc(f);
    f->reset = true;
}

void ad_defaults(obs_data_t* s)
{
    obs_data_set_default_double(s, S_DELAY_SEC, DEFAULT_DELAY_SEC);
}

void* ad_create(obs_data_t* s, obs_source_t* ctx)
{
    auto* f = new AudioDelay();
    f->context = ctx;

    struct obs_audio_info oai;
    if (obs_get_audio_info(&oai))
        f->samplerate = oai.samples_per_sec;

    ad_update(f, s);
    return f;
}

void ad_destroy(void* data)
{
    delete static_cast<AudioDelay*>(data);
}

obs_properties_t* ad_properties(void*)
{
    obs_properties_t* props = obs_properties_create();

    obs_property_t* p =
        obs_properties_add_float_slider(props, S_DELAY_SEC, "Delay", 0.0, 120.0, 0.1);
    obs_property_float_set_suffix(p, " s");

    obs_properties_add_text(
        props, "info",
        "Set the SAME delay as the matching \"Screen Delay (video)\" filter so "
        "picture and sound stay in sync.",
        OBS_TEXT_INFO);

    return props;
}

struct obs_audio_data* ad_filter_audio(void* data, struct obs_audio_data* audio)
{
    auto* f = static_cast<AudioDelay*>(data);

    size_t ch = 0;
    for (size_t i = 0; i < MAX_AV_PLANES; ++i)
        if (audio->data[i]) ++ch;
    if (ch == 0) return audio;

    if (ch != f->channels) {
        f->channels = ch;
        f->fifo.assign(ch, {});
        f->out.assign(ch, {});
        f->reset = true;
    }

    // Bypass global: pasar el audio EN VIVO y tirar el buffer, así reactivar el delay
    // vuelve a bufferear desde cero (mientras tanto el "ojo" del filtro muestra que está apagado).
    if (delays_bypassed()) {
        for (auto& q : f->fifo) q.clear();
        f->reset    = true;
        f->last_ts  = audio->timestamp;
        return audio;
    }

    if (f->reset || is_ts_jump(audio->timestamp, f->last_ts)) {
        for (auto& q : f->fifo) q.clear();
        f->reset = false;
    }
    f->last_ts = audio->timestamp;

    const uint32_t n = audio->frames;
    float livePeak = 0.0f;
    for (size_t c = 0; c < ch; ++c) {
        const float* src = reinterpret_cast<const float*>(audio->data[c]);
        for (uint32_t i = 0; i < n; ++i) {
            const float a = fabsf(src[i]);
            if (a > livePeak) livePeak = a;
        }
        f->fifo[c].insert(f->fifo[c].end(), src, src + n);
    }

    // Lo que entra ahora sale al aire dentro de delay_ms: con eso se agenda el
    // aviso anticipado, para tener tiempo de callarse antes de pisarse.
    easyobs_note_live_peak(livePeak, f->delay_ms);

    // Todavía llenando el buffer → sacar silencio (devolver null mutea esta llamada).
    if (f->fifo[0].size() < f->delaySamples + n)
        return nullptr;

    float peak = 0.0f;
    for (size_t c = 0; c < ch; ++c) {
        f->out[c].resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            const float v = f->fifo[c].front();
            f->fifo[c].pop_front();
            f->out[c][i] = v;

            const float a = fabsf(v);
            if (a > peak) peak = a;
        }
        f->output.data[c] = reinterpret_cast<uint8_t*>(f->out[c].data());
    }

    // Lo que sale ahora es voz demorada: alimenta la luz roja/verde del dock.
    easyobs_note_delayed_peak(peak);
    for (size_t c = ch; c < MAX_AV_PLANES; ++c)
        f->output.data[c] = nullptr;

    f->output.frames    = n;
    f->output.timestamp = audio->timestamp;   // mantener la línea de tiempo en vivo
    return &f->output;
}
