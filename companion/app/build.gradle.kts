plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
}

android {
    namespace  = "com.vrodcluster.companion"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.vrodcluster.companion"
        // Android 16. Older devices would need a separate target since we
        // depend on permission models, foreground-service types, and BLE
        // permissioning that's only stable on this baseline.
        minSdk        = 36
        targetSdk     = 36
        versionCode   = 1
        versionName   = "0.1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }

    testOptions {
        unitTests {
            // Robolectric reads merged manifests + resources from the
            // assembled app to construct an Application context; off
            // by default in AGP.
            isIncludeAndroidResources = true
        }
    }
}

// Robolectric's API-36 image needs JDK 21 to load. We still target JVM
// 17 for production (the rest of the build runs on 17 to match
// kotlinOptions). Forcing only the Test task onto JDK 21 keeps prod
// bytecode unchanged but lets the runtime parse the SDK 36 platform jar.
tasks.withType<Test>().configureEach {
    javaLauncher.set(javaToolchains.launcherFor {
        languageVersion.set(JavaLanguageVersion.of(21))
    })
}

kotlin {
    // Replaces the deprecated kotlinOptions {} block. Same effect: produce
    // JVM 17 bytecode to match the Java targetCompatibility above.
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.activity.compose)

    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material3.adaptive.navigation.suite)
    implementation(libs.androidx.compose.material.icons)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.kotlinx.serialization.json)
    debugImplementation(libs.androidx.compose.ui.tooling.preview)

    testImplementation(libs.junit)
    testImplementation(libs.robolectric)
    testImplementation(libs.androidx.test.core)
    androidTestImplementation(libs.androidx.junit)
}
