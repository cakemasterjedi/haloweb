plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.emblem.auto"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.emblem.auto"
        minSdk = 26
        targetSdk = 35
        versionCode = 2
        versionName = "1.1"
    }

    // Signed with the key kept next to this file, so a newer build installs
    // over an older one whichever machine built it. (A personal, sideloaded
    // app: this key isn't used for anything else.)
    signingConfigs {
        create("emblem") {
            storeFile = file("../emblem.keystore")
            storePassword = "emblem-auto"
            keyAlias = "emblem"
            keyPassword = "emblem-auto"
        }
    }
    buildTypes {
        getByName("release") {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("emblem")
        }
        getByName("debug") {
            signingConfig = signingConfigs.getByName("emblem")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation("androidx.car.app:app:1.7.0")
}
