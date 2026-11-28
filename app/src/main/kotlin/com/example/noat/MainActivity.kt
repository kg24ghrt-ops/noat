package com.example.noat

import android.content.Context
import android.os.Bundle
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.util.AttributeSet
import android.util.Log
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputMethodManager
import android.view.Choreographer
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.Button
import android.widget.EditText
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import com.example.noat.R
import com.google.android.material.floatingactionbutton.FloatingActionButton
import kotlin.math.max
import kotlin.math.min
import kotlin.text.Regex

// Custom EditText that provides our custom InputConnection.
class PaperEditText @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = android.R.attr.editTextStyle
) : EditText(context, attrs, defStyleAttr) {

    private var customInputConnection: InputConnection? = null

    fun setCustomInputConnection(connection: InputConnection?) {
        customInputConnection = connection
    }

    override fun onCreateInputConnection(editorInfo: EditorInfo): InputConnection {
        return customInputConnection ?: super.onCreateInputConnection(editorInfo)
    }
}

class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    private lateinit var vulkanSurface: SurfaceView
    private lateinit var imeInput: EditText
    private val paperLib = PaperLib()
    private var currentPaperIndex = 0
    private var currentSizeIndex = 0
    private var isRendering = false
    private var lastFrameTimeNanos: Long = 0

    // Document state (mirrors the C++ TextDocument).
    private var documentText = ""
    private var documentCursor = 0

    // Shaped run results.
    private var shapedGlyphCount = 0
    private var shapedClusterCount = 0
    private var shapedGlyphIds = IntArray(0)
    private var shapedXPos = FloatArray(0)
    private var shapedYPos = FloatArray(0)
    private var shapedXAdvance = FloatArray(0)
    private var shapedClusterIds = IntArray(0)

    // Atlas meta.
    private var atlasWidth = 0
    private var atlasHeight = 0
    private var atlasGlyphs = emptyList<AtlasGlyphEntry>()

    // Built GPU instances.
    private var gpuInstances = FloatArray(0)
    private var gpuInstanceCount = 0

    private var needsReshape = false
    private var isComposing = false
    private var updatingFromNative = false

    // Paper size dimensions for layout.
    private var currentPaperWidthMm = 210.0f
    private var currentPaperHeightMm = 297.0f
    private var currentRuleGapMm = 7.1f
    private var currentRuleTopMm = 25.4f
    private var currentMarginXMm = 31.75f

    // Keep reference to TextWatcher for proper removal.
    private var textWatcher: TextWatcher? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        WindowCompat.setDecorFitsSystemWindows(window, false)

        setContentView(R.layout.activity_main)

        ViewCompat.setOnApplyWindowInsetsListener(findViewById(android.R.id.content)) { view, windowInsets ->
            val insets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            view.updatePadding(
                left = insets.left,
                top = insets.top,
                right = insets.right,
                bottom = insets.bottom
            )
            val surfaceHeight = resources.displayMetrics.heightPixels - insets.top - insets.bottom
            paperLib.paperResize(resources.displayMetrics.widthPixels, surfaceHeight)
            needsReshape = true
            windowInsets
        }

        vulkanSurface = findViewById(R.id.vulkan_surface)
        vulkanSurface.setZOrderOnTop(false)
        vulkanSurface.holder.setFormat(android.graphics.PixelFormat.RGBA_8888)
        vulkanSurface.holder.addCallback(this)

        vulkanSurface.isFocusable = true
        vulkanSurface.isFocusableInTouchMode = true
        vulkanSurface.setOnClickListener {
            vulkanSurface.requestFocus()
            showIme()
        }

        imeInput = findViewById(R.id.ime_input)
        imeInput.isFocusable = true
        imeInput.isFocusableInTouchMode = true
        imeInput.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_IME_MULTI_LINE
        imeInput.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
        
        // Set our custom InputConnection.
        val customInputConnection = PaperInputConnection(imeInput, false)
        imeInput.setCustomInputConnection(customInputConnection)
        
        imeInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_DONE ||
                actionId == EditorInfo.IME_ACTION_NEXT ||
                actionId == EditorInfo.IME_ACTION_UNSPECIFIED
            ) {
                commitEnter()
                true
            } else false
        }

        // Use TextWatcher as backup for composition events that don't go through InputConnection.
        textWatcher = object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}

            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                if (s == null || isComposing || updatingFromNative) return
                val newText = s.toString()
                if (newText != documentText) {
                    // Full sync: native is authoritative, but we also need to sync Kotlin state.
                    // For non-composing text changes, we sync from EditText to native.
                    syncFromEditText(newText)
                }
            }

            override fun afterTextChanged(s: Editable?) {}
        }
        imeInput.addTextChangedListener(textWatcher!!)

        paperLib.paperSetAssetManager(assets)
        paperLib.paperSetSize(DefaultPaperSizeId)

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
        }

        val sizeButton = findViewById<Button>(R.id.size_button)
        sizeButton.text = PaperSizes[currentSizeIndex].name
        sizeButton.setOnClickListener {
            currentSizeIndex = (currentSizeIndex + 1) % PaperSizes.size
            val size = PaperSizes[currentSizeIndex]
            sizeButton.text = size.name
            paperLib.paperSetSize(size.id)
            updatePaperDimensions(size)
            needsReshape = true
        }
    }

    private fun updatePaperDimensions(size: PaperSize) {
        currentPaperWidthMm = size.widthMm
        currentPaperHeightMm = size.heightMm
    }

    private fun syncFromEditText(newText: String) {
        documentText = newText
        // Update native document to match.
        paperLib.paperTextSetDocument(newText)
        // Cursor position from EditText selection.
        val selStart = imeInput.selectionStart
        val selEnd = imeInput.selectionEnd
        if (selStart >= 0 && selStart <= documentText.length) {
            documentCursor = selStart
            paperLib.paperTextSetCursor(documentCursor)
        }
        needsReshape = true
    }

    private fun commitEnter() {
        paperLib.paperTextEnter()
        // Sync native cursor back to Kotlin.
        documentCursor = paperLib.paperTextGetCursorCluster()
        needsReshape = true
    }

    private fun commitBackspace() {
        paperLib.paperTextBackspace()
        documentCursor = paperLib.paperTextGetCursorCluster()
        needsReshape = true
    }

    private fun performReshapeIfNeeded() {
        if (!needsReshape) return
        needsReshape = false

        val useMyanmar = documentText.any { it.code > 0x7F }
        val fontName = if (useMyanmar) "fonts/NotoSansMyanmar-VF.ttf" else "fonts/PatrickHand-Regular.ttf"
        val script = if (useMyanmar) "Mymr" else "latn"
        val language = if (useMyanmar) "my" else "en"

        val json = paperLib.paperShapeText(fontName, documentText, 24.0f, "ltr", script, language)
        parseShapedRun(json)

        buildGpuInstances(useMyanmar)

        val glyphIds = shapedGlyphIds.distinct().toIntArray()
        if (glyphIds.isNotEmpty()) {
            val atlasJson = paperLib.paperTextPrepareAtlas(glyphIds, 24.0f, 4, fontName)
            parseAtlasMeta(atlasJson)
            // Rebuild instances with atlas rects now that we have them.
            buildGpuInstances(useMyanmar)
        }

        paperLib.paperTextUploadInstances(gpuInstances, gpuInstanceCount)
        paperLib.paperTextSetAtlas()
    }

    private fun parseShapedRun(json: String) {
        val gcMatch = Regex("""\"glyph_count\":(\d+)""").find(json)
        shapedGlyphCount = if (gcMatch != null) gcMatch.groupValues[1].toInt() else 0
        val ccMatch = Regex("""\"cluster_count\":(\d+)""").find(json)
        shapedClusterCount = if (ccMatch != null) ccMatch.groupValues[1].toInt() else 0

        shapedGlyphIds = IntArray(shapedGlyphCount)
        shapedXPos = FloatArray(shapedGlyphCount)
        shapedYPos = FloatArray(shapedGlyphCount)
        shapedXAdvance = FloatArray(shapedGlyphCount)
        shapedClusterIds = IntArray(shapedGlyphCount)

        val instanceRegex = Regex("""\{"gid":(\d+),"x":([-\d.]+),"y":([-\d.]+),"xa":([-\d.]+),"ya":([-\d.]+),"xo":([-\d.]+),"yo":([-\d.]+),"cluster":(\d+)\}""")
        var idx = 0
        for (match in instanceRegex.findAll(json)) {
            if (idx >= shapedGlyphCount) break
            shapedGlyphIds[idx] = match.groupValues[1].toInt()
            shapedXPos[idx] = match.groupValues[2].toFloat()
            shapedYPos[idx] = match.groupValues[3].toFloat()
            shapedXAdvance[idx] = match.groupValues[4].toFloat()
            shapedClusterIds[idx] = match.groupValues[8].toInt()
            idx++
        }
    }

    private data class AtlasGlyphEntry(
        val gid: Int, val x: Int, val y: Int, val w: Int, val h: Int,
        val advanceX: Float, val bearingX: Float
    )

    private fun parseAtlasMeta(json: String) {
        val wMatch = Regex("""\"width\":(\d+)""").find(json)?.groupValues?.get(1)?.toIntOrNull() ?: 0
        val hMatch = Regex("""\"height\":(\d+)""").find(json)?.groupValues?.get(1)?.toIntOrNull() ?: 0
        atlasWidth = wMatch
        atlasHeight = hMatch
        atlasGlyphs = mutableListOf()
        val glyphRegex = Regex("""\{"gid":(\d+),"x":(\d+),"y":(\d+),"w":(\d+),"h":(\d+),"advance_x":([-\d.]+),"bearing_x":([-\d.]+)\}""")
        for (match in glyphRegex.findAll(json)) {
            atlasGlyphs = atlasGlyphs + AtlasGlyphEntry(
                match.groupValues[1].toInt(),
                match.groupValues[2].toInt(),
                match.groupValues[3].toInt(),
                match.groupValues[4].toInt(),
                match.groupValues[5].toInt(),
                match.groupValues[6].toFloat(),
                match.groupValues[7].toFloat()
            )
        }
    }

    private fun buildGpuInstances(useMyanmar: Boolean) {
        // Use actual paper dimensions for mm-to-px conversion.
        val mmToPxX = resources.displayMetrics.widthPixels.toFloat() / currentPaperWidthMm
        val mmToPxY = resources.displayMetrics.heightPixels.toFloat() / currentPaperHeightMm
        val ruleGapPx = currentRuleGapMm * mmToPxY
        val ruleTopPx = currentRuleTopMm * mmToPxY
        val marginXPx = currentMarginXMm * mmToPxX
        val fontSizePx = 24.0f
        val lineWidthPx = (currentPaperWidthMm - currentMarginXMm * 2f) * mmToPxX

        val instances = mutableListOf<Float>()
        var lineIdx = 0
        var baselineY = ruleTopPx
        var clusterIdx = 0
        val maxLines = 40

        while (clusterIdx < shapedGlyphCount && lineIdx < maxLines) {
            var lineX = marginXPx
            var clustersOnLine = 0

            while (clusterIdx < shapedGlyphCount) {
                val advance = shapedXAdvance[clusterIdx]
                val clusterAdvance = if (advance > 0) advance else fontSizePx * 0.6f

                if (clustersOnLine > 0 && lineX + clusterAdvance > lineWidthPx + marginXPx) {
                    break
                }

                val gid = shapedGlyphIds[clusterIdx]
                val ag = atlasGlyphs.find { it.gid == gid }
                val ax = if (ag != null) ag.x.toFloat() else 0f
                val ay = if (ag != null) ag.y.toFloat() else 0f
                val aw = if (ag != null) ag.w.toFloat() else fontSizePx * 0.5f
                val ah = if (ag != null) ag.h.toFloat() else fontSizePx * 0.8f

                val gx = lineX
                val gy = baselineY - ah

                instances += gx
                instances += gy
                instances += aw
                instances += ah
                instances += ax
                instances += ay
                instances += aw
                instances += ah
                instances += gid.toFloat()
                instances += shapedClusterIds[clusterIdx].toFloat()
                instances += 0f
                instances += 0f

                lineX += clusterAdvance
                clusterIdx++
                clustersOnLine++
            }

            baselineY += ruleGapPx
            lineIdx++
        }

        gpuInstances = instances.toFloatArray()
        gpuInstanceCount = shapedGlyphCount
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val surface = holder.surface
        if (surface.isValid) {
            paperLib.paperInitWithSurface(surface)
            paperLib.paperSetSize(DefaultPaperSizeId)
        }
        startRenderLoop()
        vulkanSurface.requestFocus()
        showIme()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        paperLib.paperResize(width, height)
        needsReshape = true
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
        showIme()
    }

    override fun onDestroy() {
        super.onDestroy()
        stopRenderLoop()
        paperLib.paperShutdown()
    }

    private fun showIme() {
        val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        imeInput.requestFocus()
        imm.showSoftInput(imeInput, InputMethodManager.SHOW_IMPLICIT)
    }

    private fun startRenderLoop() {
        if (isRendering) return
        isRendering = true
        lastFrameTimeNanos = System.nanoTime()
        val callback = object : Choreographer.FrameCallback {
            override fun doFrame(frameTimeNanos: Long) {
                if (isRendering) {
                    val dt = (frameTimeNanos - lastFrameTimeNanos) / 1_000_000_000.0
                    lastFrameTimeNanos = frameTimeNanos

                    performReshapeIfNeeded()
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

    // Custom InputConnection that forwards all text changes to native.
    inner class PaperInputConnection(private val targetView: View, private val fullEditor: Boolean) : BaseInputConnection(targetView, fullEditor) {

        override fun commitText(text: CharSequence?, newCursorPosition: Int): Boolean {
            if (text == null) return false
            isComposing = false
            val str = text.toString()
            Log.d("PaperInput", "commitText: '$str', cursor=$newCursorPosition")
            
            // Forward to native.
            paperLib.paperTextInsert(str)
            
            // Sync Kotlin state from native.
            documentText = paperLib.paperTextGetDocument()
            documentCursor = paperLib.paperTextGetCursorCluster()
            
            // Update EditText to match.
            updateEditTextFromNative()
            needsReshape = true
            return true
        }

        override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
            Log.d("PaperInput", "deleteSurroundingText: before=$beforeLength, after=$afterLength")
            if (beforeLength > 0) {
                repeat(beforeLength) { paperLib.paperTextBackspace() }
            }
            if (afterLength > 0) {
                repeat(afterLength) { paperLib.paperTextDeleteForward() }
            }
            documentText = paperLib.paperTextGetDocument()
            documentCursor = paperLib.paperTextGetCursorCluster()
            updateEditTextFromNative()
            needsReshape = true
            return true
        }

        override fun sendKeyEvent(event: KeyEvent): Boolean {
            if (event.action == KeyEvent.ACTION_DOWN) {
                when (event.keyCode) {
                    KeyEvent.KEYCODE_DEL -> {
                        commitBackspace()
                        return true
                    }
                    KeyEvent.KEYCODE_ENTER -> {
                        commitEnter()
                        return true
                    }
                    KeyEvent.KEYCODE_DPAD_LEFT -> {
                        paperLib.paperTextMoveLeft()
                        documentCursor = paperLib.paperTextGetCursorCluster()
                        updateEditTextFromNative()
                        needsReshape = true
                        return true
                    }
                    KeyEvent.KEYCODE_DPAD_RIGHT -> {
                        paperLib.paperTextMoveRight()
                        documentCursor = paperLib.paperTextGetCursorCluster()
                        updateEditTextFromNative()
                        needsReshape = true
                        return true
                    }
                    KeyEvent.KEYCODE_DPAD_UP -> {
                        paperLib.paperTextMoveUp()
                        documentCursor = paperLib.paperTextGetCursorCluster()
                        updateEditTextFromNative()
                        needsReshape = true
                        return true
                    }
                    KeyEvent.KEYCODE_DPAD_DOWN -> {
                        paperLib.paperTextMoveDown()
                        documentCursor = paperLib.paperTextGetCursorCluster()
                        updateEditTextFromNative()
                        needsReshape = true
                        return true
                    }
                    KeyEvent.KEYCODE_MOVE_HOME -> {
                        paperLib.paperTextMoveToLineStart()
                        documentCursor = paperLib.paperTextGetCursorCluster()
                        updateEditTextFromNative()
                        needsReshape = true
                        return true
                    }
                    KeyEvent.KEYCODE_MOVE_END -> {
                        paperLib.paperTextMoveToLineEnd()
                        documentCursor = paperLib.paperTextGetCursorCluster()
                        updateEditTextFromNative()
                        needsReshape = true
                        return true
                    }
                }
            }
            return super.sendKeyEvent(event)
        }

        override fun setComposingText(text: CharSequence?, newCursorPosition: Int): Boolean {
            if (text == null) {
                isComposing = false
                paperLib.paperTextClearComposing()
            } else {
                isComposing = true
                val str = text.toString()
                Log.d("PaperInput", "setComposingText: '$str', cursor=$newCursorPosition")
                paperLib.paperTextSetComposing(str)
            }
            return true
        }

        override fun finishComposingText(): Boolean {
            isComposing = false
            paperLib.paperTextClearComposing()
            documentText = paperLib.paperTextGetDocument()
            documentCursor = paperLib.paperTextGetCursorCluster()
            updateEditTextFromNative()
            needsReshape = true
            return true
        }

        override fun getTextBeforeCursor(n: Int, flags: Int): CharSequence? {
            val start = max(0, documentCursor - n)
            return documentText.substring(start, documentCursor)
        }

        override fun getTextAfterCursor(n: Int, flags: Int): CharSequence? {
            val end = min(documentText.length, documentCursor + n)
            return documentText.substring(documentCursor, end)
        }

        override fun getSelectedText(flags: Int): CharSequence? {
            val selStart = imeInput.selectionStart
            val selEnd = imeInput.selectionEnd
            if (selStart >= 0 && selEnd >= 0 && selStart != selEnd) {
                val start = min(selStart, selEnd)
                val end = max(selStart, selEnd)
                return documentText.substring(start, end)
            }
            return null
        }

        override fun performEditorAction(actionId: Int): Boolean {
            if (actionId == EditorInfo.IME_ACTION_DONE ||
                actionId == EditorInfo.IME_ACTION_NEXT ||
                actionId == EditorInfo.IME_ACTION_UNSPECIFIED
            ) {
                commitEnter()
                return true
            }
            return super.performEditorAction(actionId)
        }
    }

    private fun updateEditTextFromNative() {
        updatingFromNative = true
        documentText = paperLib.paperTextGetDocument()
        documentCursor = paperLib.paperTextGetCursorCluster()
        // Update EditText text and cursor without triggering TextWatcher.
        textWatcher?.let { imeInput.removeTextChangedListener(it) }
        imeInput.setText(documentText)
        imeInput.setSelection(documentCursor.coerceIn(0, documentText.length))
        textWatcher?.let { imeInput.addTextChangedListener(it) }
        updatingFromNative = false
    }

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

val PaperSizes = listOf(
    PaperSize("A3",   "A3",   297f, 420f),
    PaperSize("A4",   "A4",   210f, 297f),
    PaperSize("A5",   "A5",   148f, 210f),
    PaperSize("A6",   "A6",   105f, 148f),
    PaperSize("B5",   "B5",   176f, 250f),
    PaperSize("LETTER", "Letter", 215.9f, 279.4f),
    PaperSize("LEGAL",  "Legal",  215.9f, 355.6f),
)

val DefaultPaperSizeId = "A4"

data class PaperSize(
    val id: String,
    val name: String,
    val widthMm: Float,
    val heightMm: Float
) {
    val aspectRatio: Float
        get() = widthMm / heightMm
}