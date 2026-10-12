#include "MediaDecoder.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace {

// Valores de android.media.AudioFormat.
constexpr int32_t kPcm16 = 2, kPcm8 = 3, kPcmFloat = 4, kPcm24Packed = 21, kPcm32 = 22;
constexpr const char *kKeyPcmEncoding = "pcm-encoding";
constexpr int64_t kTimeoutUs = 10000;

std::string codecName(const std::string &mime) {
    if (mime == "audio/mpeg") return "MP3";
    if (mime == "audio/mp4a-latm") return "AAC";
    if (mime == "audio/vorbis") return "OGG Vorbis";
    if (mime == "audio/opus") return "Opus";
    if (mime == "audio/flac") return "FLAC";
    if (mime == "audio/3gpp") return "AMR";
    if (mime == "audio/amr-wb") return "AMR-WB";
    std::string n = mime.rfind("audio/", 0) == 0 ? mime.substr(6) : mime;
    for (char &c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return n;
}

// Libera todo al salir, pase lo que pase.
struct Resources {
    AMediaExtractor *ex = nullptr;
    AMediaFormat *format = nullptr;
    AMediaCodec *codec = nullptr;
    bool started = false;
    ~Resources() {
        if (codec) {
            if (started) AMediaCodec_stop(codec);
            AMediaCodec_delete(codec);
        }
        if (format) AMediaFormat_delete(format);
        if (ex) AMediaExtractor_delete(ex);
    }
};

bool readOutputFormat(AMediaCodec *codec, int32_t &rate, int32_t &channels, int32_t &encoding) {
    AMediaFormat *f = AMediaCodec_getOutputFormat(codec);
    if (!f) return false;
    AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
    AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
    int32_t e = kPcm16;
    if (AMediaFormat_getInt32(f, kKeyPcmEncoding, &e)) encoding = e;
    else encoding = kPcm16;
    AMediaFormat_delete(f);
    return true;
}

// Convierte las muestras a float -1..1. Devuelve cuántos cuadros quedaron en `out`.
size_t toFloat(const uint8_t *p, size_t bytes, int32_t encoding, int channels, std::vector<float> &out) {
    size_t samples = 0;
    switch (encoding) {
        case kPcmFloat:
            samples = bytes / 4;
            out.resize(samples);
            std::memcpy(out.data(), p, samples * 4);
            break;
        case kPcm8:
            samples = bytes;
            out.resize(samples);
            for (size_t i = 0; i < samples; ++i) out[i] = (static_cast<float>(p[i]) - 128.0f) / 128.0f;
            break;
        case kPcm24Packed:
            samples = bytes / 3;
            out.resize(samples);
            for (size_t i = 0; i < samples; ++i) {
                int32_t v = p[3 * i] | (p[3 * i + 1] << 8) | (p[3 * i + 2] << 16);
                if (v & 0x800000) v -= 0x1000000;
                out[i] = static_cast<float>(v) / 8388608.0f;
            }
            break;
        case kPcm32:
            samples = bytes / 4;
            out.resize(samples);
            for (size_t i = 0; i < samples; ++i) {
                int32_t v;
                std::memcpy(&v, p + 4 * i, 4);
                out[i] = static_cast<float>(static_cast<double>(v) / 2147483648.0);
            }
            break;
        default:  // 16 bits
            samples = bytes / 2;
            out.resize(samples);
            for (size_t i = 0; i < samples; ++i) {
                int16_t v;
                std::memcpy(&v, p + 2 * i, 2);
                out[i] = static_cast<float>(v) / 32768.0f;
            }
            break;
    }
    return channels > 0 ? samples / static_cast<size_t>(channels) : 0;
}

}  // namespace

std::string decodeWithMediaCodec(int fd, int64_t length, PcmSink &sink, std::string &codecOut,
                                 std::atomic<float> *progress) {
    Resources res;
    res.ex = AMediaExtractor_new();
    if (!res.ex) return "no se pudo leer el archivo";
    if (AMediaExtractor_setDataSourceFd(res.ex, fd, 0, length) != AMEDIA_OK)
        return "no es un formato de audio que este celular pueda leer";

    std::string mime;
    const size_t count = AMediaExtractor_getTrackCount(res.ex);
    for (size_t i = 0; i < count && !res.format; ++i) {
        AMediaFormat *f = AMediaExtractor_getTrackFormat(res.ex, i);
        const char *m = nullptr;
        if (f && AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &m) && m && std::strncmp(m, "audio/", 6) == 0) {
            mime = m;
            res.format = f;
            AMediaExtractor_selectTrack(res.ex, i);
        } else if (f) {
            AMediaFormat_delete(f);
        }
    }
    if (!res.format) return "no tiene audio que se pueda leer";
    int32_t rate = 0, channels = 0;
    int64_t durationUs = 0;
    AMediaFormat_getInt32(res.format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
    AMediaFormat_getInt32(res.format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
    AMediaFormat_getInt64(res.format, AMEDIAFORMAT_KEY_DURATION, &durationUs);

    res.codec = AMediaCodec_createDecoderByType(mime.c_str());
    if (!res.codec) return "este celular no sabe decodificar " + mime;
    // Salida en float si el códec puede (no se pierden bits); si no, con uno nuevo, la de siempre.
    AMediaFormat_setInt32(res.format, kKeyPcmEncoding, kPcmFloat);
    if (AMediaCodec_configure(res.codec, res.format, nullptr, nullptr, 0) != AMEDIA_OK) {
        AMediaCodec_delete(res.codec);
        res.codec = AMediaCodec_createDecoderByType(mime.c_str());
        if (!res.codec) return "este celular no sabe decodificar " + mime;
        AMediaFormat_setInt32(res.format, kKeyPcmEncoding, kPcm16);
        if (AMediaCodec_configure(res.codec, res.format, nullptr, nullptr, 0) != AMEDIA_OK)
            return "no se pudo preparar el decodificador de " + mime;
    }
    if (AMediaCodec_start(res.codec) != AMEDIA_OK) return "no se pudo arrancar el decodificador de " + mime;
    res.started = true;

    int32_t encoding = kPcm16;
    bool haveFormat = false, begun = false, inputDone = false;
    int idle = 0;
    std::vector<float> buf;
    while (true) {
        if (!inputDone) {
            const ssize_t i = AMediaCodec_dequeueInputBuffer(res.codec, kTimeoutUs);
            if (i >= 0) {
                size_t cap = 0;
                uint8_t *in = AMediaCodec_getInputBuffer(res.codec, static_cast<size_t>(i), &cap);
                const ssize_t n = in ? AMediaExtractor_readSampleData(res.ex, in, cap) : -1;
                if (n < 0) {
                    AMediaCodec_queueInputBuffer(res.codec, static_cast<size_t>(i), 0, 0, 0,
                                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    inputDone = true;
                } else {
                    AMediaCodec_queueInputBuffer(res.codec, static_cast<size_t>(i), 0, static_cast<size_t>(n),
                                                 static_cast<uint64_t>(AMediaExtractor_getSampleTime(res.ex)), 0);
                    AMediaExtractor_advance(res.ex);
                }
            }
        }
        AMediaCodecBufferInfo info{};
        const ssize_t o = AMediaCodec_dequeueOutputBuffer(res.codec, &info, kTimeoutUs);
        if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            int32_t r = rate, c = channels;
            if (readOutputFormat(res.codec, r, c, encoding)) {
                if (begun && (r != rate || c != channels)) return "el formato cambió a mitad del archivo";
                rate = r;
                channels = c;
                haveFormat = true;
            }
            continue;
        }
        if (o == AMEDIACODEC_INFO_TRY_AGAIN_LATER || o == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            // Si ya no entra nada y no sale nada por ~10 s, el decodificador se trabó.
            if (inputDone && ++idle > 1000) return "el decodificador no respondió";
            continue;
        }
        if (o < 0) return "no se pudo decodificar (error " + std::to_string(o) + ")";
        idle = 0;
        if (info.size > 0) {
            if (!haveFormat) {
                readOutputFormat(res.codec, rate, channels, encoding);
                haveFormat = true;
            }
            if (!begun) {
                const std::string e = sink.begin(rate, channels);
                if (!e.empty()) return e;
                begun = true;
            }
            size_t size = 0;
            const uint8_t *out = AMediaCodec_getOutputBuffer(res.codec, static_cast<size_t>(o), &size);
            if (out && info.offset >= 0 && static_cast<size_t>(info.offset) + static_cast<size_t>(info.size) <= size) {
                const size_t frames = toFloat(out + info.offset, static_cast<size_t>(info.size), encoding, channels, buf);
                const std::string e = sink.write(buf.data(), frames);
                if (!e.empty()) {
                    AMediaCodec_releaseOutputBuffer(res.codec, static_cast<size_t>(o), false);
                    return e;
                }
            }
            if (progress && durationUs > 0)
                progress->store(std::clamp(static_cast<float>(info.presentationTimeUs) / static_cast<float>(durationUs),
                                           0.0f, 1.0f));
        }
        AMediaCodec_releaseOutputBuffer(res.codec, static_cast<size_t>(o), false);
        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) break;
    }
    if (!begun) return "no se obtuvo audio del archivo";
    codecOut = codecName(mime);
    return "";
}
