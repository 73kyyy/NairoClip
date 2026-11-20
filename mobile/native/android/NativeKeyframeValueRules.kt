package com.nairoclip.app

/**
 * Pure value-domain validation for the six shared-core clip animation channels.
 * This is deliberately independent of JNI so boundary behavior can be unit-tested.
 */
internal object NativeKeyframeValueRules {
    fun validate(property: Int, value: Double): String? {
        if (!value.isFinite()) return "关键帧值必须是有限数值"
        return when (property) {
            0 -> if (value !in 0.0..1.0) "透明度关键帧必须在 0..1 范围内" else null
            5 -> if (value !in 0.0..4.0) "音量关键帧必须在 0..4 范围内" else null
            NativeParameter.SCALE.engineProperty -> {
                val min = NativeTransformRange.SCALE_MIN_PERCENT.toDouble() / 100.0
                val max = NativeTransformRange.SCALE_MAX_PERCENT.toDouble() / 100.0
                if (value !in min..max) "缩放关键帧引擎值必须在 0.05..10.0（5%–1000%）范围内" else null
            }
            NativeParameter.X.engineProperty, NativeParameter.Y.engineProperty ->
                if (value !in NativeTransformRange.POSITION_MIN.toDouble()..NativeTransformRange.POSITION_MAX.toDouble())
                    "位置关键帧必须在 -10..10 范围内" else null
            NativeParameter.ROTATION.engineProperty ->
                if (value !in NativeTransformRange.ROTATION_MIN_DEGREES.toDouble()..NativeTransformRange.ROTATION_MAX_DEGREES.toDouble())
                    "旋转关键帧必须在 -3600°..3600° 范围内" else null
            else -> null
        }
    }

}
