plugins {
    id("com.android.application") version "9.3.2"
}

android {
    namespace = "me.glassbar"
    compileSdk = 36
    defaultConfig {
        applicationId = "me.glassbar"
        minSdk = 31           // Android 12+
        targetSdk = 36
        versionCode = 1
        versionName = "1.0.0"
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildFeatures { buildConfig = true }
}

dependencies {
    // shared single LSPlant engine interface (not packaged into the APK)
    compileOnly(files("libs/core.jar"))
    // NOTE: liquid-glass (backdrop) is a separate variant built on SDK37;
    // add: compileOnly "io.github.kyant0:backdrop:2.0.1" when android-37 is present.
}
