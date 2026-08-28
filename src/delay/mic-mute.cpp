// ─────────────────────────────────────────────────────────────────────────────
// mic-mute.cpp
//
// Implementa el botón de micrófono descrito en el .hpp.
//
// El estado NO se guarda acá: la única fuente de verdad es el flag "muted" de la
// propia fuente de OBS. Así el botón del dock, el mezclador de OBS y cualquier
// otro atajo muestran siempre lo mismo, sin dos estados que se puedan desfasar.
// ─────────────────────────────────────────────────────────────────────────────

#include "mic-mute.hpp"

#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>   // os_gettime_ns

#include <QSettings>
#include <QStringList>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // GetAsyncKeyState — sondeo global de la tecla

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace {

std::mutex  g_mtx;
std::string g_micName;                  // vacío = resolución automática
std::atomic<uint64_t> g_micCombo{0};    // hasta 8 VK empacados, igual que el delay

uint64_t pack_keys(const std::vector<int>& vks)
{
    uint64_t v = 0; int n = 0;
    for (int k : vks) {
        if (k <= 0 || k > 255 || n >= 8) continue;
        v |= (uint64_t)(k & 0xFF) << (8 * n);
        ++n;
    }
    return v;
}

std::vector<int> unpack_keys(uint64_t v)
{
    std::vector<int> out;
    for (int i = 0; i < 8; ++i) {
        int k = (int)((v >> (8 * i)) & 0xFF);
        if (k) out.push_back(k);
    }
    return out;
}

// ── Resolución de la fuente ──────────────────────────────────────────────────
// Devuelve una referencia nueva (hay que soltarla con obs_source_release) o null.

bool is_input_capture(const char* id)
{
    return id && (strcmp(id, "wasapi_input_capture") == 0 ||
                  strcmp(id, "coreaudio_input_capture") == 0 ||
                  strcmp(id, "pulse_input_capture") == 0);
}

bool find_input_capture(void* param, obs_source_t* src)
{
    auto** found = static_cast<obs_source_t**>(param);
    if (is_input_capture(obs_source_get_id(src))) {
        *found = obs_source_get_ref(src);
        return false;   // cortar la enumeración
    }
    return true;
}

obs_source_t* resolve_mic()
{
    {   // 1) La elegida a mano, si sigue existiendo.
        std::lock_guard<std::mutex> lk(g_mtx);
        if (!g_micName.empty()) {
            if (obs_source_t* s = obs_get_source_by_name(g_micName.c_str()))
                return s;
        }
    }

    // 2) Canales globales de entrada de OBS: Mic/Aux (3), Aux 2 (4), Aux 3 (5).
    for (uint32_t ch = 3; ch <= 5; ++ch) {
        if (obs_source_t* s = obs_get_output_source(ch)) {
            if (obs_source_get_output_flags(s) & OBS_SOURCE_AUDIO)
                return s;
            obs_source_release(s);
        }
    }

    // 3) La primera captura de entrada que haya puesta en la colección.
    obs_source_t* found = nullptr;
    obs_enum_sources(find_input_capture, &found);
    return found;
}

obs_hotkey_id g_mic_hotkey = OBS_INVALID_HOTKEY_ID;

void toggle_mic_hotkey(void*, obs_hotkey_id, obs_hotkey_t*, bool pressed)
{
    if (!pressed) return;   // solo en key-down
    easyobs_toggle_mic();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

QString easyobs_get_mic_source_name(void)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return QString::fromStdString(g_micName);
}

void easyobs_set_mic_source_name(const QString& name)
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_micName = name.toStdString();
    }
    QSettings st("MAVSoft", "EasyOBSBackups");
    st.setValue("micSourceName", name);
}

std::vector<int> easyobs_get_mic_keys(void)
{
    return unpack_keys(g_micCombo.load(std::memory_order_relaxed));
}

void easyobs_set_mic_keys(const std::vector<int>& vks)
{
    g_micCombo.store(pack_keys(vks), std::memory_order_relaxed);

    QStringList parts;
    for (int k : vks) if (k > 0) parts << QString::number(k);
    QSettings st("MAVSoft", "EasyOBSBackups");
    st.setValue("micMuteKeys", parts.join(','));
}

void easyobs_load_mic_settings(void)
{
    QSettings st("MAVSoft", "EasyOBSBackups");

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_micName = st.value("micSourceName").toString().toStdString();
    }

    std::vector<int> vks;
    const QString s = st.value("micMuteKeys").toString();
    if (!s.isEmpty())
        for (const QString& p : s.split(',', Qt::SkipEmptyParts)) vks.push_back(p.toInt());
    g_micCombo.store(pack_keys(vks), std::memory_order_relaxed);
}

bool easyobs_mic_available(void)
{
    obs_source_t* s = resolve_mic();
    if (!s) return false;
    obs_source_release(s);
    return true;
}

QString easyobs_resolved_mic_name(void)
{
    obs_source_t* s = resolve_mic();
    if (!s) return QString();
    const char* n = obs_source_get_name(s);
    QString out = n ? QString::fromUtf8(n) : QString();
    obs_source_release(s);
    return out;
}

bool easyobs_mic_muted(void)
{
    obs_source_t* s = resolve_mic();
    if (!s) return false;
    const bool muted = obs_source_muted(s);
    obs_source_release(s);
    return muted;
}

void easyobs_set_mic_muted(bool muted)
{
    obs_source_t* s = resolve_mic();
    if (!s) return;
    if (obs_source_muted(s) != muted) {
        obs_source_set_muted(s, muted);
        blog(LOG_INFO, "[EasyBackupandDelay] microfono %s (%s)",
             muted ? "SILENCIADO" : "AL AIRE", obs_source_get_name(s));
    }
    obs_source_release(s);
}

void easyobs_toggle_mic(void)
{
    // Mismo debounce que el delay: una apretada bindeada en dos lugares (la tecla
    // del plugin + el atajo de OBS) no se cancela a sí misma.
    static std::atomic<uint64_t> lastToggle{0};
    const uint64_t now  = os_gettime_ns();
    const uint64_t prev = lastToggle.load(std::memory_order_relaxed);
    if (now - prev < 200000000ULL) return;
    lastToggle.store(now, std::memory_order_relaxed);

    obs_source_t* s = resolve_mic();
    if (!s) {
        blog(LOG_WARNING, "[EasyBackupandDelay] no hay microfono para silenciar");
        return;
    }
    const bool muted = !obs_source_muted(s);
    obs_source_set_muted(s, muted);
    blog(LOG_INFO, "[EasyBackupandDelay] microfono %s (%s)",
         muted ? "SILENCIADO" : "AL AIRE", obs_source_get_name(s));
    obs_source_release(s);
}

bool easyobs_mic_keys_down(void)
{
    const uint64_t combo = g_micCombo.load(std::memory_order_relaxed);
    if (!combo) return false;   // sin asignar → nunca dispara
    for (int i = 0; i < 8; ++i) {
        const int k = (int)((combo >> (8 * i)) & 0xFF);
        if (k && !(GetAsyncKeyState(k) & 0x8000)) return false;
    }
    return true;   // dispara con TODAS las teclas del combo apretadas
}

void easyobs_register_mic_hotkey(void)
{
    g_mic_hotkey = obs_hotkey_register_frontend(
        "mav_toggle_mic_mute",
        "EasyBackupandDelay: silenciar/activar microfono",
        toggle_mic_hotkey, nullptr);
}

void easyobs_unregister_mic_hotkey(void)
{
    if (g_mic_hotkey != OBS_INVALID_HOTKEY_ID) {
        obs_hotkey_unregister(g_mic_hotkey);
        g_mic_hotkey = OBS_INVALID_HOTKEY_ID;
    }
}
