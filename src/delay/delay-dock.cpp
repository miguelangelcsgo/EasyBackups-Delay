#include "delay-dock.hpp"
#include "mic-mute.hpp"
#include "delay-playing.hpp"
#include "i18n.hpp"

#include <obs-frontend-api.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QSettings>
#include <QStringList>
#include <QString>

#include <atomic>
#include <vector>
#include <cstdint>

#define NOMINMAX
#include <windows.h>   // GetAsyncKeyState — polling global de la tecla de toggle

// El estado de bypass vive en delay-filters.cpp; el botón y la hotkey lo comparten.
bool easyobs_delays_bypassed(void);
void easyobs_set_delays_bypassed(bool on);
void easyobs_toggle_delays(void);   // flip con debounce (seguro ante doble binding)

// ── Combo global de toggle configurable (vacío = ninguno, usar la hotkey de OBS) ──
// Se guarda como atomic empacado (hasta 8 VK, un byte cada uno) para que el poll de
// 40 ms nunca toque QSettings ni un mutex; el diálogo de Backup lo actualiza a través
// del setter, que además lo persiste. El toggle dispara cuando TODAS las teclas del
// combo están apretadas a la vez.
static std::atomic<uint64_t> g_toggle_combo{0};

static uint64_t pack_keys(const std::vector<int>& vks)
{
    uint64_t v = 0; int n = 0;
    for (int k : vks) {
        if (k <= 0 || k > 255 || n >= 8) continue;
        v |= (uint64_t)(k & 0xFF) << (8 * n);
        ++n;
    }
    return v;
}

static std::vector<int> unpack_keys(uint64_t v)
{
    std::vector<int> out;
    for (int i = 0; i < 8; ++i) {
        int k = (int)((v >> (8 * i)) & 0xFF);
        if (k) out.push_back(k);
    }
    return out;
}

std::vector<int> easyobs_get_toggle_keys(void)
{
    return unpack_keys(g_toggle_combo.load(std::memory_order_relaxed));
}

void easyobs_set_toggle_keys(const std::vector<int>& vks)
{
    g_toggle_combo.store(pack_keys(vks), std::memory_order_relaxed);
    QStringList parts;
    for (int k : vks) if (k > 0) parts << QString::number(k);
    QSettings st("MAVSoft", "EasyOBSBackups");
    st.setValue("delayToggleKeys", parts.join(','));
    st.setValue("delayToggleVk", vks.empty() ? 0 : vks.front());   // compat hacia atrás
}

// Compat: accessors de una sola tecla (por si algún caller viejo los usa).
int  easyobs_get_toggle_vk(void)      { auto v = easyobs_get_toggle_keys(); return v.empty() ? 0 : v.front(); }
void easyobs_set_toggle_vk(int vk)    { std::vector<int> v; if (vk > 0) v.push_back(vk); easyobs_set_toggle_keys(v); }

static const char* kDockId = "easyobs_delay_switch";

void register_delay_dock(void)
{
    {   // Restaura el combo de toggle guardado (formato nuevo; cae al viejo de 1 tecla).
        QSettings st("MAVSoft", "EasyOBSBackups");
        std::vector<int> vks;
        const QString s = st.value("delayToggleKeys").toString();
        if (!s.isEmpty()) {
            for (const QString& p : s.split(',', Qt::SkipEmptyParts)) vks.push_back(p.toInt());
        } else {
            int old = st.value("delayToggleVk", 0).toInt();
            if (old) vks.push_back(old);
        }
        g_toggle_combo.store(pack_keys(vks), std::memory_order_relaxed);
    }

    // OBS toma ownership de este widget (se remueve vía obs_frontend_remove_dock).
    auto* w = new QWidget();
    w->setObjectName("EasyOBSDelaySwitch");

    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(8, 8, 8, 8);

    auto* btn = new QPushButton(w);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(52);
    lay->addWidget(btn);

    auto* micBtn = new QPushButton(w);
    micBtn->setCheckable(true);
    micBtn->setCursor(Qt::PointingHandCursor);
    micBtn->setMinimumHeight(52);
    lay->addWidget(micBtn);

    // Luz de "esta sonando el audio demorado": roja = tu voz demorada esta al
    // aire ahora, no hables encima; verde = no sale nada demorado.
    auto* light = new QLabel(w);
    light->setAlignment(Qt::AlignCenter);
    light->setMinimumHeight(46);
    light->setWordWrap(true);
    lay->addWidget(light);

    lay->addStretch();

    // Pinta el botón según el estado de bypass actual (y el idioma).
    auto applyState = [btn]() {
        const bool byp = easyobs_delays_bypassed();
        btn->setChecked(byp);
        btn->setText(byp ? T(S::DockBypassed) : T(S::DockActive));
        btn->setStyleSheet(byp
            ? "QPushButton{background:#8a2020;color:white;font-weight:bold;"
              "border:none;border-radius:6px;padding:6px;}"
              "QPushButton:hover{background:#a02828;}"
            : "QPushButton{background:#1f7a34;color:white;font-weight:bold;"
              "border:none;border-radius:6px;padding:6px;}"
              "QPushButton:hover{background:#25923f;}");
    };
    applyState();

    // Verde = al aire, rojo = silenciado. Si no hay ninguna fuente de micrófono
    // que resolver, el botón queda gris y deshabilitado en vez de mentir un estado.
    auto applyMicState = [micBtn]() {
        if (!easyobs_mic_available()) {
            micBtn->setEnabled(false);
            micBtn->setChecked(false);
            micBtn->setText(T(S::MicNotFound));
            micBtn->setToolTip(QString());
            micBtn->setStyleSheet("QPushButton{background:#3a3a3a;color:#999;font-weight:bold;"
                                  "border:none;border-radius:6px;padding:6px;}");
            return;
        }
        const bool muted = easyobs_mic_muted();
        micBtn->setEnabled(true);
        micBtn->setChecked(muted);
        micBtn->setText(muted ? T(S::MicMuted) : T(S::MicLive));
        micBtn->setToolTip(easyobs_resolved_mic_name());
        micBtn->setStyleSheet(muted
            ? "QPushButton{background:#8a2020;color:white;font-weight:bold;"
              "border:none;border-radius:6px;padding:6px;}"
              "QPushButton:hover{background:#a02828;}"
            : "QPushButton{background:#1f7a34;color:white;font-weight:bold;"
              "border:none;border-radius:6px;padding:6px;}"
              "QPushButton:hover{background:#25923f;}");
    };
    applyMicState();

    auto applyLight = [light](bool playing) {
        light->setText(playing ? T(S::PlayBusy) : T(S::PlayFree));
        light->setStyleSheet(playing
            ? "QLabel{background:#b02020;color:white;font-weight:bold;"
              "border-radius:6px;padding:8px;}"
            : "QLabel{background:#1f7a34;color:white;font-weight:bold;"
              "border-radius:6px;padding:8px;}");
    };
    applyLight(easyobs_delayed_playing());

    QObject::connect(btn, &QPushButton::clicked, w, [applyState]() {
        easyobs_toggle_delays();
        applyState();
    });

    QObject::connect(micBtn, &QPushButton::clicked, w, [applyMicState]() {
        easyobs_toggle_mic();
        applyMicState();
    });

    // Tick de 40 ms: (1) hacer poll de la tecla configurada y togglear en su
    // flanco de subida, (2) refrescar el botón cuando el estado compartido
    // realmente cambió (así la hotkey de OBS, la tecla y un cambio de idioma
    // mantienen el botón sincronizado).
    auto* timer = new QTimer(w);
    timer->setInterval(40);
    QObject::connect(timer, &QTimer::timeout, w, [applyState, applyMicState, applyLight]() {
        static bool prevKeyDown    = false;
        static bool prevMicKeyDown = false;
        static bool lastShown      = easyobs_delays_bypassed();
        static bool lastMicMuted   = easyobs_mic_muted();
        static bool lastMicAvail   = easyobs_mic_available();
        static bool lastPlaying    = easyobs_delayed_playing();
        static int  lastLang       = (int)currentLang();
        static bool firstPass      = true;

        const uint64_t combo = g_toggle_combo.load(std::memory_order_relaxed);
        {
            bool allDown = (combo != 0);   // vacío → nunca dispara
            for (int i = 0; i < 8 && allDown; ++i) {
                int k = (int)((combo >> (8 * i)) & 0xFF);
                if (k && !(GetAsyncKeyState(k) & 0x8000)) allDown = false;
            }
            const bool down = allDown;
            if (down && !prevKeyDown)
                easyobs_toggle_delays();      // con debounce (sobrevive al doble binding)
            prevKeyDown = down;
        }

        {   // Tecla del micrófono: mismo flanco de subida que la del delay.
            const bool down = easyobs_mic_keys_down();
            if (down && !prevMicKeyDown)
                easyobs_toggle_mic();
            prevMicKeyDown = down;
        }

        const bool byp   = easyobs_delays_bypassed();
        const int  lang  = (int)currentLang();
        if (firstPass || byp != lastShown || lang != lastLang) {
            lastShown = byp;
            applyState();
        }

        // El mute también puede cambiar desde el mezclador de OBS o desde el atajo
        // nativo: repintar cuando el estado real se movió, venga de donde venga.
        const bool muted = easyobs_mic_muted();
        const bool avail = easyobs_mic_available();
        if (firstPass || muted != lastMicMuted || avail != lastMicAvail || lang != lastLang) {
            lastMicMuted = muted;
            lastMicAvail = avail;
            applyMicState();
        }

        // Luz roja/verde del audio demorado. El aviso sonoro se dispara solo en el
        // flanco a "sonando", y desde aca (hilo de UI), nunca desde el de audio.
        const bool playing = easyobs_delayed_playing();
        easyobs_alert_tick();   // el aviso va agendado 2 s antes, no al empezar
        if (firstPass || playing != lastPlaying || lang != lastLang) {
            lastPlaying = playing;
            applyLight(playing);
        }

        lastLang  = lang;
        firstPass = false;
    });
    timer->start();

    obs_frontend_add_dock_by_id(kDockId, "EasyBackupandDelay \xE2\x80\x94 Delays", w);
}

void unregister_delay_dock(void)
{
    obs_frontend_remove_dock(kDockId);
}
