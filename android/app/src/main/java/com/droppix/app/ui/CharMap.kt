package com.droppix.app.ui

// US-QWERTY printable char -> (evdev keycode, needsShift). null = unmappable (emoji,
// accented, non-Latin). Reused by the on-screen keyboard's InputConnection; the host
// applies its own layout to the keycode, same as the physical-keyboard path.
object CharMap {
    // evdev codes for a..z (KEY_A=30, KEY_B=48, ...), index = c - 'a'.
    private val LETTER = intArrayOf(
        30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
        49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44)

    fun toEvdev(c: Char): Pair<Int, Boolean>? = when (c) {
        in 'a'..'z' -> LETTER[c - 'a'] to false
        in 'A'..'Z' -> LETTER[c - 'A'] to true
        in '1'..'9' -> (2 + (c - '1')) to false
        '0' -> 11 to false
        '!' -> 2 to true;  '@' -> 3 to true;  '#' -> 4 to true;  '$' -> 5 to true;  '%' -> 6 to true
        '^' -> 7 to true;  '&' -> 8 to true;  '*' -> 9 to true;  '(' -> 10 to true; ')' -> 11 to true
        '-' -> 12 to false; '_' -> 12 to true
        '=' -> 13 to false; '+' -> 13 to true
        '[' -> 26 to false; '{' -> 26 to true
        ']' -> 27 to false; '}' -> 27 to true
        '\\' -> 43 to false; '|' -> 43 to true
        ';' -> 39 to false; ':' -> 39 to true
        '\'' -> 40 to false; '"' -> 40 to true
        '`' -> 41 to false; '~' -> 41 to true
        ',' -> 51 to false; '<' -> 51 to true
        '.' -> 52 to false; '>' -> 52 to true
        '/' -> 53 to false; '?' -> 53 to true
        ' ' -> 57 to false
        '\n' -> 28 to false
        '\t' -> 15 to false
        else -> null
    }
}
