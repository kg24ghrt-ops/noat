package com.example.noat

import android.content.res.AssetManager
import android.view.Surface

/**
 * Kotlin wrapper for the native Vulkan paper rendering engine JNI API.
 *
 * Exposes:
 *   - paperInitWithSurface: initialize Vulkan with an Android Surface
 *   - paperSetQuality: set render quality (0=Low, 1=Balanced, 2=High, 3=Ultra)
 *   - paperSetLight: configure lighting parameters
 *   - paperSetMaterial: configure paper material parameters
 *   - paperSetSize: set paper size id
 *   - paperSetAssetManager: provide the app AssetManager for bundled font loading
 *   - paperShapeText: shape text with HarfBuzz using a bundled font (returns JSON)
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
    external fun paperSetSize(sizeId: String): Unit
    external fun paperSetAssetManager(assetManager: AssetManager): Unit
    external fun paperShapeText(
        fontName: String, text: String, fontSize: Float,
        direction: String, script: String, language: String
    ): String
    // ---- Text (handwriting) ----
    // Bake an SDF glyph atlas for the given glyph IDs at `ppem` (pixels per em)
    // using the bundled font. Returns meta JSON {width, height, glyphs}.
    // The baked atlas is held natively; call paperTextSetAtlas() to upload it.
    external fun paperTextPrepareAtlas(
        glyphIds: IntArray, ppem: Float, supersample: Int, fontName: String
    ): String
    external fun paperTextSetAtlas(): Unit
    // Upload positioned glyph instances (12 floats per instance:
    // x, y, w, h, atlasX, atlasY, atlasW, atlasH, glyphId, clusterId, pad0, pad1).
    external fun paperTextUploadInstances(instances: FloatArray, count: Int): Unit
    external fun paperTextClearInstances(): Unit
    external fun paperResize(width: Int, height: Int): Unit
    external fun paperRenderFrame(): Unit
    external fun paperGetInfo(): String
    external fun paperShutdown(): Unit

    // ---- Text document editing ----
    external fun paperTextInsert(text: String): Unit
    external fun paperTextBackspace(): Unit
    external fun paperTextEnter(): Unit
    external fun paperTextMoveLeft(): Unit
    external fun paperTextMoveRight(): Unit
    external fun paperTextMoveUp(): Unit
    external fun paperTextMoveDown(): Unit
    external fun paperTextMoveToLineStart(): Unit
    external fun paperTextMoveToLineEnd(): Unit
    external fun paperTextSetCursor(clusterIndex: Int): Unit
    external fun paperTextGetCursorX(): Float
    external fun paperTextGetCursorY(): Float
    external fun paperTextGetCursorLine(): Int
    external fun paperTextSetComposing(text: String): Unit
    external fun paperTextClearComposing(): Unit

    companion object {
        init {
            System.loadLibrary("paper-engine")
        }
    }
}
