package com.droppix.app.ui
import android.view.KeyEvent
import org.junit.Assert.assertEquals
import org.junit.Test
class KeyMapTest {
    @Test fun mapsCommonKeys() {
        assertEquals(30, KeyMap.toEvdev(KeyEvent.KEYCODE_A))        // KEY_A
        assertEquals(28, KeyMap.toEvdev(KeyEvent.KEYCODE_ENTER))    // KEY_ENTER
        assertEquals(29, KeyMap.toEvdev(KeyEvent.KEYCODE_CTRL_LEFT))// KEY_LEFTCTRL
        assertEquals(2,  KeyMap.toEvdev(KeyEvent.KEYCODE_1))        // KEY_1
        assertEquals(57, KeyMap.toEvdev(KeyEvent.KEYCODE_SPACE))    // KEY_SPACE
    }
    @Test fun unmappedReturnsZero() {
        assertEquals(0, KeyMap.toEvdev(KeyEvent.KEYCODE_VOLUME_UP))
    }
}
