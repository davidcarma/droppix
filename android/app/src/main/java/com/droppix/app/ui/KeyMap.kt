package com.droppix.app.ui
import android.view.KeyEvent
// Android KeyEvent.keyCode -> Linux evdev keycode. 0 = unmapped (caller passes the event through).
object KeyMap {
    fun toEvdev(keyCode: Int): Int = when (keyCode) {
        KeyEvent.KEYCODE_A -> 30; KeyEvent.KEYCODE_B -> 48; KeyEvent.KEYCODE_C -> 46
        KeyEvent.KEYCODE_D -> 32; KeyEvent.KEYCODE_E -> 18; KeyEvent.KEYCODE_F -> 33
        KeyEvent.KEYCODE_G -> 34; KeyEvent.KEYCODE_H -> 35; KeyEvent.KEYCODE_I -> 23
        KeyEvent.KEYCODE_J -> 36; KeyEvent.KEYCODE_K -> 37; KeyEvent.KEYCODE_L -> 38
        KeyEvent.KEYCODE_M -> 50; KeyEvent.KEYCODE_N -> 49; KeyEvent.KEYCODE_O -> 24
        KeyEvent.KEYCODE_P -> 25; KeyEvent.KEYCODE_Q -> 16; KeyEvent.KEYCODE_R -> 19
        KeyEvent.KEYCODE_S -> 31; KeyEvent.KEYCODE_T -> 20; KeyEvent.KEYCODE_U -> 22
        KeyEvent.KEYCODE_V -> 47; KeyEvent.KEYCODE_W -> 17; KeyEvent.KEYCODE_X -> 45
        KeyEvent.KEYCODE_Y -> 21; KeyEvent.KEYCODE_Z -> 44
        KeyEvent.KEYCODE_1 -> 2; KeyEvent.KEYCODE_2 -> 3; KeyEvent.KEYCODE_3 -> 4
        KeyEvent.KEYCODE_4 -> 5; KeyEvent.KEYCODE_5 -> 6; KeyEvent.KEYCODE_6 -> 7
        KeyEvent.KEYCODE_7 -> 8; KeyEvent.KEYCODE_8 -> 9; KeyEvent.KEYCODE_9 -> 10
        KeyEvent.KEYCODE_0 -> 11
        KeyEvent.KEYCODE_GRAVE -> 41; KeyEvent.KEYCODE_MINUS -> 12; KeyEvent.KEYCODE_EQUALS -> 13
        KeyEvent.KEYCODE_LEFT_BRACKET -> 26; KeyEvent.KEYCODE_RIGHT_BRACKET -> 27
        KeyEvent.KEYCODE_BACKSLASH -> 43; KeyEvent.KEYCODE_SEMICOLON -> 39
        KeyEvent.KEYCODE_APOSTROPHE -> 40; KeyEvent.KEYCODE_COMMA -> 51
        KeyEvent.KEYCODE_PERIOD -> 52; KeyEvent.KEYCODE_SLASH -> 53
        KeyEvent.KEYCODE_SPACE -> 57; KeyEvent.KEYCODE_ENTER -> 28
        KeyEvent.KEYCODE_DEL -> 14; KeyEvent.KEYCODE_FORWARD_DEL -> 111
        KeyEvent.KEYCODE_TAB -> 15; KeyEvent.KEYCODE_ESCAPE -> 1
        KeyEvent.KEYCODE_SHIFT_LEFT -> 42; KeyEvent.KEYCODE_SHIFT_RIGHT -> 54
        KeyEvent.KEYCODE_CTRL_LEFT -> 29; KeyEvent.KEYCODE_CTRL_RIGHT -> 97
        KeyEvent.KEYCODE_ALT_LEFT -> 56; KeyEvent.KEYCODE_ALT_RIGHT -> 100
        KeyEvent.KEYCODE_META_LEFT -> 125; KeyEvent.KEYCODE_META_RIGHT -> 126
        KeyEvent.KEYCODE_CAPS_LOCK -> 58
        KeyEvent.KEYCODE_DPAD_UP -> 103; KeyEvent.KEYCODE_DPAD_DOWN -> 108
        KeyEvent.KEYCODE_DPAD_LEFT -> 105; KeyEvent.KEYCODE_DPAD_RIGHT -> 106
        KeyEvent.KEYCODE_MOVE_HOME -> 102; KeyEvent.KEYCODE_MOVE_END -> 107
        KeyEvent.KEYCODE_PAGE_UP -> 104; KeyEvent.KEYCODE_PAGE_DOWN -> 109
        KeyEvent.KEYCODE_INSERT -> 110
        KeyEvent.KEYCODE_F1 -> 59; KeyEvent.KEYCODE_F2 -> 60; KeyEvent.KEYCODE_F3 -> 61
        KeyEvent.KEYCODE_F4 -> 62; KeyEvent.KEYCODE_F5 -> 63; KeyEvent.KEYCODE_F6 -> 64
        KeyEvent.KEYCODE_F7 -> 65; KeyEvent.KEYCODE_F8 -> 66; KeyEvent.KEYCODE_F9 -> 67
        KeyEvent.KEYCODE_F10 -> 68; KeyEvent.KEYCODE_F11 -> 87; KeyEvent.KEYCODE_F12 -> 88
        else -> 0
    }
}
