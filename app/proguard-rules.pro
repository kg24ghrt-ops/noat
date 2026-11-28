# ProGuard rules for Android
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile
-keep class kotlin.** { *; }
-keep class kotlin.Metadata { *; }
-dontwarn kotlin.**
-keep class androidx.** { *; }
-keep interface androidx.** { *; }

# Keep PaperLib and its native methods for JNI linkage
-keep class com.example.noat.PaperLib { *; }
-keepclassmembers class com.example.noat.PaperLib {
    native <methods>;
}