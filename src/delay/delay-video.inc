#include "delay-common.hpp"

// El video demorado se bufferea en la RAM del SISTEMA (no en VRAM): cada frame se
// lee de vuelta desde la GPU a un std::vector, así la memoria de la placa de video
// queda libre para el juego. Solo se mantienen tres objetos auxiliares chicos de GPU.
// Un frame crudo de readback que se le entrega al worker para comprimir.
struct RFrame {
    std::vector<uint8_t> px;   // RGBA empaquetado sin padding
    uint32_t w = 0, h = 0;
    uint64_t ts = 0;           // tiempo de captura (os_gettime_ns)
    bool     held = false;     // modo gated: ¿la tecla estaba apretada al capturar?
};

// Un frame comprimido en MJPEG dentro del ring de delay. Todos los frames son intra,
// así que cualquiera se puede descartar o decodificar de forma independiente — ideal
// para un ring basado en tiempo. 20s de 1080p60 entran en unos cientos de MB en vez
// de ~10 GB en crudo.
struct Pkt {
    std::vector<uint8_t> data;
    uint32_t w = 0, h = 0;
    uint64_t ts = 0;
    bool     held = false;     // modo gated: capturado mientras la tecla estaba apretada
};

class VideoDelay {
public:
    obs_source_t* context = nullptr;

    gs_texrender_t* capture = nullptr;   // renderiza el frame actual de la fuente
    gs_stagesurf_t* stage[2]        = { nullptr, nullptr };
    bool            stagePending[2] = { false, false };
    uint64_t        stageTs[2]      = { 0, 0 };
    uint32_t        stageW[2] = { 0, 0 }, stageH[2] = { 0, 0 };
    int             stageIdx = 0;

    bool            stageHeld[2] = { false, false };

    gs_texture_t*   disp  = nullptr;
    uint32_t        dispW = 0, dispH = 0;
    uint64_t        shownTs = 0;
    bool            shownHeld = false;

    std::thread             worker;
    std::mutex              mtx;
    std::condition_variable cv;
    bool                    stopWorker = false;
    std::deque<RFrame>      rawQ;
    std::vector<std::vector<uint8_t>> rawPool;
    std::deque<Pkt>         ring;
    size_t                  ringBytes = 0;
    uint64_t                droppedRaw = 0;
    std::atomic<bool>       codecFailed{false};   // lo lee el hilo de render para avisar en pantalla
    bool                    warnedRam   = false;
    std::vector<uint8_t>    decoded;
    uint32_t                decW = 0, decH = 0;
    uint64_t                decTs = 0;
    bool                    decHeld = false;
    std::vector<uint8_t>    shown;
    uint32_t                shownW = 0, shownH = 0;

    bool announcedReady = false;

    std::atomic<uint64_t> delayAtomic{0};
    std::atomic<int>      qualityAtomic{2};
    std::atomic<bool>     full444Atomic{true};
    std::atomic<int>      engineAtomic{0};

    uint64_t delay_ns    = 0;
    uint64_t interval_ns = 66666666ULL;
    double   scale       = 0.5;

    uint32_t cx = 0, cy = 0, bcx = 0, bcy = 0;
    uint64_t last_capture = 0;
    double   capture_accum = 0.0;
    bool     captured_this_tick = false;

    int lastBlock = -1;

    bool bypassed = false;

    bool              gated = false;
    std::atomic<int>  vk{'V'};
    std::atomic<bool> held{false};
    obs_hotkey_id     hotkey = OBS_INVALID_HOTKEY_ID;

    bool               sourceMode = false;
    std::mutex         targetMtx;
    obs_weak_source_t* targetWeak = nullptr;
    std::string        targetName;
    bool               guide = true;
    uint64_t           lastShownChange = 0;
};

void vd_worker(VideoDelay* f)
{
    MjpegCodec codec;
    try {
    std::unique_lock<std::mutex> lk(f->mtx);

    while (!f->stopWorker) {
        f->cv.wait_for(lk, std::chrono::milliseconds(8),
                       [f] { return f->stopWorker || !f->rawQ.empty(); });
        if (f->stopWorker) break;

        if (!f->rawQ.empty()) {
            RFrame rf = std::move(f->rawQ.front());
            f->rawQ.pop_front();
            lk.unlock();

            const int  q    = f->qualityAtomic.load(std::memory_order_relaxed);
            const bool s444 = f->full444Atomic.load(std::memory_order_relaxed);
            const int  eng  = f->engineAtomic.load(std::memory_order_relaxed);

            std::vector<uint8_t> out;
            uint64_t outTs = 0;
            const bool ok = codec.encode(rf.px.data(), rf.w, rf.h, q, s444, eng,
                                         rf.ts, out, outTs);
            lk.lock();

            if (f->rawPool.size() < 4)
                f->rawPool.push_back(std::move(rf.px));

            if (!ok) {
                if (!f->codecFailed) {
                    f->codecFailed = true;
                    blog(LOG_ERROR, "[EasyBackupandDelay] video delay: encode failed "
                         "(%ux%u) — showing live video", rf.w, rf.h);
                }
            } else {
                f->codecFailed = false;
                if (!out.empty()) {
                    if (f->ring.empty())
                        blog(LOG_INFO, "[EasyBackupandDelay] video delay (compressed): "
                             "buffering started @ %ux%u via %s (%.0f KB/frame)",
                             rf.w, rf.h, codec.encoderName(), (double)out.size() / 1024.0);
                    f->ringBytes += out.size();
                    f->ring.push_back({ std::move(out), rf.w, rf.h, outTs, rf.held });
                }
            }
        }

        const uint64_t now = os_gettime_ns();
        const uint64_t dly = f->delayAtomic.load(std::memory_order_relaxed);

        while (f->ring.size() >= 2 && f->ring[1].ts + dly <= now) {
            f->ringBytes -= f->ring.front().data.size();
            f->ring.pop_front();
        }
        const size_t kRamCap = 1500ull * 1024 * 1024;
        while (f->ringBytes > kRamCap && f->ring.size() > 1) {
            if (!f->warnedRam) {
                f->warnedRam = true;
                blog(LOG_WARNING, "[EasyBackupandDelay] video delay: ring hit the "
                     "1.5 GB cap — dropping oldest frames (lower quality/FPS)");
            }
            f->ringBytes -= f->ring.front().data.size();
            f->ring.pop_front();
        }

        if (f->ring.empty()) continue;

        std::vector<uint8_t> pktData;
        uint64_t pts = 0;
        bool     pheld = false;
        {
            Pkt& front = f->ring.front();
            if (front.ts + dly <= now && front.ts != f->decTs) {
                pktData = front.data;
                pts = front.ts; pheld = front.held;
            }
        }

        if (!pktData.empty()) {
            lk.unlock();
            std::vector<uint8_t> rgba;
            uint32_t dw = 0, dh = 0;
            const bool dok = codec.decode(pktData.data(), pktData.size(), rgba, dw, dh);
            lk.lock();
            if (dok) {
                f->decoded = std::move(rgba);
                f->decW = dw; f->decH = dh; f->decTs = pts; f->decHeld = pheld;
            }
        }
    }
    } catch (const std::exception& e) {
        blog(LOG_ERROR, "[EasyBackupandDelay] video delay worker stopped: %s", e.what());
    } catch (...) {
        blog(LOG_ERROR, "[EasyBackupandDelay] video delay worker stopped (unknown exception)");
    }
}

void vd_diag(VideoDelay* f, int state)
{
    if (f->lastBlock == state) return;
    f->lastBlock = state;

    obs_source_t* parent = obs_filter_get_parent(f->context);
    const char* src = parent ? obs_source_get_name(parent) : "?";
    switch (state) {
    case 0: blog(LOG_INFO,    "[EasyBackupandDelay] video delay on '%s': capturing", src); break;
    case 1: blog(LOG_WARNING, "[EasyBackupandDelay] video delay on '%s': no filter target", src); break;
    case 2: blog(LOG_WARNING, "[EasyBackupandDelay] video delay on '%s': source size is 0 "
                              "(window hidden/minimized or capture not attached)", src); break;
    case 3: blog(LOG_WARNING, "[EasyBackupandDelay] video delay on '%s': GPU helper alloc failed", src); break;
    }
}

void vd_free(VideoDelay* f)
{
    obs_enter_graphics();
    if (f->capture) { gs_texrender_destroy(f->capture); f->capture = nullptr; }
    for (int i = 0; i < 2; ++i) {
        if (f->stage[i]) { gs_stagesurface_destroy(f->stage[i]); f->stage[i] = nullptr; }
        f->stagePending[i] = false;
        f->stageTs[i] = 0;
        f->stageW[i] = f->stageH[i] = 0;
    }
    if (f->disp) { gs_texture_destroy(f->disp); f->disp = nullptr; }
    obs_leave_graphics();
    f->stageIdx = 0;
    f->dispW = f->dispH = 0;
    f->shownTs = 0;
    f->announcedReady = false;
}

const char* vd_get_name(void*)  { return "Screen Delay (video)"; }
const char* vdg_get_name(void*) { return "Push-to-Delay (cámara)"; }

void vd_update(void* data, obs_data_t* s)
{
    auto* f = static_cast<VideoDelay*>(data);

    f->delay_ns = (uint64_t)(obs_data_get_double(s, S_DELAY_SEC) * 1000000000.0);
    f->delayAtomic.store(f->delay_ns, std::memory_order_relaxed);

    long long fps = obs_data_get_int(s, S_BUFFER_FPS);
    if (fps < 1)  fps = 1;
    if (fps > 60) fps = 60;
    f->interval_ns = (uint64_t)(1000000000.0 / (double)fps);

    long long scalePct = obs_data_get_int(s, S_BUFFER_SCALE);
    if (scalePct < 10)  scalePct = 10;
    if (scalePct > 100) scalePct = 100;
    f->scale = (double)scalePct / 100.0;

    long long q = obs_data_get_int(s, S_CODEC_Q);
    if (q < 1)  q = 1;
    if (q > 10) q = 10;
    f->qualityAtomic.store((int)q, std::memory_order_relaxed);
    f->full444Atomic.store(obs_data_get_bool(s, S_CODEC_444), std::memory_order_relaxed);
    f->engineAtomic.store((int)obs_data_get_int(s, S_CODEC_ENGINE), std::memory_order_relaxed);

    if (f->gated)
        f->vk.store((int)obs_data_get_int(s, S_PDV_VK), std::memory_order_relaxed);
}

void vd_defaults(obs_data_t* s)
{
    obs_data_set_default_double(s, S_DELAY_SEC, DEFAULT_DELAY_SEC);
    obs_data_set_default_int (s, S_BUFFER_FPS,   60);
    obs_data_set_default_int (s, S_BUFFER_SCALE, 100);
    obs_data_set_default_int (s, S_CODEC_Q,      3);
    obs_data_set_default_bool(s, S_CODEC_444,    false);
    obs_data_set_default_int (s, S_CODEC_ENGINE, 0);
    obs_data_set_default_int (s, S_PDV_VK,       'V');
}

void* vd_create(obs_data_t* s, obs_source_t* ctx)
{
    auto* f = new VideoDelay();
    f->context = ctx;
    vd_update(f, s);
    f->worker = std::thread(vd_worker, f);
    blog(LOG_INFO, "[EasyBackupandDelay] video delay filter created (%.1fs, compressed ring)",
         (double)f->delay_ns / 1e9);
    return f;
}

void vdg_hotkey(void* data, obs_hotkey_id, obs_hotkey_t*, bool pressed)
{
    static_cast<VideoDelay*>(data)->held.store(pressed, std::memory_order_relaxed);
}

void* vd_create_gated(obs_data_t* s, obs_source_t* ctx)
{
    auto* f = new VideoDelay();
    f->context = ctx;
    f->gated   = true;
    vd_update(f, s);
    f->worker = std::thread(vd_worker, f);
    f->hotkey = obs_hotkey_register_source(
        ctx, "mav_push_to_delay_cam",
        "Push-to-Delay cámara (mantener para mostrar la cámara demorada)",
        vdg_hotkey, f);
    blog(LOG_INFO, "[EasyBackupandDelay] camera push-to-delay filter created (%.1fs)",
         (double)f->delay_ns / 1e9);
    return f;
}

void vd_destroy(void* data)
{
    auto* f = static_cast<VideoDelay*>(data);
    if (f->hotkey != OBS_INVALID_HOTKEY_ID)
        obs_hotkey_unregister(f->hotkey);
    {
        std::lock_guard<std::mutex> lk(f->mtx);
        f->stopWorker = true;
    }
    f->cv.notify_all();
    if (f->worker.joinable()) f->worker.join();
    vd_free(f);
    delete f;
}

obs_properties_t* vd_properties(void* data)
{
    auto* f = static_cast<VideoDelay*>(data);
    obs_properties_t* props = obs_properties_create();

    if (f && f->gated) {
        obs_property_t* keys = obs_properties_add_list(props, S_PDV_VK,
            "Tecla (mantener para grabar)",
            OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
        add_key_list(keys);
    }

    obs_property_t* p = obs_properties_add_float_slider(props, S_DELAY_SEC, "Delay", 0.0, 120.0, 0.1);
    obs_property_float_set_suffix(p, " s");

    p = obs_properties_add_int_slider(props, S_BUFFER_FPS,
                                      "Buffer FPS (tope; 60 = sin límite)", 1, 60, 1);
    obs_property_int_set_suffix(p, " fps");

    p = obs_properties_add_int_slider(props, S_BUFFER_SCALE,
                                      "Buffer resolution", 10, 100, 5);
    obs_property_int_set_suffix(p, " %");

    obs_property_t* eng = obs_properties_add_list(props, S_CODEC_ENGINE,
        "Codec del buffer", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(eng, "MJPEG (CPU) — compatible con todo", 0);
    obs_property_list_add_int(eng, "H.264 por hardware (GPU) — menos CPU/FPS", 1);

    obs_properties_add_int_slider(props, S_CODEC_Q,
        "Compresión (1 = máxima calidad, 10 = mínima RAM)", 1, 10, 1);
    obs_properties_add_bool(props, S_CODEC_444,
        "Color completo 4:4:4 (solo MJPEG; texto más nítido)");

    obs_properties_add_text(
        props, "info",
        (f && f->gated)
        ? "Aplicá este filtro a la cámara que quieras usar como \"callout\" demorado "
          "(no tiene que ser la del directo). Mientras MANTENÉS la tecla se GRABA la "
          "cámara; esa grabación aparece DEMORADA <delay>s después —justo cuando sale "
          "tu voz demorada, así cara y voz quedan sincronizadas— y luego desaparece.\n"
          "Poné el MISMO delay y la MISMA tecla que en \"Push-to-Delay (mic)\"."
        : "Los frames se comprimen (MJPEG, CPU en segundo plano) — 100% de "
          "resolución y 30-60 fps caben en unos cientos de MB de RAM.\n"
          "Usá el mismo delay en el filtro \"Screen Delay (audio)\" correspondiente.",
        OBS_TEXT_INFO);

    return props;
}

void vd_tick(void* data, float)
{
    auto* f = static_cast<VideoDelay*>(data);
    f->captured_this_tick = false;
}

void vd_reset_state(VideoDelay* f)
{
    std::lock_guard<std::mutex> lk(f->mtx);
    f->ring.clear();       f->ringBytes = 0;
    f->rawQ.clear();
    f->decoded.clear();    f->decW = f->decH = 0; f->decTs = 0; f->decHeld = false;
    f->shown.clear();      f->shownW = f->shownH = 0; f->shownTs = 0; f->shownHeld = false;
    f->announcedReady = false;
    f->capture_accum  = 0.0;
    f->stagePending[0] = f->stagePending[1] = false;
}

void dp_draw_guide(uint32_t w, uint32_t h)
{
    gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t* colp  = gs_effect_get_param_by_name(solid, "color");
    struct vec4 col; vec4_set(&col, 0.15f, 0.45f, 0.80f, 0.35f);
    gs_effect_set_vec4(colp, &col);
    while (gs_effect_loop(solid, "Solid"))
        gs_draw_sprite(nullptr, 0, w, h);
}

// El delay está ACTIVO pero el buffer está roto (el códec no puede comprimir, p. ej.
// FFmpeg ausente o de otra versión). En vez de pasar el directo EN SILENCIO —lo que
// filtraría el contenido que justamente se quería demorar— pintamos la pantalla de
// rojo sólido: tapa el contenido en vivo y es imposible no notarlo. El motivo exacto
// queda en el log del plugin.
void vd_draw_warning(uint32_t w, uint32_t h)
{
    gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t* colp  = gs_effect_get_param_by_name(solid, "color");
    struct vec4 col; vec4_set(&col, 0.55f, 0.04f, 0.04f, 1.0f);
    gs_effect_set_vec4(colp, &col);
    while (gs_effect_loop(solid, "Solid"))
        gs_draw_sprite(nullptr, 0, w, h);
}

void vd_render(void* data, gs_effect_t*)
{
    auto* f = static_cast<VideoDelay*>(data);

    if (delays_bypassed()) {
        if (!f->bypassed) { f->bypassed = true; vd_reset_state(f); }
        if (f->sourceMode) {
            if (f->guide) dp_draw_guide(f->cx ? f->cx : 640u, f->cy ? f->cy : 360u);
            return;
        }
        if (f->gated) {
            bool held = f->held.load(std::memory_order_relaxed);
            const int vk = f->vk.load(std::memory_order_relaxed);
            if (vk) held = held || ((GetAsyncKeyState(vk) & 0x8000) != 0);
            if (held) obs_source_skip_video_filter(f->context);
            return;
        }
        obs_source_skip_video_filter(f->context);
        return;
    }
    f->bypassed = false;

    bool camHeld = true;
    if (f->gated) {
        camHeld = f->held.load(std::memory_order_relaxed);
        const int vk = f->vk.load(std::memory_order_relaxed);
        if (vk) camHeld = camHeld || ((GetAsyncKeyState(vk) & 0x8000) != 0);
    }

    obs_source_t* target = nullptr;
    obs_source_t* parent = nullptr;
    obs_source_t* srcRef = nullptr;
    if (f->sourceMode) {
        std::lock_guard<std::mutex> lk(f->targetMtx);
        srcRef = f->targetWeak ? obs_weak_source_get_source(f->targetWeak) : nullptr;
        target = srcRef;
    } else {
        target = obs_filter_get_target(f->context);
        parent = obs_filter_get_parent(f->context);
    }

    auto bail = [&]() {
        if (f->sourceMode) {
            if (f->guide) dp_draw_guide(f->cx ? f->cx : 640u, f->cy ? f->cy : 360u);
        } else {
            obs_source_skip_video_filter(f->context);
        }
        if (srcRef) obs_source_release(srcRef);
    };

    if (!target || (!f->sourceMode && !parent)) {
        vd_diag(f, 1);
        bail();
        return;
    }

    uint32_t cx = obs_source_get_base_width(target);
    uint32_t cy = obs_source_get_base_height(target);
    if (!cx || !cy) {
        vd_diag(f, 2);
        bail();
        return;
    }

    f->cx = cx; f->cy = cy;
    f->bcx = std::max(2u, (uint32_t)(cx * f->scale) & ~1u);
    f->bcy = std::max(2u, (uint32_t)(cy * f->scale) & ~1u);

    if (!f->capture) f->capture = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    if (!f->capture) {
        vd_diag(f, 3);
        bail();
        return;
    }

    vd_diag(f, 0);

    uint64_t now = os_gettime_ns();

    if (!f->captured_this_tick) {
        f->captured_this_tick = true;

        double outFps = 30.0;
        struct obs_video_info ovi;
        if (obs_get_video_info(&ovi) && ovi.fps_den)
            outFps = (double)ovi.fps_num / (double)ovi.fps_den;
        double bufFps = 1.0e9 / (double)std::max<uint64_t>(1, f->interval_ns);
        double framesPerCapture = outFps / bufFps;
        if (framesPerCapture < 1.0) framesPerCapture = 1.0;

        bool first = (!f->stagePending[0] && !f->stagePending[1] && f->shownTs == 0);
        f->capture_accum += 1.0;
        bool due = first || (f->capture_accum >= framesPerCapture);
        if (due) {
            f->capture_accum -= framesPerCapture;
            if (f->capture_accum < 0.0) f->capture_accum = 0.0;
            const int cur = f->stageIdx;

            if (f->stagePending[cur] && f->stage[cur]) {
                uint8_t* pix = nullptr; uint32_t linesize = 0;
                if (gs_stagesurface_map(f->stage[cur], &pix, &linesize)) {
                    const uint32_t w = f->stageW[cur], h = f->stageH[cur];
                    const size_t row  = (size_t)w * 4;
                    const size_t need = row * h;

                    RFrame fr;
                    {
                        std::lock_guard<std::mutex> lk(f->mtx);
                        if (!f->rawPool.empty()) {
                            fr.px = std::move(f->rawPool.back());
                            f->rawPool.pop_back();
                        }
                    }
                    fr.px.resize(need);

                    if (linesize == row) {
                        memcpy(fr.px.data(), pix, need);
                    } else {
                        for (uint32_t y = 0; y < h; ++y)
                            memcpy(fr.px.data() + y * row, pix + (size_t)y * linesize, row);
                    }
                    gs_stagesurface_unmap(f->stage[cur]);

                    fr.w = w; fr.h = h; fr.ts = f->stageTs[cur];
                    fr.held = f->stageHeld[cur];

                    bool notify = false;
                    {
                        std::lock_guard<std::mutex> lk(f->mtx);
                        if (f->rawQ.size() < 3) {
                            f->rawQ.push_back(std::move(fr));
                            notify = true;
                        } else {
                            if (f->rawPool.size() < 4)
                                f->rawPool.push_back(std::move(fr.px));
                            if ((++f->droppedRaw % 120) == 1)
                                blog(LOG_WARNING, "[EasyBackupandDelay] video delay: encoder "
                                     "lagging, dropped %llu frame(s) — lower Buffer FPS "
                                     "or raise compression",
                                     (unsigned long long)f->droppedRaw);
                        }
                    }
                    if (notify) f->cv.notify_one();
                }
                f->stagePending[cur] = false;
            }

            if (f->stage[cur] &&
                (f->stageW[cur] != f->bcx || f->stageH[cur] != f->bcy)) {
                gs_stagesurface_destroy(f->stage[cur]);
                f->stage[cur] = nullptr;
            }
            if (!f->stage[cur])
                f->stage[cur] = gs_stagesurface_create(f->bcx, f->bcy, GS_RGBA);
            if (!f->stage[cur])
                vd_diag(f, 3);

            if (f->stage[cur] && (!f->gated || camHeld)) {
                gs_texrender_reset(f->capture);
                gs_blend_state_push();
                gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
                if (gs_texrender_begin(f->capture, f->bcx, f->bcy)) {
                    uint32_t pf   = obs_source_get_output_flags(target);
                    bool custom   = (pf & OBS_SOURCE_CUSTOM_DRAW) != 0;
                    bool async    = (pf & OBS_SOURCE_ASYNC) != 0;
                    struct vec4 clr; vec4_zero(&clr);
                    gs_clear(GS_CLEAR_COLOR, &clr, 0.0f, 0);
                    gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);
                    if (target == parent && !custom && !async)
                        obs_source_default_render(target);
                    else
                        obs_source_video_render(target);
                    gs_texrender_end(f->capture);

                    gs_texture_t* tex = gs_texrender_get_texture(f->capture);
                    if (tex) {
                        gs_stage_texture(f->stage[cur], tex);
                        f->stagePending[cur] = true;
                        f->stageTs[cur] = now;
                        f->stageW[cur]  = f->bcx;
                        f->stageH[cur]  = f->bcy;
                        f->stageIdx     = cur ^ 1;
                        f->last_capture = now;
                        f->stageHeld[cur] = camHeld;
                    }
                }
                gs_blend_state_pop();
            }
        }
    }

    bool haveNew = false;
    {
        std::lock_guard<std::mutex> lk(f->mtx);
        if (f->decTs != 0 && f->decTs != f->shownTs) {
            f->shown.swap(f->decoded);
            f->shownW = f->decW;
            f->shownH = f->decH;
            f->shownTs = f->decTs;
            f->shownHeld = f->decHeld;
            f->lastShownChange = os_gettime_ns();
            haveNew = true;
        }
    }

    const bool haveDelayed = !(f->shownTs == 0 || f->shown.empty() || !f->shownW || !f->shownH);
    const bool fresh = f->lastShownChange &&
                       (os_gettime_ns() - f->lastShownChange) < 300000000ULL;

    if (f->sourceMode) {
        if (!haveDelayed || !fresh) {
            if (f->guide) dp_draw_guide(f->cx ? f->cx : 640u, f->cy ? f->cy : 360u);
            if (srcRef) obs_source_release(srcRef);
            return;
        }
    } else if (f->gated) {
        if (!haveDelayed || !f->shownHeld || !fresh)
            return;
    } else {
        if (!haveDelayed) {
            if (f->codecFailed.load(std::memory_order_relaxed)) {
                // Delay ON pero el buffer está roto: avisar en pantalla, NO pasar en
                // vivo en silencio (ver vd_draw_warning).
                vd_draw_warning(f->cx ? f->cx : 640u, f->cy ? f->cy : 360u);
            } else {
                // Buffer todavía llenándose (los primeros <delay> s): en vivo, esperado.
                obs_source_skip_video_filter(f->context);
            }
            return;
        }
    }

    if (!f->disp || f->dispW != f->shownW || f->dispH != f->shownH) {
        if (f->disp) gs_texture_destroy(f->disp);
        f->disp  = gs_texture_create(f->shownW, f->shownH, GS_RGBA, 1, nullptr, GS_DYNAMIC);
        f->dispW = f->shownW;
        f->dispH = f->shownH;
        haveNew = true;
    }
    if (!f->disp) { bail(); return; }

    if (haveNew) {
        gs_texture_set_image(f->disp, f->shown.data(), f->shownW * 4, false);
        if (!f->announcedReady) {
            f->announcedReady = true;
            blog(LOG_INFO, "[EasyBackupandDelay] video delay READY: showing %.1fs-delayed "
                 "video (compressed ring)", (double)f->delay_ns / 1e9);
        }
    }

    gs_effect_t* effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), f->disp);
    while (gs_effect_loop(effect, "Draw"))
        gs_draw_sprite(f->disp, 0, f->cx, f->cy);

    if (srcRef) obs_source_release(srcRef);
}

enum gs_color_space vd_get_color_space(void* data, size_t count,
                                       const enum gs_color_space* preferred)
{
    (void)data;
    for (size_t i = 0; i < count; ++i)
        if (preferred[i] == GS_CS_SRGB) return GS_CS_SRGB;
    return (count > 0) ? preferred[0] : GS_CS_SRGB;
}

const char* dpv_get_name(void*) { return "Delay Playback (cámara demorada)"; }

bool dpv_add_source(void* param, obs_source_t* source)
{
    auto* list = static_cast<obs_property_t*>(param);
    if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) == 0) return true;
    if (obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT) return true;
    const char* id = obs_source_get_id(source);
    if (id && strcmp(id, "mav_delay_playback") == 0) return true;
    const char* name = obs_source_get_name(source);
    if (name && *name) obs_property_list_add_string(list, name, name);
    return true;
}

void dpv_reacquire(VideoDelay* f, const char* name)
{
    std::string n = (name && *name) ? name : "";
    {
        std::lock_guard<std::mutex> lk(f->targetMtx);
        if (n == f->targetName) return;
        f->targetName = n;
        if (f->targetWeak) { obs_weak_source_release(f->targetWeak); f->targetWeak = nullptr; }
        if (!n.empty()) {
            obs_source_t* src = obs_get_source_by_name(n.c_str());
            if (src) { f->targetWeak = obs_source_get_weak_source(src); obs_source_release(src); }
        }
    }
    vd_reset_state(f);
}

void dpv_update(void* data, obs_data_t* s)
{
    auto* f = static_cast<VideoDelay*>(data);
    const bool wantGated = (obs_data_get_int(s, S_DPV_MODE) == 1);
    if (wantGated != f->gated) { f->gated = wantGated; vd_reset_state(f); }

    vd_update(f, s);
    dpv_reacquire(f, obs_data_get_string(s, S_DPV_SOURCE));
    f->guide = obs_data_get_bool(s, S_DP_GUIDE);
}

void* dpv_create(obs_data_t* s, obs_source_t* ctx)
{
    auto* f = new VideoDelay();
    f->context    = ctx;
    f->gated      = true;
    f->sourceMode = true;
    dpv_update(f, s);
    f->worker = std::thread(vd_worker, f);
    f->hotkey = obs_hotkey_register_source(
        ctx, "mav_delay_playback_key",
        "Grabar cámara demorada (mantener)",
        vdg_hotkey, f);
    blog(LOG_INFO, "[EasyBackupandDelay] Delay Playback source created (%.1fs, '%s')",
         (double)f->delay_ns / 1e9, f->targetName.c_str());
    return f;
}

void dpv_destroy(void* data)
{
    auto* f = static_cast<VideoDelay*>(data);
    if (f->targetWeak) obs_weak_source_release(f->targetWeak);
    vd_destroy(f);
}

void dpv_defaults(obs_data_t* s)
{
    vd_defaults(s);
    obs_data_set_default_bool(s, S_DP_GUIDE, true);
    obs_data_set_default_int(s, S_DPV_MODE, 1);
}

obs_properties_t* dpv_properties(void*)
{
    obs_properties_t* props = obs_properties_create();

    obs_property_t* src = obs_properties_add_list(props, S_DPV_SOURCE,
        "Cámara/fuente a duplicar", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(src, "— elegí una fuente —", "");
    obs_enum_sources(dpv_add_source, src);

    obs_property_t* mode = obs_properties_add_list(props, S_DPV_MODE,
        "Cuándo mostrarla", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(mode, "Siempre (cámara demorada constante)", 0);
    obs_property_list_add_int(mode, "Solo mientras mantengo la tecla", 1);

    obs_property_t* keys = obs_properties_add_list(props, S_PDV_VK,
        "Tecla (para el modo \"con la tecla\")", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    add_key_list(keys);

    obs_property_t* p = obs_properties_add_float_slider(props, S_DELAY_SEC, "Delay", 0.0, 120.0, 0.1);
    obs_property_float_set_suffix(p, " s");

    obs_properties_add_bool(props, S_DP_GUIDE,
        "Mostrar recuadro guía cuando no hay video (apagalo antes de salir en vivo)");

    obs_properties_add_text(props, "dpv_info",
        "Elegí tu cámara arriba: esta fuente la CLONA con delay y la podés achicar/mover "
        "como cualquier fuente.\n"
        "• \"Siempre\": muestra la cámara demorada de forma constante (sin apretar nada).\n"
        "• \"Solo con la tecla\": aparece solo mientras mantenés la tecla (ideal para "
        "callouts; usá el mismo delay/tecla que \"Push-to-Delay (mic)\" para que voz y "
        "cámara salgan juntas).\n"
        "La cámara elegida tiene que estar activa en alguna escena para poder capturarla.",
        OBS_TEXT_INFO);
    return props;
}

void dpv_target_size(VideoDelay* f, uint32_t* w, uint32_t* h)
{
    *w = 0; *h = 0;
    obs_source_t* t = nullptr;
    {
        std::lock_guard<std::mutex> lk(f->targetMtx);
        t = f->targetWeak ? obs_weak_source_get_source(f->targetWeak) : nullptr;
    }
    if (t) {
        *w = obs_source_get_base_width(t);
        *h = obs_source_get_base_height(t);
        obs_source_release(t);
    }
}

uint32_t dpv_get_width(void* data)
{
    auto* f = static_cast<VideoDelay*>(data);
    uint32_t w, h; dpv_target_size(f, &w, &h);
    if (w) return w;
    return f->guide ? 640u : 0u;
}

uint32_t dpv_get_height(void* data)
{
    auto* f = static_cast<VideoDelay*>(data);
    uint32_t w, h; dpv_target_size(f, &w, &h);
    if (h) return h;
    return f->guide ? 360u : 0u;
}

