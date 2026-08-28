// ─────────────────────────────────────────────────────────────────────────────
// delay-visibility.cpp
//
// Implementa el "las fuentes siguen al interruptor de delays" descrito en el .hpp.
//
// Dos listas de nombres de fuente (las que van con delay y las que van sin delay)
// viven en memoria bajo un mutex y se persisten con QSettings, igual que el combo
// de teclas del dock. El estado se aplica recorriendo TODAS las escenas y sus
// grupos: cada scene item cuya fuente esté configurada se prende o apaga.
//
// Se aplica por nombre de fuente, no por scene item, a propósito: si la misma
// pantalla está puesta en tres escenas, las tres se sincronizan solas.
// ─────────────────────────────────────────────────────────────────────────────

#include "delay-visibility.hpp"

#include <obs.h>
#include <obs-module.h>

#include <QSettings>
#include <QStringList>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Definido en delay-filters.cpp — true = delays APAGADOS (todo en vivo).
bool easyobs_delays_bypassed(void);

namespace {

std::mutex               g_mtx;
std::vector<std::string> g_withDelay;      // visibles mientras el delay está ACTIVO
std::vector<std::string> g_withoutDelay;   // visibles mientras el delay está APAGADO

bool has_name(const std::vector<std::string>& v, const char* name)
{
    return name && std::find(v.begin(), v.end(), std::string(name)) != v.end();
}

void erase_name(std::vector<std::string>& v, const std::string& n)
{
    v.erase(std::remove(v.begin(), v.end(), n), v.end());
}

// Guarda ambas listas. El llamador ya tiene g_mtx tomado.
void persist_locked()
{
    QStringList a, b;
    for (const auto& s : g_withDelay)    a << QString::fromStdString(s);
    for (const auto& s : g_withoutDelay) b << QString::fromStdString(s);

    QSettings st("MAVSoft", "EasyOBSBackups");
    st.setValue("delayShowWithDelay",    a);
    st.setValue("delayShowWithoutDelay", b);
}

// ── Aplicación del estado ────────────────────────────────────────────────────
// Snapshot para que el recorrido de escenas no tenga que tomar el mutex ni leer
// QSettings (puede dispararse desde el hilo del hotkey).
struct ApplyCtx {
    bool                     bypassed = false;
    std::vector<std::string> withDelay;
    std::vector<std::string> withoutDelay;
};

bool apply_item(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
    auto* c = static_cast<ApplyCtx*>(param);

    obs_source_t* src  = obs_sceneitem_get_source(item);
    const char*   name = src ? obs_source_get_name(src) : nullptr;

    if (name) {
        // bypassed = sin delay. Con delay activo se ve la lista "withDelay".
        int want = -1;
        if      (has_name(c->withDelay,    name)) want = c->bypassed ? 0 : 1;
        else if (has_name(c->withoutDelay, name)) want = c->bypassed ? 1 : 0;

        if (want >= 0 && obs_sceneitem_visible(item) != (want != 0))
            obs_sceneitem_set_visible(item, want != 0);
    }

    // Un grupo puede estar configurado él mismo y además contener fuentes configuradas.
    if (obs_sceneitem_is_group(item))
        obs_sceneitem_group_enum_items(item, apply_item, param);

    return true;   // seguir enumerando
}

bool apply_scene(void* param, obs_source_t* sceneSrc)
{
    if (obs_scene_t* scene = obs_scene_from_source(sceneSrc))
        obs_scene_enum_items(scene, apply_item, param);
    return true;
}

// Corre en el hilo de UI de OBS; toma ownership del contexto.
void apply_task(void* param)
{
    std::unique_ptr<ApplyCtx> c(static_cast<ApplyCtx*>(param));
    obs_enum_scenes(apply_scene, c.get());
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

void easyobs_load_delay_visibility(void)
{
    QSettings st("MAVSoft", "EasyOBSBackups");
    const QStringList a = st.value("delayShowWithDelay").toStringList();
    const QStringList b = st.value("delayShowWithoutDelay").toStringList();

    std::lock_guard<std::mutex> lk(g_mtx);
    g_withDelay.clear();
    g_withoutDelay.clear();
    for (const QString& s : a) if (!s.isEmpty()) g_withDelay.push_back(s.toStdString());
    for (const QString& s : b) if (!s.isEmpty()) g_withoutDelay.push_back(s.toStdString());
}

DelayVisMode easyobs_get_delay_visibility(const QString& sourceName)
{
    const std::string n = sourceName.toStdString();

    std::lock_guard<std::mutex> lk(g_mtx);
    if (std::find(g_withDelay.begin(), g_withDelay.end(), n) != g_withDelay.end())
        return DelayVisMode::ShowWithDelay;
    if (std::find(g_withoutDelay.begin(), g_withoutDelay.end(), n) != g_withoutDelay.end())
        return DelayVisMode::ShowWithoutDelay;
    return DelayVisMode::Ignore;
}

void easyobs_set_delay_visibility(const QString& sourceName, DelayVisMode mode)
{
    const std::string n = sourceName.toStdString();
    if (n.empty()) return;

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        erase_name(g_withDelay,    n);   // una fuente está en una lista o en ninguna
        erase_name(g_withoutDelay, n);
        if      (mode == DelayVisMode::ShowWithDelay)    g_withDelay.push_back(n);
        else if (mode == DelayVisMode::ShowWithoutDelay) g_withoutDelay.push_back(n);
        persist_locked();
    }

    // Efecto inmediato: al elegir el modo la fuente ya queda como corresponde al
    // estado actual del delay, sin esperar al próximo toggle.
    easyobs_apply_delay_visibility();
}

void easyobs_apply_delay_visibility(void)
{
    auto ctx = std::make_unique<ApplyCtx>();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_withDelay.empty() && g_withoutDelay.empty())
            return;   // nada configurado → no tocar ninguna escena
        ctx->withDelay    = g_withDelay;
        ctx->withoutDelay = g_withoutDelay;
    }
    ctx->bypassed = easyobs_delays_bypassed();

    // Puede venir del hilo del hotkey: la visibilidad se toca en el hilo de UI,
    // que es donde OBS espera que se emitan las señales de scene item.
    obs_queue_task(OBS_TASK_UI, apply_task, ctx.release(), false);
}
