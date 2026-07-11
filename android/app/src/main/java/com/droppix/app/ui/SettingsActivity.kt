package com.droppix.app.ui

import android.app.Activity
import android.os.Bundle
import android.widget.*
import com.droppix.app.R
import com.droppix.app.settings.*

class SettingsActivity : Activity() {
    override fun onCreate(b: Bundle?) {
        super.onCreate(b); setContentView(R.layout.activity_settings)
        val store = SettingsStore(this); val cur = store.load()
        val resSpinner = findViewById<Spinner>(R.id.res_spinner)
        val resItems = listOf("Native") + Resolutions.PRESETS.map { "${it.first}x${it.second}" }
        resSpinner.adapter = lightAdapter(resItems)
        resSpinner.setSelection(
            if (cur.width == 0) 0 else 1 + Resolutions.PRESETS.indexOfFirst { it.first == cur.width && it.second == cur.height }.coerceAtLeast(0))
        val fpsSpinner = findViewById<Spinner>(R.id.fps_spinner)
        val fpsItems = listOf(30, 60)
        fpsSpinner.adapter = lightAdapter(fpsItems.map { it.toString() })
        fpsSpinner.setSelection(fpsItems.indexOf(cur.fps).coerceAtLeast(0))
        val audioSwitch = findViewById<Switch>(R.id.audio_switch); audioSwitch.isChecked = cur.audio
        findViewById<Button>(R.id.save_btn).setOnClickListener {
            val res = if (resSpinner.selectedItemPosition == 0) 0 to 0
                      else Resolutions.PRESETS[resSpinner.selectedItemPosition - 1]
            store.save(AppSettings(res.first, res.second, fpsItems[fpsSpinner.selectedItemPosition], audioSwitch.isChecked))
            finish()
        }
    }

    // Spinner adapter with light text in both the collapsed view and the dropdown popup, so
    // items are readable on the dark Settings background / popupBackground.
    private fun lightAdapter(items: List<String>): ArrayAdapter<String> =
        ArrayAdapter(this, R.layout.spinner_item, items).apply {
            setDropDownViewResource(R.layout.spinner_dropdown_item)
        }
}
