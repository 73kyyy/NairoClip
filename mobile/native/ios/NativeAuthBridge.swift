import Foundation
import UIKit
import WebKit
import AuthenticationServices

/// NairoClip · 第三方登录原生桥（iOS）
/// ---------------------------------------------------------------------------
/// 注入方式（创建 WKWebView 后）：
///
///   let bridge = NativeAuthBridge(webView: webView, host: self)
///   webView.configuration.userContentController.add(
///       bridge, name: "ncAuth")
///   webView.configuration.userContentController.addUserScript(bridge.platformScript)
///
/// 前端调用（assets/auth.js）：
///   window.webkit.messageHandlers.ncAuth.postMessage({provider: 'wechat'})
///
/// 回调前端：
///   window.NC_AUTH_DONE('wechat', {ok:true, uid,nickname,avatar})
///   window.NC_AUTH_DONE('wechat', {ok:false, error:'用户取消'})
///
/// 依赖（CocoaPods / SPM）：
///   WechatOpenSDK        ~> 2.0.4   （微信开放平台）
///   TencentOpenAPI       ~> 3.5.19  （QQ 互联）
///   二者均需在 Info.plist 配 URL Scheme 与 LSApplicationQueriesSchemes
/// ---------------------------------------------------------------------------
final class NativeAuthBridge: NSObject, WKScriptMessageHandler {

    private weak var webView: WKWebView?

    /// 微信 AppID
    private let wxAppId = Bundle.main.object(forInfoDictionaryKey: "NCWeChatAppId") as? String ?? ""
    /// QQ AppID
    private let qqAppId = Bundle.main.object(forInfoDictionaryKey: "NCQQAppId") as? String ?? ""
    /// 登录后端（微信 code 换 token 必须走服务端）
    private let authBackend = Bundle.main.object(forInfoDictionaryKey: "NCAuthBackend") as? String ?? ""

    /// ASAuthorizationController 必须被强引用，否则回调前就被释放
    private var appleController: ASAuthorizationController?

    /// TencentOAuth 也必须强引用，否则登录过程中被释放导致回调丢失
    private var lastQQAuth: TencentOAuth?

    private weak var host: UIViewController?

    init(webView: WKWebView, host: UIViewController) {
        self.webView = webView
        self.host = host
        super.init()
    }

    /// 在页面最早阶段声明平台，前端据此决定是否显示 Apple 登录
    var platformScript: WKUserScript {
        let js = "window.NC_PLATFORM='ios';"
        return WKUserScript(source: js, injectionTime: .atDocumentStart, forMainFrameOnly: true)
    }

    // MARK: - WKScriptMessageHandler

    func userContentController(_ controller: WKUserContentController,
                               didReceive message: WKScriptMessage) {
        guard let body = message.body as? [String: Any],
              let provider = body["provider"] as? String else {
            return
        }
        switch provider.lowercased() {
        case "wechat": startWeChat()
        case "qq":     startQQ()
        case "apple":  startApple()
        default:       finish(provider, ok: false, error: "未知登录方式")
        }
    }

    // MARK: - 微信
    private func startWeChat() {
        guard !wxAppId.isEmpty else {
            finish("wechat", ok: false, error: "未配置微信 AppID")
            return
        }
        guard WXApi.isWXAppInstalled() else {
            finish("wechat", ok: false, error: "未安装微信客户端")
            return
        }
        let req = SendAuthReq()
        req.scope = "snsapi_userinfo"
        req.state = String(Int(Date().timeIntervalSince1970))
        WXApi.registerApp(wxAppId, universalLink: "")
        WXApi.send(req) { [weak self] success in
            if !success {
                self?.finish("wechat", ok: false, error: "调起微信失败")
            }
        }
        // 真实结果在 AppDelegate / SceneDelegate 的 WXApi.handleOpenURL 回调里，
        // 拿到 code 后调用 handleWeChatCode(_:state:) —— 见下方方法。
    }

    /// 由 AppDelegate 调用：`WXApi.handleOpen(url, delegate: weChatDelegate)`
    /// 收到 SendAuthResp 后把 code 传进来。
    func handleWeChatCode(_ code: String, state: String?) {
        guard !authBackend.isEmpty else {
            finish("wechat", ok: false,
                   error: "登录服务未配置（需服务端用 code 换 access_token）")
            return
        }
        var req = URLRequest(url: URL(string: "\(authBackend)/auth/wechat")!)
        req.httpMethod = "POST"
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.httpBody = try? JSONSerialization.data(
            withJSONObject: ["code": code, "state": state ?? ""])

        URLSession.shared.dataTask(with: req) { [weak self] data, _, err in
            guard let self = self else { return }
            if let err = err {
                self.finish("wechat", ok: false, error: "换取登录态失败：\(err.localizedDescription)")
                return
            }
            guard let data = data,
                  let j = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                self.finish("wechat", ok: false, error: "服务端返回无法解析")
                return
            }
            if j["ok"] as? Bool == true {
                self.finish("wechat", ok: true,
                            uid: j["uid"] as? String,
                            nickname: j["nickname"] as? String,
                            avatar: j["avatar"] as? String)
            } else {
                self.finish("wechat", ok: false,
                            error: j["error"] as? String ?? "服务端拒绝")
            }
        }.resume()
    }

    // MARK: - QQ
    private func startQQ() {
        guard !qqAppId.isEmpty else {
            finish("qq", ok: false, error: "未配置 QQ AppID")
            return
        }
        let auth = TencentOAuth(appId: qqAppId, andDelegate: self)
        auth?.authMode = .QQAuthModeWeb   // 未装 QQ 时回落网页授权
        lastQQAuth = auth                // 强引用，回调前不能释放
        auth?.authorize(["get_user_info"])
    }

    // MARK: - Apple
    private func startApple() {
        let provider = ASAuthorizationAppleIDProvider()
        let request = provider.createRequest()
        request.requestedScopes = [.fullName, .email]

        let controller = ASAuthorizationController(authorizationRequests: [request])
        controller.delegate = self
        controller.presentationContextProvider = self
        controller.performRequests()
        appleController = controller      // 强引用，防止提前释放
    }

    // MARK: - 回调前端
    private func finish(_ provider: String,
                        ok: Bool,
                        uid: String? = nil,
                        nickname: String? = nil,
                        avatar: String? = nil,
                        error: String? = nil) {
        var payload: [String: Any] = ["ok": ok]
        if let v = uid { payload["uid"] = v }
        if let v = nickname { payload["nickname"] = v }
        if let v = avatar { payload["avatar"] = v }
        if let v = error { payload["error"] = v }

        guard let json = try? JSONSerialization.data(withJSONObject: payload),
              let jsonStr = String(data: json, encoding: .utf8) else { return }

        let escaped = provider.replacingOccurrences(of: "'", with: "")
        let js = "window.NC_AUTH_DONE && window.NC_AUTH_DONE('\(escaped)', \(jsonStr));"
        DispatchQueue.main.async { [weak self] in
            self?.webView?.evaluateJavaScript(js, completionHandler: nil)
        }
    }
}

// MARK: - Apple 登录回调
extension NativeAuthBridge: ASAuthorizationControllerDelegate,
                             ASAuthorizationControllerPresentationContextProviding {

    func presentationAnchor(for controller: ASAuthorizationController) -> ASPresentationAnchor {
        host?.view.window ?? UIWindow()
    }

    func authorizationController(controller: ASAuthorizationController,
                                didCompleteWithAuthorization authorization: ASAuthorization) {
        guard let cred = authorization.credential as? ASAuthorizationAppleIDCredential else {
            finish("apple", ok: false, error: "Apple 授权数据异常")
            return
        }
        // fullName 只在**首次**授权时返回一次，之后为 nil —— 必须提示用户
        // 或由服务端在首次登录时落库。
        let name = cred.fullName.flatMap { c in
            [c.givenName, c.familyName].compactMap { $0 }.joined(separator: " ")
        }
        finish("apple", ok: true,
               uid: cred.user,
               nickname: name?.isEmpty == false ? name : nil)
    }

    func authorizationController(controller: ASAuthorizationController,
                                didCompleteWithError error: Error) {
        let e = error as? ASAuthorizationError
        if e?.code == .canceled {
            finish("apple", ok: false, error: "用户取消")
        } else {
            finish("apple", ok: false, error: error.localizedDescription)
        }
    }
}

// MARK: - QQ 回调
extension NativeAuthBridge: TencentSessionDelegate {
    func tencentOAuth(_ oauth: TencentOAuth!, didFail error: Error!) {
        finish("qq", ok: false, error: error?.localizedDescription ?? "QQ 授权失败")
    }

    func tencentOAuth(_ oauth: TencentOAuth!,
                      didFailWithError error: Error!) {
        finish("qq", ok: false, error: error?.localizedDescription ?? "QQ 授权失败")
    }

    func tencentDidLogin() {
        guard let oauth = lastQQAuth, let openid = oauth.openId, oauth.accessToken != nil else {
            finish("qq", ok: false, error: "QQ 授权信息不完整")
            return
        }
        // 昵称头像需再调 OpenAPI；这里只回 openid，前端会用默认昵称
        finish("qq", ok: true, uid: openid)
    }

    func tencentDidNotLogin(_ cancelled: Bool) {
        finish("qq", ok: false, error: cancelled ? "用户取消" : "QQ 登录失败")
    }

    func tencentDidNotNetWork() {
        finish("qq", ok: false, error: "网络不可用")
    }
}
