# S1-4A / S1-8A Android 媒体导入实现与安全审计

状态：**代码接线已补充；设备端验收未完成**

## 已实现的路径

1. WebView 文件选择由 Android `WebChromeClient.onShowFileChooser` 接管。
2. 用户通过系统文件选择器明确选取 `content://` URI。
3. Android 原生层将选中文件复制到 `filesDir/imports/`，先写唯一临时文件、同步文件描述符，再 rename 为随机前缀文件名。
4. 只向 WebView 返回受限 `FileProvider` URI；FileProvider 只暴露 `imports/` 子目录。
5. Kotlin `importPickedMedia` 只接受随机文件标识，不接受路径；原生侧再次规范化路径并验证父目录、常规文件和非空文件。
6. `nc_import_asset` 的通用 JavaScript bridge 入口被拒绝；导入只经受控原生方法调用。
7. 素材复制已移出 Android UI 线程，并使用 generation token 避免 Activity 销毁或后续选择后回调旧 WebView。
8. 视频音轨提取改为 Android `MediaExtractor + MediaMuxer` 系统流复制，输出到应用私有 imports 目录，再由受控桥登记为音频素材；不走 FFmpeg 软件解码/编码路径。
9. 预览帧请求失败时，编辑器读取原生预览错误并显示“预览解码失败”提示，不再静默隐藏失败原因。

## 安全边界

- WebView 不传入任意绝对路径。
- 素材源文件名由系统 ContentProvider 提供；原生侧要求 32 位随机十六进制前缀及受限字符集。
- 目标目录固定在应用私有 `filesDir/imports`，并通过 canonical path 检查防止路径越界。
- 文件复制前检查声明大小与可用空间，复制期间持续检查可用空间；失败时删除临时文件和本次已复制文件。
- FileProvider 不导出，且仅开放 imports 子目录。
- 音频提取不使用 FFmpeg 的软件解码/编码器；若设备系统 MediaMuxer 不支持源音频编码，明确失败并删除不完整输出。

## 尚未完成的验收

- Android 真机文件选择器单选/多选及不同厂商 ContentProvider 测试。
- 验证 WebView Chromium 对 FileProvider URI 的 `File.name` 是否稳定返回原生生成的 storageName；若设备实现不满足，必须改用显式安全 token 传递，不得放宽为任意路径。
- 大文件复制、低磁盘空间、读取中断、Activity 重建/销毁测试。
- MediaExtractor/MediaMuxer 对 AAC、AMR 等系统支持格式和不支持格式的真机测试。
- 导入视频/图片/音频后真实预览、缩略图、时间轴登记和保存重开验证。
- 解码失败、损坏媒体、无音轨媒体的明确错误提示验证。
- Android Gradle/NDK 构建及 instrumentation 测试尚未运行。

因此 S1-4A 与 S1-8A 当前仍不可标记完成。
