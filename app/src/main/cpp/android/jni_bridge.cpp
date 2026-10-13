// Puente entre la pantalla (Kotlin, objeto Nucleo) y el núcleo en C++ (Studio).
// Aquí no hay lógica: solo se pasan datos de un lado al otro.
#include <jni.h>

#include <algorithm>
#include <string>
#include <vector>

#include "AudioInput.h"
#include "AudioOutput.h"
#include "MediaDecoder.h"
#include "Studio.h"
#include "dsp/AnalysisView.h"
#include "engine/Meters.h"

static Studio gStudio(decodeWithMediaCodec);
static AudioOutput gOutput(gStudio.mixer());
static AudioInput gInput(gStudio.input());

#define JNI(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_wavapp_mezcla_Nucleo_##name

namespace {

std::string str(JNIEnv *env, jstring s) {
    if (!s) return "";
    const char *c = env->GetStringUTFChars(s, nullptr);
    std::string r(c ? c : "");
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}

jstring jstr(JNIEnv *env, const std::string &s) { return env->NewStringUTF(s.c_str()); }

jfloatArray floats(JNIEnv *env, const std::vector<float> &v) {
    jfloatArray a = env->NewFloatArray(static_cast<jsize>(v.size()));
    if (a && !v.empty()) env->SetFloatArrayRegion(a, 0, static_cast<jsize>(v.size()), v.data());
    return a;
}

}  // namespace

// ---------- Ciclo de vida
JNI(void, init)(JNIEnv *env, jobject, jstring filesDir, jstring cacheDir) {
    gStudio.open(str(env, filesDir), str(env, cacheDir));
}
JNI(jboolean, start)(JNIEnv *, jobject) {
    gStudio.startBackground();
    const bool ok = gOutput.start();
    if (!ok) gStudio.notify("No se pudo abrir la salida de audio");
    return ok ? JNI_TRUE : JNI_FALSE;
}
JNI(void, stop)(JNIEnv *, jobject) {
    gStudio.inputClosed();  // si estaba grabando, la toma se guarda
    gInput.stop();
    gOutput.stop();
    gStudio.stopBackground();
}
JNI(void, saveNow)(JNIEnv *, jobject) { gStudio.saveNow(); }
JNI(void, clearProject)(JNIEnv *, jobject) { gStudio.clearProject(); }
JNI(jboolean, waitIdle)(JNIEnv *, jobject, jint timeoutMs) { return gStudio.waitIdle(timeoutMs) ? JNI_TRUE : JNI_FALSE; }
JNI(jintArray, outputInfo)(JNIEnv *env, jobject) {
    int32_t v[6];
    gOutput.info(v);
    jintArray a = env->NewIntArray(6);
    env->SetIntArrayRegion(a, 0, 6, v);
    return a;
}
// [pistas máx., bandas máx., medidores, datos de análisis, versiones, columnas y filas del
//  espectrograma, frecuencia del proyecto, pedazos máx., versión de la entrada, datos de la
//  entrada, id del EQ de la entrada, primera versión por espacio, versiones por espacio, datos de
//  automationNow por espacio, datos de trackParams]
JNI(jintArray, constants)(JNIEnv *env, jobject) {
    const int32_t v[] = {kMaxTracks, kMaxBands, MeterBallistics::kCount, AnalysisView::kStatCount,
                         Studio::kVersionCount, Studio::kSpectrogramCols, Studio::kSpectrogramRows, Studio::kRate,
                         kMaxRegions, Studio::kVerInput, Studio::kInputStateCount, Studio::kInputId,
                         Studio::kVerFirstSlot, Studio::kVersPerSlot, Studio::kAutoNowCount,
                         Studio::kTrackParamCount};
    const jsize n = static_cast<jsize>(sizeof v / sizeof v[0]);
    jintArray a = env->NewIntArray(n);
    env->SetIntArrayRegion(a, 0, n, v);
    return a;
}

// ---------- Estado
JNI(void, versions)(JNIEnv *env, jobject, jintArray out) {
    int32_t v[Studio::kVersionCount];
    gStudio.versions(v);
    const jsize n = std::min<jsize>(env->GetArrayLength(out), Studio::kVersionCount);
    env->SetIntArrayRegion(out, 0, n, v);
}
JNI(jstring, tracksJson)(JNIEnv *env, jobject) { return jstr(env, gStudio.tracksJson()); }
JNI(jboolean, trackParams)(JNIEnv *env, jobject, jint id, jfloatArray out) {
    float v[Studio::kTrackParamCount];
    if (!gStudio.trackParams(id, v)) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, std::min<jsize>(Studio::kTrackParamCount, env->GetArrayLength(out)), v);
    return JNI_TRUE;
}
JNI(jfloatArray, eqState)(JNIEnv *env, jobject, jint id) { return floats(env, gStudio.eqState(id)); }
JNI(jlongArray, regions)(JNIEnv *env, jobject, jint id) {
    std::vector<jlong> v;
    for (const Region &r : gStudio.regions(id)) v.insert(v.end(), {r.start, r.src, r.length, r.fadeIn, r.fadeOut});
    jlongArray a = env->NewLongArray(static_cast<jsize>(v.size()));
    if (!v.empty()) env->SetLongArrayRegion(a, 0, static_cast<jsize>(v.size()), v.data());
    return a;
}
JNI(void, masterState)(JNIEnv *env, jobject, jfloatArray out) {
    float v[6];
    gStudio.masterState(v);
    env->SetFloatArrayRegion(out, 0, std::min<jsize>(6, env->GetArrayLength(out)), v);
}
JNI(jstring, statusJson)(JNIEnv *env, jobject) { return jstr(env, gStudio.statusJson()); }
JNI(jfloat, progress)(JNIEnv *, jobject) { return gStudio.progress(); }
// 1 = se puede deshacer, 2 = se puede rehacer.
JNI(jint, history)(JNIEnv *, jobject) { return (gStudio.canUndo() ? 1 : 0) | (gStudio.canRedo() ? 2 : 0); }

// ---------- Pistas
JNI(jboolean, importFiles)(JNIEnv *env, jobject, jintArray fds, jobjectArray names) {
    const jsize n = env->GetArrayLength(fds);
    std::vector<jint> f(static_cast<size_t>(n));
    if (n > 0) env->GetIntArrayRegion(fds, 0, n, f.data());
    std::vector<int> fv(f.begin(), f.end());
    std::vector<std::string> nv;
    for (jsize i = 0; i < n; ++i) {
        auto s = static_cast<jstring>(env->GetObjectArrayElement(names, i));
        nv.push_back(str(env, s));
        env->DeleteLocalRef(s);
    }
    return gStudio.importFiles(fv, nv) ? JNI_TRUE : JNI_FALSE;
}
JNI(jboolean, loadDemo)(JNIEnv *, jobject) { return gStudio.loadDemo() ? JNI_TRUE : JNI_FALSE; }
JNI(jboolean, removeTrack)(JNIEnv *, jobject, jint id) { return gStudio.removeTrack(id) ? JNI_TRUE : JNI_FALSE; }
JNI(void, setVolume)(JNIEnv *, jobject, jint id, jfloat db) { gStudio.setVolume(id, db); }
JNI(void, setPan)(JNIEnv *, jobject, jint id, jfloat pan) { gStudio.setPan(id, pan); }
JNI(void, setMute)(JNIEnv *, jobject, jint id, jboolean on) { gStudio.setMute(id, on); }
JNI(void, setSolo)(JNIEnv *, jobject, jint id, jboolean on) { gStudio.setSolo(id, on); }
JNI(void, setPhase)(JNIEnv *, jobject, jint id, jboolean on) { gStudio.setPhase(id, on); }

// ---------- EQ
JNI(jint, addEqBand)(JNIEnv *, jobject, jint id) { return gStudio.addEqBand(id); }
JNI(void, removeEqBand)(JNIEnv *, jobject, jint id, jint pos) { gStudio.removeEqBand(id, pos); }
JNI(void, setEqBand)(JNIEnv *, jobject, jint id, jint pos, jint type, jfloat freq, jfloat gain, jfloat q, jboolean on) {
    gStudio.setEqBand(id, pos, type, freq, gain, q, on);
}
JNI(void, setEqBypass)(JNIEnv *, jobject, jint id, jboolean on) { gStudio.setEqBypass(id, on); }
JNI(void, setEqAutoGain)(JNIEnv *, jobject, jint id, jboolean on) { gStudio.setEqAutoGain(id, on); }
JNI(void, resetEq)(JNIEnv *, jobject, jint id) { gStudio.resetEq(id); }
JNI(jfloat, eqTrimDb)(JNIEnv *, jobject, jint id) { return gStudio.eqTrimDb(id); }
JNI(void, eqResponse)(JNIEnv *env, jobject, jint id, jint pos, jfloatArray freqs, jfloatArray out) {
    const jsize n = std::min(env->GetArrayLength(freqs), env->GetArrayLength(out));
    if (n <= 0) return;
    std::vector<float> f(static_cast<size_t>(n)), db(static_cast<size_t>(n));
    env->GetFloatArrayRegion(freqs, 0, n, f.data());
    gStudio.eqResponse(id, pos, f.data(), db.data(), n);
    env->SetFloatArrayRegion(out, 0, n, db.data());
}

// ---------- Master y exportar
JNI(void, setMasterDb)(JNIEnv *, jobject, jfloat db) { gStudio.setMasterDb(db); }
JNI(void, setMasterMute)(JNIEnv *, jobject, jboolean on) { gStudio.setMasterMute(on); }
JNI(void, setLimiter)(JNIEnv *, jobject, jboolean on) { gStudio.setLimiter(on); }
JNI(void, setNotch)(JNIEnv *, jobject, jint hz) { gStudio.setNotch(hz); }
JNI(void, setExportFormat)(JNIEnv *, jobject, jint bits, jint rate) { gStudio.setExportFormat(bits, rate); }
JNI(jlong, exportBytes)(JNIEnv *, jobject) { return gStudio.exportBytes(); }
JNI(jboolean, exportMix)(JNIEnv *env, jobject, jint fd, jstring name) {
    return gStudio.exportMix(fd, str(env, name)) ? JNI_TRUE : JNI_FALSE;
}
JNI(void, cancelExport)(JNIEnv *, jobject) { gStudio.cancelExport(); }

// ---------- Transporte
// Reproducir despierta la salida de audio si estaba dormida.
JNI(void, play)(JNIEnv *, jobject) {
    gOutput.start();
    gStudio.play();
}
JNI(void, pause)(JNIEnv *, jobject) { gStudio.pause(); }
JNI(void, playPause)(JNIEnv *, jobject) {
    if (!gStudio.isPlaying()) gOutput.start();
    gStudio.playPause();
}
JNI(void, stopTransport)(JNIEnv *, jobject) { gStudio.stop(); }
JNI(void, seek)(JNIEnv *, jobject, jdouble seconds) { gStudio.seek(seconds); }
JNI(jboolean, isPlaying)(JNIEnv *, jobject) { return gStudio.isPlaying() ? JNI_TRUE : JNI_FALSE; }
JNI(jdouble, positionSeconds)(JNIEnv *, jobject) { return gStudio.position(); }
JNI(jdouble, durationSeconds)(JNIEnv *, jobject) { return gStudio.duration(); }

// ---------- Medidores y análisis
JNI(void, meters)(JNIEnv *env, jobject, jlong nowMs, jfloatArray out) {
    float v[MeterBallistics::kCount];
    gStudio.meters(nowMs, v);
    env->SetFloatArrayRegion(out, 0, std::min<jsize>(MeterBallistics::kCount, env->GetArrayLength(out)), v);
}
JNI(void, clearClip)(JNIEnv *, jobject) { gStudio.clearClip(); }
JNI(void, setAnalysisSource)(JNIEnv *, jobject, jint id) { gStudio.setAnalysisSource(id); }
JNI(jboolean, analysis)(JNIEnv *env, jobject, jlong nowMs, jboolean classic, jfloatArray stats) {
    float v[AnalysisView::kStatCount];
    const bool changed = gStudio.analysisUpdate(nowMs, classic, v);
    env->SetFloatArrayRegion(stats, 0, std::min<jsize>(AnalysisView::kStatCount, env->GetArrayLength(stats)), v);
    return changed ? JNI_TRUE : JNI_FALSE;
}
JNI(jint, goniometer)(JNIEnv *env, jobject, jfloatArray out) {
    const jsize max = env->GetArrayLength(out);
    std::vector<float> v(static_cast<size_t>(max));
    const int n = gStudio.goniometer(v.data(), max);
    if (n > 0) env->SetFloatArrayRegion(out, 0, max, v.data());
    return n;
}
JNI(void, spectrumColumns)(JNIEnv *env, jobject, jfloatArray out) {
    const jsize n = env->GetArrayLength(out);
    if (n <= 0) return;
    std::vector<float> v(static_cast<size_t>(n));
    gStudio.spectrumColumns(v.data(), n);
    env->SetFloatArrayRegion(out, 0, n, v.data());
}
JNI(jbyteArray, spectrogram)(JNIEnv *env, jobject, jint id) {
    std::vector<uint8_t> v;
    if (!gStudio.spectrogram(id, v) || v.empty()) return nullptr;
    jbyteArray a = env->NewByteArray(static_cast<jsize>(v.size()));
    env->SetByteArrayRegion(a, 0, static_cast<jsize>(v.size()), reinterpret_cast<const jbyte *>(v.data()));
    return a;
}

// ---------- Entrada en vivo y grabación
// Abre la entrada (deviceId 0 = la del sistema). Devuelve los canales abiertos o 0.
JNI(jint, inputStart)(JNIEnv *, jobject, jint deviceId, jint channels) {
    gOutput.start();  // para escucharse hace falta la salida
    gStudio.inputClosed();
    const int ch = gInput.start(deviceId, channels);
    if (ch > 0) gStudio.inputOpened(ch);
    else gStudio.notify("No se pudo abrir la entrada de audio");
    return ch;
}
JNI(void, inputStop)(JNIEnv *, jobject) {
    gStudio.inputClosed();
    gInput.stop();
}
JNI(void, setInputGain)(JNIEnv *, jobject, jfloat db) { gStudio.setInputGain(db); }
JNI(void, setMonitor)(JNIEnv *, jobject, jboolean on) {
    if (on) gOutput.start();
    gStudio.setMonitor(on);
}
JNI(void, setMonitorDb)(JNIEnv *, jobject, jfloat db) { gStudio.setMonitorDb(db); }
JNI(void, inputState)(JNIEnv *env, jobject, jfloatArray out) {
    if (gInput.lost()) {
        gStudio.inputClosed();
        gStudio.notify("Se desconectó la entrada de audio");
    }
    gStudio.setLatencies(gInput.latencyFrames(), gOutput.latencyFrames());
    float v[Studio::kInputStateCount];
    gStudio.inputState(v);
    const jsize n = std::min<jsize>(env->GetArrayLength(out), Studio::kInputStateCount);
    env->SetFloatArrayRegion(out, 0, n, v);
}
JNI(jboolean, recordStart)(JNIEnv *, jobject) {
    gOutput.start();
    gStudio.setLatencies(gInput.latencyFrames(), gOutput.latencyFrames());
    return gStudio.recordStart() ? JNI_TRUE : JNI_FALSE;
}
JNI(jint, recordStop)(JNIEnv *, jobject) {
    gStudio.setLatencies(gInput.latencyFrames(), gOutput.latencyFrames());
    return gStudio.recordStop();
}
JNI(void, recordCancel)(JNIEnv *, jobject) { gStudio.recordCancel(); }
// Lo último de la toma en curso: `cols` valores de 0 a 1 que cubren `seconds`.
JNI(jfloatArray, recordingWave)(JNIEnv *env, jobject, jint cols, jfloat seconds) {
    return floats(env, gStudio.recordingWave(cols, seconds));
}

// ---------- Forma de onda
JNI(jfloatArray, regionWave)(JNIEnv *env, jobject, jint id, jint index, jlong from, jlong to, jint cols) {
    std::vector<float> v = gStudio.regionWave(id, index, from, to, cols);
    return v.empty() ? nullptr : floats(env, v);
}

// ---------- Edición
JNI(jboolean, editBegin)(JNIEnv *, jobject, jint id, jint index, jint mode) {
    return gStudio.editBegin(id, index, mode) ? JNI_TRUE : JNI_FALSE;
}
JNI(void, editDrag)(JNIEnv *, jobject, jlong delta, jlong snap) { gStudio.editDrag(delta, snap); }
JNI(void, editEnd)(JNIEnv *, jobject) { gStudio.editEnd(); }
JNI(jint, cut)(JNIEnv *, jobject, jint id, jint index) { return gStudio.cut(id, index); }
JNI(jboolean, deleteRegion)(JNIEnv *, jobject, jint id, jint index) {
    return gStudio.deleteRegion(id, index) ? JNI_TRUE : JNI_FALSE;
}
JNI(void, nudgeRegion)(JNIEnv *, jobject, jint id, jint index, jint ms) { gStudio.nudgeRegion(id, index, ms); }
JNI(void, setRegionFade)(JNIEnv *, jobject, jint id, jint index, jboolean fadeIn, jlong frames) {
    gStudio.setRegionFade(id, index, fadeIn, frames);
}
JNI(void, restoreRegions)(JNIEnv *, jobject, jint id) { gStudio.restoreRegions(id); }
JNI(jboolean, undo)(JNIEnv *, jobject) { return gStudio.undo() ? JNI_TRUE : JNI_FALSE; }
JNI(jboolean, redo)(JNIEnv *, jobject) { return gStudio.redo() ? JNI_TRUE : JNI_FALSE; }

// ---------- Automatización (param 0 = volumen, 1 = paneo)
JNI(void, setAutoMode)(JNIEnv *, jobject, jint id, jint mode) { gStudio.setAutoMode(id, mode); }
JNI(jdoubleArray, autoLane)(JNIEnv *env, jobject, jint id, jint param) {
    const std::vector<double> v = gStudio.autoLane(id, param);
    jdoubleArray a = env->NewDoubleArray(static_cast<jsize>(v.size()));
    if (a && !v.empty()) env->SetDoubleArrayRegion(a, 0, static_cast<jsize>(v.size()), v.data());
    return a;
}
JNI(void, automationNow)(JNIEnv *env, jobject, jfloatArray out) {
    float v[Studio::kAutoNowCount * kMaxTracks];
    gStudio.automationNow(v);
    const jsize n = std::min<jsize>(env->GetArrayLength(out), Studio::kAutoNowCount * kMaxTracks);
    env->SetFloatArrayRegion(out, 0, n, v);
}
JNI(jint, autoAdd)(JNIEnv *, jobject, jint id, jint param, jlong frame, jfloat pos) {
    return gStudio.autoAdd(id, param, frame, pos);
}
JNI(jboolean, autoRemove)(JNIEnv *, jobject, jint id, jint param, jint index) {
    return gStudio.autoRemove(id, param, index) ? JNI_TRUE : JNI_FALSE;
}
JNI(void, autoClear)(JNIEnv *, jobject, jint id, jint param) { gStudio.autoClear(id, param); }
JNI(jboolean, autoBegin)(JNIEnv *, jobject, jint id, jint param, jint index) {
    return gStudio.autoBegin(id, param, index) ? JNI_TRUE : JNI_FALSE;
}
JNI(void, autoDrag)(JNIEnv *, jobject, jlong deltaFrames, jfloat deltaPos) { gStudio.autoDrag(deltaFrames, deltaPos); }
JNI(void, autoEnd)(JNIEnv *, jobject) { gStudio.autoEnd(); }
JNI(void, autoTouch)(JNIEnv *, jobject, jint id, jint param, jboolean down) { gStudio.autoTouch(id, param, down); }
