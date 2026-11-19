# NOAT

An Android application built with Kotlin, XML layouts, and a native C++17 module.

## Project toolchain

- Android Gradle Plugin: 9.3.1
- Gradle: 9.7.0
- Kotlin: 2.4.21
- JDK: 17
- Compile SDK: 36
- Target SDK: 36
- Minimum SDK: 31
- NDK: 30.0.16248370
- CMake: 3.31.6
- Native C++ standard: C++17

## Build policy

Android compilation and tests run through GitHub Actions.

No local Android SDK, NDK, CMake installation, Android Studio, or local Gradle compilation is required.

## CI artifacts

The Android CI workflow runs unit tests and produces debug and release APKs.

To retrieve APKs:

1. Open the repository's Actions tab.
2. Select the Android CI workflow run.
3. Download the `noat-android-apks` artifact.

The release APK is unsigned until a signing configuration is added.

## Application details

- Application ID: `com.example.noat`
- Namespace: `com.example.noat`
- Native library: `native-lib`
- C++ standard: C++17
