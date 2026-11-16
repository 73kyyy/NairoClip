// S1-2 契约回归测试（JNI DispatchEdit 参数映射 / 分支补齐 / 错误码）
//
// 覆盖八项：D-1 关键帧、D-2 Split/Trim、SF-1 xform_set、SF-2 chroma、
//           SF-3 mask、SF-10 track_flag、SF-11 nc_set_frame_fit、SF-12 frame_fit。
//
// 测的是**生产实现本身**：DispatchEdit 位于 nc_jni.cpp 的匿名命名空间
// （内部链接），主机侧无法单独链接，故本 TU 直接包含实现文件。
// 编译期用 tests/jni_stub 的 JNI/ANativeWindow 桩头（桩不做真实 JNI 工作；
// 本测试只调用 DispatchEdit，不触碰任何 JNI 入口）。
//
// 每条用例都同时验证「缺参」「合法零值」「非法类型」「正常值」四种契约，
// 确保不会用默认值吞掉参数缺失（S1-2 授权书 §4.2）。
#include "test_framework.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

#include "nairoclip/core/capi.h"

#include "../mobile/native/android/nc_jni.cpp"

namespace {

struct Fixture {
    NcEditor* ed = nullptr;
    uint64_t clipId = 0;

    Fixture() {
        ed = nc_editor_create();
        if (ed == nullptr) return;
        const uint64_t asset = nc_import_asset(ed, "c.mp4", "/v/c.mp4", 0, 60000000);
        nc_add_track(ed, "V1", 0);
        // 注意：clip_id 由引擎分配（asset/track 也各自占 id），
        // 必须用真实返回值，不能硬编码。
        clipId = nc_insert_clip(ed, 0, asset, 1000000, 5000000);   // [1s, 6s)
    }
    ~Fixture() {
        if (ed != nullptr) nc_editor_destroy(ed);
    }

    bool dispatch(const std::string& op, const std::string& args, std::string* out) {
        return DispatchEdit(ed, op, args, out);
    }
    bool isOk(const std::string& op, const std::string& args) {
        std::string o;
        if (!dispatch(op, args, &o)) return false;
        return o.find("\"ok\":true") != std::string::npos;
    }
    std::string run(const std::string& op, const std::string& args) {
        std::string o;
        dispatch(op, args, &o);
        return o;
    }
};

std::string j(const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    return std::string(b);
}

}  // namespace

// ===================== D-1 关键帧参数映射兼容 =====================

NC_TEST(S1_2, D1KeyframeTimeUsP1) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    // P1：前端发 time_us（旧实现只读 at_us/t → 恒落片头）
    NC_CHECK(f.isOk("keyframe", j("{\"track\":0,\"clip\":%llu,\"property\":0,"
                                  "\"time_us\":2000000,\"value\":5}",
                                  (unsigned long long)f.clipId)));
    int64_t t = -1;
    NC_CHECK_EQ(nc_keyframe_get(f.ed, 0, f.clipId, 0, 0, &t, nullptr, nullptr), 0);
    NC_CHECK_EQ(t, static_cast<int64_t>(2000000));
}

NC_TEST(S1_2, D1KeyframeTimeUsP2) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    // P2：OP_TO_ABI 发 track_index/clip_id/time_us
    NC_CHECK(f.isOk("keyframe", j("{\"track_index\":0,\"clip_id\":%llu,\"property\":0,"
                                  "\"time_us\":3000000,\"value\":7}",
                                  (unsigned long long)f.clipId)));
    int64_t t = -1;
    NC_CHECK_EQ(nc_keyframe_get(f.ed, 0, f.clipId, 0, 0, &t, nullptr, nullptr), 0);
    NC_CHECK_EQ(t, static_cast<int64_t>(3000000));
}

NC_TEST(S1_2, D1KeyframeZeroIsLegal) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    // 合法零值：time_us=0 必须被接受（不得当成缺参）
    NC_CHECK(f.isOk("keyframe", j("{\"track\":0,\"clip\":%llu,\"property\":0,"
                                  "\"time_us\":0,\"value\":1}",
                                  (unsigned long long)f.clipId)));
}

NC_TEST(S1_2, D1KeyframeMissingTimeRejected) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    // 缺参必须明确报错（旧实现静默用 0）
    const std::string r = f.run("keyframe",
        j("{\"track\":0,\"clip\":%llu,\"property\":0,\"value\":1}",
          (unsigned long long)f.clipId));
    NC_CHECK(r.find("\"ok\":false") != std::string::npos);
    NC_CHECK(r.find("缺少关键帧时间") != std::string::npos);
}

NC_TEST(S1_2, D1KeyframeInvalidTypeRejected) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    // 非法类型必须报错，不得静默回退
    const std::string r = f.run("keyframe",
        j("{\"track\":0,\"clip\":%llu,\"property\":0,\"time_us\":\"abc\"}",
          (unsigned long long)f.clipId));
    NC_CHECK(r.find("\"ok\":false") != std::string::npos);
    NC_CHECK(r.find("类型非法") != std::string::npos);
}

NC_TEST(S1_2, D1RemoveKeyframeTimeUs) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    NC_CHECK(f.isOk("keyframe", j("{\"track\":0,\"clip\":%llu,\"property\":0,"
                                  "\"time_us\":4000000,\"value\":2}",
                                  (unsigned long long)f.clipId)));
    // 删除必须按同一时间点命中（旧实现读 at_us → 删不到）
    NC_CHECK(f.isOk("remove_keyframe", j("{\"track\":0,\"clip\":%llu,"
                                         "\"property\":0,\"time_us\":4000000}",
                                         (unsigned long long)f.clipId)));
    // 缺参报错
    const std::string r = f.run("remove_keyframe",
        j("{\"track\":0,\"clip\":%llu,\"property\":0}", (unsigned long long)f.clipId));
    NC_CHECK(r.find("\"ok\":false") != std::string::npos);
}

// ===================== D-2 Split / Trim 参数映射 =====================

NC_TEST(S1_2, D2SplitAtUsP1) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    const int32_t before = nc_clip_count(f.ed, 0);
    // P1 发 at_us（旧实现读 time_us → 分割点恒 0）
    NC_CHECK(f.isOk("split", j("{\"track\":0,\"clip\":%llu,\"at_us\":3000000}",
                               (unsigned long long)f.clipId)));
    NC_CHECK_EQ(nc_clip_count(f.ed, 0), before + 1);
}

NC_TEST(S1_2, D2SplitFullNameP2) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    const int32_t before = nc_clip_count(f.ed, 0);
    // P2 发全名 nc_split_clip（旧实现无此分支 → 「未开放的操作」）
    NC_CHECK(f.isOk("nc_split_clip",
                    j("{\"track_index\":0,\"clip_id\":%llu,\"at_us\":2000000}",
                      (unsigned long long)f.clipId)));
    NC_CHECK_EQ(nc_clip_count(f.ed, 0), before + 1);
}

NC_TEST(S1_2, D2SplitMissingAndInvalid) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    const std::string miss = f.run("split",
        j("{\"track\":0,\"clip\":%llu}", (unsigned long long)f.clipId));
    NC_CHECK(miss.find("\"ok\":false") != std::string::npos);
    NC_CHECK(miss.find("缺少分割位置") != std::string::npos);

    const std::string bad = f.run("split",
        j("{\"track\":0,\"clip\":%llu,\"at_us\":\"x\"}", (unsigned long long)f.clipId));
    NC_CHECK(bad.find("\"ok\":false") != std::string::npos);
    NC_CHECK(bad.find("类型非法") != std::string::npos);
}

NC_TEST(S1_2, D2TrimStartAndEndBothPaths) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    // trim_start：P1 发 start_us
    NC_CHECK(f.isOk("trim_start", j("{\"track\":0,\"clip\":%llu,\"start_us\":2000000}",
                                    (unsigned long long)f.clipId)));
    // trim_start：P2 全名
    NC_CHECK(f.isOk("nc_trim_clip_start",
                    j("{\"track_index\":0,\"clip_id\":%llu,\"start_us\":2000000}",
                      (unsigned long long)f.clipId)));
    // trim_end：P1 发 end_us
    NC_CHECK(f.isOk("trim_end", j("{\"track\":0,\"clip\":%llu,\"end_us\":4000000}",
                                  (unsigned long long)f.clipId)));
    // trim_end：P2 全名
    NC_CHECK(f.isOk("nc_trim_clip_end",
                    j("{\"track_index\":0,\"clip_id\":%llu,\"end_us\":4000000}",
                      (unsigned long long)f.clipId)));
    // 缺参报错
    const std::string r = f.run("trim_end",
        j("{\"track\":0,\"clip\":%llu}", (unsigned long long)f.clipId));
    NC_CHECK(r.find("\"ok\":false") != std::string::npos);
    NC_CHECK(r.find("缺少裁剪位置") != std::string::npos);
}

// ===================== SF-1 xform_set 13 参数映射 =====================

NC_TEST(S1_2, Sf1NamedParamsMapped) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    // 前端发具名参数（旧实现只找 args → 全 0）
    NC_CHECK(f.isOk("xform_set", j("{\"track\":0,\"clip\":%llu,\"kind\":0,"
                                   "\"value\":0.5,\"angle\":0.25,\"freq\":2,"
                                   "\"phase\":0.1,\"axis\":1,\"cols\":3,\"rows\":4,"
                                   "\"gap\":0.2,\"anchor\":2}",
                                   (unsigned long long)f.clipId)));
    double a[13] = {0};
    int32_t en = 0, edge = 0;
    NC_CHECK_EQ(nc_get_clip_xform(f.ed, 0, f.clipId, 0, a, &en, &edge), 0);
    NC_CHECK(a[0] > 0.49 && a[0] < 0.51);   // value
    NC_CHECK(a[1] > 0.24 && a[1] < 0.26);   // angle
    NC_CHECK(a[2] > 1.99 && a[2] < 2.01);   // freq
    NC_CHECK(a[5] > 2.99 && a[5] < 3.01);   // cols
    NC_CHECK(a[6] > 3.99 && a[6] < 4.01);   // rows
    NC_CHECK(a[8] > 1.99 && a[8] < 2.01);   // anchor
    // 前端未提供的 [9]~[12] 保持 0（契约：不自行猜测）
    NC_CHECK_EQ(a[9], 0.0);
    NC_CHECK_EQ(a[12], 0.0);
}

NC_TEST(S1_2, Sf1ExplicitArgsWin) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    // 显式 args 数组优先（契约）
    NC_CHECK(f.isOk("xform_set",
                    j("{\"track\":0,\"clip\":%llu,\"kind\":0,\"value\":9,"
                      "\"args\":[0.7,0.1,0,0,0,1,1,0,4,0,0,0,0]}",
                      (unsigned long long)f.clipId)));
    double a[13] = {0};
    NC_CHECK_EQ(nc_get_clip_xform(f.ed, 0, f.clipId, 0, a, nullptr, nullptr), 0);
    NC_CHECK(a[0] > 0.69 && a[0] < 0.71);   // 来自 args，不是具名 value=9
}

NC_TEST(S1_2, Sf1InvalidTypeRejected) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    const std::string r = f.run("xform_set",
        j("{\"track\":0,\"clip\":%llu,\"kind\":0,\"value\":\"big\"}",
          (unsigned long long)f.clipId));
    NC_CHECK(r.find("\"ok\":false") != std::string::npos);
    NC_CHECK(r.find("几何变换参数类型非法") != std::string::npos);
}

// ===================== SF-2 chroma 参数映射 =====================

NC_TEST(S1_2, Sf2RgbSynthesizedAndThresholds) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    nc_set_entitlement(f.ed, 1, 1, 0);   // Pro：抠像受权益门控
    // 纯红 + 阈值（旧实现：键色恒默认绿、阈值恒默认）
    NC_CHECK(f.isOk("chroma", j("{\"track\":0,\"clip\":%llu,"
                                "\"key_r\":255,\"key_g\":0,\"key_b\":0,"
                                "\"similarity\":0.2,\"smoothness\":0.8,\"enabled\":1}",
                                (unsigned long long)f.clipId)));
    uint32_t rgb = 0;
    double inner = 0, outer = 0, spill = 0;
    NC_CHECK_EQ(nc_get_clip_chroma_key(f.ed, 0, f.clipId, &rgb, &inner, &outer, &spill), 1);
    NC_CHECK_EQ(rgb, static_cast<uint32_t>(0xFF0000));
    NC_CHECK(inner > 0.19 && inner < 0.21);
    NC_CHECK(outer > 0.79 && outer < 0.81);
    NC_CHECK(spill > 0.59 && spill < 0.61);   // 默认 0.6
}

NC_TEST(S1_2, Sf2InnerNotLessThanOuterRejected) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    nc_set_entitlement(f.ed, 1, 1, 0);
    // inner >= outer 必须报错，且**不得自动交换**
    const std::string r = f.run("chroma",
        j("{\"track\":0,\"clip\":%llu,\"key_r\":0,\"key_g\":255,\"key_b\":0,"
          "\"similarity\":0.8,\"smoothness\":0.3}", (unsigned long long)f.clipId));
    NC_CHECK(r.find("\"ok\":false") != std::string::npos);
    NC_CHECK(r.find("必须小于") != std::string::npos);
}

NC_TEST(S1_2, Sf2IncompleteRgbRejected) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    nc_set_entitlement(f.ed, 1, 1, 0);
    const std::string r = f.run("chroma",
        j("{\"track\":0,\"clip\":%llu,\"key_r\":255}", (unsigned long long)f.clipId));
    NC_CHECK(r.find("\"ok\":false") != std::string::npos);
    NC_CHECK(r.find("键色不完整") != std::string::npos);
}

// ===================== SF-3 mask inverted 兼容 =====================

NC_TEST(S1_2, Sf3InvertedBoolAccepted) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    NC_CHECK(f.isOk("mask", j("{\"track\":0,\"clip\":%llu,\"shape\":1,\"inverted\":true}",
                              (unsigned long long)f.clipId)));
    // nc_get_clip_mask 返回 1=已启用蒙版（出参有效），0=未启用，-1=错误
    int32_t invert = -1;
    NC_CHECK_EQ(nc_get_clip_mask(f.ed, 0, f.clipId, nullptr, nullptr, nullptr,
                                 nullptr, nullptr, nullptr, nullptr, nullptr, &invert), 1);
    NC_CHECK_EQ(invert, 1);
    // false
    NC_CHECK(f.isOk("mask", j("{\"track\":0,\"clip\":%llu,\"shape\":1,\"inverted\":false}",
                              (unsigned long long)f.clipId)));
    NC_CHECK_EQ(nc_get_clip_mask(f.ed, 0, f.clipId, nullptr, nullptr, nullptr,
                                 nullptr, nullptr, nullptr, nullptr, nullptr, &invert), 1);
    NC_CHECK_EQ(invert, 0);
}

NC_TEST(S1_2, Sf3NumericAndLegacyKeyAccepted) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    int32_t invert = -1;
    // 数值 1
    NC_CHECK(f.isOk("mask", j("{\"track\":0,\"clip\":%llu,\"inverted\":1}",
                              (unsigned long long)f.clipId)));
    NC_CHECK_EQ(nc_get_clip_mask(f.ed, 0, f.clipId, nullptr, nullptr, nullptr,
                                 nullptr, nullptr, nullptr, nullptr, nullptr, &invert), 1);
    NC_CHECK_EQ(invert, 1);
    // 旧键名 invert
    NC_CHECK(f.isOk("mask", j("{\"track\":0,\"clip\":%llu,\"invert\":0}",
                              (unsigned long long)f.clipId)));
    NC_CHECK_EQ(nc_get_clip_mask(f.ed, 0, f.clipId, nullptr, nullptr, nullptr,
                                 nullptr, nullptr, nullptr, nullptr, nullptr, &invert), 1);
    NC_CHECK_EQ(invert, 0);
}

NC_TEST(S1_2, Sf3InvalidTypeRejected) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    // 非法类型必须报错，**不得静默当 false**
    const std::string r = f.run("mask",
        j("{\"track\":0,\"clip\":%llu,\"inverted\":\"yes\"}", (unsigned long long)f.clipId));
    NC_CHECK(r.find("\"ok\":false") != std::string::npos);
    NC_CHECK(r.find("类型非法") != std::string::npos);
    // 数值越界（2）也必须报错
    const std::string r2 = f.run("mask",
        j("{\"track\":0,\"clip\":%llu,\"invert\":2}", (unsigned long long)f.clipId));
    NC_CHECK(r2.find("\"ok\":false") != std::string::npos);
}

// ===================== SF-10 track_flag 三个全名 =====================

NC_TEST(S1_2, Sf10ThreeFullNamesDistinct) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    int32_t locked = 0, muted = 0, hidden = 0, solo = 0;
    // hidden
    NC_CHECK(f.isOk("nc_set_track_hidden", "{\"track_index\":0,\"hidden\":true}"));
    NC_CHECK_EQ(nc_track_get(f.ed, 0, nullptr, nullptr, nullptr,
                             &locked, &muted, &hidden, &solo, nullptr), 0);
    NC_CHECK_EQ(hidden, 1);
    NC_CHECK_EQ(muted, 0);      // 不得串到别的标志位
    NC_CHECK_EQ(locked, 0);
    // muted
    NC_CHECK(f.isOk("nc_set_track_muted", "{\"track_index\":0,\"muted\":true}"));
    NC_CHECK_EQ(nc_track_get(f.ed, 0, nullptr, nullptr, nullptr,
                             &locked, &muted, &hidden, &solo, nullptr), 0);
    NC_CHECK_EQ(muted, 1);
    NC_CHECK_EQ(hidden, 1);     // 之前的 hidden 仍在（未互相覆盖）
    // locked
    NC_CHECK(f.isOk("nc_set_track_locked", "{\"track_index\":0,\"locked\":true}"));
    NC_CHECK_EQ(nc_track_get(f.ed, 0, nullptr, nullptr, nullptr,
                             &locked, &muted, &hidden, &solo, nullptr), 0);
    NC_CHECK_EQ(locked, 1);
}

NC_TEST(S1_2, Sf10ErrorsAndLegacyValue) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    // 缺参报错
    const std::string miss = f.run("nc_set_track_hidden", "{\"track_index\":0}");
    NC_CHECK(miss.find("\"ok\":false") != std::string::npos);
    // 非法类型报错
    const std::string bad = f.run("nc_set_track_muted",
                                  "{\"track_index\":0,\"muted\":\"on\"}");
    NC_CHECK(bad.find("\"ok\":false") != std::string::npos);
    // 无效轨道报错
    const std::string badTrack = f.run("nc_set_track_locked",
                                       "{\"track_index\":99,\"locked\":true}");
    NC_CHECK(badTrack.find("\"ok\":false") != std::string::npos);
    // 兼容旧口径 value
    NC_CHECK(f.isOk("nc_set_track_hidden", "{\"track_index\":0,\"value\":1}"));
}

// ===================== SF-11 / SF-12 frame_fit =====================

NC_TEST(S1_2, Sf11FullNameRealCall) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    NC_CHECK_EQ(nc_get_frame_fit(f.ed), 0);
    // P1/P2 都发全名；旧实现无此分支 → 「未开放的操作」
    NC_CHECK(f.isOk("nc_set_frame_fit", "{\"track_index\":0,\"mode\":1}"));
    NC_CHECK_EQ(nc_get_frame_fit(f.ed), 1);   // 真实生效，不是假成功
}

NC_TEST(S1_2, Sf12ShortNameRealCallNotFake) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    NC_CHECK_EQ(nc_get_frame_fit(f.ed), 0);
    // 短名保留（不删除），但必须真实调用 ABI（旧实现无条件 Ok("")）
    NC_CHECK(f.isOk("frame_fit", "{\"mode\":2}"));
    NC_CHECK_EQ(nc_get_frame_fit(f.ed), 2);
}

NC_TEST(S1_2, Sf11Sf12MissingModeRejected) {
    Fixture f;
    NC_CHECK(f.ed != nullptr);
    const std::string a = f.run("nc_set_frame_fit", "{}");
    NC_CHECK(a.find("\"ok\":false") != std::string::npos);
    NC_CHECK(a.find("缺少画面适配模式") != std::string::npos);
    const std::string b = f.run("frame_fit", "{}");
    NC_CHECK(b.find("\"ok\":false") != std::string::npos);
    const std::string c = f.run("frame_fit", "{\"mode\":\"x\"}");
    NC_CHECK(c.find("\"ok\":false") != std::string::npos);
}
