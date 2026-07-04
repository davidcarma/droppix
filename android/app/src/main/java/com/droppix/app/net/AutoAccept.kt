package com.droppix.app.net

/**
 * A tablet auto-accepts a host-initiated WAKE only from a host it has already
 * paired (pinned its TLS cert). An unpaired host still requires the confirm
 * dialog + PIN, so it can never be auto-connected. Named as a seam so the
 * decision is unit-testable apart from ConnectActivity.
 */
fun shouldAutoAccept(isPaired: Boolean): Boolean = isPaired
