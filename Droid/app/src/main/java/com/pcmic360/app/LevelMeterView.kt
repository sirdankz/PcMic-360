package com.pcmic360.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import kotlin.math.min

class LevelMeterView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val track = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF202C3C.toInt() }
    private val level = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF4F91F2.toInt() }
    private val gate = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFF0B84B.toInt()
        strokeWidth = dp(2f)
    }

    private var rms = 0.0
    private var threshold = 220
    private val maxLevel = PcMicService.GATE_MAX_RMS.toDouble()

    fun setLevel(value: Double, gateThreshold: Int) {
        rms = value.coerceAtLeast(0.0)
        threshold = gateThreshold.coerceAtLeast(0)
        invalidate()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val desired = dp(22f).toInt()
        setMeasuredDimension(
            MeasureSpec.getSize(widthMeasureSpec),
            resolveSize(desired, heightMeasureSpec)
        )
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val radius = height / 2f
        val rect = RectF(0f, 0f, width.toFloat(), height.toFloat())
        canvas.drawRoundRect(rect, radius, radius, track)

        val fraction = min(1.0, rms / maxLevel).toFloat()
        if (fraction > 0f) {
            val filled = RectF(0f, 0f, width * fraction, height.toFloat())
            canvas.drawRoundRect(filled, radius, radius, level)
        }

        if (threshold > 0) {
            val x = (width * min(1.0, threshold / maxLevel)).toFloat()
            canvas.drawLine(x, 0f, x, height.toFloat(), gate)
        }
    }

    private fun dp(value: Float): Float = value * resources.displayMetrics.density
}
