package com.droppix.app.ui

import android.content.Context
import android.util.AttributeSet
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

class DisplaySurfaceView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    private var surfaceCb: ((Surface) -> Unit)? = null

    init { holder.addCallback(this) }

    fun awaitSurface(cb: (Surface) -> Unit) {
        val s = holder.surface
        if (s != null && s.isValid) cb(s) else surfaceCb = cb
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceCb?.let { it(holder.surface) }
        surfaceCb = null
    }
    override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, ht: Int) {}
    override fun surfaceDestroyed(holder: SurfaceHolder) {}
}
