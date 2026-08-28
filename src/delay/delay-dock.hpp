#pragma once
// Un pequeño panel acoplable agregado a la ventana principal de OBS con un único
// botón grande de toggle que enciende/apaga TODOS los filtros de delay de una
// vez — el mismo estado que maneja la hotkey global "mav_toggle_all_delays".
// Fácil de encontrar durante un stream.

void register_delay_dock(void);
void unregister_delay_dock(void);
