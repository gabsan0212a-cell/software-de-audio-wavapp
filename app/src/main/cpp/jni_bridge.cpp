#include <jni.h>

#include <algorithm>
#include <string>
#include <vector>

#include "AudioEngine.h"

static AudioEngine gEngine;
static thread_local std::string tError;  // último error en este hilo

#define JNI(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_wavapp_mezcla_NativeAudio_##name

namespace {

std::string str(JNIEnv *env, jstring s) {
    const char *c = env->GetStringUTFChars(s, nullptr);
    std::string r(c ? c : "");
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}

jlongArray longs(JNIEnv *env, std::initializer_list<jlong> v) {
    jlongArray arr = env->NewLongArray(static_cast<jsize>(v.size()));
    env->SetLongArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.begin());
    return arr;
}

// [espacio, canales, cuadros] de una pista recién agregada, o null.
jlongArray trackResult(JNIEnv *env, int slot) {
    if (slot < 0) return nullptr;
    return longs(env, {slot, gEngine.trackChannels(slot), gEngine.trackFrames(slot)});
}

}  // namespace

// ---------- Motor
JNI(jboolean, start)(JNIEnv *, jobject) { return gEngine.start() ? JNI_TRUE : JNI_FALSE; }
JNI(void, stop)(JNIEnv *, jobject) { gEngine.stop(); }
JNI(jint, sampleRate)(JNIEnv *, jobject) { return AudioEngine::kSampleRate; }
JNI(jint, deviceRate)(JNIEnv *, jobject) { return gEngine.deviceRate(); }
JNI(jint, maxTracks)(JNIEnv *, jobject) { return kMaxTracks; }
JNI(jintArray, outputInfo)(JNIEnv *env, jobject) {
    int32_t v[5];
    gEngine.outputInfo(v);
    jintArray arr = env->NewIntArray(5);
    env->SetIntArrayRegion(arr, 0, 5, v);
    return arr;
}
JNI(jstring, lastError)(JNIEnv *env, jobject) { return env->NewStringUTF(tError.c_str()); }

// ---------- Pistas
// [espacio, canales, cuadros, frecuencia original, canales originales, bits, esFloat, eraEstereoIgual]
JNI(jlongArray, importWav)(JNIEnv *env, jobject, jint fd, jstring dstPath) {
    ImportInfo info;
    const int slot = gEngine.importTrack(fd, str(env, dstPath), info, tError);
    if (slot < 0) return nullptr;
    return longs(env, {slot, info.channels, info.frames, info.srcRate, info.srcChannels, info.srcBits,
                       info.srcFloat ? 1 : 0, info.dualMono ? 1 : 0});
}
JNI(jlongArray, addTrackFile)(JNIEnv *env, jobject, jstring path) {
    return trackResult(env, gEngine.addTrackFile(str(env, path), tError));
}
JNI(jlongArray, addDemoTrack)(JNIEnv *env, jobject, jint index, jstring path) {
    return trackResult(env, gEngine.addDemoTrack(index, str(env, path), tError));
}
JNI(void, removeTrack)(JNIEnv *, jobject, jint slot) { gEngine.removeTrack(slot); }
JNI(jfloat, importProgress)(JNIEnv *, jobject) { return gEngine.importProgress(); }

JNI(jfloatArray, overview)(JNIEnv *env, jobject, jint slot, jint columns) {
    std::vector<float> v = gEngine.overview(slot, columns);
    if (v.empty()) return nullptr;
    jfloatArray arr = env->NewFloatArray(static_cast<jsize>(v.size()));
    env->SetFloatArrayRegion(arr, 0, static_cast<jsize>(v.size()), v.data());
    return arr;
}

JNI(void, setTrackGain)(JNIEnv *, jobject, jint slot, jfloat gain) { gEngine.mixer().setTrackGain(slot, gain); }
JNI(void, setTrackPan)(JNIEnv *, jobject, jint slot, jfloat pan) { gEngine.mixer().setTrackPan(slot, pan); }
JNI(void, setTrackMute)(JNIEnv *, jobject, jint slot, jboolean on) { gEngine.mixer().setTrackMute(slot, on); }
JNI(void, setTrackSolo)(JNIEnv *, jobject, jint slot, jboolean on) { gEngine.mixer().setTrackSolo(slot, on); }
JNI(void, setTrackInvert)(JNIEnv *, jobject, jint slot, jboolean on) {
    gEngine.mixer().setTrackInvert(slot, on);
}

// ---------- EQ por pista
JNI(void, setEqBand)(JNIEnv *, jobject, jint slot, jint band, jboolean present, jboolean on, jint type,
                     jfloat freq, jfloat gain, jfloat q) {
    if (Equalizer *eq = gEngine.mixer().trackEq(slot)) eq->setBand(band, present, on, type, freq, gain, q);
}
JNI(void, setEqBypass)(JNIEnv *, jobject, jint slot, jboolean on) {
    if (Equalizer *eq = gEngine.mixer().trackEq(slot)) eq->setBypass(on);
}
JNI(void, setEqAutoGain)(JNIEnv *, jobject, jint slot, jboolean on) {
    if (Equalizer *eq = gEngine.mixer().trackEq(slot)) eq->setAutoGain(on);
}
JNI(jfloat, eqTrimDb)(JNIEnv *, jobject, jint slot) {
    Equalizer *eq = gEngine.mixer().trackEq(slot);
    return eq ? eq->trimDb() : 0.0f;
}
JNI(void, eqResponse)(JNIEnv *env, jobject, jint slot, jint band, jfloatArray freqs, jfloatArray out) {
    Equalizer *eq = gEngine.mixer().trackEq(slot);
    const jsize n = std::min(env->GetArrayLength(freqs), env->GetArrayLength(out));
    if (!eq || n <= 0) return;
    std::vector<float> f(n), db(n);
    env->GetFloatArrayRegion(freqs, 0, n, f.data());
    eq->response(band, f.data(), db.data(), n);
    env->SetFloatArrayRegion(out, 0, n, db.data());
}

// ---------- Master
JNI(void, setMasterGain)(JNIEnv *, jobject, jfloat gain) { gEngine.mixer().setMasterGain(gain); }
JNI(void, setLimiter)(JNIEnv *, jobject, jboolean on) { gEngine.mixer().setLimiter(on); }

// ---------- Transporte
JNI(void, play)(JNIEnv *, jobject) { gEngine.mixer().play(); }
JNI(void, pause)(JNIEnv *, jobject) { gEngine.mixer().pause(); }
JNI(void, stopTransport)(JNIEnv *, jobject) { gEngine.mixer().stop(); }
JNI(void, seek)(JNIEnv *, jobject, jdouble seconds) {
    gEngine.mixer().seek(static_cast<int64_t>(seconds * AudioEngine::kSampleRate));
}
JNI(jboolean, isPlaying)(JNIEnv *, jobject) { return gEngine.mixer().isPlaying() ? JNI_TRUE : JNI_FALSE; }
JNI(jdouble, positionSeconds)(JNIEnv *, jobject) {
    return static_cast<double>(gEngine.mixer().position()) / AudioEngine::kSampleRate;
}
JNI(jdouble, durationSeconds)(JNIEnv *, jobject) {
    return static_cast<double>(gEngine.mixer().length()) / AudioEngine::kSampleRate;
}

// ---------- Medidores
JNI(jint, meterCount)(JNIEnv *, jobject) { return kMeterCount; }
JNI(void, readMeters)(JNIEnv *env, jobject, jfloatArray out) {
    float m[kMeterCount];
    gEngine.mixer().readMeters(m);
    const jsize n = std::min<jsize>(env->GetArrayLength(out), kMeterCount);
    env->SetFloatArrayRegion(out, 0, n, m);
}
