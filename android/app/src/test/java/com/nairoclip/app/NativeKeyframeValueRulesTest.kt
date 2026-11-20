package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class NativeKeyframeValueRulesTest {
    @Test
    fun opacityAcceptsOnlyZeroToOne() {
        assertNull(NativeKeyframeValueRules.validate(0, 0.0))
        assertNull(NativeKeyframeValueRules.validate(0, 1.0))
        assertEquals("透明度关键帧必须在 0..1 范围内", NativeKeyframeValueRules.validate(0, -0.001))
        assertEquals("透明度关键帧必须在 0..1 范围内", NativeKeyframeValueRules.validate(0, 1.001))
    }

    @Test
    fun volumeAcceptsOnlyZeroToFour() {
        assertNull(NativeKeyframeValueRules.validate(5, 0.0))
        assertNull(NativeKeyframeValueRules.validate(5, 4.0))
        assertEquals("音量关键帧必须在 0..4 范围内", NativeKeyframeValueRules.validate(5, -0.001))
        assertEquals("音量关键帧必须在 0..4 范围内", NativeKeyframeValueRules.validate(5, 4.001))
    }

    @Test
    fun transformChannelsRetainTheirCoreDomains() {
        assertNull(NativeKeyframeValueRules.validate(1, 0.05))
        assertNull(NativeKeyframeValueRules.validate(1, 10.0))
        assertNull(NativeKeyframeValueRules.validate(2, -10.0))
        assertNull(NativeKeyframeValueRules.validate(3, 10.0))
        assertNull(NativeKeyframeValueRules.validate(4, 3600.0))
        assertEquals("缩放关键帧引擎值必须在 0.05..10.0（5%–1000%）范围内", NativeKeyframeValueRules.validate(1, 10.001))
    }

    @Test
    fun rejectsNonFiniteValuesAcrossChannels() {
        assertEquals("关键帧值必须是有限数值", NativeKeyframeValueRules.validate(0, Double.NaN))
        assertEquals("关键帧值必须是有限数值", NativeKeyframeValueRules.validate(5, Double.POSITIVE_INFINITY))
        assertEquals("关键帧值必须是有限数值", NativeKeyframeValueRules.validate(4, Double.NEGATIVE_INFINITY))
    }
    @Test
    fun keyframeTimesMustBeNonNegativeAndStrictlyIncreasing() {
        assertEquals(null, NativeKeyframeValueRules.validateTimes(emptyList()))
        assertEquals(null, NativeKeyframeValueRules.validateTimes(listOf(0L, 500_000L, 1_000_000L)))
        assertEquals("关键帧时间不能为负数", NativeKeyframeValueRules.validateTimes(listOf(-1L)))
        assertEquals("关键帧时间必须严格递增且不能重复", NativeKeyframeValueRules.validateTimes(listOf(0L, 500_000L, 500_000L)))
        assertEquals("关键帧时间必须严格递增且不能重复", NativeKeyframeValueRules.validateTimes(listOf(1_000_000L, 500_000L)))
    }

}
