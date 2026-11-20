package com.example.noat

/**
 * Kotlin wrapper for the native paper rendering engine JNI API.
 *
 * Exposes:
 *   - paperInit / paperInitWithCaps: initialize the native renderer
 *   - paperSetQuality: set render quality (0=Low, 1=Balanced, 2=High, 3=Ultra)
 *   - paperSetLight: configure lighting parameters
 *   - paperSetMaterial: configure paper material parameters
 *   - paperResize: notify the renderer of surface size changes
 *   - paperBeginFrame / paperRenderFrame / paperEndFrame: frame lifecycle
 *   - paperGetInfo: human-readable engine info string
 *   - paperShutdown: release native resources
 */
class PaperLib {
    external fun paperInit(): Unit
    external fun paperInitWithCaps(
        major: Int, minor: Int,
        glslMajor: Int, glslMinor: Int,
        renderer: String, vendor: String, version: String, glslVersion: String,
        isMali: Boolean,
        maxTextureSize: Int, maxTextureImageUnits: Int, maxCombinedTextureImageUnits: Int,
        maxVertexAttribs: Int, maxDrawBuffers: Int,
        hasTessellation: Boolean, hasComputeShader: Boolean,
        hasAstc: Boolean, hasEtc2: Boolean, hasFramebufferFetch: Boolean
    ): Unit
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
    external fun paperBeginFrame(): Unit
    external fun paperRenderFrame(): Unit
    external fun paperEndFrame(): Unit
    external fun paperGetInfo(): String
    external fun paperShutdown(): Unit

    companion object {
        init {
            System.loadLibrary("paper-engine")
        }
    }
}
