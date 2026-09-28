plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

// The tax engine is C++ built with the NDK. Pass -PnoNative to build only the Kotlin side
// (for example to check the UI code on a machine without the NDK); the app then can't run.
val buildNative = !project.hasProperty("noNative")

android {
    namespace = "org.opentax.app"
    compileSdk = 37
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "org.opentax.app"
        minSdk = 28 // Android 9: getrandom() and std::filesystem in the NDK
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0"
        if (buildNative) {
            externalNativeBuild {
                cmake {
                    arguments += listOf("-DANDROID_STL=c++_static")
                }
            }
            ndk {
                abiFilters += listOf("arm64-v8a", "x86_64")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }

    if (buildNative) {
        externalNativeBuild {
            cmake {
                path = file("src/main/cpp/CMakeLists.txt")
                version = "4.1.2"
            }
        }
    }
}

dependencies {
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
}
