package com.droppix.app.net

import android.content.Context
import android.os.Build
import java.util.UUID

object DeviceIdentity {
    fun displayName(ctx: Context): String = Build.MODEL ?: "Android"
    fun stableId(ctx: Context): String {
        val p = ctx.getSharedPreferences("droppix", Context.MODE_PRIVATE)
        return p.getString("device_id", null) ?: UUID.randomUUID().toString().also {
            p.edit().putString("device_id", it).apply()
        }
    }
}
