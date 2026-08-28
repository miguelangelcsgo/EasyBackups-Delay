#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// mic-mute.hpp
//
// Silenciar / activar el micrófono desde el dock del plugin, con estado a la
// vista (verde = al aire, rojo = silenciado) y una tecla global propia, igual
// que el interruptor de delays.
//
// La fuente se puede elegir a mano; si no se elige ninguna, se resuelve sola:
// primero los canales globales de audio de OBS (Mic/Aux, Aux 2, Aux 3) y, si
// están vacíos, la primera fuente de captura de entrada que haya en la colección.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <vector>

// Fuente elegida por nombre. Vacío = automática (ver arriba).
QString easyobs_get_mic_source_name(void);
void    easyobs_set_mic_source_name(const QString& name);

// Combo global de teclas (códigos VK de Windows), como el del delay.
std::vector<int> easyobs_get_mic_keys(void);
void             easyobs_set_mic_keys(const std::vector<int>& vks);

// Carga lo persistido en QSettings. Llamar una vez en obs_module_load.
void easyobs_load_mic_settings(void);

// ── Estado ───────────────────────────────────────────────────────────────────
bool easyobs_mic_available(void);   // ¿se pudo resolver alguna fuente?
bool easyobs_mic_muted(void);       // false si no hay fuente
void easyobs_toggle_mic(void);      // flip con debounce (seguro ante doble binding)
void easyobs_set_mic_muted(bool muted);

// Nombre de la fuente que se está usando ahora (para mostrarlo en el tooltip).
QString easyobs_resolved_mic_name(void);

// ¿Está apretado AHORA el combo configurado? (lo sondea el timer del dock).
// False si no hay combo asignado.
bool easyobs_mic_keys_down(void);

// Hotkey del frontend (aparece en OBS → Ajustes → Atajos).
void easyobs_register_mic_hotkey(void);
void easyobs_unregister_mic_hotkey(void);
