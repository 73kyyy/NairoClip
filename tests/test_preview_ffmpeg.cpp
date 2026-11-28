#include "test_framework.h"

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"
#include <fstream>
using namespace nairoclip::core;

NC_TEST(PreviewFFmpeg, UnsupportedSystemDecoderDoesNotFallBack) {
    std::ifstream media("/tmp/media_gen/test_av.mp4");
    if (!media.good()) return;
    FFmpegMediaReader reader;
    const Result result = reader.open("/tmp/media_gen/test_av.mp4");
    NC_CHECK(!result.isOk());
    NC_CHECK(!reader.isOpen());
}
#endif
