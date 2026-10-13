// La pantalla de la app (Kotlin de app/src/main/java) con Compose de escritorio.
// Por omisión Compose 1.5.12: corre sin librerías de Google Maven (dibuja las capturas).
// -Pcompose=1.7.3: compila contra la versión de la app (solo para revisar que compile).
val compose = (findProperty("compose") as String?) ?: "1.5.12"

plugins {
    kotlin("jvm") version "2.0.21"
    id("org.jetbrains.kotlin.plugin.compose") version "2.0.21"
}

dependencies {
    implementation("org.jetbrains.compose.foundation:foundation-desktop:$compose")
    implementation("org.jetbrains.compose.ui:ui-desktop:$compose")
    implementation("org.jetbrains.skiko:skiko-awt-runtime-linux-x64:0.7.85.4")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-swing:1.7.3")
    implementation("org.json:json:20240303")
}

kotlin {
    // Todo el Kotlin de la app menos lo que es solo de Android (aquí lo reemplazan los
    // archivos de src/main/kotlin).
    sourceSets["main"].kotlin.srcDir("../../app/src/main/java")
    sourceSets["main"].kotlin.exclude("**/MainActivity.kt", "**/ui/Plataforma.kt")
}

tasks.register<JavaExec>("previa") {
    classpath = sourceSets["main"].runtimeClasspath
    mainClass.set("previa.PreviaKt")
    jvmArgs("-Djava.library.path=${layout.buildDirectory.get().asFile}/nativo", "-Djava.awt.headless=true")
    args(
        (findProperty("salida") as String?) ?: "${projectDir}/capturas",
        (findProperty("escenas") as String?) ?: "todas",
    )
}
