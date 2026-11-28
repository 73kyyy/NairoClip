#include "test_framework.h"

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"
#include <fstream>
using namespace nairoclip::core;

namespace {
const char* kTestMedia = "/tmp/media_gen/test_av.mp4";
bool testMediaExists() {
    std::ifstream f(kTestMedia);
    return f.good();
}
}

NC_TEST(MediaFFmpeg, BundledSoftwareDecoderIsNeverUsed) {
    if (!testMediaExists()) return;
    FFmpegMediaReader reader;
    const Result result = reader.open(kTestMedia);
    NC_CHECK(!result.isOk());
    NC_CHECK(!reader.isOpen());
}

NC_TEST(MediaFFmpeg, OpenNonexistentFails) {
    FFmpegMediaReader reader;
    const Result result = reader.open("/tmp/does_not_exist_xyz.mp4");
    NC_CHECK(!result.isOk());
    NC_CHECK(!reader.isOpen());
}

NC_TEST(MediaFFmpeg, AvailabilityReflectsContainerSupport) {
    NC_CHECK(ffmpegAvailable());
}
#else
NC_TEST(MediaFFmpeg, NotCompiled) {
    NC_CHECK(true);
}
#endif
