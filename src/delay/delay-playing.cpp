// ─────────────────────────────────────────────────────────────────────────────
// delay-playing.cpp
//
// Implementa el detector descrito en el .hpp y el aviso sonoro.
//
// El hilo de audio solo hace un store atómico con la hora del último pico que
// paso el umbral: nada de locks ni de asignaciones ahi. El resto (decidir el
// estado, tocar el aviso) corre en el hilo de UI, desde el timer del dock.
// ─────────────────────────────────────────────────────────────────────────────

#include "delay-playing.hpp"

#include <obs-module.h>
#include <util/platform.h>   // os_gettime_ns

#include <QSettings>
#include <QString>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>        // waveOut*

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace {

// ── Detección ────────────────────────────────────────────────────────────────

// −45 dBFS: por encima de eso ya es voz audible, por debajo es ruido de fondo o
// el silencio que el propio delay mete mientras la lane esta vacia.
constexpr float    kThreshold = 0.0056f;
constexpr uint64_t kHangNs    = 600000000ULL;   // 600 ms sin señal antes de volver a verde

std::atomic<uint64_t> g_lastLoudNs{0};

// Aviso ANTICIPADO: suena kPreAlertNs antes de que empiece a salir la voz
// demorada, para dar tiempo a callarse. Se agenda al detectar que arrancaste a
// hablar: eso va a salir al aire dentro de "delay" segundos.
constexpr int kLeadMin = 1, kLeadMax = 10, kLeadDefault = 2;
std::atomic<int> g_leadSec{kLeadDefault};        // segundos de anticipación

std::atomic<uint64_t> g_lastLiveNs{0};
std::atomic<uint64_t> g_preAlertAtNs{0};

// Mínimo entre dos avisos. Sin esto el pitido se dispara sin parar: el flanco de
// arranque se re-arma con solo kHangNs de silencio, y una fuente con audio
// entrecortado —una captura de navegador, por ejemplo— produce esos huecos cada
// pocos segundos. Cada pitido abre y cierra el dispositivo de salida, así que
// repetirlo seguido entrecorta TODO el audio del sistema, no solo el del aviso.
constexpr uint64_t kAlertCooldownNs = 20000000000ULL;   // 20 s
std::atomic<uint64_t> g_lastAlertNs{0};

std::atomic<bool> g_monitorOn{false};

// ── Aviso sonoro ─────────────────────────────────────────────────────────────

// DESTILDADO por defecto: abrir el dispositivo de audio para pitar es
// intrusivo, y el usuario no lo pidió si no lo tildó.
std::atomic<bool> g_alertEnabled{false};
std::atomic<int>  g_alertDevice{-1};       // -1 = WAVE_MAPPER (predeterminado)
std::atomic<bool> g_alertBusy{false};      // evita apilar avisos

// Dos blips cortos y agudos: se distinguen del contenido del stream y no tapan
// lo que estas escuchando.
std::vector<int16_t> build_blips(uint32_t rate)
{
    const double freq[2] = { 1320.0, 1760.0 };
    const uint32_t blipMs = 70, gapMs = 45;
    const uint32_t blipN = rate * blipMs / 1000;
    const uint32_t gapN  = rate * gapMs / 1000;

    std::vector<int16_t> pcm;
    pcm.reserve((blipN + gapN) * 2);

    for (int b = 0; b < 2; ++b) {
        for (uint32_t i = 0; i < blipN; ++i) {
            const double t = (double)i / rate;
            // Ventana de subida/bajada para que no chasquee en los bordes.
            const double env = std::sin(3.14159265358979 * (double)i / (double)blipN);
            const double s = std::sin(2.0 * 3.14159265358979 * freq[b] * t) * env * 0.35;
            pcm.push_back((int16_t)(s * 32767.0));
        }
        if (b == 0) pcm.insert(pcm.end(), gapN, 0);
    }
    return pcm;
}

void play_blocking(int deviceId)
{
    constexpr uint32_t kRate = 48000;
    static const std::vector<int16_t> pcm = build_blips(kRate);

    WAVEFORMATEX wf{};
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 1;
    wf.nSamplesPerSec  = kRate;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = (WORD)(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    HWAVEOUT h = nullptr;
    const UINT dev = (deviceId < 0) ? (UINT)WAVE_MAPPER : (UINT)deviceId;
    if (waveOutOpen(&h, dev, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        blog(LOG_WARNING, "[EasyBackupandDelay] no pude abrir el dispositivo del aviso (%d)", deviceId);
        return;
    }

    WAVEHDR hdr{};
    hdr.lpData         = (LPSTR)pcm.data();
    hdr.dwBufferLength = (DWORD)(pcm.size() * sizeof(int16_t));

    if (waveOutPrepareHeader(h, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR) {
        waveOutWrite(h, &hdr, sizeof(hdr));
        // Esperar a que termine antes de cerrar; si no, se corta el sonido.
        for (int i = 0; i < 200 && !(hdr.dwFlags & WHDR_DONE); ++i)
            Sleep(5);
        waveOutUnprepareHeader(h, &hdr, sizeof(hdr));
    }
    waveOutClose(h);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

void easyobs_note_delayed_peak(float peak)
{
    if (peak > kThreshold)
        g_lastLoudNs.store(os_gettime_ns(), std::memory_order_relaxed);
}

bool easyobs_delayed_playing(void)
{
    const uint64_t last = g_lastLoudNs.load(std::memory_order_relaxed);
    if (!last) return false;
    const uint64_t now = os_gettime_ns();
    return (now >= last) && (now - last) < kHangNs;
}

void easyobs_note_live_peak(float peak, uint64_t delayMs)
{
    if (peak <= kThreshold) return;

    const uint64_t now  = os_gettime_ns();
    const uint64_t prev = g_lastLiveNs.exchange(now, std::memory_order_relaxed);

    // Solo en el flanco de arranque: si venis hablando seguido no reagendamos el
    // aviso en cada bloque de audio.
    const bool onset = (prev == 0) || (now < prev) || (now - prev) > kHangNs;
    if (!onset) return;

    const uint64_t delayNs = delayMs * 1000000ULL;
    const uint64_t leadNs  = (uint64_t)g_leadSec.load(std::memory_order_relaxed) * 1000000000ULL;
    // Si el delay es mas corto que la anticipacion, avisar ya mismo.
    const uint64_t at = now + (delayNs > leadNs ? delayNs - leadNs : 0);

    // NO pisar un aviso ya agendado. Hablar normal deja pausas de mas de kHangNs
    // entre frases, y cada una cuenta como arranque nuevo: con un store a secas,
    // cada pausa corria el aviso pendiente otros (delay - anticipacion) hacia
    // adelante y no sonaba nunca, salvo que te callaras todo ese rato. El aviso
    // tiene que quedar clavado al PRIMER arranque de la tanda, que es el que
    // marca cuando empieza a salir la voz demorada.
    uint64_t none = 0;
    if (g_preAlertAtNs.compare_exchange_strong(none, at, std::memory_order_relaxed)) {
        // Traza de soporte: sin esto, "no suena el bip" se diagnostica a ciegas.
        // Es rala (una por tanda de habla), no ensucia el log.
        blog(LOG_INFO, "[EasyBackupandDelay] aviso agendado en %llu ms (delay %llu ms, anticipacion %d s)",
             (unsigned long long)((at - now) / 1000000ULL),
             (unsigned long long)delayMs, g_leadSec.load(std::memory_order_relaxed));
    }
}

void easyobs_alert_tick(void)
{
    const uint64_t at = g_preAlertAtNs.load(std::memory_order_relaxed);
    if (!at) return;

    const uint64_t now = os_gettime_ns();
    if (now < at) return;

    g_preAlertAtNs.store(0, std::memory_order_relaxed);
    if (!g_alertEnabled.load(std::memory_order_relaxed)) {
        blog(LOG_INFO, "[EasyBackupandDelay] aviso NO suena: la casilla esta destildada");
        return;
    }

    const uint64_t last = g_lastAlertNs.load(std::memory_order_relaxed);
    if (last && (now - last) < kAlertCooldownNs) {
        blog(LOG_INFO, "[EasyBackupandDelay] aviso NO suena: en cooldown, faltan %llu ms",
             (unsigned long long)((kAlertCooldownNs - (now - last)) / 1000000ULL));
        return;
    }
    g_lastAlertNs.store(now, std::memory_order_relaxed);

    blog(LOG_INFO, "[EasyBackupandDelay] sonando aviso previo (dispositivo %d)",
         g_alertDevice.load(std::memory_order_relaxed));
    easyobs_play_alert();
}

// ── Monitor de la voz demorada ───────────────────────────────────────────────

namespace {

bool is_our_audio_delay(const char* id)
{
    return id && (strcmp(id, "mav_screen_delay_audio") == 0 ||
                  strcmp(id, "mav_push_to_delay")      == 0);
}

void count_delay_filter(obs_source_t*, obs_source_t* filter, void* param)
{
    if (is_our_audio_delay(obs_source_get_id(filter)))
        (*static_cast<int*>(param))++;
}

bool apply_monitor_to_source(void* param, obs_source_t* src)
{
    int found = 0;
    obs_source_enum_filters(src, count_delay_filter, &found);
    if (!found) return true;

    const bool on = *static_cast<bool*>(param);
    const obs_monitoring_type want =
        on ? OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT : OBS_MONITORING_TYPE_NONE;

    // Solo tocar si hace falta: así no pisamos en cada arranque el monitoreo que
    // el usuario haya puesto a mano, ni llenamos el log repitiendo lo mismo.
    if (obs_source_get_monitoring_type(src) == want) return true;

    obs_source_set_monitoring_type(src, want);
    blog(LOG_INFO, "[EasyBackupandDelay] monitor de voz demorada %s en '%s'",
         on ? "ACTIVADO" : "desactivado", obs_source_get_name(src));
    return true;
}

} // namespace

bool easyobs_delay_monitor_enabled(void)
{
    return g_monitorOn.load(std::memory_order_relaxed);
}

void easyobs_apply_delay_monitor(void)
{
    bool on = g_monitorOn.load(std::memory_order_relaxed);
    obs_enum_sources(apply_monitor_to_source, &on);
}

void easyobs_set_delay_monitor(bool on)
{
    g_monitorOn.store(on, std::memory_order_relaxed);
    QSettings("MAVSoft", "EasyOBSBackups").setValue("delayMonitor", on);
    easyobs_apply_delay_monitor();
}

void easyobs_play_alert(void)
{
    // Un aviso a la vez: si todavia esta sonando el anterior, se ignora.
    bool expected = false;
    if (!g_alertBusy.compare_exchange_strong(expected, true)) return;

    const int dev = g_alertDevice.load(std::memory_order_relaxed);
    std::thread([dev]() {
        play_blocking(dev);
        g_alertBusy.store(false, std::memory_order_relaxed);
    }).detach();
}

// ── Configuración ────────────────────────────────────────────────────────────

void easyobs_load_playing_settings(void)
{
    QSettings st("MAVSoft", "EasyOBSBackups");
    g_alertEnabled.store(st.value("alertEnabled", false).toBool(), std::memory_order_relaxed);
    g_alertDevice.store(st.value("alertDeviceId", -1).toInt(), std::memory_order_relaxed);
    g_monitorOn.store(st.value("delayMonitor", false).toBool(), std::memory_order_relaxed);
    easyobs_set_alert_lead_sec(st.value("alertLeadSec", kLeadDefault).toInt());
}

bool easyobs_alert_enabled(void)
{
    return g_alertEnabled.load(std::memory_order_relaxed);
}

void easyobs_set_alert_enabled(bool on)
{
    g_alertEnabled.store(on, std::memory_order_relaxed);
    QSettings("MAVSoft", "EasyOBSBackups").setValue("alertEnabled", on);
}

int easyobs_alert_lead_sec(void)
{
    return g_leadSec.load(std::memory_order_relaxed);
}

void easyobs_set_alert_lead_sec(int sec)
{
    if (sec < kLeadMin) sec = kLeadMin;
    if (sec > kLeadMax) sec = kLeadMax;
    g_leadSec.store(sec, std::memory_order_relaxed);
    QSettings("MAVSoft", "EasyOBSBackups").setValue("alertLeadSec", sec);
}

int easyobs_alert_device(void)
{
    return g_alertDevice.load(std::memory_order_relaxed);
}

void easyobs_set_alert_device(int deviceId)
{
    g_alertDevice.store(deviceId, std::memory_order_relaxed);
    QSettings("MAVSoft", "EasyOBSBackups").setValue("alertDeviceId", deviceId);
}

QStringList easyobs_alert_device_names(void)
{
    QStringList names;
    const UINT n = waveOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        WAVEOUTCAPSW caps{};
        if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            names << QString::fromWCharArray(caps.szPname);
        else
            names << QString("Dispositivo %1").arg(i);
    }
    return names;
}

void easyobs_shutdown_alert(void)
{
    // El aviso se toca en un hilo suelto que abre y cierra el dispositivo solo;
    // esperamos a que termine para no cerrar OBS con el hilo a medio camino.
    for (int i = 0; i < 100 && g_alertBusy.load(std::memory_order_relaxed); ++i)
        Sleep(10);
}
