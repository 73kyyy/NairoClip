# S5 自动字幕模型安装与密钥链路审计

日期：2026-11-22  
范围：NairoClip Android / Whisper 自动字幕模型路径  
分支：`work/v3.3.3-s5-native-editor-foundation`

## 结论

**当前 Whisper 自动字幕会话具备“从已解密内存加载模型”的底层接口，但 Android 尚未形成可验收的安全模型安装、密钥管理、解密与会话加载闭环。** 不应把现有 `installModel()` 当成加密安装流程，也不应把 `nc_model_decrypt()` 的存在当成 Android 已接入 Keystore。

## 已核实的代码事实

1. `mobile/native/android/NativeEngineBridge.kt::installModel(modelId, filePath)`
   - 原始实现直接接受调用方提供的任意 `filePath`；后续安全修正已将规范化路径限制在应用私有模型目录内。
   - 通过 `File.readBytes()` 一次性把整个模型读入 JVM 内存。
   - 在本地计算 SHA-256，并把同一个本地计算值作为 `expected_sha256_hex` 传入 JNI。
   - 当前签名没有来自可信服务器清单的期望摘要，也没有签名清单验证。\n   - 后续安全修正已把路径规范化并限制在应用私有模型目录内，拒绝目录外路径/符号链接逃逸；同时限制超过 JNI byte-array 上限的文件，并在 JNI 返回后清零临时 Java byte array。

2. `mobile/native/android/nc_jni.cpp::nativeInstallModel`
   - 再把整个 Java byte array 复制到 C++ `std::vector<uint8_t>`。
   - 调用 `nc_model_install()` 校验并记录路径/状态；这条 JNI 路径没有执行磁盘加密、原子替换或 Keystore 操作。
   - 大模型安装会同时产生 Java byte array 与 C++ vector 的大块内存副本，存在明显的峰值内存 / OOM 风险。

3. `core/src/capi.cpp::nc_model_install`
   - `verifyModelPayload()` 校验传入字节与传入摘要是否一致，然后记录 `ModelRecord`。
   - 它不负责文件读写，也不负责把模型密文写入安全目录。
   - 由于 Android 当前把本地计算的摘要原样作为期望摘要传入，这不是对服务器发布文件的独立真实性验证。

4. `core/src/model_manager.cpp` / `core/src/crypto.cpp`
   - 已有 SHA-256、AES-256-CTR 与模型载荷加/解密原语。
   - AES-CTR 本身不提供认证标签；安全性依赖外部可信摘要/签名链路。
   - 当前 Android `NativeEngineBridge.kt` 没有 `AndroidKeyStore`、密钥解包或模型解密调用。

5. `core/src/capi.cpp::nc_model_decrypt` 与 Whisper 会话：
   - C API 提供模型解密函数，但本次核验未发现对应 Android JNI / Kotlin 安全加载桥。
   - `nc_speech_session_load_memory()` 要求 encoder、decoder、tokenizer 三者以明文内存缓冲传入，并在会话内加载。
   - `NativeSpeechSessionWorker.load()` 接收调用方提供的 direct ByteBuffer，并在调用完成后尽力清零；它不负责从已安装模型文件安全取密钥、解密或验证文件签名。

## 风险等级

- **P0：可信完整性链路缺失。** 本地 SHA 与本地文件自校验不能替代服务器签名清单中的预期 SHA-256。
- **P0：安全安装闭环未实现。** 当前 JNI 安装路径只验证并记录模型，不负责加密写入或原子安装；不能据此声称模型静态存储为密文。
- **P1：Android Keystore 未接入。** 当前没有可核实的 Keystore 密钥创建、密钥封装/解封、轮换或删除链路。
- **P1：大模型内存峰值风险。** Kotlin 全量读入后 JNI 再复制一次，不适用于 509 MiB / 1.9 GiB 等模型目录项。
- **已缓解：文件路径边界。** `installModel()` 通过 `NativeModelPathPolicy` 检查规范化路径必须位于应用私有模型目录内，拒绝目录外路径、目录本身和符号链接逃逸；临时 Java byte array 在 JNI 返回后尽力清零。`NativeModelPathPolicyTest` 已覆盖目录内文件、根目录、目录外路径、`../` 路径穿越及符号链接目标。

## 后续实施顺序（不代表已完成）

1. 定义可信模型清单：模型 ID、版本、精确字节数/允许范围、文件 SHA-256、算法版本、密钥版本；清单必须通过服务端签名验证。
2. 将下载与安装拆分为受控 staging 流程：规范化私有目录、限制路径穿越、限制大小、流式哈希、失败清理、安装前完整性复验。
3. Android 使用 Android Keystore 保存不可导出的设备密钥；服务器模型密钥通过已认证协议解封装，禁止写入文件、日志或持久化明文配置。
4. 使用带认证的加密封装（优先 AES-256-GCM 或经审查的 AEAD），为每个模型版本/安装实例生成唯一 nonce；校验认证标签后才允许推理。不得把裸 AES-CTR 当作 AEAD。
5. 以临时密文文件写入、fsync、原子 rename 完成安装；更新元数据与文件状态必须是可恢复事务。
6. 增加 Android JNI 的流式文件校验/解密接口，避免把 1–2 GiB 模型复制两次；推理加载优先使用受控 direct buffer / mmap 策略，并尽力清零明文缓冲。
7. 将安全模型加载器接入 `NativeSpeechSessionWorker`，确保 encoder、decoder、tokenizer 来自同一已验证模型版本；任一文件校验/解密失败时不得进入 loaded 状态。
8. 补充测试：签名错误、摘要错误、密文篡改、认证标签错误、nonce 复用保护、路径穿越、超大文件、磁盘空间不足、安装中断/恢复、原子替换、Keystore 密钥丢失/轮换、并发加载与清理。

## 验收口径

只有当 Android 真实下载/安装路径、Keystore 密钥管理、AEAD 验证、原子落盘、内存解密、Whisper 三文件加载和失败清理全部接通，并有 CI 与真机证据时，才能将“自动字幕模型安全安装闭环”标记完成。

本审计只记录本次源码核验发现，不修改 Activity 正式入口、不删除 WebView、不绕过 S1-3 / S3-1 门禁。


## 2026-11-22 复核补充：PCM 时间戳与模型安装边界

### 自动字幕 PCM 时间轴

- 新增 `NativeSpeechPtsTracker`，以首个输出 PTS 和累计解码 PCM 帧数计算预期时间，不再用“上一块实际 PTS + 本块帧时长”逐块累积误差。
- 默认容差为 2,000us：允许解码器时间戳量化误差，但拒绝超过容差的正向间隙和负向重叠，避免将有时间断层的音频静默拼接后继续生成字幕。
- `AndroidAudioPcmDecoder` 在输出 PCM 块进入重采样器前执行连续性校验；首块 PTS 仍作为音频起点返回，供片段 seek-preroll 裁剪与时间轴映射使用。
- 新增测试覆盖累计时钟、逐块抖动不累积、正向间隙、负向重叠、原 50ms 容差漏洞及 2ms 边界。
- 当前 PR HEAD `848d113b8d25e82b3c1182b1ba82def66d0323e3` 的 S5 Android Validation 已通过：arm64-v8a NDK 交叉编译、Host CMake/CTest、Android Kotlin 编译和 JVM 单元测试。全量 CI 的普通 build-and-test 已通过；ASAN 与 UBSan 仍在运行。以上不构成 MediaCodec 真机兼容性验收。

### 模型安全链路复核（仍未闭环）

- 再次核实 `core/src/model_manager.cpp::verifyModelPayload`：core 会将传入 payload 的 SHA-256 与 `expectedSha256Hex` 做恒定时间比较，但它无法判断摘要是否来自可信发行方。
- `mobile/native/android/NativeEngineBridge.kt::installModel` 当前仍从同一待安装文件计算摘要，再将该值作为 expected digest 传入；因此该步骤只检测传输/读取后的一致性，不能防止攻击者同时替换文件内容。
- `mobile/native/android/nc_jni.cpp::nativeInstallModel` 将 Java byte array 再复制到 C++ vector；大模型峰值内存会同时包含至少一份 Java 文件副本和一份 C++ payload 副本。现有大于 JNI byte-array 上限的拒绝逻辑只能避免整数溢出，不能解决数百 MiB 文件的 OOM 风险。
- `nc_model_install` 只更新模型状态记录，不负责将下载文件加密写入、原子替换或认证后解密；`nc_model_decrypt` 使用 AES-CTR，未提供 AEAD 认证标签。Android 侧仍未接入 Keystore 密钥封装/解封和认证解密。
- `deriveModelCounter(modelId, version)` 产生确定性 CTR 初始计数器。迁移到 AEAD 时，必须由每个模型密文实例生成唯一随机 nonce，并将 nonce、算法/密钥版本、模型 ID、模型版本、长度等元数据纳入认证；不能沿用“模型 ID + 版本确定性 nonce”作为新 AEAD 的 nonce 策略。
- 因此模型安装仍保持未完成状态；不得将本地 SHA-256、自定义 CTR 或模型状态记录描述为可信模型发布验证、加密静态存储或 Keystore 保护。

### 下一步实施约束

1. 先定义并测试签名 manifest 的 canonical serialization、签名算法/公钥轮换和模型元数据绑定，再接入网络下载；客户端内置发行公钥只能用于验签，不能把服务器传来的摘要本身当作信任根。
2. 将 JNI byte-array 安装接口迁移为受控 staging 文件/文件描述符流式接口，先完成长度上限、流式摘要、文件身份/路径校验、失败清理和原子替换，再接入解密。
3. Android Keystore 仅保存不可导出的设备包装密钥；模型内容密钥须经已认证协议封装，且不能写入日志、普通配置或明文文件。
4. AEAD 必须在认证标签验证成功后才向推理加载器暴露明文；如果采用分块 AEAD，每块必须有独立 nonce/唯一派生子 nonce、序号和认证关联数据，并验证整文件 manifest，防止块重排、截断和拼接攻击。
5. 模型安全接口变更需保留 API 24 最低版本兼容性，并新增 Android 单元/仪器测试；没有真机证据时不得标记 MediaCodec、Keystore 或大模型内存峰值验收通过。


### 2026-11-23 — ASR cancellation dispatch race
- Static review found a race between the worker accepting a task and the native speech-session transcribe call entering its RUNNING state. A cancel request in that window can set the Kotlin cancellation token while the native cancel API still reports NOT_FOUND; native inference startup also resets its internal cancellation flag.
- Added a worker-side guard around both direct transcribe and decode/transcribe/insert inference calls. It checks the cancellation token immediately before dispatch and again after synchronous JNI return. A cancellation that wins the dispatch window therefore prevents inference from starting or discards returned segments before subtitle mapping/commit.
- Added JVM regression tests for cancellation before dispatch, cancellation during the synchronous native call, and the successful non-cancelled path.
- Limitation: this closes the lost-result/incorrect-commit race; it does not guarantee immediate interruption if native inference has already started. Prompt interruption remains dependent on the native recognizer's cooperative progress callback. Android CI must validate the new Kotlin code; device-level cancellation latency remains unverified.
