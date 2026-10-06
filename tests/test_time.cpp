#include "test_framework.h"
#include "nairoclip/core/time.h"
using namespace nairoclip::core;
NC_TEST(Time, FrameConversions) {
    const FrameRate fps = fps30();
    NC_CHECK(timeAtFrame(fps, 0) == Rational::make(0));
    NC_CHECK(timeAtFrame(fps, 30) == Rational::make(1));
    NC_CHECK(timeAtFrame(fps, 15) == Rational::make(1, 2));
    NC_CHECK_EQ(frameAtTime(fps, Rational::make(1)), 30);
    NC_CHECK_EQ(frameAtTime(fps, Rational::make(3, 2)), 45);
}
NC_TEST(Time, FractionalFrameRate) {
    const FrameRate fps = fps2997();
    NC_CHECK(fps.toDouble() > 29.97 && fps.toDouble() < 29.98);
    NC_CHECK(timeAtFrame(fps, 30000) == Rational::make(1001));
    NC_CHECK_EQ(frameAtTime(fps, Rational::make(1001)), 30000);
    NC_CHECK_EQ(frameAtTime(fps, Rational::make(1)), 29);
}
NC_TEST(Time, Microseconds) {
    NC_CHECK_EQ(toMicroseconds(fromSeconds(1)), 1000000);
    NC_CHECK_EQ(toMicroseconds(fromMilliseconds(1500)), 1500000);
    NC_CHECK(fromMicroseconds(250000) == Rational::make(1, 4));
}
NC_TEST(Time, Formatting) {
    NC_CHECK_EQ(formatClock(fromMilliseconds(61500)), std::string("01:01.500"));
    NC_CHECK_EQ(formatClock(Rational::make(3661500, 1000)), std::string("01:01:01.500"));
    NC_CHECK_EQ(formatTimecode(fps30(), fromSeconds(61) + Rational::make(1, 2)),
                std::string("00:01:01:15"));
}
