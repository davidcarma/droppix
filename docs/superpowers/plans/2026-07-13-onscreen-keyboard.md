# On-screen Keyboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the Android client type on the host with the system soft keyboard — an overlay toggle button shows the IME, whose committed characters are translated to evdev keycodes and sent through the existing `KeyListener → client.sendKey` path.

**Architecture:** `GlDisplayView` becomes a text editor (`onCheckIsTextEditor`); a custom `KeyInputConnection` intercepts `commitText`/`deleteSurroundingText` and maps each character to an evdev keycode (+ Shift) via a standalone `CharMap`, reusing the physical-keyboard `sendKey` path. **Android-only. No wire/host/protocol change.**

**Tech Stack:** Kotlin/Android, Gradle.

## Global Constraints

- **Android-only, no wire/host change.** Reuse the existing `GlDisplayView.KeyListener` (`onKey(keycode, action)`) → `client.sendKey`. Do NOT add a message, touch the protocol, or change the host/Linux client.
- **evdev keycodes (verbatim):** `KEY_LEFTSHIFT = 42`, `KEY_BACKSPACE = 14`, `KEY_DELETE = 111`, `KEY_SPACE = 57`, `KEY_ENTER = 28`, `KEY_TAB = 15`. Letter/digit/symbol codes are in the `CharMap` table in Task 1 — copy those numbers verbatim (they match `input-event-codes.h` and the physical-keyboard `KeyMap`).
- **A shifted char emits `Shift↓ → key↓ → key↑ → Shift↑`.** `action`: `1`=down, `0`=up (same as `sendKey`).
- **Per-character delivery:** the InputConnection sets `inputType = TYPE_CLASS_TEXT or TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or TYPE_TEXT_FLAG_NO_SUGGESTIONS` to disable predictive composing, so the IME calls `commitText` per character.
- **US-QWERTY assumption** (host applies its own layout, same as the physical-keyboard path). Unmappable chars (emoji, accented, non-Latin) are skipped.
- **Window:** `SOFT_INPUT_ADJUST_NOTHING` so the keyboard overlays without resizing/panning the GL surface.
- **Build env** (repo on CIFS no-exec mount): `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew <task>'` — `sh gradlew` (not `./gradlew`), set `ANDROID_HOME`.
- Work on branch `feat/onscreen-keyboard` (off `master`). Commit after each task.

---

### Task 1: `CharMap` — char → (evdev, shift)

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/ui/CharMap.kt`
- Test: `android/app/src/test/java/com/droppix/app/ui/CharMapTest.kt`

**Interfaces:**
- Produces: `object CharMap { fun toEvdev(c: Char): Pair<Int, Boolean>? }` — `(evdevKeycode, needsShift)`, or `null` for unmappable.

- [ ] **Step 1: Write the failing test** — create `CharMapTest.kt`

```kotlin
package com.droppix.app.ui
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
class CharMapTest {
    @Test fun letters() {
        assertEquals(30 to false, CharMap.toEvdev('a'))
        assertEquals(30 to true,  CharMap.toEvdev('A'))
        assertEquals(44 to false, CharMap.toEvdev('z'))
        assertEquals(50 to false, CharMap.toEvdev('m'))
    }
    @Test fun digitsAndShiftedDigits() {
        assertEquals(2  to false, CharMap.toEvdev('1'))
        assertEquals(11 to false, CharMap.toEvdev('0'))
        assertEquals(2  to true,  CharMap.toEvdev('!'))
        assertEquals(11 to true,  CharMap.toEvdev(')'))
    }
    @Test fun symbols() {
        assertEquals(53 to false, CharMap.toEvdev('/'))
        assertEquals(53 to true,  CharMap.toEvdev('?'))
        assertEquals(12 to false, CharMap.toEvdev('-'))
        assertEquals(12 to true,  CharMap.toEvdev('_'))
    }
    @Test fun whitespace() {
        assertEquals(57 to false, CharMap.toEvdev(' '))
        assertEquals(28 to false, CharMap.toEvdev('\n'))
        assertEquals(15 to false, CharMap.toEvdev('\t'))
    }
    @Test fun unmapped() {
        assertNull(CharMap.toEvdev('é'))
        assertNull(CharMap.toEvdev('£'))
        assertNull(CharMap.toEvdev('中'))
    }
}
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*CharMapTest*"'`
Expected: FAIL (unresolved `CharMap`).

- [ ] **Step 3: Implement** — create `CharMap.kt`

```kotlin
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
```

- [ ] **Step 4: Run to verify PASS** — same as Step 2. Expected: PASS (all 5 tests).

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/CharMap.kt android/app/src/test/java/com/droppix/app/ui/CharMapTest.kt
git commit -m "feat(android): CharMap char->evdev+shift (US-QWERTY)"
```

---

### Task 2: `GlDisplayView` — InputConnection capture

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/ui/GlDisplayView.kt`

**Interfaces:**
- Consumes: `CharMap.toEvdev` (Task 1); the existing `keyListener` field + `KeyListener.onKey(keycode, action)`.
- Produces: `GlDisplayView.onCheckIsTextEditor()` / `onCreateInputConnection()` that feed the soft keyboard into `keyListener`.

- [ ] **Step 1: Read the existing key path.** In `GlDisplayView.kt`, find the `@Volatile private var keyListener: KeyListener?` field and the `KeyListener` interface (from the physical-keyboard feature). The new code reuses `keyListener` to emit key events. Note the imports block.

- [ ] **Step 2: Implement.** Add imports as needed (`android.text.InputType`, `android.view.inputmethod.BaseInputConnection`, `android.view.inputmethod.EditorInfo`, `android.view.inputmethod.InputConnection`, `android.view.View`). Add to `GlDisplayView`:

```kotlin
override fun onCheckIsTextEditor(): Boolean = true

override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
    // VISIBLE_PASSWORD + NO_SUGGESTIONS disables predictive composing, so the IME commits
    // each character immediately (commitText per char) instead of a composing region we'd
    // have to reconcile. NO_FULLSCREEN keeps the extract editor from covering the stream.
    outAttrs.inputType = InputType.TYPE_CLASS_TEXT or
        InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or
        InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
    outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
    return KeyInputConnection(this, false)
}

// Soft-keyboard input arrives as committed text / deletions (not KeyEvents). Translate each
// char to an evdev keycode (+ Shift) and push it through the same keyListener the physical
// keyboard uses. Enter/Del that an IME sends as real KeyEvents flow through onKeyDown/onKeyUp
// via BaseInputConnection.sendKeyEvent's default, so they need no handling here.
private inner class KeyInputConnection(v: View, full: Boolean) : BaseInputConnection(v, full) {
    override fun commitText(text: CharSequence?, newCursorPosition: Int): Boolean {
        text?.forEach { typeChar(it) }
        return true
    }
    override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
        repeat(beforeLength) { tapKey(14) }   // KEY_BACKSPACE
        repeat(afterLength)  { tapKey(111) }  // KEY_DELETE
        return true
    }
}

private fun tapKey(evdev: Int) {
    val l = keyListener ?: return
    l.onKey(evdev, 1); l.onKey(evdev, 0)
}

private fun typeChar(c: Char) {
    val (evdev, shift) = CharMap.toEvdev(c) ?: return
    val l = keyListener ?: return
    if (shift) l.onKey(42, 1)        // KEY_LEFTSHIFT down
    l.onKey(evdev, 1); l.onKey(evdev, 0)
    if (shift) l.onKey(42, 0)        // KEY_LEFTSHIFT up
}
```

- [ ] **Step 3: Build (IME capture not unit-testable — on-device in Task 4)**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug 2>&1 | tail -6'`
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/GlDisplayView.kt
git commit -m "feat(android): soft-keyboard InputConnection -> sendKey"
```

---

### Task 3: `StreamActivity` — overlay toggle button

**Files:**
- Modify: `android/app/src/main/res/layout/activity_stream.xml`
- Modify: `android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt`

**Interfaces:**
- Consumes: the Task 2 InputConnection (the IME attaches to `surfaceView`).

- [ ] **Step 1: Layout.** In `activity_stream.xml`, add a `Button` `@+id/kbd_overlay_btn` to the topmost `FrameLayout` (a sibling of `settings_overlay_btn`, so it's above the surface). Mirror `settings_overlay_btn`'s style and `layout_gravity`, offset so the two don't overlap (e.g. place the keyboard button next to / left of the Settings button via `layout_marginEnd`). Label it `"Keyboard"` (or a short glyph).

- [ ] **Step 2: `StreamActivity` wiring.** Add imports `android.view.WindowManager` and `android.view.inputmethod.InputMethodManager`.
  - In `onCreate` (after `setContentView`): `window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING)`, and wire the button:
    ```kotlin
    findViewById<Button>(R.id.kbd_overlay_btn).setOnClickListener { toggleSoftKeyboard() }
    ```
  - Add a field `private var imeShown = false` and the method:
    ```kotlin
    private fun toggleSoftKeyboard() {
        val imm = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager
        if (imeShown) {
            imm.hideSoftInputFromWindow(surfaceView.windowToken, 0)
        } else {
            surfaceView.requestFocus()
            imm.showSoftInput(surfaceView, 0)
        }
        imeShown = !imeShown
    }
    ```
    (`surfaceView` already has `isFocusableInTouchMode = true` from the physical-keyboard feature, so it can receive IME focus. Dismissing via Back may desync `imeShown` by one tap — acceptable; the next tap re-syncs.)

- [ ] **Step 3: Build**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug 2>&1 | tail -6'`
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
git add android/app/src/main/res/layout/activity_stream.xml android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt
git commit -m "feat(android): on-screen keyboard overlay toggle button"
```

---

### Task 4: Verification

**Files:** none.

- [ ] **Step 1: Full Android unit tests + build**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest assembleDebug'`
Expected: unit tests green (incl. `CharMapTest`, and the existing `KeyMapTest`/`AppSettingsTest`); `BUILD SUCCESSFUL`.

- [ ] **Step 2: On-device.** Start a stream; tap the overlay **Keyboard** button → the soft keyboard appears over the stream (GL surface not resized/panned). Typing letters/digits/symbols produces the right characters on the host; **capitals** and **shifted symbols** (`!@#…?`) work; **Backspace** deletes; **Enter** newlines; tap the button again (or Back) hides it. The physical-keyboard path and touch/mouse are unaffected.

- [ ] **Step 3: Commit any fixes; otherwise done.**

---

## Self-review notes

- **Spec coverage:** `CharMap` + tests (T1); InputConnection capture (T2); overlay toggle + `adjustNothing` (T3); verification (T4). Every spec section maps to a task.
- **Reuses the shipped path:** `keyListener.onKey(evdev, action)` is the exact interface the physical keyboard uses → no wire/host change.
- **Type/keycode consistency:** `CharMap.toEvdev(Char): Pair<Int,Boolean>?`; Shift=42, Backspace=14, Delete=111, Space=57, Enter=28, Tab=15 — consistent with `KeyMap`/`input-event-codes.h`. Letter array verified (a=30, m=50, z=44).
- **Per-char delivery** via the visible-password/no-suggestions `inputType` avoids composing-region reconciliation.
- **No placeholders:** `CharMap` table and all handler code are complete; the only prose step is the layout button placement (mirrors the existing `settings_overlay_btn`).
