// -PconX86=true: el APK final también para x86_64 (solo para probarlo en el emulador).
val conX86 = (project.findProperty("conX86") as String?) == "true"

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.wavapp.mezcla"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.wavapp.mezcla"
        minSdk = 26
        targetSdk = 35
        versionCode = 11
        versionName = "0.7.1"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-O2")
                arguments += "-DANDROID_STL=c++_shared"  // Oboe lo requiere
            }
        }
    }

    // Llave fija: así cada APK nuevo se instala encima del anterior sin desinstalar.
    signingConfigs {
        getByName("debug") {
            storeFile = file("debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    buildTypes {
        // "debug" es para las pruebas en el emulador (x86_64 incluido), con el C++ optimizado igual.
        getByName("debug") {
            ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
            externalNativeBuild { cmake { arguments += "-DCMAKE_BUILD_TYPE=Release" } }
        }
        // "release" es el APK que se instala: R8 quita lo que no se usa y optimiza (más ligero y
        // más fluido que debug), solo para celulares ARM. Misma llave: se instala encima.
        getByName("release") {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName("debug")
            ndk {
                abiFilters += listOf("arm64-v8a", "armeabi-v7a")
                if (conX86) abiFilters += "x86_64"
            }
        }
    }

    buildFeatures {
        prefab = true
        compose = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("com.google.oboe:oboe:1.9.3")

    val composeBom = platform("androidx.compose:compose-bom:2024.12.01")
    implementation(composeBom)
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")

    androidTestImplementation(composeBom)
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test:runner:1.6.2")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
