#include "modules/hypergryph_account_strings.hpp"
#include <array>

namespace endfield::modules::hypergryph {
namespace {
struct Variant {std::string_view english,simplified,traditional,japanese,korean;};
// Windows wording for the two Keychain references (same sentences otherwise).
constexpr Variant consent{
    "Sign in to the official website in a private session. EndfieldHUD will save only this community session, encrypted for your Windows account on this PC, to read your game profile. Your password and passport token are not collected. This is an unofficial integration.",
    "在独立会话中登录官方网站。EndfieldHUD 仅将此次社区会话加密保存在本机当前 Windows 账户下，用于读取游戏资料，不收集密码或通行证令牌。这是非官方关联功能。",
    "在獨立工作階段中登入官方網站。EndfieldHUD 僅將此次社群工作階段加密保存在本機目前的 Windows 帳戶下，用於讀取遊戲資料，不收集密碼或通行證權杖。這是非官方連結功能。",
    "独立したセッションで公式サイトにログインします。EndfieldHUDはゲームのプロフィールを読み取るため、このコミュニティのセッションだけを、このPCのWindowsアカウント用に暗号化して保存します。パスワードやパスポートのトークンは収集しません。この連携は非公式です。",
    "별도 세션에서 공식 웹사이트에 로그인합니다. EndfieldHUD는 게임 프로필을 읽기 위해 이 커뮤니티 세션만 이 PC의 Windows 계정용으로 암호화하여 저장합니다. 비밀번호나 패스포트 토큰은 수집하지 않습니다. 비공식 연동 기능입니다."};
constexpr Variant vault{
    "The saved sign-in could not be updated. Try again.",
    "无法更新已保存的登录信息，请重试。",
    "無法更新已儲存的登入資訊，請重試。",
    "保存されたログイン情報を更新できませんでした。もう一度お試しください。",
    "저장된 로그인 정보를 업데이트하지 못했습니다. 다시 시도하세요."};
// WebView2 Runtime explanation (Windows only; see the header).
constexpr Variant runtimeTitle{
    "Microsoft Edge WebView2 Runtime required",
    "需要 Microsoft Edge WebView2 运行时",
    "需要 Microsoft Edge WebView2 執行階段",
    "Microsoft Edge WebView2 ランタイムが必要です",
    "Microsoft Edge WebView2 런타임이 필요합니다"};
constexpr Variant runtimeDetail{
    "The official sign-in page opens in Microsoft Edge WebView2, which is not installed on this PC. EndfieldHUD does not include it. Download the Evergreen Runtime from Microsoft’s official page, install it, then choose Check Again.",
    "官方登录页面需要在 Microsoft Edge WebView2 中打开，但本机尚未安装。EndfieldHUD 不附带该组件。请从 Microsoft 官方页面下载并安装 Evergreen 运行时，然后选择“重新检查”。",
    "官方登入頁面需要在 Microsoft Edge WebView2 中開啟，但本機尚未安裝。EndfieldHUD 不隨附此元件。請從 Microsoft 官方頁面下載並安裝 Evergreen 執行階段，然後選擇「重新檢查」。",
    "公式のログインページは Microsoft Edge WebView2 で開きますが、この PC にはインストールされていません。EndfieldHUD には同梱されていません。Microsoft の公式ページから Evergreen ランタイムをダウンロードしてインストールし、「再確認」を選んでください。",
    "공식 로그인 페이지는 Microsoft Edge WebView2에서 열리지만 이 PC에는 설치되어 있지 않습니다. EndfieldHUD에는 포함되어 있지 않습니다. Microsoft 공식 페이지에서 Evergreen 런타임을 다운로드하여 설치한 다음 \"다시 확인\"을 선택하세요."};
constexpr Variant runtimeDownload{
    "Open Microsoft Download Page",
    "打开 Microsoft 下载页面",
    "開啟 Microsoft 下載頁面",
    "Microsoft のダウンロードページを開く",
    "Microsoft 다운로드 페이지 열기"};
constexpr Variant runtimeCheck{"Check Again","重新检查","重新檢查","再確認","다시 확인"};
constexpr Variant runtimeStillMissing{
    "WebView2 Runtime is still not installed.",
    "仍未安装 WebView2 运行时。",
    "仍未安裝 WebView2 執行階段。",
    "WebView2 ランタイムはまだインストールされていません。",
    "WebView2 런타임이 아직 설치되지 않았습니다."};
constexpr Variant runtimeStatus{
    "Sign-in needs the Microsoft Edge WebView2 Runtime.",
    "登录需要 Microsoft Edge WebView2 运行时。",
    "登入需要 Microsoft Edge WebView2 執行階段。",
    "ログインには Microsoft Edge WebView2 ランタイムが必要です。",
    "로그인하려면 Microsoft Edge WebView2 런타임이 필요합니다."};
std::string_view pick(const Variant& v,core::Language language) {
    switch(language) {
        case core::Language::simplifiedChinese: return v.simplified;
        case core::Language::traditionalChinese: return v.traditional;
        case core::Language::japanese: return v.japanese;
        case core::Language::korean: return v.korean;
        case core::Language::system: case core::Language::english: break;
    }
    return v.english;
}
}
std::string accountText(std::string_view english,std::string_view chinese,core::Language language,AccountWording wording) {
    if(wording==AccountWording::windows) {
        if(english==macConsentDetail&&chinese==macConsentDetailChinese) return std::string(pick(consent,language));
        if(english==macVaultFailure&&chinese==macVaultFailureChinese) return std::string(pick(vault,language));
    }
    return core::localized(english,chinese,language);
}
WebView2RuntimeStrings webView2RuntimeStrings(core::Language language) {
    const auto text=[language](const Variant& v){return std::string(pick(v,language));};
    return {text(runtimeTitle),text(runtimeDetail),std::string(webView2DownloadURL),text(runtimeDownload),text(runtimeCheck),text(runtimeStillMissing),text(runtimeStatus)};
}
}
