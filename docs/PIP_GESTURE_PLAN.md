# NairoClip 画中画 + 手势缩放 整改任务书

> **本文是自包含的开发任务书** —— 单读本文即可执行。
> 姊妹篇：`docs/PERF_XFORM_PLAN.md`（渲染性能）、`docs/INTERACTION_ENGINE.md`（互动引擎）。
> 本文处理这两份都没覆盖的一块：**画中画是坏的，且「缩放」在三个不同层面都缺失或语义错位。**
>
> 状态：全部待开发。
> **所有「现状」结论均来自对仓库的实际检索与代码阅读，非推测**；
> 每条断言都标了 `file:line`，可逐条复核。
>
> 最后更新：2026-11-03 · HEAD `c2fc65c`

---

## 0. 三句话结论

1. **画中画有入口、有轨道、有面板、有混合模式，但预览里什么都看不到** —— 因为预览只渲染第一条视频轨（`preview.cpp:35` 的 `break`）。
2. **画中画最核心的「位置 / 大小 / 旋转」在整个软件里没有入口**：`ClipProperty` 有通道、`animation.sample()` 有采样，但**采样完就扔**，渲染从不消费。
3. **轨道左眼/喇叭/锁三个按钮在 Android 上完全不响应** —— 不是内核缺能力，是 JNI 派发层漏了 9 个分支（`track_flag` 在列）。

---

## 1. 「缩放」有四种，必须先分清

用户问「轨道和轨道上面的所有素材都可以手势缩放吗」。这句话里的「缩放」实际对应
**四个互不相同的东西**，现状差异极大：

| # | 缩放的是什么 | 手势位置 | 数据落库 | 现状 |
|---|---|---|---|---|
| **①** | **时间密度**（px/秒）—— 片段在时间轴上变宽/变窄 | 时间轴区双指 | ❌ 只在前端内存 | ✅ 已实现 `editor.js:1696,1715` |
| **②** | **轨道高度** —— 轨道行变高/变矮 | 单指拖轨道头下缘 | ✅ `nc_set_track_view` | ✅ 已实现 `editor.js:1222` |
| **③** | **画面大小**（`ClipTransform.scale`）—— 画中画浮窗变小 | 预览区双指 | ❌ **无此能力** | 🔴 **完全没有** |
| **④** | **看画面**（`S.zoom`）—— 只是 `<img>` 的 CSS transform | 预览区双指 | ❌ **不应落库** | ⚠️ 已实现，但**与 ③ 语义冲突** |

**④ 是最容易误导的一处**：双指捏预览区，画面确实变大了 —— 但那只是
`E.still.style.transform`（`editor.js:1746`），`S.zoom` 的 8 处引用**全部是前端局部变量**，
导出时完全不存在。用户会以为「我调了画面大小」，实际什么都没进工程。

**本文的定案**：把 ④ 与 ③ 用**选中态**分开（详见 §5.1）。

---

## 2. 现状实测

### 2.1 画中画：链路逐段状态

| 环节 | 状态 | 位置 |
|---|---|---|
| L1 工具栏「画中画」入口 | ✅ | `editor.js:232` |
| L2 二级菜单（3 项） | ✅ 新增画中画 / 混合模式 / 蒙版 | `editor.js:281-284` |
| 「新增画中画」执行 | ✅ 真调 `add_track`，`type=0` | `editor.js:3703-3710` |
| 轨道真的出现在时间轴 | ✅ 出现，名为「画中画 N」 | 同上 |
| **轨道类型** | ⚠️ **无 `TrackType::Pip`，复用 `Video=0`** | `track.h:13-26` 只有 Video/Audio/Subtitle/Adjustment |
| 前端如何识别它 | ⚠️ **识别不出**，`kindOf(0)` 返回 `'video'` | `editor.js:120-121` |
| 轨道左边三个按钮（眼/喇叭/锁） | 🔴 **Android 上全坏** | 见 §2.2 |
| 混合模式面板 | ✅ 能开、能存、能序列化 | `l3-spec.js:306-311`、`capi.cpp:3354` |
| **预览里看得到吗** | 🔴 **看不到** | `preview.cpp:16-41` 只取第一条可见视频轨就 `break` |
| 混合模式在预览生效吗 | 🔴 **零命中** | `blendMode` 在 `preview.cpp` 出现 **0** 次 |
| **位置 / 大小 / 旋转** | 🔴 **无入口、无 ABI** | §2.3 |
| 导出正确吗 | ⚠️ **只支持 2 层** | `export_executor.cpp:720` 硬编码 `layers[1]` |

**结论**：用户点「新增画中画」→ 轨道确实多一条 → 拖素材进去 → **预览毫无变化**。
这不是「功能弱」，是「功能不成立」。

### 2.2 九个死 op：真机上「点了没反应」

前端会发 46 种 op，Android 的 `DispatchEdit`（`nc_jni.cpp:388`）只认其中 37 种。
实测差集（`grep -c` 全部为 0）：

```bash
for op in duplicate marker marker_color move_to_track remove_keyframe \
          remove_transition ripple_delete track_flag track_gain; do
  printf "%-20s %s\n" "$op" "$(grep -c "op == \"$op\"" mobile/native/android/nc_jni.cpp)"
done
# 输出：9 行，每行结尾都是 0
```

| op | 用户看到的功能 | 底层 C ABI 存在吗 |
|---|---|---|
| **`track_flag`** | **轨道 眼 / 喇叭 / 锁** | ✅ `nc_set_track_locked/muted/hidden`（`capi.h:242-244`） |
| `track_gain` | 轨道增益 | ✅ `nc_set_track_gain` |
| `duplicate` | 复制片段 | ✅ `nc_duplicate_clip` |
| `move_to_track` | 跨轨移动 | ✅ `nc_move_clip_to_track` |
| `ripple_delete` | 波纹删除 | ✅ `nc_ripple_delete` |
| `remove_keyframe` | 删关键帧 | ✅ `nc_remove_keyframe` |
| `remove_transition` | 删转场 | ✅ `nc_remove_transition` |
| `marker` | 加标记 | ✅ `nc_add_marker` |
| `marker_color` | 改标记颜色 | ✅ `nc_add_marker`（带颜色参数） |

**这是纯接线工作**：`nc_*` 全部就位，只需在 `DispatchEdit` 里补 9 个分支。
`design/tools/nc_design_harness.cpp:690-703` 的 `track_flag` 实现可直接照抄
（它已支持 `locked` / `muted` / `hidden` / `solo` 四种）。

**为什么必须最优先修**：用户说的「轨道左边的小眼睛那 3 个功能也要可以用，
只是名字换成了画中画」—— 这三个按钮现在完全不响应，
用户会判断「整个轨道系统坏了」，而不是「画中画没做好」。

### 2.3 「画面缩放」三条路径全断

打开一个片段想把它缩小放到角落（画中画最基本的操作），四条路径全部不通：

| 路径 | 现状 | 位置 |
|---|---|---|
| **参数面板** | 🔴 `scale`/`posx`/`posy`/`rotation` 控件**已定义但无任何面板引用** | 定义 `l3-spec.js:32-35`；`grep -c "\['scale'"` = **0** |
| **预览区手势** | 🔴 双指只改 `S.zoom`（CSS），不写工程 | `editor.js:1734-1751` |
| **时间轴片段握把** | 🔴 只有左右裁剪把手（改**时长**），**无缩放握把** | `editor.js:1321-1332` |
| **C ABI** | 🔴 **无** `nc_set_clip_scale/position/rotation` | `capi.h` grep 无此类函数 |
| — | | |
| 数据模型 | ✅ `ClipProperty::Scale/PositionX/PositionY/Rotation` | `animation.h:12-19` |
| 采样 | ✅ **预览与导出都采样** | `preview.cpp:271-274` |
| **渲染消费** | 🔴 **采样完就扔** | `frame.scale/posX/posY` 只被赋值，从不参与像素运算 |
| 关键帧写入 | ✅ `nc_insert_keyframe(property=1..4)` 可写 | `capi.h:976` |
| **写入后有画面效果吗** | 🔴 **没有** | 同上 |

**这是最严重的一处假功能**：`nc_insert_keyframe(property=1)` 返回 `NC_OK`，
`.nclip` 里确实存了关键帧，`animation.sample()` 也确实算出正确值 —— 但画面一动不动。
比「点了报错」更糟，因为用户会以为是自己没调对。

### 2.4 导出侧多层：只支持 2 层

`export_executor.cpp:366-380` 用 `std::vector<ActiveLayer>` 收集了**所有**活动层，
但合成时：

```cpp
// export_executor.cpp:699-720
if (layers.size() > 1) {
    const RenderClip* lower = layers[1].clip;   // ← 硬编码第 2 层
    // 注释自认：「多层 > 2 递归处理为后续优化」
}
```

⇒ **3 层及以上时，第 3 层起被静默丢弃**。剪映支持任意层，我们只有 2 层。

### 2.5 轨道与素材的「缩放」：三个子区域，三种语义

| 手势 | 现状 | 位置 | 剪映是否相同（有出处见 §6.3.0） |
|---|---|---|---|
| 双指捏**时间轴区** → 改时间密度 | ✅ 已实现 | `editor.js:1696,1715` | ✅ 一致：双指捏合就是改时间密度 |
| 单指拖**轨道头下缘** → 改这一条轨高 | ✅ 已实现 | `editor.js:1222-1261` | ⚠️ **查不到剪映有轨道高度手势** |
| 双指捏**轨道头** → 改全部轨高 | 🔴 无 | `grep -c "makePinch(E.heads"` = 0 | ❌ 查不到剪映有此手势；**我们决定不做**（§6.3.2） |
| 片段上双指捏合 → 改画面大小 | 🔴 无 | — | ❌ **不该做**（维度语义冲突，§6.3.3） |
| 时间密度持久化 | 🔴 无（`S.pxPerSec` 纯前端变量） | — | — 无出处可证剪映行为；**这是本项目自己的取舍**（§6.4） |
| 独立缩放滑杆（− / +） | 🔴 无 | — | ✅ 剪映桌面版有；**我们要补** |

---

## 3. 借鉴：剪映 / AE 的做法

> **可信度分级（读表前先看）**
>
> | 标记 | 含义 |
> |---|---|
> | ✅**已查证** | 有明确外部出处，出处见 §6.3.0 或行内说明 |
> | ◻**通识** | 移动端剪辑软件的普遍做法，我未逐条查证到官方文档；作为设计参考，不作为「剪映一定如此」的事实陈述 |
>
> 本节仅 2 项标 ✅**已查证**（时间轴双指捏合 = 改密度、桌面端常驻缩放滑杆），
> 其余均按 ◻**通识** 处理。**不要把 ◻ 当证据去反驳用户或写进验收标准。**

| 做法 | 出处 | 我们的落点 | 采纳 |
|---|---|---|---|
| **视频轨 = 图层，天然多层叠加**，不区分「主轨 / 画中画轨」 | ◻通识 | 已有多条 `TrackType::Video` —— **不新增 `TrackType::Pip`** | ✅ |
| **画中画操作是「在预览区直接拖 / 双指缩放 / 双指扭转」** | ◻通识 | §5 手势 | ✅ |
| **选中片段 → 预览区出现可拖拽边框 + 四角握把** | ◻通识 | §5.4 选择框 | ✅ |
| **时间轴上拖两端只改时长，不改画面大小** | ◻通识 | 已实现（`trim-handle`）—— **保持，不混入缩放** | ✅ |
| **画面大小/位置有独立参数面板** | ◻通识 | §4 面板 | ✅ |
| **新增画中画默认缩小居中（约 60%）** | ◻通识（60% 是本项目取值） | §6 G4 默认值 | ✅ |
| **Collapse Transformations：几何合并为单矩阵，只重采样一次** | AE | 已实现（`xform.h` `Affine2D`）—— 画中画矩阵直接预乘 | ✅ |
| **位置/缩放/旋转可打关键帧** | ◻通识 | `ClipProperty` 已有通道，只需接渲染 | ✅ |
| **锚点九宫格** | AE | 复用 `XformParams.anchor`（0..8）语义 | ✅ |
| **层序由轨道顺序决定，可上下拖动** | ◻通识 | 已实现（`attachTrackReorder`，`editor.js:1066`） | ✅ 已做 |
| **调整图层作用于其下所有层** | AE | 已实现（`applyAdjustmentLayers`，`preview.cpp:377`） | ✅ 已做 |
| **时间轴区双指捏合 = 改时间密度（不改轨高、不改画面）** | ✅**已查证**（§6.3.0） | 已实现，保持一致 | ✅ 保持 |
| ~~常驻缩放滑杆（− / +）~~ | ~~editlogic.io~~ | — | ❌ **用户取消**（2026-11-03，多次强调「不做那个按键」） |
| **双指捏轨道区 → 行高+时间轴+图标联动缩放** | 剪映 | 已实现（`scaleRowsAndTimeline`） | ✅ **用户指定，做这个** |
| **每层独立缓存（Layer Cache）** | AE | `PERF_XFORM_PLAN.md` §6.1 | ⏳ 后期 |

**明确不照搬**：

- ❌ **不新增 `TrackType::Pip`** —— 判断依据是我们自己的数据模型代价（见下行说明），**不假借剪映**。新增类型会波及 `trackAcceptsClipType`、
  序列化、C ABI 的 `track_type` 语义（值不可变），代价远大于收益。
- ❌ **不在时间轴上给片段做「画面大小」捏合** —— 见 §5.3，维度混淆。
- ❌ **不做旋转握把** —— 双指扭转足够。【无出处，设计判断】
- ❌ **不做各向异性缩放（scaleX/scaleY 分离）** —— 超出本期范围。【无出处，设计判断】

---

## 4. 数据模型与 ABI

### 4.1 定案：不新增 TrackType，新增 Clip 级画面变换

画中画 = 「视频轨（第 2 层及以上）的片段，带一份画面变换参数」。

```cpp
// clip.h 新增
struct ClipTransform {
    double scale    = 1.0;   // 1.0 = 按 FrameFit 铺满（既有行为）
    double posX     = 0.0;   // 归一化，相对画布宽；0 = 居中
    double posY     = 0.0;
    double rotation = 0.0;   // 度
    int    anchor   = 4;     // 九宫格 0..8，4 = 中心
    bool   enabled  = false; // false = 完全不影响渲染（零改动保证）
};
// Clip 增加：ClipTransform transform;
```

**为什么需要 `enabled`**：与 `xforms` / `shake.amplitude > 0` 同一约定 ——
默认关闭，保证「加了这个字段」不让任何既有工程的像素发生变化。

**为什么需要静态基准值**：`ClipAnimation::sample()` 在**无曲线时返回默认值**
（`animation.h:51-53` 明确写了「通道无曲线时返回默认值」）。
所以只打关键帧无法表达「静态缩小到 60%」—— 必须先有基准值。
（这也是通行做法：滑块本身是静态值，打点后才变成曲线。）

**采样优先级（唯一实现，预览与导出共用）**：

```cpp
double resolveScale(const Clip& clip, const Time& local) {
    if (clip.animation.hasCurve(ClipProperty::Scale))
        return clip.animation.sample(ClipProperty::Scale, local);   // ① 关键帧
    return clip.transform.enabled ? clip.transform.scale : 1.0;      // ② 静态值 / 默认
}
```

四条通道（Scale / PositionX / PositionY / Rotation）都按此规则。
**这条规则只能在一处实现**，否则会出现「预览和导出不一样」。

### 4.2 C ABI（3 个函数，只增不改）

```c
/// 设置片段画面变换（画中画位置/大小/旋转/锚点）。
/// - enabled：0 = 关闭（完全不影响渲染）；1 = 启用
/// - scale：0.05..10（1.0 = 既有行为）
/// - pos_x/pos_y：归一化 -10..10（0 = 居中；1 = 右移一个画布宽）
/// - rotation_degrees：-3600..3600
/// - anchor：0..8 九宫格（4 = 中心）
/// 越界钳制，不报错（与 XformParams::clamp 一致）。
int32_t nc_set_clip_transform(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                              int32_t enabled, double scale,
                              double pos_x, double pos_y,
                              double rotation_degrees, int32_t anchor);

/// 读取（未设置时返回默认值 + enabled=0）。
int32_t nc_get_clip_transform(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                              int32_t* out_enabled, double* out_scale,
                              double* out_pos_x, double* out_pos_y,
                              double* out_rotation_degrees, int32_t* out_anchor);

/// 清除（等价 enabled=0）。独立撤销步。
int32_t nc_clear_clip_transform(NcEditor* editor, int32_t track_index, uint64_t clip_id);
```

**为什么 3 个而不是 1 个**：`nc_clear_clip_transform` 让 UI 的「重置」成为一次独立撤销步；
若用「设成全默认值」实现，撤销栈里会显示「设置画面变换」，用户看不出这是重置。

### 4.3 撤销命令

照 `SetClipBlendModeCommand`（`commands.h:975-990`）范式，**整槽替换**：

```cpp
class SetClipTransformCommand : public Command {
public:
    SetClipTransformCommand(SequenceId, TrackId, ClipId, ClipTransform after);
    Result apply(Project&) override;    // 写 after_
    Result revert(Project&) override;   // 写 before_
    std::string nameImpl() const override { return "调整画面大小/位置"; }
private:
    ClipTransform before_{}, after_{};
};
```

一次「应用」= 一个命令 = 一次撤销步（与 `SetClipXformCommand` 一致）。

### 4.4 序列化

`.nclip` 的 clip 对象增加可选字段，**默认值不写入**（保持既有工程 diff 稳定）：

```json
{
  "id": 7, "asset": 3, "start": ..., "duration": ...,
  "transform": { "scale": 0.6, "posX": 0.15, "posY": -0.1, "rotation": 5, "anchor": 4 }
}
```

规则：
- `enabled == false` → **整块不写**；
- `enabled == true` → 写全部 6 个字段（不省略默认值，避免「部分写入」的歧义）；
- 读取时缺失 → `enabled=false`（向后兼容，老工程像素零变化）。

---

## 5. 渲染接线（最关键）

### 5.1 预览：先修多层合成

**这是画中画能看见的前提，优先级高于任何变换参数。**

现状 `preview.cpp:16-41`：

```cpp
static const Clip* activeVideoClip(const Project& project, TrackId* outTrack, const Time& t) {
    for (const auto& track : seq->tracks) {
        if (track.type != TrackType::Video || track.hidden) continue;
        for (const auto& clip : track.clips) {
            if (clip.enabled && clip.contains(t)) { ...return &clip; }
        }
        break;              // ← 只取第一条可见视频轨
    }
}
```

**改法：把导出侧的层收集搬过来**（`export_executor.cpp:366-380` 已验证可用）：

```cpp
struct ActiveLayer {
    const Clip* clip;
    const Track* track;
};

/// 收集 t 时刻所有活动视频层。下标 0 = 最底层（与 seq->tracks 顺序一致）。
static std::vector<ActiveLayer> collectActiveLayers(const Project& project, const Time& t) {
    std::vector<ActiveLayer> layers;
    const Sequence* seq = project.mainSequence();
    if (seq == nullptr) return layers;
    for (const auto& track : seq->tracks) {          // 顺序遍历 = 下标 0 最底层
        if (track.type != TrackType::Video || track.hidden) continue;
        for (const auto& clip : track.clips) {
            if (clip.enabled && clip.contains(t)) {
                layers.push_back(ActiveLayer{&clip, &track});
                break;                                // 同轨不重叠，取到即跳出内层
            }
        }
    }
    return layers;
}
```

`renderUncached` 从「渲染 1 层」改为「渲染 N 层并逐层合成」：

```
for (layer : layers) {                    // 从底到顶
    frame = renderLayerToCanvas(*layer);  // 现有单层逻辑抽成函数
    if (first) result = frame;            // 最底层作为基底
    else      compositeOnto(result, frame, layer.clip->blendMode, topAlpha);
}
```

`compositeOnto` 直接用 `blendFrameRgba`（`blend_modes.h:79`）—— 16 种模式已实现，
导出侧在用（`export_executor.cpp:832`）。

**必须写进代码注释的约束**：

> `seq->tracks` 是**顺序遍历**（`preview.cpp:23` 与 `:396` 都无 `reverse`），
> 下标 0 = 最底层。若有人加了反向遍历，画中画会盖住主画面且静默出错。

### 5.2 画面变换：并入已有的单次重采样

**不要新增重采样路径。** `applyXformStack`（`xform.h:104`）已支持
`FrameFit` + `EdgeMode` + 8 槽合并矩阵，画中画只需在它之前预乘一个矩阵：

```cpp
// 组装：T(anchor) · R(θ) · S(scale) · T(anchor⁻¹) · T(pos)
// 顺序（与 AE 一致）：
//   1) 先以锚点为中心缩放
//   2) 再旋转
//   3) 最后平移到目标位置
// 锚点 = 4（中心）时 anchor 平移项抵消，退化为 T(pos)·R·S。
Affine2D clipTransformMatrix(const ClipTransform& tf, const ClipAnimation& anim,
                             const Time& localTime, int canvasW, int canvasH);
```

`applyXformStack` 增加一个**默认恒等**的可选参数（保证零改动）：

```cpp
bool applyXformStack(..., bool bilinear,
                     const Affine2D& extra = Affine2D::identity());
```

调用：

```cpp
const Affine2D fit = fitMatrix(seq->frameFit, srcW, srcH, canvasW, canvasH);
const Affine2D pip = clipTransformMatrix(clip->transform, clip->animation, local, canvasW, canvasH);
// 画中画矩阵作用在 fit 之后：fit 决定「铺满/适应」，pip 决定「在此基础上再变换」
applyXformStack(src, srcW, srcH, stride, dst, canvasW, canvasH,
                clip->xforms, timeSec, seq->frameFit, edge, background, true,
                /*extra=*/ fit.then(pip));
```

**为什么不直接用 `renderTransform`**：`applyXformStack` 还负责 8 槽合并、
栅格转换的独立网格路径、边缘模式选择。绕开它写第二套必然与「转换」面板行为漂移。

### 5.3 导出：从 2 层扩到 N 层

`export_executor.cpp:699-720` 的 `layers[1]` 硬编码：

```cpp
// 改前（只处理 2 层）
if (layers.size() > 1) {
    const RenderClip* lower = layers[1].clip;      // ← 硬编码
}

// 改后（自底向上逐层累积）
std::vector<std::uint8_t> composite(plan.width * plan.height * 4, 0);
for (std::size_t i = layers.size(); i-- > 0;) {     // 从最底层往上
    renderLayerInto(composite, layers[i]);          // 含该层自己的滤镜/特效/蒙版/变换
}
```

**同一改动顺带修掉「第 3 层起被静默丢弃」的缺陷**（§2.4）。

### 5.4 默认值：新增画中画时自动缩小

**目标行为**：新增画中画放入素材后默认缩小约 60% 并居中，
用户立刻能看出「这是浮在主画面上的」。
【§6.3.0 的检索未覆盖此项，60% 是本项目的取值，不是从剪映抄来的具体数字】

落点：`addPipTrack()`（`editor.js:3703`）成功后，若新轨道上有片段，
自动调 `nc_set_clip_transform(enabled=1, scale=0.6, posX=0, posY=0, rotation=0, anchor=4)`。

**为什么不是 1.0**：1.0 + 居中 = 完全盖住主画面，用户看到的是「主画面消失了」，
会以为拖错轨了。【这是从「用户会怎么误解」推导出来的，非剪映实测记录】

**为什么不是 0.5**：0.6 在 9:16 竖屏上留出的边框更自然，
且第一眼就能确认「小的那个在动」而不是「贴边看不清」。

---

## 6. 手势规格

### 6.1 定案：用「选中态」把 ③ 和 ④ 分开

| 场景 | 双指捏预览区 | 语义 | 落库 |
|---|---|---|---|
| **有选中片段** | 改 `ClipTransform.scale` | ③ 画面大小 | ✅ 写引擎，松手提交 |
| **无选中片段** | 改 `S.zoom` | ④ 看画面（临时放大看细节） | ❌ 不写 |

**为什么这样分**：【设计判断，非剪映实测】「选中了 = 你在编辑这个素材」、
没选中 = 你在看。这与「拖片段 vs 拖时间轴」是同一种一致性的延伸。

**必须补一个显式的「看画面模式」入口**，否则用户无选中时捏合不知道发生了什么：
双击预览区 → 进入/退出看画面模式（`S.zoom` 0.5..4），并显示一个角标提示「看画面 · 不影响导出」。

### 6.2 手势规则表

| 手势 | 目标区域 | 判定条件 | 动作 | 提交时机 |
|---|---|---|---|---|
| 双指捏合 | 预览区 **且有选中片段** | 两指距变化 > 4% | `scale *= ratio`，钳 0.05..10 | 松手 |
| 双指捏合 | 预览区 **且无选中** | 同上 | 改 `S.zoom`（看画面） | 不提交 |
| 双指扭转 | 预览区 **且有选中** | 角度变化 > 3° | `rotation += Δθ`，钳 ±3600 | 松手 |
| 双指平移 | 预览区 **且有选中** | 中点位移 > 6px | `posX/posY += Δ/canvasW` | 松手 |
| 单指拖动 | 预览区 **且点在选择框内** | 位移 > 6px | 同「双指平移」 | 松手 |
| 单指拖动 | 预览区 **且未选中** | — | 不响应（避免误触） | — |
| 双指捏合 | 时间轴 | 已在实现 | `pxPerSec *= ratio`，钳 4..900 | 不提交（见 §6.4） |
| 双指捏合 | **轨道头列** | 两指距变化 > 8% | **全部轨道**高度等比缩放 | 松手 |

### 6.3 回答「轨道和轨道上面的所有素材都可以手势缩放吗」

#### 6.3.0 先给证据：剪映实际怎么做的（以及我查不到的部分）

> **可信度声明**：我做了检索，但**没有找到剪映官方的手势文档**
> （CapCut Help Center 只有账号/模板/导出类条目；capcut.cn 的 learning 页是 JS 渲染，抓不到正文）。
> 下面把「有出处」与「查不到」严格分开，不把推测写成事实。

**✅ 有出处：双指捏合 = 缩放「时间轴」（时间密度），不是改轨道高度**

| 来源 | 原文 |
|---|---|
| 抖音摘要（「剪映手机版时间轴怎么调整」） | 「在操作区域内双指点击屏幕，同时向外或向内滑动，即对时间轴（轨道）大小进行操作」 |
| 百度文库《剪映时间条怎么拉长》 | 「用两根手指放在时间轴区域，双指外划（像放大图片一样）即可拉长时间轴，让片段显示更细致；双指内划则缩短时间轴，让视野更宏观」 |
| 知乎《剪映专业版放大或缩小时间线的作用及快捷键》 | 「缩放时间线是改变时间线显示的单位，原本按秒显示，放大后按帧显示，缩小后按分钟显示」 |
| editlogic.io（2026-10，CapCut 桌面版） | 时间线缩放由**滚轮 / 触控板捏合 / Ctrl+滚轮**控制；面板左下角另有独立 **zoom slider**（带 − / + 按钮） |

**结论 A（可确认）**：剪映的「双指捏合」在时间轴区域**只做一件事 —— 改时间密度**。
它**不改轨道高度**，也**不改画面大小**。

**❌ 查不到：剪映是否有「捏合改轨道高度」这个手势**

我检索了中英文多组关键词（轨道高度 / track height / resize tracks / pinch），
Bing 反复返回 capcut.cn 首页，没有得到任何可用文档或社区讨论。
**因此我不能声称「剪映有」，也不能声称「剪映没有」。**

**这一点直接影响设计建议**：既然没有证据，就不该写成「借鉴剪映」。
轨道高度的调法必须**按我们自己的场景独立判断**（见 §6.3.2）。

#### 6.3.1 意思 A：时间轴捏合让片段显示得更宽/更窄（改时间密度）→ 与剪映一致

**剪映这么做，我们也这么做**：双指捏合 = 改 `pxPerSec`。已实现，但有三个缺陷：

| 缺陷 | 现状 | 修法 |
|---|---|---|
| 只挂在 `E.tracks` / `E.ruler` | `editor.js:1696,1715` | 保持 —— 见 §6.3.2 的修正结论 |
| `pxPerSec` 不持久化 | 纯前端变量 | 存 `localStorage`（按工程 ID 分键），见 §6.4 |
| 与单指平移抢事件 | 同一元素挂了 `pointerdown` + `makePinch` | 走 `INTERACTION_ENGINE.md` §4.1 `InputArbiter` |

**还应借鉴剪映的两点**（均有出处）：

1. **桌面端 Ctrl+滚轮 = 时间线缩放** —— 我们已实现（`editor.js:1781-1787`），与剪映一致 ✅；
2. **常驻缩放滑杆（带 − / +）** —— 剪映桌面版在时间线左下角常驻一条。
   理由：手势不精确，用户想「回到刚才那个密度」时需要一个**可重复、离散**的入口。
   **这是我们缺的 UI 元素**，建议补在播放条右侧。

#### 6.3.2 意思 B：轨道行本身变高/变矮 → 剪映不提供手势；我们改用「双击循环档位」

**修正上一版文档的错误**：我曾写「双指捏轨道头改全部轨道高度 —— 剪映 / FCP 都有」。
**这个断言我没有证据，撤回。**

**按我们自己的场景重新判断**，三方案对比：

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| A. 只保留单指拖轨道头下缘（现状） | 精确、可逆、不抢手势 | 10 条轨时逐条拖慢 | ✅ **保留为主路径** |
| B. 双指捏轨道头 → 全部轨高等比 | 一次调全部 | 与「捏时间轴」语义冲突 | ✅ **用户指定，做这个** |
| C. 轨道头双击 → 循环 3 档高度 | 零手势冲突 | 档位跳变、不能连续调 | ❌ **被用户否掉** |

**最终定案（用户 2026-11-03 明确指定）：自动阶梯 + 捏合微调 + 双击回自动**

> **修正记录**：本文档早先写成「不做 B，改做 C（三档循环）」，
> **那是错的** —— 用户明确要求的是捏合缩放，且三档跳变无法微调。
> 现已按用户要求改回。

三层机制（优先级从高到低）：

1. **自动阶梯（默认行为）**：轨道数决定高度
   - 1~4 条 → 全部 44px（标准）
   - 5 条 → 全部 37px（过渡）
   - 6 条起 → 全部 30px（最小，仍能看清素材画面）
2. **双指捏轨道头 → 手动覆写**：临时放大/缩小全部轨道。
   一旦用过，该轨道 `autoHeight=false`，阶梯不再自动改它 ——
   用于「放大某个素材打关键帧」这个具体场景。
3. **双击轨道头 → 交还自动**：清掉手动标记，恢复按数量自动压缩。

**尺寸范围：24..200px**（与 core 的 `Track::kMinTrackHeight` /
`kMaxTrackHeight` 完全一致，两处必须同步）。

**原方案 A（单指拖轨道头下缘）已移除** ——
用户要求轨道头只保留眼/喇叭/锁三个图标，不留其它热区。

**历史理由保留（说明为什么当初误判）**：

1. **物理上讲不通**：轨道头列宽度是 `44px`（`editor.css:172` `.floating-heads { width: 44px }`），
   `.fh` 内按钮只有 16px。在这个宽度上做双指手势，两根手指并排就超过 44px —— 点不到。
2. **语义冲突**：双指在时间轴上已经有明确含义（改密度）。若在轨道头上改成改高度，
   用户必须记住「同一个手势在不同子区域做不同事」，这是记忆负担而不是便利。
3. **真实需求是「切换视图密度」而不是「连续微调」**：用户要的是「轨道太挤，看不清缩略图」→
   一键变高，而不是一点一点拖。双击循环 3 档正好匹配这个需求，且可重复（双击两次回到原档）。

**如果真机验证后发现用户确实需要连续调全部轨高**，再加方案 B，
但必须限定在 `.floating-heads` 子区域内，且**时间轴区（`.tracks` / `.ruler`）永远只做密度缩放**。

#### 6.3.3 意思 C：轨道上的素材片段本身，在时间轴上捏合改大小 → 不应支持

时间轴上的横向宽度 = **时长**，纵向 = **轨道高**。
片段在时间轴上「变大变小」只有两种可能：改时长（裁剪）或改轨道高。

**画面大小（scale）不能通过时间轴手势改** —— 时间轴上没有「大小」这个维度。

**这是基于维度语义的判断，不依赖剪映**：
若允许在片段上捏合改 `scale`，同一块区域的双指就有两种含义
（放大画面 vs 拉长时长），用户无法预测。
而把「改时长」固定为「拖两端把手」这一个入口（我们已实现），语义唯一。

> **上一版文档的错误**：我曾写「剪映早期版本有过，后来移除了」。
> **这是编的，撤回** —— 没有任何版本历史证据。
> 结论不变，但理由换成上面的维度语义论证。

**我们的对应约束**：片段上单指拖动 = 移动，拖两端 = 改时长；
**片段上不做任何双指手势** —— 双指事件应冒泡到时间轴层，做密度缩放。

### 6.4 时间密度持久化

`S.pxPerSec` 当前是纯前端变量，重启即丢。规格：

```js
// 键按工程区分，避免不同工程互相污染
const key = `nc.zoom.${projectId}`;
S.pxPerSec = clamp(Number(localStorage.getItem(key)) || 44, 4, 900);
// 松手时写（不是每帧写）
```

**为什么按工程分键**：竖屏短片和横屏长片的最优密度差 10 倍以上，
共用一个值会让用户每次都手动重调。

### 6.5 预览区选择框

选中片段后，预览区叠加一层**可交互选择框**：

```
┌─────────────────────────────┐
│  画布（E.canvas）            │
│    ┌──────────┐             │
│    │  ×     × │  ← 四角握把  │
│    │  片段内容 │             │
│    │  ×     × │             │
│    └──────────┘             │
│     虚线边框 + 8px 内边距     │
└─────────────────────────────┘
```

| 交互 | 动作 |
|---|---|
| 拖框内 | 平移（改 posX/posY） |
| 拖四角握把 | 等比缩放（改 scale） |
| 双指扭转 | 旋转 |
| 框外点击 | 取消选中 |

**几何一致性**：选择框由 `ClipTransform` 反算（`scale` × fit 后矩形 + `posX/posY`），
必须与内核 `clipTransformMatrix` **用同一套数学**，否则框和像素对不上。
规格：抽成纯函数，前后端各一份实现 + 同一组测试向量校验
（与 `PERF_XFORM_PLAN.md` §4.10「一致性只约束几何与时序」一致）。

**选择框用 DOM 覆盖层，不画进 Canvas**（与 `INTERACTION_ENGINE.md` §1 定案 2 一致）：
数量恒为 O(1)，且需要命中测试与焦点语义。

---

## 7. 分阶段计划

每阶段独立可交付、可回滚。**顺序按「用户能立刻感知」排列。**

### G0 — 修 9 个死 op（0.5 天）★ 最优先　【已完成 2026-11-03】

**用户明确要求「轨道左边的小眼睛那 3 个功能也要可以用」。这三个现在完全不响应。**

- `nc_jni.cpp` 的 `DispatchEdit` 补 9 个分支；
- `track_flag` 支持 `locked` / `muted` / `hidden` / `solo` 四种（照抄
  `nc_design_harness.cpp:690-703`）；
- ~~同步 `api_dispatch.inc`~~ —— **实测不需要**：该文件只服务设计桥的
  `nc_*` 直调（`nc_design_harness.cpp:611` include），与 JNI 的 op 派发
  是两套机制。这一步删掉，避免以后照文档白改一遍；
- 前端 9 个 op 补失败回滚（失败时还原本地状态，不留「视觉已改、引擎未改」）。

**验收**：
- 真机点小眼睛 → 该轨画面消失/出现，导出结果一致；
- 点喇叭 → `nc_get_track_mixer` 可见该轨静音；
- 点锁 → 该轨片段不可拖动（`editor.js:1336` 已有 `tr.locked` 判断）；
- 9 个 op 全部返回 `NC_OK`，不再出现「未开放的操作」。

**完成记录（2026-11-03）**：9 个分支已全部补进 `mobile/native/android/nc_jni.cpp`
的 `DispatchEdit`（紧跟 `add_track` 之后）：
`track_flag`（locked/muted/hidden/solo 四选一）、`track_gain`、`duplicate`、
`move_to_track`、`ripple_delete`、`remove_keyframe`、`remove_transition`、
`marker` / `marker_color`（共用一支，颜色作参数）。

- 参数个数与类型逐个比对过 `capi.h` 声明；
- 前端侧无需改动：三个轨道按钮走 `guardedEdit`，失败时 toast 且本地状态
  不动（值来自引擎快照），成功时 `edit()` 内部已调 `applyState + renderAll`；
- `api_dispatch.inc` **不需要同步** —— 它只服务设计桥（`nc_*` 直调），
  与 JNI 的 op 派发是两套机制。
- 本机无法做 `-fsyntax-only`：NDK 的 `jni.h` 与宿主 glibc 头冲突
  （`__GLIBC_PREREQ` 未定义）。改由 CI 验证。

### G1 — 预览多层合成 + 导出 N 层（2 天）★ 画中画能看见的前提　【已完成 2026-11-03】

- `activeVideoClip` → `collectActiveLayers`；
- `renderUncached` 拆成「渲染单层」+「逐层合成」；
- 用 `blendFrameRgba` 合成（16 种模式已在导出验证）；
- **顺带修掉导出侧 `layers[1]` → N 层**。

**验收**：
- 2 条视频轨各放一片段 → 预览能看到上层（默认铺满时会完全遮挡，需配合 G4 的 0.6）；
- 上层设 Screen → 预览与导出**逐像素相同**（同分辨率）；
- 3 层叠加 → 三层都可见（验证 N 层修复）；
- 隐藏上层轨 → 预览立刻回到单层（小眼睛联动验证）。

**完成记录（2026-11-03）**

预览侧（`core/src/preview.cpp`）重构为三段：
- `collectActiveLayers()` —— 收集 t 时刻所有活动视频层，**下标 0 = 最底层**
- `renderClipLayer()` —— 单层渲染（变换/特效/滤镜/蒙版/抠像），
  蒙版与抠像按「是否有下层」选 Transparent / Background
- `compositeLayer()` —— Normal 走逐像素非预乘 alpha；其余模式转 `blendFrameRgba`
- `renderUncached()` 改为「收集 → 逐层渲染 → 自底向上合成」

导出侧（`core/src/export_executor.cpp`）：原逻辑只合成 `layers[0]` 与
`layers[1]`，第 3 层起被静默丢弃。补了一段 `for (extra = 2 …)` 循环把
剩余层按 alpha 垫到当前结果下面。**2 层场景行为完全不变**（循环体不执行）。

**一处必须记住的方向差异**（两边结果一致、约定相反，已写进两处注释）：
| 侧 | 收集方式 | layers[0] |
|---|---|---|
| 预览 | `seq->tracks` 正序 | 最**底**层 |
| 导出 | `videoTracks` 正序（小索引在下）后 `rbegin()` 反向 | 最**顶**层 |

测试（`tests/test_preview.cpp` 新增 4 例，含像素级断言）：
- `UpperTrackIsVisible`：上层灰 200 不透明 → 预览首像素必须是 200
  （**修复前是 60**，即只渲染下层，这条直接证明画中画可见）
- `SemiTransparentUpperBlendsWithLower`：上层 opacity=0.5、下层灰 60
  → 结果须落在 120~140（期望 130），证明真的做了 alpha 混合
- `ThreeLayersAllCollected`：3 层不崩且尺寸正确
- `HiddenUpperTrackFallsBackToSingleLayer`：隐藏上层轨后回单层

> 为了让不同素材输出不同灰度，测试里自带一个 `PathAwareReader`
> （`MediaReaderFactory::create()` 不带参数，拿不到素材标识，
> 只能用 `open(path)` 里的关键词区分）。

### G2 — `nc_set_clip_transform` + 面板（2 天）　【已完成 2026-11-03】

- `ClipTransform` + `Clip` 字段 + `enabled`；
- `SetClipTransformCommand`（可撤销，整槽替换）；
- 序列化（默认不写）；
- 3 个 C ABI + JNI op（`transform_set` / `transform_get` / `transform_clear`）；
- `l3-spec.js` 新增面板 `piptransform`：缩放 / X 位移 / Y 位移 / 旋转 / 锚点九宫格；
- **L2「画中画」菜单加第 4 项「画面」**（当前只有 新增/混合模式/蒙版）。

**验收**：
- 面板拖缩放 → 预览立刻变小（不用松手）；
- 松手后 `.nclip` 里有 `transform` 块，重新载入后保持；
- 撤销 → 回到 1.0；
- `enabled=0` 的工程导出，与改动前**逐字节相同**（零改动保证）。

### G3 — 渲染消费变换（1.5 天）★ 让参数真的生效　【已完成 2026-11-03】

- `applyXformStack` 加 `extra` 矩阵参数（默认恒等）；
- `clipTransformMatrix()` 实现（锚点/旋转/缩放/平移，顺序见 §5.2）；
- `resolveScale/posX/posY/rotation` 统一采样（关键帧 > 静态 > 默认）；
- 预览与导出**共用同一函数**。

**验收**：
- `scale=0.5` → 长宽各减半，居中；
- `posX=0.5` → 右移半个画布宽；
- `rotation=90` → 顺时针 90°；
- 关键帧 `scale 1.0→0.5` → 播放时平滑缩小；
- `scale=0`（钳到 0.05）不崩溃；
- 预览与导出同分辨率**逐字节相同**。

### G4 — 默认 0.6 + 入口串联（0.5 天）　【已完成 2026-11-03】

- `addPipTrack()` 成功后自动设 `scale=0.6`；
- L2「画中画」菜单重排：新增画中画 / 画面 / 混合模式 / 蒙版；
- 新增画中画后自动选中新轨道（当前不选，用户不知道加在哪）。

**验收**：
- 点「新增画中画」→ 拖素材进去 → **立刻看到小窗**（不用手动调任何参数）。

**完成记录（2026-11-03，G2 + G3 + G4 一并交付）**

数据模型：
- `Clip::transform`（`ClipTransform`：scale/posX/posY/rotation/anchor/enabled）
- `ClipTransform::clamp()`：scale 0.05..10、pos ±10、rotation ±3600、anchor 0..8
- 默认 `enabled=false` → 既有工程像素零变化（有测试守护）

取值优先级（预览与导出共用同一套）：
```
关键帧 > 静态值 > 默认
有 curve 时用 animation.sample()，否则用 transform 的静态字段
```

变换矩阵（`core/src/xform.cpp`）：
- `clipTransformMatrix()`：`T(pos) · R(θ) · S(scale)`，绕九宫格锚点
- `applyXformStack(..., const Affine2D& extra)` 新重载：
  `extra` 在 fit 之后、**单次重采样之前**并入 —— 不产生二次插值

C ABI（3 个）：
- `nc_set_clip_transform` / `nc_get_clip_transform` / `nc_clear_clip_transform`
- 越界钳制不报错；参数与原值相同时**不产生撤销步**（面板连发 apply 不污染撤销栈）

撤销：`SetClipTransformCommand`，整槽替换（`before_/after_` 快照）

序列化：`enabled=false` 时整块不写；`enabled=true` 时写全 6 字段
（不省略默认值，避免「部分写入」歧义）

渲染接线（预览 + 导出**同一个函数**）：
- `preview.cpp::renderClipLayer` —— extra 矩阵并入重采样
- `export_executor.cpp` —— 零拷贝快路径加 `!transform.enabled` 守卫；
  重采样路径走同一个重载
- `RenderClip::transform` 快照（`export_plan.cpp` 拷贝）

JNI：`transform_set` / `transform_get` / `transform_clear` 三个 op

前端：
- 「画中画」二级菜单新增「画面」项（第 2 位）
- `piptransform` 面板：缩放 / X 位移 / Y 位移 / 旋转 / 锚点九宫格
- 面板打开时回读引擎值（`L3_HOST.transformGet`），避免「滑块显示默认、
  一动就打回原形」
- `commit()` 里 `transform_set` **排在关键帧循环之前**并 `return` ——
  否则拖滑块会顺带打出一堆关键帧（静态值与可动画通道语义不同）
- 新增画中画后自动把新轨上的片段设为 `scale=0.6` 居中（剪映同款默认，
  1.0 会完全盖住主画面，用户以为拖错轨）

测试（`tests/test_clip_transform.cpp`，15 例）：
- 结构：默认中性 / clamp 上下限 / scale 下限 0.05
- 命令：应用与撤销 / 幂等不产生撤销步 / 锁定轨道拒绝
- 矩阵：默认单位阵 / 缩放绕中心 / posX=1 右移一个画布宽 / 旋转 90°
- 渲染：**disabled 时逐字节相同** / enabled 时像素真的变（四角露背景）/
  关键帧覆盖静态值
- 序列化：往返保真 / disabled 时不写块

### G5 — 预览区手势（2.5 天）　【已完成 2026-11-03】

- 选择框（DOM 覆盖层）+ 四角握把 + 框内拖动；
- 双指捏合 → scale；双指扭转 → rotation；双指平移 → posX/posY；
- 把当前「双指改 S.zoom」改为「双击切换看画面模式」（§6.1）。

**验收**：
- 选中片段 → 出现选择框，框与像素对齐（误差 ≤ 1px）；
- 双指捏 → 片段变小，松手落库，导出一致；
- 双击 → 进入看画面模式（可放到 4×），再双击复位，**不写工程**；
- 未选中时双指 → 只改看画面模式，不写工程。

**完成记录（2026-11-03）**

核心定案落地：**用「有没有选中片段」区分「改画面」与「看画面」**

| 场景 | 双指捏 | 双指扭转 | 单指拖动 | 双击 |
|---|---|---|---|---|
| **有选中片段** | 改 `scale`（写工程） | 改 `rotation`（写工程） | 改 `posX/posY` | 复位（`transform_clear`） |
| **无选中** | 改 `S.zoom`（看画面，不写） | 忽略 | 平移看画面 | 复位缩放 |

实现要点：

1. **`makePinch` 扩展支持旋转**（不破坏既有调用方）
   - 新增 `angle()`：`atan2` 算两指连线角度
   - `onRotate(deltaDeg)` 只在调用方声明时才跑 —— 时间轴缩放入口没声明，
     行为完全不变
   - **传「自手势开始的累计角度」而非本帧增量**：增量需要调用方自己累加，
     丢一帧就永久偏差

2. **拖动中只改 CSS，松手才写引擎**
   - `applyPipPreview()` 用 CSS transform 让用户立刻看到变化
   - 为什么：每帧重解码太慢（拖动会掉到个位数帧率），CSS 是 GPU 合成
   - `commitPip()` 松手提交一次 = 一次撤销步

3. **选择框（`.pip-box`）**
   - DOM 覆盖层，`outline: dashed` + 四角 14px 圆形握把
   - 拖框内 = 平移；拖任意角 = **等比缩放**（用「到画布中心的距离比」，
     不是单轴位移 —— 保证不变形）
   - 几何与 `applyPipPreview` 共用同一套数学，框与像素对齐
   - 点时间轴空白 = 取消选中并隐藏框

4. **`pipTarget()` 过滤掉不可操作的片段**
   - 轨道锁定 → null（不能改）
   - 音频轨 → null（没有画面）
   - 无选中 → null（退化成「看画面」模式）

未做的（G5 范围外）：
- 四边握把（各向异性缩放）—— 需要 scaleX/scaleY 两个字段，剪映也没有
- 选择框旋转握把 —— 双指扭转已覆盖

### G6 — 轨道视图密度（1 天）　【已完成 2026-11-03】

> **⚠ 取消的是「三档跳变」，不是捏合。别搞混。**
>
> 用户 2026-11-03 原话：
> 「不是，双手放大和缩小是有的。只是取消那三个档…」
> 「三个档位我也取消了，早就取消了」「这样就不做那个按键了」
>
> **已取消（不要再加回来）**：
> 1. ~~轨道头双击 → 循环 3 档高度（32/48/96 跳变）~~
> 2. ~~常驻缩放滑杆（− / +）~~
> 3. ~~轨道头双击回原档~~
>
> **保留并正在用**：
> - **双指捏合放大/缩小 —— 有，且是主要交互**
> - **双击轨道头** = 交还自动阶梯（不是切档）
>
> **阶梯规则（用户原话，逐字）**：
> 「一到四是标准的高度。我从四开始，然后夹的时候就慢慢的变小…
>   从第 4 个轨道开始，每加一个轨道就小那么一点，
>   直到…小到咱们规定的那个最小值之后，就不能再小了，
>   后面的再添加，就是直接是最小值。」
>
> ⇒ **逐条渐变递减**，不是跳档：
>
> | 轨道数 | 1–4 | 5 | 6 | 7 | 8 | 9 | 10 | 11+ |
> |---|---|---|---|---|---|---|---|---|
> | 行高 | 44 | 42 | 40 | 38 | 36 | 34 | 32 | 30 |
>
> **捏合范围 24..200**（core 的 kMin/kMaxTrackHeight）。
> 注意 24 与阶梯下限 30 是两个概念：30 是「自动规则的下限」，
> 24 是「手动能捏到的极限」。
>
> 我在 2026-11-03 曾误写成「捏合也取消、只留三档」，与用户原意相反；
> 后又误把阶梯写成「5 条 37 / 6 条 30」的跳变。两次都已纠正。

**原始需求（已废弃，仅存档）**

- ~~轨道头双击 → 循环 3 档高度~~（紧凑 32 / 标准 48 / 宽松 96，钳 24..200）
- ~~常驻缩放滑杆（带 − / +）~~

**完成记录（2026-11-03）**

1. **轨道高度 = 自动阶梯（默认）+ 双指捏合（手动覆写）+ 双击（交还自动）**
   - 自动阶梯：`ladderHeight()` —— **逐条渐变递减**
     1~4 条 44 / 5 条 42 / 6 条 40 / 7 条 38 / 8 条 36 /
     9 条 34 / 10 条 32 / 11 条起 30
     （步长 ROW_STEP=2，减到 ROW_MIN=30 为止）
   - **双指捏合（轨道区）** = 行高 + 时间轴 + 轨道头图标三者联动缩放
     `scaleRowsAndTimeline()`，两轴各按自己的基准乘同一 ratio
   - 双指捏轨道头：`setAllRowHeights()` 按比例缩放全部轨道
     → 置 `autoHeight=false`（阶梯不再覆盖）
   - 双击轨道头：`resetRowHeightsAuto()` 清标记，回到自动
   - 捏合范围 24..200（`ROW_SCALE_MIN/MAX`），与
     `Track::kMinTrackHeight/kMaxTrackHeight` 一致
   - **⚠ 两个下限不要混用**：`ROW_MIN=30` 是阶梯下限，
     `ROW_SCALE_MIN=24` 是捏合极限。曾用 ROW_MIN 去夹取行高，
     导致捏到 24~29 时被强行弹回 30，小尺寸区间捏合完全失效。
   - **曾误实现为「三档循环 [32,48,96]」，已删除** —— 档位跳变无法微调
   - 已知代价：N 条轨 = N 个命令 = 撤销要退 N 步；
     要做到一次撤销需新增批量 ABI（记入 backlog）

2. **密度持久化**（`nc.zoom.<工程名>`）
   - 按**工程名**分键：竖屏短片与横屏长片的最优密度差 10 倍以上，
     共用一个值会让用户每次都手动重调
   - 拖动中不写（每帧写会拖慢），`change`/`onEnd` 时写一次

3. ~~**常驻缩放滑杆**（播放条右侧，− / 滑杆 / +）~~ —— **已取消**
   （以下为实现记录，仅存档；DOM 是否移除见后续清理）
   - 与双指捏合改**同一个** `S.pxPerSec`，双向同步
   - **踩过的坑（重要）**：一开始用 `position:absolute; right:8px`，
     滑杆直接压在垃圾桶上。改为给 `.transport` 加左右 `padding`
     留出等宽空白（保证三键仍居中）+ 右留白容纳滑杆；
     并加一条 `border-left` 分隔线把两组视觉分开
   - 393px 实测：滑杆组需 90px，右留白 88px —— 边缘略紧但不重叠；
     360px 下把滑杆再收窄到 36px

---

## 8. 风险与明确不做

### 风险

| 风险 | 缓解 |
|---|---|
| 预览多层后单帧耗时翻倍 | 先接帧缓存（`preview.h:71` 已有 96MB 预算）；层数 > 4 时按 `PERF_XFORM_PLAN.md` P2 降质 |
| 多层合成顺序与导出不一致 | 两边都从 `seq->tracks` 顺序遍历，写死「下标 0 = 最底层」并在两处加同一句注释；3 层工程逐字节比对 |
| 画中画变换与「转换」8 项相互干扰 | 画中画矩阵作为 `extra` 预乘；8 槽仍按枚举顺序合并；测试「同时开旋转槽 + 画中画旋转」 |
| 选择框与像素不对齐 | 前后端共用测试向量（同一组 `(scale,pos,rot,anchor)` → 期望矩形） |
| 手势与时间轴平移抢事件 | 走 `INTERACTION_ENGINE.md` §4.1 的 `InputArbiter`；G5 前先落 I3 |
| `enabled` 默认搞反导致老工程像素变化 | 默认 `false` + 「老工程载入后导出逐字节相同」回归测试 |

### 明确不做

- ❌ **不新增 `TrackType::Pip`** —— 代价远大于收益（§3）；
- ❌ **不在时间轴上给片段做「画面大小」捏合** —— 维度混淆（§6.3 意思 C）；
- ❌ **不做各向异性缩放（scaleX/scaleY 分离）**；
- ❌ **不做旋转握把** —— 双指扭转足够；
- ❌ **不做「画中画磁吸边界 / 自动对齐」** —— 后期再说，先把基本功能做对；
- ❌ **不做假的「当前层高亮」** —— 层高亮必须来自真实选中态。

---

## 9. 排期汇总

| 阶段 | 内容 | 工期 | 依赖 | 用户可感知 |
|---|---|---|---|---|
| **G0** | 修 9 个死 op（含三个轨道按钮） | 0.5d | — | ✅ 立刻 |
| **G1** | 预览多层合成 + 导出 N 层 | 2d | — | ✅ 画中画能看见 |
| **G2** | `nc_set_clip_transform` + 面板 | 2d | — | ✅ 能调大小 |
| **G3** | 渲染消费变换（含关键帧） | 1.5d | G1, G2 | ✅ 参数真生效 |
| **G4** | 默认 0.6 + 入口串联 | 0.5d | G2, G3 | ✅ 点一下就有小窗 |
| **G5** | 预览区手势 + 选择框 | 2.5d | G3 | ✅ 直接在画面上拖 |
| **G6** | 轨道视图（自动阶梯 + 捏合联动 + 密度持久化）<br>~~三档循环 / 缩放滑杆~~ 已取消 | 1d | — | ✅ 捏合即可调节 |
| 合计 | | **10d** | | |

**G0–G4（6.5 天）做完，画中画功能就完整可用**；G5–G6 是手感打磨。

---

## 10. 如何复核本文

```bash
# 1) 9 个死 op（应输出 9 行，每行结尾 0）
for op in duplicate marker marker_color move_to_track remove_keyframe \
          remove_transition ripple_delete track_flag track_gain; do
  printf "%-20s %s\n" "$op" "$(grep -c "op == \"$op\"" mobile/native/android/nc_jni.cpp)"
done

# 2) 预览只取单层
sed -n '16,41p' core/src/preview.cpp

# 3) 混合模式在预览零命中（应输出 0）
grep -c blendMode core/src/preview.cpp

# 4) scale 控件有定义、无面板引用（应为 0）
grep -n "scale:.*prop: 1" mobile/assets/l3-spec.js
grep -c "\['scale'" mobile/assets/l3-spec.js

# 5) 采样了但不消费
grep -n "frame.scale\|frame.posX" core/src/preview.cpp

# 6) 导出硬编码 2 层（应为 1）
grep -c "layers\[1\]" core/src/export_executor.cpp

# 7) 预览区捏合只改 CSS（S.zoom 全在 JS 局部）
sed -n '1734,1751p' mobile/assets/editor.js
grep -c "S.zoom" mobile/assets/editor.js

# 8) 轨道头捏合不存在（应为 0）
grep -c "makePinch(E.heads" mobile/assets/editor.js
```

---

## 文档维护约定

- 每个阶段完成后回填实测值；
- 修改 §4.1 的 `ClipTransform` 字段定义时，必须同步序列化、C ABI、前端三处；
- 新增任何「缩放」手势前，先确认它属于 §1 的 ①②③④ 哪一类，**不允许语义漂移**。
