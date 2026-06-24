package com.droppix.app.stats

// Cross-thread snapshot of the latest measurements. Writers: net thread (rtt,fps),
// decoder (decodeLag). Reader: UI overlay. Volatile primitives are sufficient.
class StatsSink {
    @Volatile var rttMs: Double = 0.0
    @Volatile var fps: Double = 0.0
    @Volatile var decodeLagMs: Double = 0.0
}
