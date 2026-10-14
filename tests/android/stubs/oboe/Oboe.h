#pragma once
// Lo mínimo de Oboe que usa app/src/main/cpp/android (solo para revisar que compile en Linux:
// tests/android/compilar.sh). No hace nada.
#include <cstdint>
#include <memory>

namespace oboe {
enum class Direction { Output, Input };
enum class PerformanceMode { None, PowerSaving, LowLatency };
enum class SharingMode { Exclusive, Shared };
enum class AudioFormat { Invalid, Unspecified, I16, Float };
enum class SampleRateConversionQuality { None, Fastest, Low, Medium, High, Best };
enum class InputPreset { Generic, Camcorder, VoiceRecognition, VoiceCommunication, Unprocessed, VoicePerformance };
enum class Result { OK, ErrorDisconnected, ErrorInternal };
enum class StreamState { Uninitialized, Open, Starting, Started, Pausing, Paused, Flushing, Flushed, Stopping, Stopped, Closing, Closed, Disconnected };
enum class DataCallbackResult { Continue, Stop };
namespace ChannelCount { constexpr int Stereo = 2; }
inline const char *convertToText(Result) { return ""; }

template <typename T>
class ResultWithValue {
public:
    explicit operator bool() const { return true; }
    T value() const { return T(); }
    Result error() const { return Result::OK; }
};

class AudioStream;
class AudioStreamDataCallback {
public:
    virtual ~AudioStreamDataCallback() = default;
    virtual DataCallbackResult onAudioReady(AudioStream *stream, void *audioData, int32_t numFrames) = 0;
};
class AudioStreamErrorCallback {
public:
    virtual ~AudioStreamErrorCallback() = default;
    virtual void onErrorAfterClose(AudioStream *, Result) {}
};

class AudioStream {
public:
    virtual ~AudioStream() = default;
    Result requestStart() { return Result::OK; }
    Result stop(int64_t = 0) { return Result::OK; }
    Result close() { return Result::OK; }
    int32_t getSampleRate() const { return 48000; }
    int32_t getChannelCount() const { return 2; }
    int32_t getFramesPerBurst() const { return 192; }
    int32_t getBufferCapacityInFrames() const { return 4096; }
    int32_t getBufferSizeInFrames() const { return 384; }
    ResultWithValue<int32_t> setBufferSizeInFrames(int32_t) { return {}; }
    SharingMode getSharingMode() const { return SharingMode::Exclusive; }
    StreamState getState() const { return StreamState::Started; }
    Result waitForStateChange(StreamState, StreamState *, int64_t) { return Result::OK; }
    ResultWithValue<int32_t> getXRunCount() { return {}; }
    ResultWithValue<double> calculateLatencyMillis() { return {}; }
    ResultWithValue<int32_t> read(void *, int32_t, int64_t) { return {}; }
    void setPerformanceHintEnabled(bool) {}
};

class AudioStreamBuilder {
public:
    AudioStreamBuilder *setDirection(Direction) { return this; }
    AudioStreamBuilder *setPerformanceMode(PerformanceMode) { return this; }
    AudioStreamBuilder *setSharingMode(SharingMode) { return this; }
    AudioStreamBuilder *setFormat(AudioFormat) { return this; }
    AudioStreamBuilder *setChannelCount(int) { return this; }
    AudioStreamBuilder *setSampleRate(int32_t) { return this; }
    AudioStreamBuilder *setSampleRateConversionQuality(SampleRateConversionQuality) { return this; }
    AudioStreamBuilder *setInputPreset(InputPreset) { return this; }
    AudioStreamBuilder *setDataCallback(AudioStreamDataCallback *) { return this; }
    AudioStreamBuilder *setErrorCallback(AudioStreamErrorCallback *) { return this; }
    AudioStreamBuilder *setBufferCapacityInFrames(int32_t) { return this; }
    AudioStreamBuilder *setDeviceId(int32_t) { return this; }
    Result openStream(std::shared_ptr<AudioStream> &s) { s = std::make_shared<AudioStream>(); return Result::OK; }
};
}  // namespace oboe
