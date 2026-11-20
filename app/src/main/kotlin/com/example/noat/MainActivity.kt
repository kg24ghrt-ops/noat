package com.example.noat

import android.os.Bundle
import android.opengl.GLSurfaceView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.floatingactionbutton.FloatingActionButton
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import com.example.noat.R

class MainActivity : AppCompatActivity() {

    private lateinit var paperSurface: GLSurfaceView
    private lateinit var paperRenderer: PaperRenderer
    private val paperLib = PaperLib()
    private var currentPaperIndex = 0

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

        // Initialize paper rendering surface
        paperSurface = findViewById(R.id.paper_surface)
        paperRenderer = PaperRenderer()

        // Allow the surface to be composited behind other views (TextView, FAB)
        paperSurface.setZOrderMediaOverlay(true)
        paperSurface.setEGLConfigChooser(8, 8, 8, 8, 0, 0)
        paperSurface.setRenderer(paperRenderer)
        paperSurface.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY)

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
    }

    override fun onPause() {
        super.onPause()
        paperSurface.onPause()
    }

    override fun onResume() {
        super.onResume()
        paperSurface.onResume()
    }

    override fun onDestroy() {
        super.onDestroy()
        paperRenderer.release()
    }
}
