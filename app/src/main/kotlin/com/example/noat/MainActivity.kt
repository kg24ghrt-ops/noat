package com.example.noat

import android.os.Bundle
import android.view.Choreographer
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import com.example.noat.R
import android.widget.TextView
import android.widget.Button
import com.google.android.material.floatingactionbutton.FloatingActionButton

class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    private lateinit var vulkanSurface: SurfaceView
    private val paperLib = PaperLib()
    private var currentPaperIndex = 0
    private var currentSizeIndex = 0
    private var isRendering = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Enable edge-to-edge
        WindowCompat.setDecorFitsSystemWindows(window, false)

        setContentView(R.layout.activity_main)

        // Apply window insets for edge-to-edge
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(android.R.id.content)) { view, windowInsets ->
            val insets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            view.updatePadding(
                left = insets.left,
                top = insets.top,
                right = insets.right,
                bottom = insets.bottom
            )
            windowInsets
        }

        // Initialize Vulkan rendering surface
        vulkanSurface = findViewById(R.id.vulkan_surface)
        vulkanSurface.setZOrderOnTop(false)
        vulkanSurface.holder.setFormat(android.graphics.PixelFormat.RGBA_8888)
        vulkanSurface.holder.addCallback(this)

        // Show engine info in the TextView
        val textView = findViewById<TextView>(R.id.textView)
        textView.text = getString(R.string.app_name)

        // FAB: cycle through paper material types on each tap
        val types = PaperMaterialPresets.values()
        val fab = findViewById<FloatingActionButton>(R.id.fab)
        fab.setOnClickListener {
            currentPaperIndex = (currentPaperIndex + 1) % types.size
            val type = types[currentPaperIndex]
            paperLib.paperSetMaterial(
                type.ordinal,
                0.92f, 0.94f, 0.98f,
                0.4f, 0.1f, 0.6f, 0.5f, 1.0f, 0.08f
            )
            textView.text = "${getString(R.string.app_name)} — ${type.name}"
        }

        // Size button: cycle through paper sizes (next to the FAB).
        val sizeButton = findViewById<Button>(R.id.size_button)
        sizeButton.text = PaperSizes[currentSizeIndex].name
        sizeButton.setOnClickListener {
            currentSizeIndex = (currentSizeIndex + 1) % PaperSizes.size
            val size = PaperSizes[currentSizeIndex]
            sizeButton.text = size.name
            paperLib.paperSetSize(size.id)
        }
    }

    // SurfaceHolder.Callback

    override fun surfaceCreated(holder: SurfaceHolder) {
        val surface = holder.surface
        if (surface.isValid) {
            paperLib.paperInitWithSurface(surface)
            // Default to A4 on the live renderer (C++ also defaults to A4).
            paperLib.paperSetSize(DefaultPaperSizeId)
        }
        startRenderLoop()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        paperLib.paperResize(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        stopRenderLoop()
        paperLib.paperShutdown()
    }

    override fun onPause() {
        super.onPause()
        stopRenderLoop()
    }

    override fun onResume() {
        super.onResume()
        startRenderLoop()
    }

    override fun onDestroy() {
        super.onDestroy()
        stopRenderLoop()
        paperLib.paperShutdown()
    }

    private fun startRenderLoop() {
        if (isRendering) return
        isRendering = true
        val callback = object : Choreographer.FrameCallback {
            override fun doFrame(frameTimeNanos: Long) {
                if (isRendering) {
                    paperLib.paperRenderFrame()
                    Choreographer.getInstance().postFrameCallback(this)
                }
            }
        }
        Choreographer.getInstance().postFrameCallback(callback)
    }

    private fun stopRenderLoop() {
        isRendering = false
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
}

/**
 * Paper size table — one source of truth for dimensions and labels.
 * Dimensions in mm, portrait orientation.
 *
 * These ids MUST match the C++ kPaperSizes table in renderer.cpp exactly.
 * The id (not an ordinal) is passed over JNI to the native renderer so it
 * stays stable across enum reordering.
 */
val PaperSizes = listOf(
    PaperSize("A3",   "A3",   297f, 420f),
    PaperSize("A4",   "A4",   210f, 297f),
    PaperSize("A5",   "A5",   148f, 210f),
    PaperSize("A6",   "A6",   105f, 148f),
    PaperSize("LETTER", "Letter", 215.9f, 279.4f),
    PaperSize("LEGAL",  "Legal",  215.9f, 355.6f),
)

/** Default paper size id. Matches kDefaultPaperSizeId in renderer.cpp. */
val DefaultPaperSizeId = "A4"

/**
 * A selectable paper size.
 * aspectRatio = widthMm / heightMm (all A-series share 1:sqrt(2)).
 */
data class PaperSize(
    val id: String,          // stable id, passed over JNI
    val name: String,        // label shown on the size button
    val widthMm: Float,
    val heightMm: Float
) {
    val aspectRatio: Float
        get() = widthMm / heightMm
}
