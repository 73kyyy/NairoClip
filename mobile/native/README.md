# 原生接入（第三方登录）

本目录存放**第三方登录 SDK 与原生桥实现**。前端 `assets/auth.js` 只负责调用，
真正的 App 拉起与授权在原生层完成。

## 前端契约

```js
// 发起授权（点列表里的微信/QQ/Apple 即调用）
window.NCAndroid.authorize('wechat')                            // Android
window.webkit.messageHandlers.ncAuth.postMessage({provider:'wechat'})  // iOS

// 原生层授权完成后回调前端
window.NC_AUTH_DONE('wechat', { ok: true,  uid, nickname, avatar })
window.NC_AUTH_DONE('wechat', { ok: false, error: '用户取消' })
```

`NC_AUTH_DONE` 收到 `ok:false` 时，前端只提示原因，**不写登录态、不跳转**——
用户拒绝授权后仍停留在当前窗口。

## 目录

```
native/
  libs/
    wechat-sdk-6.8.40.aar                 微信 OpenSDK（官方 Maven）
    open_sdk_3.5.19_r9483ffc7_lite.jar    QQ 互联 SDK（官方 CDN）
  android/
    NativeAuthBridge.kt    JavascriptInterface 实现
    WXEntryActivity.kt     微信回调入口（必须在 <包名>.wxapi 下）
  ios/
    NativeAuthBridge.swift WKScriptMessageHandler + ASAuthorization
  assets/
    android/mipmap-*/ic_launcher.png       各密度图标
    ios/AppIcon-1024.png                    App Store 图标
    appicon.svg                             矢量源文件
```

## SDK 来源与校验

| SDK | 版本 | 来源 | MD5 |
|---|---|---|---|
| 微信 OpenSDK | 6.8.40 | `repo1.maven.org/maven2/com/tencent/mm/opensdk/wechat-sdk-android/` | `51a620bba4216ac03cefa9016c870e5c` |
| QQ 互联 SDK | 3.5.19 | `tangram-1251316161.file.myqcloud.com`（wiki.connect.qq.com 官方下载页） | `e359b2c40d2a127b73eb5b7f6f383edd` |

两者均为官方发布包，未做任何修改。

## Android 接入

### build.gradle

```gradle
android {
    compileSdk 34
    defaultConfig {
        applicationId "com.nairoclip.app"
        // 通过 BuildConfig 注入，避免把 AppID 硬编码在源码里
        buildConfigField "String", "WECHAT_APP_ID", "\"wx你的AppId\""
        buildConfigField "String", "QQ_APP_ID", "\"100xxxxxx\""
        buildConfigField "String", "AUTH_BACKEND", "\"https://api.your-domain.com\""
    }
    buildFeatures { buildConfig true }
}

dependencies {
    implementation files('libs/wechat-sdk-6.8.40.aar')
    implementation files('libs/open_sdk_3.5.19_r9483ffc7_lite.jar')
    implementation 'com.squareup.okhttp3:okhttp:4.12.0'
}
```

### AndroidManifest.xml

```xml
<!-- 微信：路径必须是 <包名>.wxapi.WXEntryActivity，类名固定，否则收不到回调 -->
<activity
    android:name=".wxapi.WXEntryActivity"
    android:exported="true"
    android:taskAffinity="com.nairoclip.app"
    android:launchMode="singleTask"
    android:theme="@android:style/Theme.Translucent.NoTitleBar" />

<!-- QQ：授权回调 -->
<activity
    android:name="com.tencent.tauth.AuthActivity"
    android:launchMode="singleTask"
    android:noHistory="true" />
<activity
    android:name="com.tencent.connect.common.AssistActivity"
    android:screenOrientation="portrait"
    android:theme="@android:style/Theme.Translucent.NoTitleBar" />

<uses-permission android:name="android.permission.INTERNET" />
```

### Activity 里注入桥

```kotlin
webView.settings.javaScriptEnabled = true
webView.addJavascriptInterface(NativeAuthBridge(this, webView), "NCAndroid")
webView.loadUrl("file:///android_asset/www/01-home.html")
```

## iOS 接入

### Podfile

```ruby
pod 'WechatOpenSDK', '~> 2.0.4'
pod 'TencentOpenAPI', '~> 3.5.19'
```

### Info.plist

```xml
<key>NCWeChatAppId</key><string>wx你的AppId</string>
<key>NCQQAppId</key><string>你的QQAppId</string>
<key>NCAuthBackend</key><string>https://api.your-domain.com</string>

<key>LSApplicationQueriesSchemes</key>
<array>
  <string>weixin</string>
  <string>weixinULAPI</string>
  <string>mqq</string>
  <string>mqqopensdkapiV2</string>
</array>

<key>CFBundleURLTypes</key>
<array>
  <dict>
    <key>CFBundleURLSchemes</key>
    <array><string>wx你的AppId</string></array>
  </dict>
  <dict>
    <key>CFBundleURLSchemes</key>
    <array><string>tencent你的QQAppId</string></array>
  </dict>
</array>
```

### 创建 WebView 时注入

```swift
let bridge = NativeAuthBridge(webView: webView, host: self)
webView.configuration.userContentController.add(bridge, name: "ncAuth")
webView.configuration.userContentController.addUserScript(bridge.platformScript)
```

`platformScript` 会注入 `window.NC_PLATFORM='ios'`，前端据此显示 Apple 登录那一行。

## 服务端必须实现的部分（微信）

微信登录的 `code` 换 `access_token` **必须在服务端完成**——`appsecret` 不能放客户端。

```
POST {AUTH_BACKEND}/auth/wechat
Content-Type: application/json
{ "code": "微信返回的 code", "state": "发起时带的 state" }

200 → { "ok": true, "uid": "...", "nickname": "...", "avatar": "..." }
4xx → { "ok": false, "error": "具体原因" }
```

未配置 `AUTH_BACKEND` 时，客户端会明确报「登录服务未配置」，不会伪造登录成功。

QQ 客户端授权可直接拿到 `openid` + `access_token`，昵称头像由客户端再调
`graph.qq.com/user/get_user_info`（已在 `NativeAuthBridge.kt` 内实现）。

Apple 登录无需服务端换 token（`identityToken` 可自行验签，若需要用户信息落库则建议校验）。

## 引擎桥（NCEngine）：JNI / C interop 直调

> 依据 `docs/UI_INTEGRATION.md §7「平台层对接（不要用 HTTP）」`。
> 前端**不含任何 fetch / URL / 端口探测**，只认平台注入的 `window.NCEngine`。

### 前端契约

| 方法 | 返回 | 对应 C ABI |
|---|---|---|
| `NCEngine.state()` | 工程快照 JSON | `nc_project_*` / `nc_track_get` / `nc_clip_info` / `nc_asset_get` |
| `NCEngine.call(fn, argsJson)` | `{ok,code,message,result}` | `nc_split_clip` / `nc_remove_clip` / `nc_set_clip_filter` … |
| `NCEngine.frameDataUrl(timeUs, w)` | `data:image/png;base64,…` | `nc_preview_render` |
| `NCEngine.thumbDataUrl(assetId, i, w)` | 同上 | `nc_thumbnail_generate` |
| `NCEngine.presetFrame(kind, style, t, w)` | 同上 | `nc_set_clip_filter`/`nc_add_effect`/`nc_set_clip_mask` + 渲染 + 还原 |
| `NCEngine.waveform(assetId)` | `{"buckets":[…]}` | `nc_waveform_generate` + `nc_waveform_bucket_get` |
| `NCEngine.models()` | `{"models":[…]}` | `nc_model_catalog` / `nc_model_info` / `nc_model_status` |
| `NCEngine.listProjects()` | `{"projects":[…]}` | 平台层扫描私有目录（C ABI 无目录接口） |

**像素格式**：`nc_preview_render` / `nc_thumbnail_generate` 输出 **RGBA8 原始像素**，
不是 PNG。桥在平台层编码（Android `Bitmap.compress`，iOS `UIImage.pngData`），
再转 `data:` URL 给 `<img>`，全程无网络。

### Android

```kotlin
NativeEngineBridge.loadLibrary()                    // System.loadLibrary("nc_jni")
val engine = NativeEngineBridge(applicationContext)
webView.addJavascriptInterface(engine, "NCEngine")
// onDestroy: engine.release()
```

`native/android/nc_jni.cpp` → 编译进 `libnc_jni.so`：

```cmake
add_library(nc_jni SHARED nc_jni.cpp)
target_include_directories(nc_jni PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include)
target_link_libraries(nc_jni nairoclip_core)
```

### iOS

```swift
let bridge = NativeEngineBridge()
config.userContentController.addUserScript(
    WKUserScript(source: bridge.injectedScript(),
                 injectionTime: .atDocumentStart, forMainFrameOnly: true))

// WKUIDelegate：把同步 prompt 转给桥（WebKit 下唯一可靠的同步 JS→Native 通道）
func webView(_ webView: WKWebView,
             runJavaScriptTextInputPanelWithPrompt prompt: String,
             defaultText: String?,
             initiatedByFrame frame: WKFrameInfo,
             completionHandler: @escaping (String?) -> Void) {
    completionHandler(bridge.handle(prompt: prompt))
}
```

### 串行化

`NcEditor*` 不支持多线程并发：Android 侧 `@Synchronized`、iOS 侧 `NSLock`，
所有 `nc_*` 调用都在同一把锁内执行。

## 关于「不用 HTTP」的说明

`docs/UI_INTEGRATION.md §7` 要求：**本地剪辑引擎**对平台层走 JNI/Swift 直调 C ABI，
不要用 HTTP。这是针对引擎调用说的。

第三方登录本身**无法**绕开 HTTP：
- 微信/QQ 的 OAuth 是对方服务器的 HTTPS 接口
- `code` 换 token 只能服务端做

两者不冲突：引擎走 C ABI，登录走 HTTPS。

## 图标

`native/assets/` 下已生成：

| 平台 | 路径 | 尺寸 |
|---|---|---|
| Android | `android/mipmap-mdpi/ic_launcher.png` | 48 |
| Android | `android/mipmap-hdpi/ic_launcher.png` | 72 |
| Android | `android/mipmap-xhdpi/ic_launcher.png` | 96 |
| Android | `android/mipmap-xxhdpi/ic_launcher.png` | 144 |
| Android | `android/mipmap-xxxhdpi/ic_launcher.png` | 192 |
| iOS | `ios/AppIcon-1024.png` | 1024 |

重新生成：`bash tools/build_appicon.sh`

Android 自适应图标（Android 8+ 建议）：

```xml
<!-- res/mipmap-anydpi-v26/ic_launcher.xml -->
<adaptive-icon>
  <background android:drawable="@color/icon_bg" />  <!-- #0A0A0A -->
  <foreground android:drawable="@mipmap/ic_launcher_foreground" />
</adaptive-icon>
```

> 注：自适应图标的前景层需要单独导出（字标居中、四周留 33% 安全边距）。
> 当前 `ic_launcher.png` 是完整方形图标，可直接用作传统图标或 iOS 图标。
