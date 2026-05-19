pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

plugins {
    // Auto-resolve JDK toolchains (eg. JDK 21 for Robolectric SDK-36
    // tests) from foojay if not installed locally. Without this,
    // toolchains-as-version-spec fails on machines that only have JDK 17.
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}
dependencyResolutionManagement {
    repositoriesMode = RepositoriesMode.FAIL_ON_PROJECT_REPOS
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "VRodCompanion"
include(":app")
