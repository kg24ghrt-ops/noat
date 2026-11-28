plugins {
    id("com.android.application")
}

android {
    namespace = "com.example.noat"
    compileSdk = 36
    buildToolsVersion = "36.0.0"

    defaultConfig {
        applicationId = "com.example.noat"
        minSdk = 31
        targetSdk = 36
        versionCode = 2
        versionName = "1.1.0"

        testInstrumentationRunner =
            "androidx.test.runner.AndroidJUnitRunner"

    }

    signingConfigs {
        create("release") {
            val storeFile = System.getenv("ANDROID_KEYSTORE_FILE")
            val storePassword = System.getenv("ANDROID_KEYSTORE_PASSWORD")
            val keyAlias = System.getenv("ANDROID_KEY_ALIAS")
            val keyPassword = System.getenv("ANDROID_KEY_PASSWORD")

            val envVars = mapOf(
                "ANDROID_KEYSTORE_FILE" to storeFile,
                "ANDROID_KEYSTORE_PASSWORD" to storePassword,
                "ANDROID_KEY_ALIAS" to keyAlias,
                "ANDROID_KEY_PASSWORD" to keyPassword
            )

            val allPresent = envVars.all { (it.value ?: "").isNotBlank() }

            if (allPresent) {
                this.storeFile = file(storeFile!!)
                this.storePassword = storePassword!!
                this.keyAlias = keyAlias!!
                this.keyPassword = keyPassword!!
            }
            // When credentials are incomplete (e.g. PRs without signing secrets),
            // leave signingConfig unset so the release build is unsigned.
            // The workflow's validation step fails clearly on push/main when
            // secrets are missing.
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            val signingCredentialsComplete = listOf(
                "ANDROID_KEYSTORE_FILE",
                "ANDROID_KEYSTORE_PASSWORD",
                "ANDROID_KEY_ALIAS",
                "ANDROID_KEY_PASSWORD"
            ).all { System.getenv(it)?.isNotBlank() == true }
            if (signingCredentialsComplete) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }


    buildFeatures {
        viewBinding = true
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }
}


kotlin {
    compilerOptions {
        jvmTarget.set(
            org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
        )
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.8.0")
    implementation("com.google.android.material:material:1.14.0")
    implementation("androidx.constraintlayout:constraintlayout:2.2.2")

    implementation("androidx.activity:activity-ktx:1.13.0")
    implementation("androidx.lifecycle:lifecycle-runtime:2.11.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel:2.11.0")

    testImplementation("junit:junit:4.13.2")

    androidTestImplementation("androidx.test.ext:junit:1.3.0")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.7.0")
}
