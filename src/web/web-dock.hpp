#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// web-dock.hpp — dock "Web / Stream": agrega una página web (por ejemplo el
// stream de otro compañero) como un recuadro (browser source) en la escena
// actual, con URL, tamaño y audio opcional editables. Una vez agregado, el
// recuadro se mueve/redimensiona en el preview como cualquier fuente.
// ─────────────────────────────────────────────────────────────────────────────

void register_web_dock(void);
void unregister_web_dock(void);
