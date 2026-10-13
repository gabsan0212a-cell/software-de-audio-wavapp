// Vista previa de la pantalla en Linux, sin emulador (solo para desarrollar; no es parte de
// la app ni del CI). Ver previa.sh.
pluginManagement {
    repositories {
        // Espejo de Maven Central (por si el central limita las descargas).
        maven("https://maven-central.storage-download.googleapis.com/maven2/")
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        maven("https://maven-central.storage-download.googleapis.com/maven2/")
        mavenCentral()
    }
}
rootProject.name = "previa"
