import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val localProps = Properties().apply {
    val f = rootProject.file("local.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}
fun secret(key: String, env: String): String =
    (localProps.getProperty(key) ?: System.getenv(env) ?: "").trim()

android {
    namespace = "com.nairoclip.app"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.nairoclip.app"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"

        buildConfigField("String", "WECHAT_APP_ID", "\"${secret("wx.appId", "WX_APP_ID")}\"")
        buildConfigField("String", "QQ_APP_ID", "\"${secret("qq.appId", "QQ_APP_ID")}\"")
        buildConfigField("String", "AUTH_BACKEND", "\"${secret("auth.backend", "AUTH_BACKEND")}\"")

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
    }

    buildFeatures {
        buildConfig = true
    }

    // 每次打包前刷新前端资源，避免 APK 里是旧版 UI
    tasks.named("preBuild") { dependsOn("syncMobileUi") }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    signingConfigs {
        // 签名文件由 CI 生成到 android/app/nairoclip-release.jks（不入库）。
        // 本地没有该文件时退回 debug 签名，保证 assembleRelease 仍可跑。
        val ks = file("nairoclip-release.jks")
        if (ks.exists()) {
            create("release") {
                storeFile = ks
                storePassword = secret("keystore.password", "KEYSTORE_PASSWORD").ifEmpty { "nairoclip" }
                keyAlias = secret("keystore.alias", "KEYSTORE_ALIAS").ifEmpty { "nairoclip" }
                keyPassword = secret("keystore.keyPassword", "KEY_PASSWORD").ifEmpty { "nairoclip" }
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = if (file("nairoclip-release.jks").exists())
                signingConfigs.getByName("release")
            else signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }

    packaging {
        resources.excludes += setOf(
            "META-INF/*.kotlin_module",
            "META-INF/DEPENDENCIES",
            "META-INF/LICENSE*",
            "META-INF/NOTICE*"
        )
    }
}

/* 把 mobile/ 的前端资源同步进 Android assets。
   ---------------------------------------------------------------------------
   之前 assets 里放的是 make_static.py 导出的「单文件静态版」，
   那是给浏览器双击打开用的快照（engine_static.js 只读构建时数据）。
   真机上必须用**页面版**：pages/*.html + assets/*.js，
   走 engine.js → window.NCEngine（NativeEngineBridge 直调 C ABI），
   这样编辑、预览、导出才是活的。

   目录映射（WebView 里以 https://appassets.androidplatform.net/assets/ 为根）：
     mobile/pages/*.html   → assets/pages/*.html
     mobile/assets/**      → assets/assets/**（图片/字体/脚本）
     mobile/static/**      → assets/static/**（黏土图、图标等静态资源） */
val syncMobileUi = tasks.register<Copy>("syncMobileUi") {
    val mobile = rootProject.file("../mobile")
    from(mobile.resolve("pages")) { into("pages") }
    from(mobile.resolve("assets")) { into("assets") }
    from(mobile.resolve("static")) { into("static") }
    into(layout.projectDirectory.dir("src/main/assets"))
    /* 排除项（都是「不该进 APK」的东西）：
       · static/nairoclip-*.html 单文件静态版 —— 由 tools/make_*_static.py
         导出，内联的是构建时数据快照，装进 APK 只会让 UI 变成只读。
         注意这条必须覆盖全部 6 个产物：
           nairoclip-editor-static.html / home / me / auth / phone / settings。
         早前只写了 `nairoclip-*-static.html`，只命中 editor 那一个，
         其余 5 个仍会打进 APK —— 一旦有人误开就是只读界面。
       · _probe.html 调试页
       · thumbs.db / .DS_Store 之类的系统垃圾 */
    exclude("**/nairoclip-*.html")
    exclude("**/_probe.html")
    exclude("**/Thumbs.db", "**/.DS_Store")
    includeEmptyDirs = false
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.webkit:webkit:1.11.0")
    implementation(files("libs/wechat-sdk-6.8.40.aar"))
    implementation(files("libs/open_sdk_3.5.19_r9483ffc7_lite.jar"))
}
