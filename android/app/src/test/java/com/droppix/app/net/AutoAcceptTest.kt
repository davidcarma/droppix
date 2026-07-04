package com.droppix.app.net

import org.junit.Assert.*
import org.junit.Test

class AutoAcceptTest {
    @Test fun acceptsWhenPaired() { assertTrue(shouldAutoAccept(true)) }
    @Test fun promptsWhenNotPaired() { assertFalse(shouldAutoAccept(false)) }
}
