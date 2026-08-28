#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// delay-visibility.hpp
//
// Ata la visibilidad de fuentes al estado del interruptor de delays: elegís qué
// fuentes se muestran mientras el delay está ACTIVO y cuáles mientras está
// APAGADO, y el plugin las prende/apaga solo cuando el estado cambia.
//
// Caso típico: una pantalla que muestra TODO (visible solo sin delay) y otra que
// muestra solo el juego (visible solo con delay). Apretás la tecla del delay y
// las dos se intercambian sin tocar nada más.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>

// Qué hacer con una fuente cuando cambia el estado del delay.
enum class DelayVisMode {
    Ignore           = 0,   // no la toca nunca (default)
    ShowWithDelay    = 1,   // visible con el delay ACTIVO, oculta sin delay
    ShowWithoutDelay = 2,   // visible con el delay APAGADO (todo en vivo), oculta con delay
};

// Carga la configuración persistida (QSettings). Llamar una vez en obs_module_load.
void easyobs_load_delay_visibility(void);

// Modo configurado para una fuente (por nombre). Ignore si nunca se tocó.
DelayVisMode easyobs_get_delay_visibility(const QString& sourceName);

// Cambia el modo de una fuente, lo persiste y aplica el estado actual en el acto.
void easyobs_set_delay_visibility(const QString& sourceName, DelayVisMode mode);

// Aplica el estado ACTUAL del delay a todas las fuentes configuradas. Se puede
// llamar desde cualquier hilo: el trabajo se encola en el hilo de UI de OBS.
// No hace nada si no hay ninguna fuente configurada.
void easyobs_apply_delay_visibility(void);
