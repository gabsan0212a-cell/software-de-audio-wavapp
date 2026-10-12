#include <jni.h>
#include "ToneEngine.h"

static ToneEngine gEngine;

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_wavapp_mezcla_NativeAudio_start(JNIEnv *, jobject) {
    return gEngine.start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_wavapp_mezcla_NativeAudio_stop(JNIEnv *, jobject) {
    gEngine.stop();
}

JNIEXPORT void JNICALL
Java_com_wavapp_mezcla_NativeAudio_setToneOn(JNIEnv *, jobject, jboolean on) {
    gEngine.setToneOn(on == JNI_TRUE);
}

JNIEXPORT jint JNICALL
Java_com_wavapp_mezcla_NativeAudio_sampleRate(JNIEnv *, jobject) {
    return gEngine.sampleRate();
}

}
