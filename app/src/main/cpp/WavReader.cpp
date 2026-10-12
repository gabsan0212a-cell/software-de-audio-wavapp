#include "WavReader.h"

#include <cstring>

namespace {

uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

// Decodifica una muestra de `bytes` bytes. Los enteros se leen alineados a la izquierda,
// así 24 bits guardados en 32 suenan igual que 24 en 24.
inline float decode(const uint8_t *p, int bytes, bool isFloat) {
    if (isFloat) {
        if (bytes == 4) { float v; std::memcpy(&v, p, 4); return v; }
        double v; std::memcpy(&v, p, 8); return static_cast<float>(v);
    }
    switch (bytes) {
        case 1: return (int(p[0]) - 128) / 128.0f;
        case 2: return int16_t(le16(p)) / 32768.0f;
        case 3: return (int32_t((p[0] << 8) | (p[1] << 16) | (uint32_t(p[2]) << 24)) >> 8) / 8388608.0f;
        default: return int32_t(le32(p)) / 2147483648.0f;
    }
}

}  // namespace

WavReader::~WavReader() {
    if (mFile) std::fclose(mFile);
}

std::string WavReader::open(FILE *f) {
    if (!f) return "No se pudo abrir el archivo";
    if (mFile) std::fclose(mFile);
    mFile = f;

    uint8_t h[12];
    if (std::fread(h, 1, 12, f) != 12 || std::memcmp(h, "RIFF", 4) || std::memcmp(h + 8, "WAVE", 4))
        return "No es un archivo WAV";

    bool haveFmt = false;
    int format = 0, bits = 0, validBits = 0, blockAlign = 0;
    uint32_t byteRate = 0, dataSize = 0;
    for (;;) {
        uint8_t ch[8];
        if (std::fread(ch, 1, 8, f) != 8) return "WAV sin datos de audio";
        uint32_t size = le32(ch + 4);

        if (!std::memcmp(ch, "fmt ", 4)) {
            if (size < 16 || size > 256) return "Encabezado fmt inválido";
            uint8_t fm[256];
            if (std::fread(fm, 1, size, f) != size) return "Encabezado fmt incompleto";
            format = le16(fm);
            mChannels = le16(fm + 2);
            mSampleRate = static_cast<int>(le32(fm + 4));
            byteRate = le32(fm + 8);
            blockAlign = le16(fm + 12);
            bits = le16(fm + 14);
            if (format == 0xFFFE && size >= 26) {  // WAVE_FORMAT_EXTENSIBLE
                validBits = le16(fm + 18);
                format = le16(fm + 24);
            }
            if (size & 1) fseeko(f, 1, SEEK_CUR);
            haveFmt = true;
        } else if (!std::memcmp(ch, "data", 4)) {
            if (!haveFmt) return "WAV sin encabezado fmt";
            dataSize = size;
            break;
        } else if (fseeko(f, static_cast<off_t>(size) + (size & 1), SEEK_CUR) != 0) {
            return "WAV dañado";
        }
    }

    // Algunas grabadoras escriben mal blockAlign (p. ej. 2 en estéreo de 16 bits). Si no cuadra
    // con canales y bits, el dato de bytes por segundo decide cuál de los dos es el correcto.
    const int sampleBytes = (bits + 7) / 8;
    const int expected = mChannels * sampleBytes;
    if (mChannels >= 1 && sampleBytes > 0 && blockAlign < expected) {
        if (blockAlign > 0 && blockAlign % sampleBytes == 0 &&
            byteRate == static_cast<uint32_t>(mSampleRate) * blockAlign)
            mChannels = blockAlign / sampleBytes;  // los canales estaban mal
        else
            blockAlign = expected;  // blockAlign estaba mal (o en 0)
    }
    mContainer = mChannels >= 1 ? blockAlign / mChannels : 0;
    mIsFloat = (format == 3);
    mBits = (validBits > 0 && validBits < bits) ? validBits : bits;
    bool ok = mChannels >= 1 && mSampleRate >= 8000 && mSampleRate <= 768000 &&
              blockAlign % mChannels == 0 && bits >= 8 && bits <= mContainer * 8 &&
              ((format == 1 && mContainer >= 1 && mContainer <= 4) ||
               (format == 3 && (mContainer == 4 || mContainer == 8)));
    if (!ok) {
        char msg[160];
        std::snprintf(msg, sizeof msg,
                      "Formato WAV no soportado (formato=%d, canales=%d, %d Hz, %d bits, bloque=%d)",
                      format, mChannels, mSampleRate, bits, blockAlign);
        return msg;
    }
    mBlockAlign = blockAlign;

    // Si el tamaño de "data" está en 0 o pasa del final del archivo, leemos hasta el final.
    mDataOffset = ftello(f);
    fseeko(f, 0, SEEK_END);
    int64_t remaining = ftello(f) - mDataOffset;
    fseeko(f, mDataOffset, SEEK_SET);
    int64_t bytes = (dataSize == 0 || dataSize > remaining) ? remaining : dataSize;
    mFrames = mFramesLeft = bytes / mBlockAlign;
    if (mFrames == 0) return "El WAV está vacío";
    return "";
}

size_t WavReader::read(float *dst, size_t maxFrames) {
    if (!mFile || mFramesLeft <= 0) return 0;
    size_t want = maxFrames < static_cast<uint64_t>(mFramesLeft) ? maxFrames : static_cast<size_t>(mFramesLeft);
    mBuf.resize(want * mBlockAlign);
    size_t n = std::fread(mBuf.data(), mBlockAlign, want, mFile);
    mFramesLeft -= static_cast<int64_t>(n);
    const size_t samples = n * mChannels;
    const uint8_t *p = mBuf.data();
    for (size_t i = 0; i < samples; ++i, p += mContainer) dst[i] = decode(p, mContainer, mIsFloat);
    if (n < want) mFramesLeft = 0;  // archivo más corto de lo esperado
    return n;
}

std::string readWav(FILE *f, Clip &out) {
    WavReader r;
    std::string err = r.open(f);
    if (!err.empty()) return err;
    out.channels = r.channels();
    out.sampleRate = r.sampleRate();
    out.bits = r.bits();
    out.isFloat = r.isFloat();
    out.data.resize(static_cast<size_t>(r.frames()) * r.channels());
    size_t got = 0, n;
    while ((n = r.read(out.data.data() + got * r.channels(), 4096)) > 0) got += n;
    out.data.resize(got * r.channels());
    out.frames = static_cast<int64_t>(got);
    if (out.frames == 0) return "El WAV está vacío";
    return "";
}
