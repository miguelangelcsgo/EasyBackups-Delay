#pragma once
#include <cstdint>
#include <vector>
#include <deque>
#include <utility>

// ─────────────────────────────────────────────────────────────────────────────
// MjpegCodec  (nombre engañoso mantenido para evitar churn — ahora es un pequeño
// codec multi-engine)
//
// Comprime/descomprime frames RGBA para el ring de delay. Cada frame se mantiene
// INTRA (decodificable de forma independiente) para que el ring basado en tiempo
// pueda descartar o decodificar cualquier frame a voluntad.
//
//   • Engine 0 = MJPEG (CPU)         — funciona en cualquier máquina, sin GPU.
//   • Engine 1 = H.264 hardware (GPU) — auto-detecta h264_amf (AMD) / h264_nvenc
//     (NVIDIA) / h264_qsv (Intel) / h264_mf (Media Foundation), configurado
//     all-intra (gop=1). Descarga el encode a la GPU así la CPU queda libre
//     para el juego. Cae de nuevo a MJPEG si no abre ningún encoder por hardware.
//
// decode() auto-detecta MJPEG vs H.264 desde el header del packet, así un ring
// que mezcla ambos por un momento (justo tras cambiar de engine) igual se
// reproduce correctamente.
//
// Las DLLs de avcodec/avutil/swscale son las que trae el propio OBS, así que no
// hacen falta archivos extra en runtime.
// ─────────────────────────────────────────────────────────────────────────────

struct AVCodecContext;
struct SwsContext;
struct AVFrame;
struct AVPacket;

// True si las DLLs de FFmpeg (avcodec/avutil/swscale) que trae OBS están presentes
// y se pueden usar. Se sondea UNA sola vez. Con delay-load (ver CMakeLists), si la
// versión no coincide o faltan, el módulo IGUAL carga: esta función lo detecta para
// que el delay se desactive con un AVISO VISIBLE en vez de romper la carga con
// "error 126" y dejar pasar el directo en silencio.
bool easyobs_ffmpeg_available();

class MjpegCodec {
public:
    enum Engine { ENGINE_MJPEG = 0, ENGINE_HW_H264 = 1 };

    MjpegCodec() = default;
    ~MjpegCodec();

    MjpegCodec(const MjpegCodec&) = delete;
    MjpegCodec& operator=(const MjpegCodec&) = delete;

    // Codifica un frame RGBA compactado (w*h*4 bytes).
    // quality 1 (mejor)..10 (más chico); full444 es solo para MJPEG; engine: ver Engine.
    // inTs es el timestamp de captura del frame; como los encoders por hardware
    // pueden retener uno o dos frames, el packet devuelto puede pertenecer a un
    // frame ANTERIOR — outTs se setea al inTs de ESE frame para que el caller lo
    // guarde bajo el tiempo correcto.
    // Devuelve false ante un error real. Devuelve true con `out` VACÍO cuando el
    // frame fue aceptado pero todavía no salió ningún packet (latencia del
    // encoder — normal).
    bool encode(const uint8_t* rgba, uint32_t w, uint32_t h,
                int quality, bool full444, int engine,
                uint64_t inTs, std::vector<uint8_t>& out, uint64_t& outTs);

    // Decodifica un packet (MJPEG o H.264, auto-detectado) a RGBA compactado.
    bool decode(const uint8_t* data, size_t size,
                std::vector<uint8_t>& rgbaOut, uint32_t& w, uint32_t& h);

    // Nombre del encoder realmente en uso (ej. "h264_amf", "mjpeg"), para logs.
    const char* encoderName() const { return encName_; }

private:
    bool ensureEncoder(uint32_t w, uint32_t h, int quality, bool full444, int engine);
    bool ensureDecoder(int codecId);   // AV_CODEC_ID_MJPEG o AV_CODEC_ID_H264
    void freeEncoder();
    void freeDecoder();

    // Estado del encoder
    AVCodecContext* enc_   = nullptr;
    SwsContext*     toYuv_ = nullptr;
    AVFrame*        yuv_   = nullptr;
    AVPacket*       pkt_   = nullptr;
    uint32_t ew_ = 0, eh_ = 0;
    int  eq_   = 0;
    bool e444_ = false;
    int  ee_   = -1;                // engine configurado actualmente
    int  encCodecId_ = 0;          // AV_CODEC_ID del encoder abierto
    char encName_[32] = "none";    // nombre del encoder, para logging
    int64_t nextPts_ = 0;          // debe crecer de forma monótona
    // Mapea cada pts del encoder al timestamp de captura del frame, así un
    // packet retrasado (latencia del encoder por hardware) se guarda bajo el
    // tiempo correcto.
    std::deque<std::pair<int64_t, uint64_t>> ptsMap_;

    // Parameter sets (SPS/PPS) del encoder H.264, entregados al decoder para que
    // cualquier packet decodifique de forma independiente.
    std::vector<uint8_t> h264Extradata_;

    // Estado del decoder (se reconstruye cuando cambia el codec del packet entrante)
    AVCodecContext* dec_    = nullptr;
    int             decCodecId_ = 0;
    std::vector<uint8_t> decExtradata_;   // SPS/PPS con los que se abrió el decoder
    SwsContext*     toRgba_ = nullptr;
    AVFrame*        dfr_    = nullptr;
    AVPacket*       dpkt_   = nullptr;
};
