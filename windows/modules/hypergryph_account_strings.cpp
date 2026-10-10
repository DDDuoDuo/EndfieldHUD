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
}
