package com.example.noat

import android.opengl.GLES30
import android.opengl.GLSurfaceView
import kotlin.math.sqrt
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * GLSurfaceView.Renderer implementation that bridges Android's GLSurfaceView
 * lifecycle with the native C++ paper rendering engine.
 *
 * On surface creation, queries the OpenGL ES capabilities (version, extensions,
 * limits) and passes them to the native renderer via JNI. Each frame, calls
 * the native render loop.
 *
 * Mali-specific considerations:
 *   - Capabilities are queried at runtime (never assumed).
 *   - Tessellation is never auto-enabled (Arm recommends avoiding it on tile-based GPUs).
 *   - ASTC/ETC2 texture format support is detected from extensions.
 */
class PaperRenderer : GLSurfaceView.Renderer {

    private val paperLib = PaperLib()
    private var surfaceWidth = 0
    private var surfaceHeight = 0
    private var initialized = false

    // Default lighting (directional, warm)
    private val lightDir = FloatArray(3)
    private val lightColor = FloatArray(3)
    private val envColor = FloatArray(3)

    // Default material (standard notebook paper)
    private val paperColor = FloatArray(3)

    override fun onSurfaceCreated(gl: GL10, config: EGLConfig) {
        // Query OpenGL ES capabilities from the active context
        val versionStr = GLES30.glGetString(GLES30.GL_VERSION).toString()
        var esMajor = 3
        var esMinor = 0
        val esIdx = versionStr.indexOf("OpenGL ES ")
        if (esIdx >= 0) {
            val start = esIdx + 10
            if (start < versionStr.length) {
                esMajor = versionStr[start].digitToInt() ?: 3
                if (start + 2 < versionStr.length && versionStr[start + 1] == '.') {
                    esMinor = versionStr[start + 2].digitToInt() ?: 0
                }
            }
        }

        val glslStr = GLES30.glGetString(GLES30.GL_SHADING_LANGUAGE_VERSION).toString()
        var glslMajor = 3
        var glslMinor = 0
        val glslIdx = glslStr.indexOf("OpenGL ES GLSL ES ")
        if (glslIdx >= 0) {
            val start = glslIdx + 18
            if (start < glslStr.length) {
                glslMajor = glslStr[start].digitToInt() ?: 3
                if (start + 2 < glslStr.length && glslStr[start + 1] == '.') {
                    glslMinor = glslStr[start + 2].digitToInt() ?: 0
                }
            }
        }

        val rendererStr = GLES30.glGetString(GLES30.GL_RENDERER).toString()
        val vendorStr = GLES30.glGetString(GLES30.GL_VENDOR).toString()
        val versionFullStr = GLES30.glGetString(GLES30.GL_VERSION).toString()
        val glslFullStr = GLES30.glGetString(GLES30.GL_SHADING_LANGUAGE_VERSION).toString()

        val isMali = rendererStr.contains("Mali", ignoreCase = true)

        val maxTexSize = IntArray(1)
        val maxTexUnits = IntArray(1)
        val maxCombinedTexUnits = IntArray(1)
        val maxVertexAttribs = IntArray(1)
        val maxDrawBuffers = IntArray(1)
        GLES30.glGetIntegerv(GLES30.GL_MAX_TEXTURE_SIZE, maxTexSize, 0)
        GLES30.glGetIntegerv(GLES30.GL_MAX_TEXTURE_IMAGE_UNITS, maxTexUnits, 0)
        GLES30.glGetIntegerv(GLES30.GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, maxCombinedTexUnits, 0)
        GLES30.glGetIntegerv(GLES30.GL_MAX_VERTEX_ATTRIBS, maxVertexAttribs, 0)
        GLES30.glGetIntegerv(GLES30.GL_MAX_DRAW_BUFFERS, maxDrawBuffers, 0)

        // Extension checks
        val extensions = GLES30.glGetString(GLES30.GL_EXTENSIONS).toString()
        val hasTessellation = extensions.contains("GL_OES_tessellation_shader") || esMinor >= 2
        val hasComputeShader = extensions.contains("GL_OES_compute_shader") || esMinor >= 1
        val hasAstc = extensions.contains("GL_COMPRESSED_RGBA_ASTC_4x4_KHR") || esMinor >= 2
        val hasEtc2 = true // ETC2 is core in GLES 3.0
        val hasFramebufferFetch = extensions.contains("GL_ARM_shader_framebuffer_fetch")

        // Initialize native engine with capabilities
        paperLib.paperInitWithCaps(
            esMajor, esMinor,
            glslMajor, glslMinor,
            rendererStr, vendorStr, versionFullStr, glslFullStr,
            isMali,
            maxTexSize[0], maxTexUnits[0], maxCombinedTexUnits[0],
            maxVertexAttribs[0], maxDrawBuffers[0],
            hasTessellation, hasComputeShader,
            hasAstc, hasEtc2, hasFramebufferFetch
        )

        // Setup default lighting (directional, warm paper lighting)
        lightDir[0] = 0.5f
        lightDir[1] = -1.0f
        lightDir[2] = 0.5f
        val len = sqrt(lightDir[0] * lightDir[0] + lightDir[1] * lightDir[1] + lightDir[2] * lightDir[2]).toFloat()
        lightDir[0] /= len
        lightDir[1] /= len
        lightDir[2] /= len

        lightColor[0] = 1.0f
        lightColor[1] = 1.0f
        lightColor[2] = 0.95f

        envColor[0] = 0.8f
        envColor[1] = 0.85f
        envColor[2] = 0.9f

        paperColor[0] = 0.92f
        paperColor[1] = 0.94f
        paperColor[2] = 0.98f

        // Set default light
        paperLib.paperSetLight(
            lightDir[0], lightDir[1], lightDir[2],
            lightColor[0], lightColor[1], lightColor[2],
            1.0f, 0.3f,
            envColor[0], envColor[1], envColor[2], 0.4f
        )

        // Set default material: standard notebook paper
        val presets = PaperMaterialPresets.StandardNotebook.ordinal
        paperLib.paperSetMaterial(
            presets,
            paperColor[0], paperColor[1], paperColor[2],
            0.4f, // roughness
            0.1f, 0.6f, // specular
            0.5f, 1.0f, 0.08f // fiber, grain, thickness
        )

        // Set quality (balanced by default)
        paperLib.paperSetQuality(1) // Balanced

        // Resize to current surface
        if (surfaceWidth > 0 && surfaceHeight > 0) {
            paperLib.paperResize(surfaceWidth, surfaceHeight)
        }

        initialized = true
    }

    override fun onSurfaceChanged(gl: GL10, width: Int, height: Int) {
        surfaceWidth = width
        surfaceHeight = height
        if (initialized) {
            paperLib.paperResize(width, height)
        }
    }

    override fun onDrawFrame(gl: GL10) {
        if (!initialized) return
        paperLib.paperBeginFrame()
        paperLib.paperRenderFrame()
        paperLib.paperEndFrame()
    }

    fun release() {
        paperLib.paperShutdown()
        initialized = false
    }
}

/**
 * Paper material type presets (mirrors the C++ PaperType enum).
 * Order must match C++ PaperType enum definition.
 */
enum class PaperMaterialPresets {
    StandardNotebook,
    PremiumWriting,
    Printer,
    Recycled,
    Watercolor,
    Cotton,
    Handmade,
    RoughTextured,
    Parchment;
}
