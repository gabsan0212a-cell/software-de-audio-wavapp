#include "WavReader.h"

#include <cstring>

namespace {

uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

float decode(const uint8_t *p, int bits, bool isFloat) {
    if (isFloat) {
        if (bits == 32) { float v; std::memcpy(&v, p, 4); return v; }
        double v; std::memcpy(&v, p, 8); return static_cast<float>(v);
    }
    switch (bits) {
        case 8: return (int(p[0]) - 128) / 128.0f;
        case 16: return int16_t(le16(p)) / 32768.0f;
        case 24: return (int32_t((p[0] << 8) | (p[1] << 16) | (uint32_t(p[2]) << 24)) >> 8) / 8388608.0f;
        default: return int32_t(le32(p)) / 2147483648.0f;
    }
}

std::string fail(FILE *f, const char *msg) {
    std::fclose(f);
    return msg;
}

}  // namespace

std::string readWav(FILE *f, Clip &out) {
    if (!f) return "No se pudo abrir el archivo";
    uint8_t h[12];
    if (std::fread(h, 1, 12, f) != 12 || std::memcmp(h, "RIFF", 4) || std::memcmp(h + 8, "WAVE", 4))
        return fail(f, "No es un archivo WAV");

    bool haveFmt = false;
    int format = 0, bits = 0, validBits = 0, blockAlign = 0;
    uint32_t dataSize = 0;
    for (;;) {
        uint8_t ch[8];
        if (std::fread(ch, 1, 8, f) != 8) return fail(f, "WAV sin datos de audio");
        uint32_t size = le32(ch + 4);

        if (!std::memcmp(ch, "fmt ", 4)) {
            if (size < 16 || size > 256) return fail(f, "Encabezado fmt inválido");
            uint8_t fm[256];
            if (std::fread(fm, 1, size, f) != size) return fail(f, "Encabezado fmt incompleto");
            format = le16(fm);
            out.channels = le16(fm + 2);
            out.sampleRate = static_cast<int32_t>(le32(fm + 4));
            blockAlign = le16(fm + 12);
            bits = le16(fm + 14);
            if (format == 0xFFFE && size >= 26) {  // WAVE_FORMAT_EXTENSIBLE
                validBits = le16(fm + 18);
                format = le16(fm + 24);
            }
            if (size & 1) std::fseek(f, 1, SEEK_CUR);
            haveFmt = true;
        } else if (!std::memcmp(ch, "data", 4)) {
            if (!haveFmt) return fail(f, "WAV sin encabezado fmt");
            dataSize = size;
            break;
        } else {
            std::fseek(f, size + (size & 1), SEEK_CUR);
        }
    }

    // Algunos programas guardan, p. ej., 24 bits dentro de 32 (alineados a la izquierda)
    // o dejan blockAlign en 0. El tamaño real de cada muestra lo da blockAlign.
    if (out.channels >= 1 && blockAlign == 0) blockAlign = out.channels * ((bits + 7) / 8);
    const int container = out.channels >= 1 ? blockAlign / out.channels : 0;
    out.isFloat = (format == 3);
    out.bits = (validBits > 0 && validBits < bits) ? validBits : bits;
    bool ok = out.channels >= 1 && out.sampleRate >= 8000 && blockAlign % out.channels == 0 &&
              bits >= 8 && bits <= container * 8 &&
              ((format == 1 && container >= 1 && container <= 4) ||
               (format == 3 && (container == 4 || container == 8)));
    if (!ok) {
        char msg[160];
        std::snprintf(msg, sizeof msg,
                      "Formato WAV no soportado (formato=%d, canales=%d, %d Hz, %d bits, bloque=%d)",
                      format, out.channels, out.sampleRate, bits, blockAlign);
        return fail(f, msg);
    }

    // Algunas grabadoras dejan el tamaño de "data" en 0 o mal: entonces leemos hasta el final.
    long start = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    int64_t remaining = std::ftell(f) - start;
    std::fseek(f, start, SEEK_SET);
    int64_t bytes = (dataSize == 0 || dataSize > remaining) ? remaining : dataSize;
    int64_t framesLeft = bytes / blockAlign;

    const int bytesPerSample = container;
    std::vector<uint8_t> buf(static_cast<size_t>(blockAlign) * 4096);
    out.data.clear();
    out.data.reserve(static_cast<size_t>(framesLeft * out.channels));
    size_t n;
    while (framesLeft > 0 &&
           (n = std::fread(buf.data(), blockAlign, framesLeft < 4096 ? framesLeft : 4096, f)) > 0) {
        framesLeft -= n;
        for (size_t i = 0; i < n * out.channels; ++i)
            out.data.push_back(decode(buf.data() + i * bytesPerSample, container * 8, out.isFloat));
    }
    std::fclose(f);
    out.frames = static_cast<int64_t>(out.data.size()) / out.channels;
    if (out.frames == 0) return "El WAV está vacío";
    return "";
}
