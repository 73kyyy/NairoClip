import Foundation
import UIKit
import WebKit

/// NairoClip · 引擎桥（iOS，C interop 直调 C ABI，不走 HTTP）
/// ---------------------------------------------------------------------------
/// 依据 docs/UI_INTEGRATION.md §7：
///   · iOS = Swift + C interop 直调 nc_*
///   · 单个 NcEditor* 不支持多线程并发 → 本类用 NSLock 串行化
///   · char* 用 nc_free_string 释放；uint8_t* 用 nc_free_buffer 释放
///   · 长任务（导出/AI 推理）必须在工作线程执行
///
/// 注意：WKWebView 的 messageHandlers 是**异步**的，而前端 NCEngine 需要同步
/// 返回（与 Android 的 @JavascriptInterface 保持一致）。因此这里不通过
/// postMessage 来回传，而是在 WKUserScript 里把同步实现注入到页面：
///
///     let bridge = NativeEngineBridge()
///     let config = WKWebViewConfiguration()
///     config.userContentController.addUserScript(
///         WKUserScript(source: bridge.injectedScript(),
///                      injectionTime: .atDocumentStart, forMainFrameOnly: true))
///
/// 注入脚本内部用 `prompt("ncjni:" + payload)` 同步回调到原生
/// （WKUIDelegate.webView(_:runJavaScriptTextInputPanelWithPrompt:)），
/// 这是 WebKit 下唯一可靠的同步 JS→Native 通道。
final class NativeEngineBridge: NSObject {

    private var editor: OpaquePointer?
    private let lock = NSLock()

    /// 注入到 JS 的同步桥：window.NCEngine
    func injectedScript() -> String {
        return """
        (function () {
          if (window.NCEngine) return;

          function ask(payload) {
            // 同步：由 WKUIDelegate 的 prompt 回调立即返回字符串
            var r = window.prompt('ncjni:' + JSON.stringify(payload), '');
            return (r === null || r === undefined) ? '' : String(r);
          }

          window.NCEngine = {
            call: function (fn, argsJson) {
              return ask({ op: 'call', fn: fn, args: argsJson || '{}' });
            },
            state: function () {
              return ask({ op: 'state' });
            },
            frameDataUrl: function (timeUs, width) {
              return ask({ op: 'frame', t: timeUs, w: width });
            },
            thumbDataUrl: function (assetId, index, width) {
              return ask({ op: 'thumb', id: assetId, i: index, w: width });
            },
            presetFrame: function (kind, style, timeUs, width) {
              return ask({ op: 'preset', kind: kind, s: style, t: timeUs, w: width });
            },
            waveform: function (assetId) {
              return ask({ op: 'wave', id: assetId });
            },
            models: function () {
              return ask({ op: 'models' });
            },
            listProjects: function () {
              return ask({ op: 'projects' });
            }
          };
        })();
        """
    }

    // MARK: - 生命周期

    override init() {
        super.init()
        editor = nc_editor_create()
    }

    deinit {
        if let e = editor { nc_editor_destroy(e) }
        editor = nil
    }

    private func ensure() -> OpaquePointer? {
        if editor == nil { editor = nc_editor_create() }
        return editor
    }

    // MARK: - prompt 分发
    //
    // 在 WKUIDelegate 里这样接：
    //   func webView(_ webView: WKWebView,
    //                runJavaScriptTextInputPanelWithPrompt prompt: String,
    //                defaultText: String?,
    //                initiatedByFrame frame: WKFrameInfo,
    //                completionHandler: @escaping (String?) -> Void) {
    //       completionHandler(bridge.handle(prompt: prompt))
    //   }

    /// 处理 `ncjni:{...}` 形式的同步请求，返回给 JS 的字符串。
    func handle(prompt: String) -> String {
        let prefix = "ncjni:"
        guard prompt.hasPrefix(prefix) else { return "" }
        let body = String(prompt.dropFirst(prefix.count))
        guard let data = body.data(using: .utf8),
              let obj = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
              let op = obj["op"] as? String else {
            return errJSON("请求格式非法")
        }

        lock.lock()
        defer { lock.unlock() }

        guard let ed = ensure() else { return errJSON("引擎未初始化") }

        switch op {
        case "state":
            return buildState(ed)
        case "call":
            let fn = (obj["fn"] as? String) ?? ""
            let args = (obj["args"] as? String) ?? "{}"
            return dispatchEdit(ed, fn: fn, args: args)
        case "frame":
            let t = (obj["t"] as? NSNumber)?.int64Value ?? 0
            let w = (obj["w"] as? NSNumber)?.int32Value ?? 282
            return frameDataUrl(ed, timeUs: t, width: w)
        case "thumb":
            let id = (obj["id"] as? NSNumber)?.uint64Value ?? 0
            let i = (obj["i"] as? NSNumber)?.int32Value ?? 0
            let w = (obj["w"] as? NSNumber)?.int32Value ?? 120
            return thumbDataUrl(ed, assetId: id, index: i, width: w)
        case "preset":
            let kind = (obj["kind"] as? String) ?? "filter"
            let s = (obj["s"] as? NSNumber)?.int32Value ?? 0
            let t = (obj["t"] as? NSNumber)?.int64Value ?? 1_600_000
            let w = (obj["w"] as? NSNumber)?.int32Value ?? 160
            return presetFrameDataUrl(ed, kind: kind, style: s, timeUs: t, width: w)
        case "wave":
            let id = (obj["id"] as? NSNumber)?.uint64Value ?? 0
            return waveformJSON(ed, assetId: id)
        case "models":
            return modelsJSON(ed)
        case "projects":
            return projectsJSON()
        default:
            return errJSON("未开放的操作：\(op)")
        }
    }

    // MARK: - 快照

    private func buildState(_ ed: OpaquePointer) -> String {
        var w: Int32 = 0, h: Int32 = 0
        nc_project_canvas(ed, &w, &h)
        // 只取一次：char* 由库分配，读取后立刻释放（避免二次调用漏释放）
        let rawName = nc_project_name(ed)
        let pname = rawName.map { String(cString: $0) } ?? "未命名项目"
        if let p = rawName { nc_free_string(p) }

        var json = "{\"project\":{"
        json += "\"name\":\"\(esc(pname))\","
        json += "\"width\":\(w),\"height\":\(h),"
        json += "\"duration_us\":\(nc_project_duration_us(ed))},"

        json += "\"tracks\":["
        let tn = nc_track_count(ed)
        for ti in 0..<tn {
            if ti > 0 { json += "," }
            var tid: UInt64 = 0
            var tname: UnsafeMutablePointer<CChar>?
            var ttype: Int32 = 0
            var locked: Int32 = 0, muted: Int32 = 0, hidden: Int32 = 0, solo: Int32 = 0
            var gain: Double = 0
            let rc = nc_track_get(ed, ti, &tid, &tname, &ttype,
                                  &locked, &muted, &hidden, &solo, &gain)
            if rc != NC_OK { json += "{}"; continue }
            let name = tname.map { String(cString: $0) } ?? ""
            if let n = tname { nc_free_string(n) }

            json += "{\"id\":\(tid),\"index\":\(ti),\"name\":\"\(esc(name))\","
            json += "\"type\":\(ttype),\"locked\":\(locked),\"muted\":\(muted),"
            json += "\"hidden\":\(hidden),\"solo\":\(solo),\"gain_db\":\(gain),"

            json += "\"clips\":["
            let cn = nc_clip_count(ed, ti)
            for ci in 0..<cn {
                if ci > 0 { json += "," }
                var cid: UInt64 = 0, aid: UInt64 = 0
                var start: Int64 = 0, dur: Int64 = 0
                var sn: Int64 = 1, sd: Int64 = 1
                var en: Int32 = 1
                let crc = nc_clip_info(ed, ti, UInt32(ci), &cid, &start, &dur,
                                       &aid, &sn, &sd, &en)
                if crc != NC_OK { json += "{}"; continue }
                json += "{\"id\":\(cid),\"asset_id\":\(aid),\"start_us\":\(start),"
                json += "\"duration_us\":\(dur),\"speed_num\":\(sn),"
                json += "\"speed_den\":\(sd),\"enabled\":\(en)}"
            }
            json += "]}"
        }
        json += "],\"assets\":["

        let an = nc_asset_count(ed)
        for ai in 0..<an {
            if ai > 0 { json += "," }
            var aid: UInt64 = 0
            var aname: UnsafeMutablePointer<CChar>?
            var apath: UnsafeMutablePointer<CChar>?
            var atype: Int32 = 0
            var adur: Int64 = 0
            if nc_asset_get(ed, ai, &aid, &aname, &apath, &atype, &adur) != NC_OK {
                json += "{}"; continue
            }
            let nm = aname.map { String(cString: $0) } ?? ""
            let pt = apath.map { String(cString: $0) } ?? ""
            if let n = aname { nc_free_string(n) }
            if let p = apath { nc_free_string(p) }
            json += "{\"id\":\(aid),\"name\":\"\(esc(nm))\",\"path\":\"\(esc(pt))\","
            json += "\"type\":\(atype),\"duration_us\":\(adur)}"
        }
        json += "],\"can_undo\":\(nc_can_undo(ed)),\"can_redo\":\(nc_can_redo(ed))}"
        return okJSON(json)
    }

    // MARK: - 写入

    private func dispatchEdit(_ ed: OpaquePointer, fn: String, args: String) -> String {
        let a = Args(args)
        let ti = Int32(a.int("track", a.int("track_index", 0)))
        let cid = UInt64(a.int("clip_id", a.int("clip", 0)))

        switch fn {
        case "split":
            let rc = nc_split_clip(ed, ti, cid, a.int("time_us", 0))
            return rc == NC_OK ? okJSON("") : errJSON("分割失败，错误码 \(rc)")
        case "delete":
            let rc = nc_remove_clip(ed, ti, cid)
            return rc == NC_OK ? okJSON("") : errJSON("删除失败，错误码 \(rc)")
        case "trim_start":
            let rc = nc_trim_clip_start(ed, ti, cid, a.int("time_us", 0))
            return rc == NC_OK ? okJSON("") : errJSON("裁剪失败，错误码 \(rc)")
        case "trim_end":
            let rc = nc_trim_clip_end(ed, ti, cid, a.int("time_us", 0))
            return rc == NC_OK ? okJSON("") : errJSON("裁剪失败，错误码 \(rc)")
        case "move":
            let rc = nc_move_clip(ed, ti, cid, a.int("start_us", 0))
            return rc == NC_OK ? okJSON("") : errJSON("移动失败，错误码 \(rc)")
        case "enabled":
            let rc = nc_set_clip_enabled(ed, ti, cid, Int32(a.int("enabled", 1)))
            return rc == NC_OK ? okJSON("") : errJSON("切换失败，错误码 \(rc)")
        case "opacity":
            let rc = nc_set_clip_opacity(ed, ti, cid, a.dbl("value", 1.0))
            return rc == NC_OK ? okJSON("") : errJSON("设置不透明度失败，错误码 \(rc)")
        case "filter":
            let rc = nc_set_clip_filter(ed, ti, cid, Int32(a.int("style", 0)),
                                        a.dbl("intensity", 60.0))
            return rc == NC_OK ? okJSON("") : errJSON("设置滤镜失败，错误码 \(rc)")
        case "rename_project":
            let rc = nc_set_project_name(ed, a.str("name"))
            return rc == NC_OK ? okJSON("") : errJSON("重命名失败，错误码 \(rc)")
        case "rename_track":
            let rc = nc_rename_track(ed, ti, a.str("name"))
            return rc == NC_OK ? okJSON("") : errJSON("重命名轨道失败，错误码 \(rc)")
        case "add_track":
            let id = nc_add_track(ed, a.str("name"), Int32(a.int("type", 0)))
            return id != 0 ? okJSON("{\"track_id\":\(id)}") : errJSON("添加轨道失败")
        case "undo":
            let rc = nc_undo(ed)
            return rc == NC_OK ? okJSON("") : errJSON("无可撤销操作，错误码 \(rc)")
        case "redo":
            let rc = nc_redo(ed)
            return rc == NC_OK ? okJSON("") : errJSON("无可重做操作，错误码 \(rc)")
        default:
            return errJSON("未开放的操作：\(fn)")
        }
    }

    // MARK: - 图像

    private func frameDataUrl(_ ed: OpaquePointer, timeUs: Int64, width: Int32) -> String {
        var rgba: UnsafeMutablePointer<UInt8>?
        var w: Int32 = 0, h: Int32 = 0
        var clipId: UInt64 = 0
        var op: Double = 1, sc: Double = 1, px: Double = 0, py: Double = 0, rot: Double = 0
        var tt: Int32 = -1
        var tp: Double = 0
        let rc = nc_preview_render(ed, timeUs, &w, &h, &rgba, &clipId,
                                   &op, &sc, &px, &py, &rot, &tt, &tp)
        guard rc == NC_OK, let buf = rgba, w > 0, h > 0 else { return "" }
        defer { nc_free_buffer(buf) }
        return rgbaToDataUrl(buf, width: Int(w), height: Int(h))
    }

    private func thumbDataUrl(_ ed: OpaquePointer, assetId: UInt64,
                              index: Int32, width: Int32) -> String {
        var images: UnsafeMutablePointer<UnsafeMutablePointer<UInt8>?>?
        var times: UnsafeMutablePointer<Int64>?
        var count: Int32 = 0
        let rc = nc_thumbnail_generate(ed, assetId, 1, width, width * 2,
                                       &images, times, &count)
        guard rc == NC_OK, let arr = images, count > 0 else { return "" }
        defer {
            for i in 0..<Int(count) {
                if let p = arr[i] { nc_free_buffer(p) }
            }
        }
        var tw: Int32 = 0, th: Int32 = 0
        nc_thumbnail_size(ed, &tw, &th)
        let idx = (index >= 0 && index < count) ? Int(index) : 0
        guard tw > 0, th > 0, let pix = arr[idx] else { return "" }
        return rgbaToDataUrl(pix, width: Int(tw), height: Int(th))
    }

    /// 预设缩略帧：临时套预设→渲染→还原（与设计桥 /api/preset_frame 等价）
    private func presetFrameDataUrl(_ ed: OpaquePointer, kind: String,
                                    style: Int32, timeUs: Int64, width: Int32) -> String {
        // 找第一条有片段的主轨
        var ti: Int32 = -1
        var cid: UInt64 = 0
        let tn = nc_track_count(ed)
        for i in 0..<tn {
            if nc_clip_count(ed, i) <= 0 { continue }
            var id: UInt64 = 0, aid: UInt64 = 0
            var st: Int64 = 0, du: Int64 = 0, sn: Int64 = 1, sd: Int64 = 1
            var en: Int32 = 1
            if nc_clip_info(ed, i, 0, &id, &st, &du, &aid, &sn, &sd, &en) == NC_OK {
                ti = i; cid = id; break
            }
        }
        if ti < 0 { return "" }

        var oldStyle: Int32 = 0
        var oldInt: Double = 0
        let wasOn = nc_get_clip_filter(ed, ti, cid, &oldStyle, &oldInt)

        var tempEffect: UInt64 = 0
        var touchedFilter = false
        var touchedMask = false

        switch kind {
        case "filter":
            nc_set_clip_filter(ed, ti, cid, style, 100.0)
            touchedFilter = true
        case "effect":
            tempEffect = nc_add_effect(ed, ti, cid, style, 0.85)
        case "mask":
            nc_set_clip_mask(ed, ti, cid, style, 0.5, 0.5, 0, 1.0, 1.0, 0.06, 0.0, 0)
            touchedMask = true
        default:
            break
        }

        var rgba: UnsafeMutablePointer<UInt8>?
        var w: Int32 = 0, h: Int32 = 0
        var clipId: UInt64 = 0
        var op: Double = 1, sc: Double = 1, px: Double = 0, py: Double = 0, rot: Double = 0
        var tt: Int32 = -1
        var tp: Double = 0
        let rc = nc_preview_render(ed, timeUs, &w, &h, &rgba, &clipId,
                                   &op, &sc, &px, &py, &rot, &tt, &tp)

        // 还原
        if touchedFilter {
            nc_set_clip_filter(ed, ti, cid, wasOn > 0 ? oldStyle : 0,
                               wasOn > 0 ? oldInt : 0.0)
        }
        if tempEffect != 0 { nc_remove_effect(ed, ti, cid, tempEffect) }
        if touchedMask {
            nc_set_clip_mask(ed, ti, cid, 0, 0.5, 0.5, 0, 1.0, 1.0, 0.0, 0.0, 0)
        }
        nc_preview_invalidate(ed)

        guard rc == NC_OK, let buf = rgba, w > 0, h > 0 else { return "" }
        defer { nc_free_buffer(buf) }
        return rgbaToDataUrl(buf, width: Int(w), height: Int(h))
    }

    /// RGBA8 → PNG → data:URL
    private func rgbaToDataUrl(_ buf: UnsafeMutablePointer<UInt8>,
                               width: Int, height: Int) -> String {
        let count = width * height
        guard count > 0 else { return "" }

        var pixels = [UInt8](repeating: 0, count: count * 4)
        for i in 0..<count {
            let s = i * 4
            // 统一成 RGBA 顺序（C ABI 已按 RGBA 输出）
            pixels[s] = buf[s]
            pixels[s + 1] = buf[s + 1]
            pixels[s + 2] = buf[s + 2]
            pixels[s + 3] = buf[s + 3]
        }

        guard let provider = CGDataProvider(data: Data(pixels) as CFData),
              let cg = CGImage(width: width, height: height,
                               bitsPerComponent: 8, bitsPerPixel: 32,
                               bytesPerRow: width * 4,
                               space: CGColorSpaceCreateDeviceRGB(),
                               bitmapInfo: CGBitmapInfo(rawValue:
                                   CGImageAlphaInfo.premultipliedLast.rawValue),
                               provider: provider, decode: nil,
                               shouldInterpolate: false,
                               intent: .defaultIntent) else { return "" }

        let ui = UIImage(cgImage: cg)
        guard let png = ui.pngData() else { return "" }
        return "data:image/png;base64," + png.base64EncodedString()
    }

    // MARK: - JSON 小工具

    private func okJSON(_ inner: String) -> String {
        "{\"ok\":true,\"code\":0,\"message\":\"\",\"result\":\(inner.isEmpty ? "{}" : inner)}"
    }
    private func errJSON(_ msg: String) -> String {
        "{\"ok\":false,\"code\":-1,\"message\":\"\(esc(msg))\",\"result\":{}}"
    }
    private func esc(_ s: String) -> String {
        var o = ""
        for c in s.unicodeScalars {
            switch c {
            case "\"": o += "\\\""
            case "\\": o += "\\\\"
            case "\n": o += "\\n"
            case "\r": o += "\\r"
            case "\t": o += "\\t"
            default:
                if c.value < 0x20 {
                    o += String(format: "\\u%04x", c.value)
                } else {
                    o.unicodeScalars.append(c)
                }
            }
        }
        return o
    }
}

// MARK: - 参数读取

/// 极简扁平 JSON 读取（与 JNI 侧同语义）
private struct Args {
    private let raw: String
    init(_ s: String) { raw = s }

    private func find(_ key: String) -> Int? {
        guard let r = raw.range(of: "\"\(key)\"") else { return nil }
        guard let colon = raw.range(of: ":", range: r.upperBound..<raw.endIndex) else { return nil }
        return raw.distance(from: raw.startIndex, to: colon.upperBound)
    }

    func int(_ key: String, _ def: Int64) -> Int64 {
        guard let at = find(key) else { return def }
        let rest = String(raw.dropFirst(at)).trimmingCharacters(in: .whitespaces)
        var num = ""
        for ch in rest {
            if ch.isNumber || ch == "-" || ch == "+" { num.append(ch) } else { break }
        }
        return Int64(num) ?? def
    }

    func dbl(_ key: String, _ def: Double) -> Double {
        guard let at = find(key) else { return def }
        let rest = String(raw.dropFirst(at)).trimmingCharacters(in: .whitespaces)
        var num = ""
        for ch in rest {
            if ch.isNumber || ch == "-" || ch == "+" || ch == "." { num.append(ch) } else { break }
        }
        return Double(num) ?? def
    }

    func str(_ key: String) -> String { str(key, "") }

    func str(_ key: String, _ def: String) -> String {
        guard let at = find(key) else { return def }
        let rest = raw.dropFirst(at)
        guard let q = rest.firstIndex(of: "\"") else { return def }
        let after = rest.index(after: q)
        guard let end = rest[after...].firstIndex(of: "\"") else { return def }
        return String(rest[after..<end])
    }
}

// MARK: - 工程列表（平台层职责，C ABI 无目录接口）

extension NativeEngineBridge {
    /// 扫描 Documents 下的 .nclip 草稿，返回 {"projects":[...]}
    fileprivate func projectsJSON() -> String {
        let fm = FileManager.default
        guard let dir = fm.urls(for: .documentDirectory, in: .userDomainMask).first else {
            return "{\"projects\":[]}"
        }
        var items: [String] = []
        if let files = try? fm.contentsOfDirectory(at: dir,
                                                   includingPropertiesForKeys: [.fileSizeKey]) {
            for f in files where f.pathExtension == "nclip" {
                let size = (try? f.resourceValues(forKeys: [.fileSizeKey]))?.fileSize ?? 0
                items.append("""
                {"name":"\(esc(f.deletingPathExtension().lastPathComponent))",\
                "file":"\(esc(f.lastPathComponent))",\
                "bytes":\(size)}
                """)
            }
        }
        return "{\"projects\":[\(items.joined(separator: ","))]}"
    }
}
