#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// delay-playing.hpp
//
// "¿Está sonando ahora el audio demorado?" — para no pisar tu propia voz.
//
// Los motores de audio (delay-audio.inc y delay-push.inc) reportan el pico de la
// señal DEMORADA que acaban de emitir. Si ese pico pasa el umbral, el estado es
// "reproduciendo" y se mantiene así un rato más (hang) para que no titile entre
// palabra y palabra. El dock lo pinta como luz roja/verde:
//
//   ROJA  = está saliendo la voz demorada → no hables
//   VERDE = no sale nada demorado         → podés hablar
//
// Además puede sonar un aviso al pasar a rojo. El aviso se toca por waveOut a un
// dispositivo de salida elegible, NO por el mezclador de OBS, así que no entra a
// ninguna pista de emisión ni de grabación. Ojo: si elegís el mismo dispositivo
// que captura "Audio del escritorio", OBS lo va a capturar igual — hay que usar
// una salida distinta de la que OBS está capturando.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QStringList>

#include <cstdint>

// ── Reporte desde el hilo de audio (lock-free, no bloquea) ───────────────────
// peak: valor absoluto máximo del bloque demorado que se acaba de emitir (0..1).
void easyobs_note_delayed_peak(float peak);

// Pico de la señal EN VIVO que ENTRA al delay, más cuánto dura ese delay.
// Con eso se sabe de antemano cuándo va a empezar a salir tu voz demorada, y el
// aviso puede sonar 2 s ANTES en vez de encima. delayMs: el delay del filtro.
void easyobs_note_live_peak(float peak, uint64_t delayMs);

// ── Estado ───────────────────────────────────────────────────────────────────
bool easyobs_delayed_playing(void);   // true = está sonando algo demorado

// ── Aviso sonoro ─────────────────────────────────────────────────────────────
void easyobs_play_alert(void);        // toca el aviso ya mismo (usado por el test)

// Dispara el aviso anticipado si ya llegó su momento. La llama el timer del dock
// en cada tick; nunca el hilo de audio.
void easyobs_alert_tick(void);

// ── Escuchar la voz demorada (monitor) ───────────────────────────────────────
// Pone la fuente que lleva el filtro de delay en "monitorear y transmitir": la
// voz demorada sigue yendo al stream igual que siempre y además se escucha por
// el dispositivo de monitoreo de OBS.
//
// OJO: que eso NO entre al stream depende de la configuración de OBS, no del
// plugin. El monitoreo sale por el "Dispositivo de monitoreo" de Ajustes →
// Audio; si ese dispositivo es el mismo que captura tu "Audio del escritorio",
// el loopback lo vuelve a tomar y termina al aire igual.
bool easyobs_delay_monitor_enabled(void);
void easyobs_set_delay_monitor(bool on);
void easyobs_apply_delay_monitor(void);   // reaplica al arrancar / cambiar de colección

// ── Configuración (persistida en QSettings) ──────────────────────────────────
void easyobs_load_playing_settings(void);

bool easyobs_alert_enabled(void);
void easyobs_set_alert_enabled(bool on);

// Cuántos segundos ANTES de que empiece a salir la voz demorada suena el aviso.
// Se acota a 1..10: más que eso no da tiempo de reaccionar, da tiempo de olvidarse.
// Si el delay del filtro es más corto que esto, el aviso suena enseguida.
int  easyobs_alert_lead_sec(void);
void easyobs_set_alert_lead_sec(int sec);

// Índice de dispositivo de waveOut; -1 (WAVE_MAPPER) = el predeterminado.
int  easyobs_alert_device(void);
void easyobs_set_alert_device(int deviceId);

// Nombres de los dispositivos de salida, en orden de índice (0..n-1).
QStringList easyobs_alert_device_names(void);

// Cierra el dispositivo del aviso, si quedó abierto.
void easyobs_shutdown_alert(void);
