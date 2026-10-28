package org.mh4u.port.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import org.mh4u.port.overlay.CustomLayoutEditorView

/** Lets the settings open the layout editor of the running game (the game screen registers itself here). */
object LayoutEditor {
    var opener: (() -> Unit)? = null

    /** False when no game is running: there is nothing to lay out. */
    fun open(): Boolean {
        val open = opener ?: return false
        open()
        return true
    }
}

/** The editor over the game, with Save, Reset and Cancel. [onDone] closes it. */
@Composable
fun CustomLayoutEditorHost(onDone: () -> Unit) {
    var editor by remember { mutableStateOf<CustomLayoutEditorView?>(null) }
    Box(Modifier.fillMaxSize()) {
        AndroidView(factory = { CustomLayoutEditorView(it).also { view -> editor = view } }, modifier = Modifier.fillMaxSize())
        Row(
            Modifier.align(Alignment.BottomCenter).padding(12.dp).background(Color(0xCC121214)).padding(8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            AmberButton("Save") { editor?.saveLayout(); onDone() }
            QuietButton("Reset") { editor?.resetToDefaultLayout() }
            QuietButton("Cancel") { editor?.restoreSavedLayout(); onDone() }
        }
    }
}
