package com.example.noat

import android.view.Surface

/**
 * Kotlin wrapper for the native Vulkan paper rendering engine JNI API.
 *
 * Exposes:
 *   - paperInitWithSurface: initialize Vulkan with an Android Surface
 *   - paperSetQuality: set render quality (0=Low, 1=Balanced, 2=High, 3=Ultra)
 *   - paperSetLight: configure lighting parameters
 *   - paperSetMaterial: configure paper material parameters
 *   - paperResize: notify the renderer of surface size changes
 *   - paperRenderFrame: render one frame
 *   - paperGetInfo: human-readable engine info string
 *   - paperShutdown: release native resources
 */
class PaperLib {
    external fun paperInitWithSurface(surface: Surface): Unit
    external fun paperSetQuality(quality: Int): Unit
    external fun paperSetLight(
        dirX: Float, dirY: Float, dirZ: Float,
        colorR: Float, colorG: Float, colorB: Float,
        intensity: Float, ambient: Float,
        envR: Float, envG: Float, envB: Float, envIntensity: Float
    ): Unit
    external fun paperSetMaterial(
        paperType: Int,
        r: Float, g: Float, b: Float,
        roughness: Float,
        specularStrength: Float, specularRoughness: Float,
        fiberStrength: Float, grainScale: Float, thickness: Float
    ): Unit
    external fun paperResize(width: Int, height: Int): Unit
    external fun paperRenderFrame(): Unit
    external fun paperGetInfo(): String
    external fun paperShutdown(): Unit

    companion object {
        init {
            System.loadLibrary("paper-engine")
        }
    }
}
