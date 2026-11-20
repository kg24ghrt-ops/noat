package com.example.noat

import android.os.Bundle
import android.opengl.GLSurfaceView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import com.example.noat.R
import android.widget.TextView

class MainActivity : AppCompatActivity() {

    private lateinit var paperSurface: GLSurfaceView
    private lateinit var paperRenderer: PaperRenderer

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
        // Info will be populated after surface creation via a callback
        // For now, show app name; the native message is available via paperGetInfo
        textView.text = getString(R.string.app_name)
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
