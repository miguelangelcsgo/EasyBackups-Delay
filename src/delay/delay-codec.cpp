#include "delay-codec.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Disponibilidad de FFmpeg (sondeo único)
//
// Las DLLs de avcodec/avutil/swscale las provee OBS. El plugin las linkea con
// DELAY-LOAD (ver CMakeLists), así que si faltan o la versión no coincide el
// módulo IGUAL carga; la PRIMERA llamada a un símbolo de la DLL ausente dispara una
// excepción estructurada de Windows. La atrapamos acá una sola vez tocando un
// símbolo trivial de cada una de las tres DLLs: si algo falla, el delay queda
// desactivado y el resto del plugin (backup, docks) sigue andando.
// ─────────────────────────────────────────────────────────────────────────────
#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static bool probe_ffmpeg()
{
    __try {
        volatile unsigned v = avcodec_version();
        v ^= avutil_version();
        v ^= swscale_version();
        (void)v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#else
static bool probe_ffmpeg() { return true; }
#endif

bool easyobs_ffmpeg_available()
{
    static const bool ok = probe_ffmpeg();
    return ok;
}

// Encoders H.264 por hardware a probar, mejor primero. Se usa el primero que abra.
static const char* kHwH264[] = { "h264_amf", "h264_nvenc", "h264_qsv", "h264_mf" };

// ─────────────────────────────────────────────────────────────────────────────
// Encoder
// ─────────────────────────────────────────────────────────────────────────────

void MjpegCodec::freeEncoder()
{
    if (enc_)   avcodec_free_context(&enc_);
    if (toYuv_) { sws_freeContext(toYuv_); toYuv_ = nullptr; }
    if (yuv_)   av_frame_free(&yuv_);
    if (pkt_)   av_packet_free(&pkt_);
    ew_ = eh_ = 0;
    ee_ = -1;
    encCodecId_ = 0;
    std::strcpy(encName_, "none");
    nextPts_ = 0;
    ptsMap_.clear();
}

// Intenta abrir un encoder H.264 por hardware (all-intra). Devuelve true si tiene éxito.
static AVCodecContext* openHwH264(const char* name, uint32_t w, uint32_t h,
                                  int quality)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    if (!codec) return nullptr;

    AVCodecContext* c = avcodec_alloc_context3(codec);
    if (!c) return nullptr;

    c->width       = (int)w;
    c->height      = (int)h;
    c->pix_fmt     = AV_PIX_FMT_NV12;      // los encoders por hardware toman NV12
    c->time_base   = { 1, 30 };
    c->framerate   = { 30, 1 };
    c->gop_size    = 1;                     // cada frame IDR → ring all-intra
    c->max_b_frames = 0;
    c->flags      |= AV_CODEC_FLAG_LOW_DELAY | AV_CODEC_FLAG_GLOBAL_HEADER;

    if (quality < 1)  quality = 1;
    if (quality > 10) quality = 10;
    const int qp = 18 + (quality - 1) * (40 - 18) / 9;    // 1→18 .. 10→40

    // Bitrate de fallback para encoders que usan rate control en vez de CQP.
    const double bpp = 0.45 - (double)(quality - 1) * (0.45 - 0.05) / 9.0;
    c->bit_rate     = (int64_t)((double)w * (double)h * 30.0 * bpp);
    c->rc_max_rate  = c->bit_rate;         // silencia el warning de VBR de AMF

    // Por encoder: forzar ULTRA-LOW-LATENCY (1-entra-1-sale, sin buffering de
    // frames) y QP constante para que el delay quede sincronizado y cada frame
    // sea independiente.
    if (!std::strcmp(name, "h264_amf")) {
        av_opt_set    (c->priv_data, "usage", "ultralowlatency", 0);
        av_opt_set    (c->priv_data, "rc",    "cqp",             0);
        av_opt_set_int(c->priv_data, "qp_i",  qp,                0);
        av_opt_set_int(c->priv_data, "qp_p",  qp,                0);
        av_opt_set    (c->priv_data, "header_insertion_mode", "idr", 0);
    } else if (!std::strcmp(name, "h264_nvenc")) {
        av_opt_set    (c->priv_data, "preset", "p1",      0);
        av_opt_set    (c->priv_data, "tune",   "ull",     0);
        av_opt_set    (c->priv_data, "rc",     "constqp", 0);
        av_opt_set_int(c->priv_data, "qp",     qp,        0);
        av_opt_set    (c->priv_data, "delay",  "0",       0);
    } else if (!std::strcmp(name, "h264_qsv")) {
        av_opt_set_int(c->priv_data, "async_depth", 1, 0);
        c->global_quality = qp;
    }

    if (avcodec_open2(c, codec, nullptr) < 0) {
        avcodec_free_context(&c);
        return nullptr;
    }
    return c;
}

bool MjpegCodec::ensureEncoder(uint32_t w, uint32_t h, int quality, bool full444,
                               int engine)
{
    if (enc_ && ew_ == w && eh_ == h && eq_ == quality && e444_ == full444 &&
        ee_ == engine)
        return true;

    freeEncoder();

    AVPixelFormat inFmt = AV_PIX_FMT_YUVJ420P;   // pixel format de entrada del codec

    if (engine == ENGINE_HW_H264) {
        AVCodecContext* c = nullptr;
        const char* chosen = nullptr;
        for (const char* name : kHwH264) {
            c = openHwH264(name, w, h, quality);
            if (c) { chosen = name; break; }
        }
        if (c) {
            enc_        = c;
            encCodecId_ = AV_CODEC_ID_H264;
            std::strncpy(encName_, chosen, sizeof(encName_) - 1);
            encName_[sizeof(encName_) - 1] = 0;
            h264Extradata_.assign(c->extradata, c->extradata + c->extradata_size);
            inFmt = AV_PIX_FMT_NV12;
        } else {
            engine = ENGINE_MJPEG;   // sin encoder por hardware → cae a MJPEG
        }
    }

    if (!enc_) {   // camino MJPEG
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        if (!codec) return false;

        inFmt = full444 ? AV_PIX_FMT_YUVJ444P : AV_PIX_FMT_YUVJ420P;

        enc_ = avcodec_alloc_context3(codec);
        if (!enc_) return false;

        enc_->width          = (int)w;
        enc_->height         = (int)h;
        enc_->pix_fmt        = inFmt;
        enc_->time_base      = { 1, 60 };
        enc_->flags         |= AV_CODEC_FLAG_QSCALE;
        enc_->global_quality = FF_QP2LAMBDA * quality;
        enc_->thread_count   = 1;
        enc_->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;

        if (avcodec_open2(enc_, codec, nullptr) < 0) { freeEncoder(); return false; }
        encCodecId_ = AV_CODEC_ID_MJPEG;
        std::strcpy(encName_, "mjpeg");
    }

    // Frame de entrada + packet + scaler RGBA→(inFmt) compartidos.
    yuv_ = av_frame_alloc();
    pkt_ = av_packet_alloc();
    if (!yuv_ || !pkt_) { freeEncoder(); return false; }

    yuv_->format = inFmt;
    yuv_->width  = (int)w;
    yuv_->height = (int)h;
    if (av_frame_get_buffer(yuv_, 0) < 0) { freeEncoder(); return false; }

    toYuv_ = sws_getContext((int)w, (int)h, AV_PIX_FMT_RGBA,
                            (int)w, (int)h, inFmt,
                            SWS_POINT, nullptr, nullptr, nullptr);
    if (!toYuv_) { freeEncoder(); return false; }

    ew_ = w; eh_ = h; eq_ = quality; e444_ = full444; ee_ = engine;
    return true;
}

bool MjpegCodec::encode(const uint8_t* rgba, uint32_t w, uint32_t h,
                        int quality, bool full444, int engine,
                        uint64_t inTs, std::vector<uint8_t>& out, uint64_t& outTs)
{
    out.clear();
    outTs = inTs;
    if (!easyobs_ffmpeg_available()) return false;   // FFmpeg ausente/incompatible → nunca lo tocamos
    if (!rgba || !w || !h) return false;
    if (quality < 1)  quality = 1;
    if (quality > 10) quality = 10;

    if (!ensureEncoder(w, h, quality, full444, engine)) return false;

    if (av_frame_make_writable(yuv_) < 0) return false;

    const uint8_t* srcData[1]   = { rgba };
    const int      srcStride[1] = { (int)(w * 4) };
    sws_scale(toYuv_, srcData, srcStride, 0, (int)h, yuv_->data, yuv_->linesize);

    if (encCodecId_ == AV_CODEC_ID_MJPEG)
        yuv_->quality = FF_QP2LAMBDA * quality;

    const int64_t pts = nextPts_++;
    yuv_->pts = pts;
    ptsMap_.push_back({ pts, inTs });

    if (avcodec_send_frame(enc_, yuv_) < 0) return false;

    int ret = avcodec_receive_packet(enc_, pkt_);
    if (ret == AVERROR(EAGAIN))
        return true;              // aceptado, el packet todavía no salió (latencia)
    if (ret < 0)
        return false;            // error real

    // Resuelve el timestamp de captura del packet a partir de su pts.
    const int64_t p = pkt_->pts;
    while (!ptsMap_.empty() && ptsMap_.front().first < p)
        ptsMap_.pop_front();
    if (!ptsMap_.empty() && ptsMap_.front().first == p) {
        outTs = ptsMap_.front().second;
        ptsMap_.pop_front();
    }

    out.assign(pkt_->data, pkt_->data + pkt_->size);
    av_packet_unref(pkt_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Decoder
// ─────────────────────────────────────────────────────────────────────────────

void MjpegCodec::freeDecoder()
{
    if (dec_)    avcodec_free_context(&dec_);
    if (toRgba_) { sws_freeContext(toRgba_); toRgba_ = nullptr; }
    if (dfr_)    av_frame_free(&dfr_);
    if (dpkt_)   av_packet_free(&dpkt_);
    decCodecId_ = 0;
    decExtradata_.clear();
}

bool MjpegCodec::ensureDecoder(int codecId)
{
    // Reutiliza el decoder solo si el codec coincide Y (para H.264) fue abierto
    // con los MISMOS parameter sets. Los SPS/PPS del encoder cambian ante un
    // cambio de resolución o calidad, y un decoder que todavía tiene los SPS
    // viejos falla al decodificar los packets nuevos (el video con delay
    // volvería silenciosamente a vivo).
    if (dec_ && decCodecId_ == codecId &&
        (codecId != AV_CODEC_ID_H264 || decExtradata_ == h264Extradata_))
        return true;

    freeDecoder();

    const AVCodec* codec = avcodec_find_decoder((AVCodecID)codecId);
    if (!codec) return false;

    dec_ = avcodec_alloc_context3(codec);
    if (!dec_) return false;
    dec_->thread_count = 1;
    dec_->flags |= AV_CODEC_FLAG_LOW_DELAY;

    // Alimenta al decoder H.264 con los SPS/PPS del encoder para que cualquier packet decodifique solo.
    if (codecId == AV_CODEC_ID_H264 && !h264Extradata_.empty()) {
        dec_->extradata = (uint8_t*)av_mallocz(h264Extradata_.size() +
                                               AV_INPUT_BUFFER_PADDING_SIZE);
        if (dec_->extradata) {
            memcpy(dec_->extradata, h264Extradata_.data(), h264Extradata_.size());
            dec_->extradata_size = (int)h264Extradata_.size();
        }
    }

    if (avcodec_open2(dec_, codec, nullptr) < 0) { freeDecoder(); return false; }

    dfr_  = av_frame_alloc();
    dpkt_ = av_packet_alloc();
    if (!dfr_ || !dpkt_) { freeDecoder(); return false; }

    decCodecId_   = codecId;
    decExtradata_ = h264Extradata_;   // recuerda los SPS/PPS con los que abrimos
    return true;
}

bool MjpegCodec::decode(const uint8_t* data, size_t size,
                        std::vector<uint8_t>& rgbaOut, uint32_t& w, uint32_t& h)
{
    if (!easyobs_ffmpeg_available()) return false;   // FFmpeg ausente/incompatible → nunca lo tocamos
    if (!data || size < 4) return false;

    // Detecta el formato: los packets MJPEG empiezan con el marcador SOI de JPEG
    // 0xFFD8; cualquier otra cosa en este pipeline es H.264.
    const int codecId = (data[0] == 0xFF && data[1] == 0xD8)
                        ? AV_CODEC_ID_MJPEG : AV_CODEC_ID_H264;

    if (!ensureDecoder(codecId)) return false;

    if (av_new_packet(dpkt_, (int)size) < 0) return false;
    memcpy(dpkt_->data, data, size);

    int ret = avcodec_send_packet(dec_, dpkt_);
    av_packet_unref(dpkt_);
    if (ret < 0) return false;

    if (avcodec_receive_frame(dec_, dfr_) < 0) return false;

    w = (uint32_t)dfr_->width;
    h = (uint32_t)dfr_->height;

    toRgba_ = sws_getCachedContext(toRgba_,
                                   dfr_->width, dfr_->height,
                                   (AVPixelFormat)dfr_->format,
                                   dfr_->width, dfr_->height, AV_PIX_FMT_RGBA,
                                   SWS_POINT, nullptr, nullptr, nullptr);
    if (!toRgba_) { av_frame_unref(dfr_); return false; }

    rgbaOut.resize((size_t)w * h * 4);
    uint8_t* dstData[1]    = { rgbaOut.data() };
    const int dstStride[1] = { (int)(w * 4) };
    sws_scale(toRgba_, dfr_->data, dfr_->linesize, 0, dfr_->height,
              dstData, dstStride);

    av_frame_unref(dfr_);
    return true;
}

MjpegCodec::~MjpegCodec()
{
    freeEncoder();
    freeDecoder();
}
