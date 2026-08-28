#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// delay-common.hpp
//
// Prólogo compartido por delay-filters.cpp y por los motores delay-*.inc (que se
// incluyen textualmente dentro de una única unidad de traducción). Reúne las
// claves de settings, los helpers de tiempo/audio, el estado global de bypass y
// la lista de teclas, para que cada .inc sea autoexplicativo y no dependa del
// orden en que se incluyen.
// ─────────────────────────────────────────────────────────────────────────────

#include <obs-module.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>      // VK_*, GetAsyncKeyState

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cmath>

// ── Claves de settings (obs_data) ────────────────────────────────────────────
#define S_DELAY_SEC    "delay_s"
#define S_BUFFER_FPS   "buffer_fps"
#define S_BUFFER_SCALE "buffer_scale"
#define S_CODEC_Q      "codec_quality"
#define S_CODEC_444    "codec_full444"
#define S_CODEC_ENGINE "codec_engine"    // 0 = MJPEG (CPU), 1 = H.264 por hardware
#define S_PDV_VK       "pdv_vk"          // tecla sondeada globalmente del Push-to-Delay (cámara)
#define S_DP_GUIDE     "dp_guide"        // Playback: mostrar un recuadro guía mientras está inactivo (para ubicarlo)
#define S_DPV_SOURCE   "dpv_source"      // Delay Playback: nombre de la fuente a clonar
#define S_DPV_MODE     "dpv_mode"        // Delay Playback: 0 = siempre activo, 1 = solo mientras se mantiene la tecla

#define DEFAULT_DELAY_SEC 20.0
#define DEFAULT_DELAY_MS  20000   // solo para el inicializador interno del struct

// ── Helpers de tiempo / audio ────────────────────────────────────────────────

// Detecta un salto de timestamp (hacia atrás o un hueco > 1 s) → hay que resetear.
static inline bool is_ts_jump(uint64_t ts, uint64_t prev)
{
    return ts < prev || (ts - prev) > 1000000000ULL;
}

// dB → multiplicador lineal.
static inline float db_to_mul(float db) { return powf(10.0f, db / 20.0f); }

// Coeficiente de un filtro de un polo para un tiempo (ms) dado y un sample rate.
static inline float one_pole_coef(float time_ms, uint32_t sr)
{
    float tau = (time_ms <= 0.0f) ? 0.0f : time_ms / 1000.0f;
    if (tau <= 0.0f) return 1.0f;
    return 1.0f - expf(-1.0f / (tau * (float)sr));
}

// ── Reporte de nivel del audio demorado ──────────────────────────────────────
// Definida en delay-playing.cpp. Los motores le pasan el pico del bloque DEMORADO
// que acaban de emitir; el dock lo usa para la luz roja/verde ("¿estoy pisando mi
// propia voz?"). Es lock-free: se puede llamar desde el hilo de audio.
void easyobs_note_delayed_peak(float peak);

// Pico de la señal EN VIVO que entra al delay + cuánto dura el delay: con eso el
// aviso puede sonar 2 s ANTES de que esa voz empiece a salir al aire.
void easyobs_note_live_peak(float peak, uint64_t delayMs);

// ── Estado global de "bypassear todos los delays" ────────────────────────────
// La DEFINICIÓN de g_delay_bypass vive en delay-filters.cpp; acá solo se declara
// para que todos los motores y el resto del archivo lean el MISMO flag.
extern std::atomic<bool> g_delay_bypass;

static inline bool delays_bypassed()
{
    return g_delay_bypass.load(std::memory_order_relaxed);
}

// ── Lista de teclas (VK) compartida por todos los selectores de tecla ────────
static inline void add_key_list(obs_property_t* keys)
{
    obs_property_list_add_int(keys, "Ninguna (usar solo el atajo de OBS)", 0);
    for (char c = 'A'; c <= 'Z'; ++c) { char n[2] = { c, 0 }; obs_property_list_add_int(keys, n, c); }
    for (char c = '0'; c <= '9'; ++c) { char n[2] = { c, 0 }; obs_property_list_add_int(keys, n, c); }
    for (int i = 1; i <= 12; ++i) {
        char n[4]; snprintf(n, sizeof(n), "F%d", i);
        obs_property_list_add_int(keys, n, VK_F1 + (i - 1));
    }
    obs_property_list_add_int(keys, "Botón lateral del mouse 1 (X1)", VK_XBUTTON1);
    obs_property_list_add_int(keys, "Botón lateral del mouse 2 (X2)", VK_XBUTTON2);
    obs_property_list_add_int(keys, "Ctrl",  VK_CONTROL);
    obs_property_list_add_int(keys, "Alt",   VK_MENU);
    obs_property_list_add_int(keys, "Shift", VK_SHIFT);
    obs_property_list_add_int(keys, "Espacio", VK_SPACE);
    obs_property_list_add_int(keys, "Tab",   VK_TAB);
    obs_property_list_add_int(keys, "Bloq Mayús", VK_CAPITAL);
}
