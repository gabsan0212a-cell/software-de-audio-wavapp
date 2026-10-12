#include <jni.h>
#include "AudioEngine.h"

static AudioEngine gEngine;

#define JNI(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_wavapp_mezcla_NativeAudio_##name

JNI(jboolean, start)(JNIEnv *, jobject) { return gEngine.start() ? JNI_TRUE : JNI_FALSE; }
JNI(void, stop)(JNIEnv *, jobject) { gEngine.stop(); }
JNI(jint, deviceRate)(JNIEnv *, jobject) { return gEngine.deviceRate(); }

JNI(jstring, loadWav)(JNIEnv *env, jobject, jint fd) {
    return env->NewStringUTF(gEngine.loadWav(fd).c_str());
}
JNI(void, play)(JNIEnv *, jobject) { gEngine.play(); }
JNI(void, pause)(JNIEnv *, jobject) { gEngine.pause(); }
JNI(void, stopPlayback)(JNIEnv *, jobject) { gEngine.stopPlayback(); }
JNI(jboolean, isPlaying)(JNIEnv *, jobject) { return gEngine.isPlaying() ? JNI_TRUE : JNI_FALSE; }
JNI(jdouble, positionSeconds)(JNIEnv *, jobject) { return gEngine.positionSeconds(); }

// [frecuencia, canales, bits, esFloat, frames] o null si no hay clip.
JNI(jlongArray, clipInfo)(JNIEnv *env, jobject) {
    const Clip *c = gEngine.clip();
    if (!c) return nullptr;
    jlong v[5] = {c->sampleRate, c->channels, c->bits, c->isFloat ? 1 : 0, c->frames};
    jlongArray arr = env->NewLongArray(5);
    env->SetLongArrayRegion(arr, 0, 5, v);
    return arr;
}
