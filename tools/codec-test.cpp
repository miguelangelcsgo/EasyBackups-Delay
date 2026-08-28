// Test de ida y vuelta autónomo para MjpegCodec (no necesita OBS).
// Codifica frames RGBA sintéticos, los decodifica de vuelta y reporta tamaño + PSNR.
#include "../src/delay/delay-codec.hpp"

#include <cstdio>
#include <cmath>
#include <vector>
#include <cstdint>

static void fillPattern(std::vector<uint8_t>& px, uint32_t w, uint32_t h, int seed)
{
    px.resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = &px[((size_t)y * w + x) * 4];
            // Gradientes + detalle de alta frecuencia "tipo texto" + barras de color.
            p[0] = (uint8_t)((x * 255 / w + seed * 40) & 0xFF);
            p[1] = (uint8_t)((y * 255 / h) & 0xFF);
            p[2] = (uint8_t)(((x / 8 + y / 8 + seed) % 2) ? 230 : 20);
            if ((x % 97) < 2) { p[0] = 255; p[1] = 255; p[2] = 255; }  // líneas finas
            p[3] = 255;
        }
    }
}

static double psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    if (a.size() != b.size() || a.empty()) return -1.0;
    double mse = 0.0;
    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if ((i & 3) == 3) continue;   // saltar alfa
        double d = (double)a[i] - (double)b[i];
        mse += d * d;
        ++n;
    }
    mse /= (double)n;
    if (mse <= 0.0) return 99.0;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

static bool runCase(MjpegCodec& codec, uint32_t w, uint32_t h, int q, bool s444,
                    int frames, int engine = 0)
{
    // 4:2:0 suaviza legítimamente este peor-caso sintético (líneas de color de 1px);
    // el H.264 por hardware también tiene pérdida, así que usamos un piso indulgente ahí.
    const double minPsnr = engine ? 22.0 : (s444 ? 40.0 : 30.0);
    printf("== %ux%u q%d %s, %d frames, engine=%s ==\n", w, h, q,
           s444 ? "4:4:4" : "4:2:0", frames, engine ? "HW-H264" : "MJPEG");
    bool allOk = true;
    for (int i = 0; i < frames; ++i) {
        std::vector<uint8_t> src;
        fillPattern(src, w, h, i);

        std::vector<uint8_t> pkt;
        uint64_t outTs = 0;
        if (!codec.encode(src.data(), w, h, q, s444, engine, (uint64_t)i, pkt, outTs)) {
            printf("  frame %d: ENCODE FAILED\n", i);
            allOk = false;
            continue;
        }
        if (i == 0) printf("  encoder = %s\n", codec.encoderName());
        if (pkt.empty()) {   // retenido por el encoder (latencia) — no es un error
            printf("  frame %d: buffered (latency)\n", i);
            continue;
        }

        std::vector<uint8_t> back;
        uint32_t dw = 0, dh = 0;
        if (!codec.decode(pkt.data(), pkt.size(), back, dw, dh)) {
            printf("  frame %d: DECODE FAILED (pkt %zu bytes)\n", i, pkt.size());
            allOk = false;
            continue;
        }
        if (dw != w || dh != h) {
            printf("  frame %d: SIZE MISMATCH %ux%u\n", i, dw, dh);
            allOk = false;
            continue;
        }
        // El packet puede pertenecer a un frame anterior (latencia del encoder) —
        // comparar contra ESE frame, identificado por outTs (pasamos el índice como inTs).
        std::vector<uint8_t> ref;
        fillPattern(ref, w, h, (int)outTs);
        double p = psnr(ref, back);
        printf("  pkt for frame %llu: %6zu KB, PSNR %.1f dB %s\n",
               (unsigned long long)outTs, pkt.size() / 1024, p,
               p >= minPsnr ? "OK" : "LOW!");
        if (p < minPsnr) allOk = false;
    }
    return allOk;
}

int main()
{
    MjpegCodec codec;
    bool ok = true;

    ok &= runCase(codec, 1920, 1080, 1, true,  5);   // MJPEG máxima fidelidad
    ok &= runCase(codec, 1920, 1080, 1, false, 2);   // MJPEG 4:2:0
    ok &= runCase(codec, 1280,  720, 2, true,  2);   // cambio de tamaño a mitad
    ok &= runCase(codec, 1920, 1080, 1, true,  2);   // volver al tamaño anterior

    // H.264 por hardware (auto-detecta AMF/NVENC/QSV/MF; cae a MJPEG si no hay).
    printf("\n--- hardware engine ---\n");
    ok &= runCase(codec, 1920, 1080, 3, false, 6, 1);   // 6 frames para exponer la latencia
    ok &= runCase(codec, 1280,  720, 3, false, 3, 1);   // cambio de tamaño

    printf("\n%s\n", ok ? "ALL PASS" : "FAILURES PRESENT");
    return ok ? 0 : 1;
}
