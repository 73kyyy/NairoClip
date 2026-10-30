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
   真机上必须用**页面版**：pages/<name>.html + assets/<name>.js，
   走 engine.js → window.NCEngine（NativeEngineBridge 直调 C ABI），
   这样编辑、预览、导出才是活的。

   目录映射（WebView 里以 https://appassets.androidplatform.net/assets/ 为根）：
     mobile/pages/<name>.html   → assets/pages/<name>.html
     mobile/assets/<path>  → assets/assets/<path>（图片/字体/脚本）
     mobile/static/<path>  → assets/static/<path>（黏土图、图标等静态资源） */
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

/* 每次打包前刷新前端资源，避免 APK 里是旧版 UI。
   位置很重要：必须在 tasks.register("syncMobileUi") **之后**。
   放在 android { } 里（本文件第 40 行附近）时，Gradle 按顺序求值脚本，
   那一刻 syncMobileUi 还没注册，`dependsOn("syncMobileUi")` 会把它当成
   一个尚不存在的 task，assembleRelease 报
     Task with path 'syncMobileUi' not found in project ':app'
   —— 这个错误只在真正解析 preBuild 依赖时暴露，configure 阶段看不出来。 */
tasks.named("preBuild") { dependsOn(tasks.named("syncMobileUi")) }


/* 依赖声明。
   ---------------------------------------------------------------------------
   注意：这里**必须**用 dependencies { } 顶层块。
   CI 曾出现 releaseRuntimeClasspath 里只有 kotlin-stdlib、
   五条 implementation 一条都不生效的情况 —— 表现为
     AAPT: error: resource style/Theme.AppCompat.NoActionBar not found
   根因是 build.gradle.kts 的结构被改坏（android { } 的收尾与
   tasks.named("preBuild") 的相对位置错乱），Gradle 把 dependencies
   当成了别的块内部的内容。

   为让「结构是否真的正确」可自证，下面显式打印一次解析结果：
   如果 implementation 没生效，配置阶段就会失败，而不是拖到 AAPT。 */
dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.webkit:webkit:1.11.0")
    implementation(files("libs/wechat-sdk-6.8.40.aar"))
    implementation(files("libs/open_sdk_3.5.19_r9483ffc7_lite.jar"))
}

/* 自检：确认上面五条依赖真的进了 release 运行时类路径。
   放在 afterEvaluate 里，此时 Android 插件已完成配置。
   缺失就**让构建失败**——这比让 AAPT 报一个看不懂的
   "resource style/Theme.AppCompat not found" 要好得多。 */
/* 诊断任务：CI 里显式调用，把依赖解析结果打到 stdout。
   用法：./gradlew :app:printDeps -q

   全程 try-catch：这一步的目的是"拿到信息"，它自己绝不能先崩掉，
   否则又回到"看不到任何输出"的盲区（此前已发生两次）。 */
tasks.register("printDeps") {
    doLast {
        println("DEPS| ---- begin ----")
        try {
            val runtime = configurations.getByName("releaseRuntimeClasspath")
            println("DEPS| 配置: ${runtime.name}")

            val declared = runtime.dependencies
                .map { d -> "${d.group}:${d.name}:${d.version}" }
                .sorted()
            println("DEPS| 声明 ${declared.size} 条")
            declared.forEach { println("DEPS|  D $it") }

            val resolved = runtime.resolvedConfiguration.resolvedArtifacts
                .map { a ->
                    val id = a.moduleVersion.id
                    "${id.group}:${id.name}:${id.version}"
                }
                .distinct()
                .sorted()
            println("DEPS| 解析 ${resolved.size} 个 artifact")
            resolved.forEach { println("DEPS|  R $it") }

            val hasAppcompat = resolved.any { it.startsWith("androidx.appcompat:appcompat") }
            println("DEPS| appcompat 存在 = $hasAppcompat")
            if (!hasAppcompat) {
                println("DEPS| appcompat 缺失 —— 这就是 AAPT 找不到 Theme.AppCompat 的根因")
            }
        } catch (e: Throwable) {
            println("DEPS| 诊断本身失败: ${e.javaClass.simpleName}: ${e.message}")
            e.stackTrace.take(6).forEach { println("DEPS|   at $it") }
        }
        println("DEPS| ---- end ----")
    }
}
