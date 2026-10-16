#include "packaging/setup_text.hpp"
#include <array>
#include <stdexcept>
#include <vector>

namespace endfield::packaging {
namespace {
struct Entry {
    SetupText id;
    std::string_view en, zhHans, zhHant, ja, ko;
};
using T = SetupText;
// "Launch at login" and the module names follow the app's own catalog.
constexpr std::array<Entry, static_cast<std::size_t>(T::count)> entries{{
    {T::setupTitle, "EndfieldHUD Setup", "EndfieldHUD 安装程序", "EndfieldHUD 安裝程式", "EndfieldHUD セットアップ", "EndfieldHUD 설치 프로그램"},
    {T::uninstallTitle, "Uninstall EndfieldHUD", "卸载 EndfieldHUD", "解除安裝 EndfieldHUD", "EndfieldHUD のアンインストール", "EndfieldHUD 제거"},
    {T::installHeading, "Install EndfieldHUD {0}", "安装 EndfieldHUD {0}", "安裝 EndfieldHUD {0}", "EndfieldHUD {0} をインストール", "EndfieldHUD {0} 설치"},
    {T::installBody,
     "EndfieldHUD will be installed for your Windows account in:\n{0}\n\nAdministrator permission is not needed. Your EndfieldHUD data stays in:\n{1}",
     "EndfieldHUD 将为当前 Windows 账户安装到：\n{0}\n\n无需管理员权限。你的 EndfieldHUD 数据保存在：\n{1}",
     "EndfieldHUD 將為目前的 Windows 帳戶安裝到：\n{0}\n\n不需要系統管理員權限。你的 EndfieldHUD 資料保存在：\n{1}",
     "EndfieldHUD を現在の Windows アカウント用に次の場所へインストールします：\n{0}\n\n管理者権限は不要です。EndfieldHUD のデータは次の場所に保存されます：\n{1}",
     "EndfieldHUD를 현재 Windows 계정용으로 다음 위치에 설치합니다:\n{0}\n\n관리자 권한이 필요하지 않습니다. EndfieldHUD 데이터는 다음 위치에 보관됩니다:\n{1}"},
    {T::upgradeHeading, "Update EndfieldHUD to {0}", "将 EndfieldHUD 更新到 {0}", "將 EndfieldHUD 更新至 {0}", "EndfieldHUD を {0} にアップデート", "EndfieldHUD를 {0}(으)로 업데이트"},
    {T::upgradeBody,
     "EndfieldHUD {0} is installed in:\n{1}\n\nSetup will replace it with {2}. Your notes, settings and other data are kept.",
     "已安装的 EndfieldHUD {0} 位于：\n{1}\n\n安装程序会将其替换为 {2}。你的便笺、设置和其他数据都会保留。",
     "已安裝的 EndfieldHUD {0} 位於：\n{1}\n\n安裝程式會將其取代為 {2}。你的便箋、設定和其他資料都會保留。",
     "EndfieldHUD {0} が次の場所にインストールされています：\n{1}\n\nセットアップで {2} に置き換えます。メモ、設定、その他のデータは保持されます。",
     "EndfieldHUD {0}이(가) 다음 위치에 설치되어 있습니다:\n{1}\n\n설치 프로그램이 {2}(으)로 교체합니다. 메모, 설정 및 기타 데이터는 유지됩니다."},
    {T::repairHeading, "Repair EndfieldHUD {0}", "修复 EndfieldHUD {0}", "修復 EndfieldHUD {0}", "EndfieldHUD {0} を修復", "EndfieldHUD {0} 복구"},
    {T::repairBody,
     "This version is already installed in:\n{0}\n\nSetup will restore its files. Your notes, settings and other data are kept.",
     "此版本已安装在：\n{0}\n\n安装程序将恢复其文件。你的便笺、设置和其他数据都会保留。",
     "此版本已安裝在：\n{0}\n\n安裝程式將還原其檔案。你的便箋、設定和其他資料都會保留。",
     "このバージョンは次の場所にインストール済みです：\n{0}\n\nセットアップでファイルを復元します。メモ、設定、その他のデータは保持されます。",
     "이 버전은 이미 다음 위치에 설치되어 있습니다:\n{0}\n\n설치 프로그램이 파일을 복원합니다. 메모, 설정 및 기타 데이터는 유지됩니다."},
    {T::launchAtLogin, "Launch at login", "登录时启动", "登入時啟動", "ログイン時に起動", "로그인 시 실행"},
    {T::installButton, "Install", "安装", "安裝", "インストール", "설치"},
    {T::updateButton, "Update", "更新", "更新", "アップデート", "업데이트"},
    {T::repairButton, "Repair", "修复", "修復", "修復", "복구"},
    {T::continueButton, "Close EndfieldHUD and continue", "关闭 EndfieldHUD 并继续", "關閉 EndfieldHUD 並繼續", "EndfieldHUD を終了して続行", "EndfieldHUD를 종료하고 계속"},
    {T::openButton, "Open EndfieldHUD", "打开 EndfieldHUD", "開啟 EndfieldHUD", "EndfieldHUD を開く", "EndfieldHUD 열기"},
    {T::closeButton, "Close", "关闭", "關閉", "閉じる", "닫기"},
    {T::runningHeading, "EndfieldHUD is running", "EndfieldHUD 正在运行", "EndfieldHUD 正在執行", "EndfieldHUD は実行中です", "EndfieldHUD가 실행 중입니다"},
    {T::runningInstallBody,
     "Setup will close EndfieldHUD after saving your data, and reopen it when the update is finished.",
     "安装程序会在保存你的数据后关闭 EndfieldHUD，并在更新完成后重新打开。",
     "安裝程式會在儲存你的資料後關閉 EndfieldHUD，並在更新完成後重新開啟。",
     "セットアップはデータを保存してから EndfieldHUD を終了し、アップデート完了後に再び開きます。",
     "설치 프로그램이 데이터를 저장한 후 EndfieldHUD를 종료하고, 업데이트가 끝나면 다시 엽니다."},
    {T::runningUninstallBody,
     "EndfieldHUD will be closed after saving your data, then removed.",
     "将在保存你的数据后关闭 EndfieldHUD，然后将其移除。",
     "將在儲存你的資料後關閉 EndfieldHUD，然後將其移除。",
     "データを保存してから EndfieldHUD を終了し、削除します。",
     "데이터를 저장한 후 EndfieldHUD를 종료하고 제거합니다."},
    {T::installingHeading, "Installing EndfieldHUD…", "正在安装 EndfieldHUD…", "正在安裝 EndfieldHUD…", "EndfieldHUD をインストールしています…", "EndfieldHUD 설치 중…"},
    {T::stepExtract, "Copying files…", "正在复制文件…", "正在複製檔案…", "ファイルをコピーしています…", "파일 복사 중…"},
    {T::stepStop, "Closing EndfieldHUD…", "正在关闭 EndfieldHUD…", "正在關閉 EndfieldHUD…", "EndfieldHUD を終了しています…", "EndfieldHUD 종료 중…"},
    {T::stepRegister, "Registering EndfieldHUD…", "正在注册 EndfieldHUD…", "正在註冊 EndfieldHUD…", "EndfieldHUD を登録しています…", "EndfieldHUD 등록 중…"},
    {T::stepFinish, "Finishing…", "正在完成…", "正在完成…", "完了しています…", "마무리 중…"},
    {T::installedHeading, "EndfieldHUD is installed", "EndfieldHUD 已安装", "EndfieldHUD 已安裝", "EndfieldHUD をインストールしました", "EndfieldHUD가 설치되었습니다"},
    {T::updatedHeading, "EndfieldHUD is updated", "EndfieldHUD 已更新", "EndfieldHUD 已更新", "EndfieldHUD をアップデートしました", "EndfieldHUD가 업데이트되었습니다"},
    {T::repairedHeading, "EndfieldHUD is repaired", "EndfieldHUD 已修复", "EndfieldHUD 已修復", "EndfieldHUD を修復しました", "EndfieldHUD가 복구되었습니다"},
    {T::installedBody,
     "Open EndfieldHUD from the Start menu. It runs in the notification area.",
     "可从“开始”菜单打开 EndfieldHUD，它会在通知区域中运行。",
     "可從「開始」功能表開啟 EndfieldHUD，它會在通知區域中執行。",
     "スタートメニューから EndfieldHUD を開けます。通知領域で動作します。",
     "시작 메뉴에서 EndfieldHUD를 열 수 있습니다. 알림 영역에서 실행됩니다."},
    {T::failedHeading, "EndfieldHUD was not installed", "EndfieldHUD 未安装", "EndfieldHUD 未安裝", "EndfieldHUD はインストールされませんでした", "EndfieldHUD가 설치되지 않았습니다"},
    {T::failedBody,
     "Setup restored the previous state; nothing was changed.\n\n{0}",
     "安装程序已恢复之前的状态，未作任何更改。\n\n{0}",
     "安裝程式已還原先前的狀態，未作任何變更。\n\n{0}",
     "セットアップは以前の状態に戻しました。何も変更されていません。\n\n{0}",
     "설치 프로그램이 이전 상태로 되돌렸습니다. 변경된 내용은 없습니다.\n\n{0}"},
    {T::refusedHeading, "Setup did not change anything", "安装程序未作任何更改", "安裝程式未作任何變更", "セットアップは何も変更しませんでした", "설치 프로그램이 아무것도 변경하지 않았습니다"},
    {T::refusedNewer, "A newer EndfieldHUD ({0}) is already installed.", "已安装更新版本的 EndfieldHUD（{0}）。", "已安裝較新版本的 EndfieldHUD（{0}）。",
     "より新しい EndfieldHUD（{0}）がインストールされています。", "더 최신 버전의 EndfieldHUD({0})가 이미 설치되어 있습니다."},
    {T::refusedForeign, "The folder {0} already exists and was not created by EndfieldHUD Setup.", "文件夹 {0} 已存在，且不是由 EndfieldHUD 安装程序创建的。",
     "資料夾 {0} 已存在，且不是由 EndfieldHUD 安裝程式建立的。", "フォルダー {0} は既に存在し、EndfieldHUD セットアップが作成したものではありません。",
     "{0} 폴더가 이미 있으며 EndfieldHUD 설치 프로그램이 만든 폴더가 아닙니다."},
    {T::refusedIncomplete, "This setup is an incomplete developer build and cannot be installed.", "此安装程序是不完整的开发版本，无法安装。",
     "此安裝程式是不完整的開發版本，無法安裝。", "このセットアップは不完全な開発ビルドのため、インストールできません。", "이 설치 프로그램은 불완전한 개발 빌드이므로 설치할 수 없습니다."},
    {T::refusedPath, "The install folder path is too long or cannot be used:\n{0}", "安装文件夹路径过长或无法使用：\n{0}", "安裝資料夾路徑過長或無法使用：\n{0}",
     "インストール先フォルダーのパスが長すぎるか、使用できません：\n{0}", "설치 폴더 경로가 너무 길거나 사용할 수 없습니다:\n{0}"},
    {T::refusedWindows,
     "EndfieldHUD requires Windows 11 (build 22000 or later). This PC runs Windows build {0}.",
     "EndfieldHUD 需要 Windows 11（内部版本 22000 或更高版本）。此电脑运行的 Windows 内部版本为 {0}。",
     "EndfieldHUD 需要 Windows 11（組建 22000 或更新版本）。此電腦執行的 Windows 組建為 {0}。",
     "EndfieldHUD には Windows 11（ビルド 22000 以降）が必要です。この PC の Windows はビルド {0} です。",
     "EndfieldHUD를 사용하려면 Windows 11(빌드 22000 이상)이 필요합니다. 이 PC의 Windows 빌드는 {0}입니다."},
    {T::damagedPayload, "This setup file is damaged. Download it again.", "此安装文件已损坏，请重新下载。", "此安裝檔案已損毀，請重新下載。",
     "このセットアップファイルは破損しています。もう一度ダウンロードしてください。", "이 설치 파일이 손상되었습니다. 다시 다운로드하세요."},
    {T::busy, "Another EndfieldHUD Setup is already running.", "另一个 EndfieldHUD 安装程序正在运行。", "另一個 EndfieldHUD 安裝程式正在執行。",
     "別の EndfieldHUD セットアップが既に実行中です。", "다른 EndfieldHUD 설치 프로그램이 이미 실행 중입니다."},
    {T::closeFailed, "EndfieldHUD could not be closed. Quit it from the notification area and try again.",
     "无法关闭 EndfieldHUD。请从通知区域退出它，然后重试。", "無法關閉 EndfieldHUD。請從通知區域結束它，然後再試一次。",
     "EndfieldHUD を終了できませんでした。通知領域から終了してから、もう一度お試しください。", "EndfieldHUD를 종료할 수 없습니다. 알림 영역에서 종료한 후 다시 시도하세요."},
    {T::details, "Details", "详细信息", "詳細資訊", "詳細", "자세히"},
    {T::uninstallHeading, "Uninstall EndfieldHUD {0}?", "要卸载 EndfieldHUD {0} 吗？", "要解除安裝 EndfieldHUD {0} 嗎？",
     "EndfieldHUD {0} をアンインストールしますか？", "EndfieldHUD {0}을(를) 제거하시겠습니까?"},
    {T::uninstallBody,
     "EndfieldHUD will be removed from:\n{0}\n\nUnless you choose to delete them, your notes, settings and other data are kept in:\n{1}",
     "将从以下位置移除 EndfieldHUD：\n{0}\n\n除非你选择删除，否则以下位置中的便笺、设置和其他数据都会保留：\n{1}",
     "將從以下位置移除 EndfieldHUD：\n{0}\n\n除非你選擇刪除，否則以下位置中的便箋、設定和其他資料都會保留：\n{1}",
     "EndfieldHUD を次の場所から削除します：\n{0}\n\n削除を選択しない限り、次の場所のメモ、設定、その他のデータは保持されます：\n{1}",
     "다음 위치에서 EndfieldHUD를 제거합니다:\n{0}\n\n삭제를 선택하지 않으면 다음 위치의 메모, 설정 및 기타 데이터는 유지됩니다:\n{1}"},
    {T::deleteData, "Also delete my EndfieldHUD data", "同时删除我的 EndfieldHUD 数据", "同時刪除我的 EndfieldHUD 資料", "EndfieldHUD のデータも削除する", "EndfieldHUD 데이터도 삭제"},
    {T::uninstallButton, "Uninstall", "卸载", "解除安裝", "アンインストール", "제거"},
    {T::confirmDataHeading, "Delete all EndfieldHUD data?", "要删除所有 EndfieldHUD 数据吗？", "要刪除所有 EndfieldHUD 資料嗎？",
     "EndfieldHUD のすべてのデータを削除しますか？", "모든 EndfieldHUD 데이터를 삭제하시겠습니까?"},
    {T::confirmDataBody,
     "This permanently deletes your notes, settings, profile, calendar, archive, Reader library, map, saved sign-in and other EndfieldHUD data in:\n{0}\n\nThis cannot be undone.",
     "这将永久删除以下位置中的便笺、设置、个人资料、日历、档案库、阅读器书库、地图、已保存的登录信息和其他 EndfieldHUD 数据：\n{0}\n\n此操作无法撤销。",
     "這將永久刪除以下位置中的便箋、設定、個人資料、日曆、檔案庫、閱讀器書庫、地圖、已儲存的登入資訊和其他 EndfieldHUD 資料：\n{0}\n\n此操作無法復原。",
     "次の場所にあるメモ、設定、プロフィール、カレンダー、アーカイブ、リーダーのライブラリ、マップ、保存されたサインイン情報などの EndfieldHUD データを完全に削除します：\n{0}\n\nこの操作は元に戻せません。",
     "다음 위치의 메모, 설정, 프로필, 달력, 아카이브, 리더 서재, 지도, 저장된 로그인 정보 및 기타 EndfieldHUD 데이터를 영구적으로 삭제합니다:\n{0}\n\n이 작업은 되돌릴 수 없습니다."},
    {T::deleteDataButton, "Delete data and uninstall", "删除数据并卸载", "刪除資料並解除安裝", "データを削除してアンインストール", "데이터 삭제 후 제거"},
    {T::keepDataButton, "Keep my data", "保留我的数据", "保留我的資料", "データを保持する", "데이터 유지"},
    {T::uninstallingHeading, "Removing EndfieldHUD…", "正在移除 EndfieldHUD…", "正在移除 EndfieldHUD…", "EndfieldHUD を削除しています…", "EndfieldHUD 제거 중…"},
    {T::uninstalledHeading, "EndfieldHUD was uninstalled", "EndfieldHUD 已卸载", "EndfieldHUD 已解除安裝", "EndfieldHUD をアンインストールしました", "EndfieldHUD가 제거되었습니다"},
    {T::dataKept, "Your data was kept in:\n{0}", "你的数据保留在：\n{0}", "你的資料保留在：\n{0}", "データは次の場所に保持されています：\n{0}", "데이터는 다음 위치에 유지되었습니다:\n{0}"},
    {T::dataDeleted, "Your EndfieldHUD data was deleted.", "你的 EndfieldHUD 数据已删除。", "你的 EndfieldHUD 資料已刪除。", "EndfieldHUD のデータを削除しました。", "EndfieldHUD 데이터가 삭제되었습니다."},
    {T::uninstallFailedHeading, "EndfieldHUD was not removed completely", "EndfieldHUD 未完全移除", "EndfieldHUD 未完全移除",
     "EndfieldHUD を完全には削除できませんでした", "EndfieldHUD가 완전히 제거되지 않았습니다"},
    {T::uninstallFailedBody,
     "Close programs that use these files, then run the uninstaller again.\n\n{0}",
     "请关闭正在使用这些文件的程序，然后再次运行卸载程序。\n\n{0}",
     "請關閉正在使用這些檔案的程式，然後再次執行解除安裝程式。\n\n{0}",
     "これらのファイルを使用しているプログラムを閉じてから、アンインストーラーをもう一度実行してください。\n\n{0}",
     "이 파일을 사용 중인 프로그램을 닫은 후 제거 프로그램을 다시 실행하세요.\n\n{0}"},
    {T::notInstalled, "EndfieldHUD is not installed in this folder.", "此文件夹中未安装 EndfieldHUD。", "此資料夾中未安裝 EndfieldHUD。",
     "このフォルダーには EndfieldHUD がインストールされていません。", "이 폴더에는 EndfieldHUD가 설치되어 있지 않습니다."},
    {T::keptFiles,
     "These files were not created by Setup or are in use, and were kept:\n{0}",
     "以下文件不是由安装程序创建或正在使用，已保留：\n{0}",
     "以下檔案不是由安裝程式建立或正在使用，已保留：\n{0}",
     "次のファイルはセットアップで作成されたものではないか使用中のため、保持されました：\n{0}",
     "다음 파일은 설치 프로그램이 만든 파일이 아니거나 사용 중이어서 유지되었습니다:\n{0}"},
}};

constexpr bool ordered() {
    for (std::size_t n = 0; n < entries.size(); ++n)
        if (static_cast<std::size_t>(entries[n].id) != n) return false;
    return true;
}
static_assert(ordered(), "Setup text entries must follow the SetupText order");
} // namespace

std::string_view setupText(SetupText id, core::Language language) noexcept {
    const auto index = static_cast<std::size_t>(id);
    if (index >= entries.size()) return {};
    const auto& e = entries[index];
    switch (language) {
    case core::Language::simplifiedChinese: return e.zhHans;
    case core::Language::traditionalChinese: return e.zhHant;
    case core::Language::japanese: return e.ja;
    case core::Language::korean: return e.ko;
    case core::Language::system:
    case core::Language::english: break;
    }
    return e.en;
}

std::string setupText(SetupText id, core::Language language, std::initializer_list<std::string_view> arguments) {
    const std::vector<std::string_view> values(arguments);
    if (auto rendered = core::renderTranslation(setupText(id, language), values)) return std::move(*rendered);
    throw std::invalid_argument("Setup text arguments do not match its placeholders");
}

std::size_t setupTextArguments(SetupText id) noexcept {
    const auto text = setupText(id, core::Language::english);
    std::size_t count = 0;
    for (std::size_t at = 0; at + 2 < text.size(); ++at)
        if (text[at] == '{' && text[at + 1] >= '0' && text[at + 1] <= '9' && text[at + 2] == '}') ++count;
    return count;
}
} // namespace endfield::packaging
