#include "app/areas/mac_import_flow.hpp"
#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <span>

namespace endfield::areas {
namespace {
namespace fs = std::filesystem;
namespace mig = ehud::migration;
using S = mig::MacImportStore;
using Status = mig::MacImportStatus;
using Language = core::Language;

// One Windows-only string in the five HUD languages (English, Simplified
// Chinese, Traditional Chinese, Japanese, Korean). {0}/{1} are placeholders.
struct Text { std::string_view en, hans, hant, ja, ko; };
std::string pick(const Text& t, Language l) {
    switch (l) {
    case Language::simplifiedChinese: return std::string(t.hans);
    case Language::traditionalChinese: return std::string(t.hant);
    case Language::japanese: return std::string(t.ja);
    case Language::korean: return std::string(t.ko);
    default: return std::string(t.en);
    }
}
std::string format(const Text& t, Language l, std::initializer_list<std::string> values) {
    const auto pattern = pick(t, l);
    std::vector<std::string_view> arguments(values.begin(), values.end());
    auto rendered = core::renderTranslation(pattern, arguments);
    return rendered ? std::move(*rendered) : pattern;
}
// Sentences: a space between them in English and Korean, none in Chinese
// and Japanese (full-width punctuation already ends each sentence).
std::string join(Language l, std::initializer_list<std::string> parts) {
    const bool spaced = !(l == Language::simplifiedChinese || l == Language::traditionalChinese || l == Language::japanese);
    std::string out;
    for (const auto& part : parts) {
        if (part.empty()) continue;
        if (!out.empty() && spaced) out += ' ';
        out += part;
    }
    return out;
}
std::string catalog(std::string_view english, std::string_view chinese, Language l) { return core::localized(english, chinese, l); }
std::string number(std::size_t n) { return std::to_string(n); }
std::string megabytes(std::uint64_t bytes) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buffer;
}
fs::path utf8Path(std::string_view text) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())); }

namespace T {
constexpr Text entry{"Import macOS data…", "导入 macOS 数据…", "匯入 macOS 資料…", "macOS のデータを読み込む…", "macOS 데이터 가져오기…"};
constexpr Text introTitle{"Import macOS data?", "导入 macOS 数据？", "匯入 macOS 資料？", "macOS のデータを読み込みますか？", "macOS 데이터를 가져올까요?"};
constexpr Text introMessage{
    "Choose the folder made by the EndfieldHUD exporter on your Mac. Nothing changes until you confirm; current data is backed up first.",
    "请选择 Mac 上 EndfieldHUD 导出工具生成的文件夹。确认前不会更改任何内容，现有数据会先备份。",
    "請選擇 Mac 上 EndfieldHUD 匯出工具產生的資料夾。確認前不會變更任何內容，現有資料會先備份。",
    "Mac の EndfieldHUD エクスポートツールで作成したフォルダを選択してください。確定するまで何も変更されず、現在のデータは先にバックアップされます。",
    "Mac의 EndfieldHUD 내보내기 도구로 만든 폴더를 선택하세요. 확인하기 전에는 아무것도 바뀌지 않으며 현재 데이터는 먼저 백업됩니다."};
constexpr Text choosingMessage{"Choose the exported folder in the file dialog.", "请在文件对话框中选择导出的文件夹。", "請在檔案對話框中選擇匯出的資料夾。",
    "ファイルダイアログでエクスポートしたフォルダを選択してください。", "파일 대화 상자에서 내보낸 폴더를 선택하세요."};
constexpr Text chooseFolder{"Choose Folder…", "选择文件夹…", "選擇資料夾…", "フォルダを選択…", "폴더 선택…"};
constexpr Text pickerTitle{"Choose the exported macOS data folder", "选择导出的 macOS 数据文件夹", "選擇匯出的 macOS 資料資料夾",
    "エクスポートした macOS データのフォルダを選択", "내보낸 macOS 데이터 폴더 선택"};
constexpr Text pickerAccept{"Choose", "选择", "選擇", "選択", "선택"};
constexpr Text pickerBusy{"The file picker is busy. Try again in a moment.", "文件选择器正忙，请稍后再试。", "檔案選擇器正忙，請稍後再試。",
    "ファイル選択が使用中です。しばらくしてからもう一度お試しください。", "파일 선택기가 사용 중입니다. 잠시 후 다시 시도하세요."};
constexpr Text pickerUnavailable{"The file picker is unavailable.", "文件选择器不可用。", "檔案選擇器無法使用。", "ファイル選択を利用できません。", "파일 선택기를 사용할 수 없습니다."};
constexpr Text chooseOne{"Choose one exported folder.", "请选择一个导出的文件夹。", "請選擇一個匯出的資料夾。", "エクスポートしたフォルダを 1 つ選択してください。", "내보낸 폴더를 하나만 선택하세요."};
constexpr Text stagingTitle{"Checking export…", "正在检查导出数据…", "正在檢查匯出資料…", "エクスポートを確認しています…", "내보낸 데이터를 확인하는 중…"};
constexpr Text stagingOpening{"Reading the export manifest. EndfieldHUD keeps running; nothing is written to your data.",
    "正在读取导出清单。EndfieldHUD 会继续运行，不会写入你的数据。", "正在讀取匯出清單。EndfieldHUD 會繼續執行，不會寫入你的資料。",
    "エクスポートの一覧を読み込んでいます。EndfieldHUD は動作を続け、データには何も書き込みません。",
    "내보내기 목록을 읽는 중입니다. EndfieldHUD는 계속 실행되며 데이터에는 아무것도 기록하지 않습니다."};
constexpr Text stagingVerifying{"Verifying files: {0} of {1} MB. EndfieldHUD keeps running; nothing is written to your data.",
    "正在校验文件：{0} / {1} MB。EndfieldHUD 会继续运行，不会写入你的数据。", "正在驗證檔案：{0} / {1} MB。EndfieldHUD 會繼續執行，不會寫入你的資料。",
    "ファイルを検証しています：{0} / {1} MB。EndfieldHUD は動作を続け、データには何も書き込みません。",
    "파일 확인 중: {0} / {1} MB. EndfieldHUD는 계속 실행되며 데이터에는 아무것도 기록하지 않습니다."};
constexpr Text stagingValidating{"Checking data sets: {0} of {1}. EndfieldHUD keeps running; nothing is written to your data.",
    "正在检查数据：{0} / {1}。EndfieldHUD 会继续运行，不会写入你的数据。", "正在檢查資料：{0} / {1}。EndfieldHUD 會繼續執行，不會寫入你的資料。",
    "データを確認しています：{0} / {1}。EndfieldHUD は動作を続け、データには何も書き込みません。",
    "데이터 확인 중: {0} / {1}. EndfieldHUD는 계속 실행되며 데이터에는 아무것도 기록하지 않습니다."};
constexpr Text cancellingTitle{"Cancelling…", "正在取消…", "正在取消…", "キャンセルしています…", "취소하는 중…"};
constexpr Text cancellingMessage{"Removing the temporary copy. Your data was not changed.", "正在删除临时副本。你的数据未被更改。", "正在刪除暫存副本。你的資料未被變更。",
    "一時コピーを削除しています。データは変更されていません。", "임시 사본을 삭제하는 중입니다. 데이터는 변경되지 않았습니다."};
constexpr Text replaceTitle{"Import again?", "再次导入？", "再次匯入？", "もう一度読み込みますか？", "다시 가져올까요?"};
constexpr Text replaceMessage{"macOS data was already imported on this PC. Importing again replaces it; the current data is kept as a new backup.",
    "此电脑已导入过 macOS 数据。再次导入将替换它，现有数据会另存为新的备份。", "此電腦已匯入過 macOS 資料。再次匯入將取代它，現有資料會另存為新的備份。",
    "この PC には macOS のデータがすでに読み込まれています。もう一度読み込むと置き換えられ、現在のデータは新しいバックアップとして保持されます。",
    "이 PC에는 이미 macOS 데이터를 가져왔습니다. 다시 가져오면 대체되며 현재 데이터는 새 백업으로 보관됩니다."};
constexpr Text importAgain{"Import Again", "再次导入", "再次匯入", "もう一度読み込む", "다시 가져오기"};
constexpr Text reviewTitle{"Review import", "检查导入内容", "檢查匯入內容", "読み込み内容の確認", "가져오기 검토"};
constexpr Text reviewReady{"{0} of {1} data sets are ready.", "{1} 项数据中有 {0} 项可导入。", "{1} 項資料中有 {0} 項可匯入。",
    "{1} 件中 {0} 件のデータを読み込めます。", "{1}개 중 {0}개 데이터를 가져올 수 있습니다."};
constexpr Text reviewRejected{"{0} cannot be imported and will be skipped.", "{0} 项无法导入，将被跳过。", "{0} 項無法匯入，將被略過。",
    "{0} 件は読み込めないためスキップされます。", "{0}개는 가져올 수 없어 건너뜁니다."};
constexpr Text reviewRestart{"EndfieldHUD will save, close, import and reopen; current data is kept as a backup.",
    "EndfieldHUD 将保存、关闭、导入并重新打开，现有数据会保留为备份。", "EndfieldHUD 將儲存、關閉、匯入並重新開啟，現有資料會保留為備份。",
    "EndfieldHUD は保存して終了し、読み込んでから再び開きます。現在のデータはバックアップとして保持されます。",
    "EndfieldHUD가 저장 후 종료하고 가져온 다음 다시 엽니다. 현재 데이터는 백업으로 보관됩니다."};
constexpr Text reviewInProcess{"Current data is kept as a backup.", "现有数据会保留为备份。", "現有資料會保留為備份。",
    "現在のデータはバックアップとして保持されます。", "현재 데이터는 백업으로 보관됩니다."};
constexpr Text reviewNoRestart{"This build cannot restart to finish the import.", "此版本无法重新启动以完成导入。", "此版本無法重新啟動以完成匯入。",
    "このビルドでは再起動して読み込みを完了できません。", "이 빌드에서는 다시 시작하여 가져오기를 완료할 수 없습니다."};
constexpr Text reviewNothing{"The export contains no data EndfieldHUD can import.", "导出内容中没有可导入的数据。", "匯出內容中沒有可匯入的資料。",
    "エクスポートに読み込めるデータがありません。", "내보낸 데이터에 가져올 수 있는 항목이 없습니다."};
constexpr Text import{"Import", "导入", "匯入", "読み込む", "가져오기"};
constexpr Text importRestart{"Import and Restart", "导入并重启", "匯入並重新啟動", "読み込んで再起動", "가져오고 다시 시작"};
constexpr Text skipImport{"Skip and Import", "跳过并导入", "略過並匯入", "スキップして読み込む", "건너뛰고 가져오기"};
constexpr Text restartingTitle{"Restarting to import…", "正在重启以导入…", "正在重新啟動以匯入…", "読み込むために再起動しています…", "가져오기 위해 다시 시작하는 중…"};
constexpr Text restartingMessage{"EndfieldHUD is saving your data. It will close, import the export and reopen.",
    "EndfieldHUD 正在保存数据，随后将关闭、导入并重新打开。", "EndfieldHUD 正在儲存資料，隨後將關閉、匯入並重新開啟。",
    "EndfieldHUD はデータを保存しています。終了して読み込み、再び開きます。", "EndfieldHUD가 데이터를 저장하는 중입니다. 종료 후 가져오고 다시 엽니다."};
constexpr Text restartDeclined{"EndfieldHUD could not save everything, so it did not restart. Nothing was imported.",
    "EndfieldHUD 未能保存全部内容，因此没有重启，也未导入任何内容。", "EndfieldHUD 未能儲存全部內容，因此沒有重新啟動，也未匯入任何內容。",
    "EndfieldHUD がすべてを保存できなかったため再起動しませんでした。何も読み込まれていません。",
    "EndfieldHUD가 모든 내용을 저장하지 못해 다시 시작하지 않았습니다. 아무것도 가져오지 않았습니다."};
constexpr Text committingTitle{"Importing…", "正在导入…", "正在匯入…", "読み込んでいます…", "가져오는 중…"};
constexpr Text committingCarry{"Keeping existing data: {0} of {1} MB.", "正在保留现有数据：{0} / {1} MB。", "正在保留現有資料：{0} / {1} MB。",
    "既存のデータを保持しています：{0} / {1} MB。", "기존 데이터 유지 중: {0} / {1} MB."};
constexpr Text committingActivate{"Switching to the imported data…", "正在切换到导入的数据…", "正在切換到匯入的資料…",
    "読み込んだデータに切り替えています…", "가져온 데이터로 전환하는 중…"};
constexpr Text completeTitle{"Import complete", "导入完成", "匯入完成", "読み込みが完了しました", "가져오기 완료"};
constexpr Text completeMessage{"Imported {0} data sets from EndfieldHUD {1} for Mac.", "已从 Mac 版 EndfieldHUD {1} 导入 {0} 项数据。",
    "已從 Mac 版 EndfieldHUD {1} 匯入 {0} 項資料。", "Mac 版 EndfieldHUD {1} から {0} 件のデータを読み込みました。", "Mac용 EndfieldHUD {1}에서 데이터 {0}개를 가져왔습니다."};
constexpr Text completeRelink{"{0} file references need relinking in their modules.", "有 {0} 个文件引用需要在对应模块中重新关联。",
    "有 {0} 個檔案參照需要在對應模組中重新連結。", "{0} 件のファイル参照を各モジュールで再リンクしてください。", "{0}개의 파일 참조를 각 모듈에서 다시 연결해야 합니다."};
constexpr Text failedTitle{"Import failed", "导入失败", "匯入失敗", "読み込みに失敗しました", "가져오기 실패"};
constexpr Text cancelledTitle{"Import cancelled", "导入已取消", "匯入已取消", "読み込みをキャンセルしました", "가져오기 취소됨"};
constexpr Text unchanged{"Your data was not changed.", "你的数据未被更改。", "你的資料未被變更。", "データは変更されていません。", "데이터는 변경되지 않았습니다."};
constexpr Text errorInvalid{"The folder is not a complete EndfieldHUD Mac export.", "该文件夹不是完整的 EndfieldHUD Mac 导出数据。", "該資料夾不是完整的 EndfieldHUD Mac 匯出資料。",
    "このフォルダは EndfieldHUD の完全な Mac エクスポートではありません。", "이 폴더는 완전한 EndfieldHUD Mac 내보내기가 아닙니다."};
constexpr Text errorCorrupt{"Some exported files do not match the export manifest.", "部分导出文件与导出清单不符。", "部分匯出檔案與匯出清單不符。",
    "一部のエクスポートファイルが一覧と一致しません。", "일부 내보낸 파일이 내보내기 목록과 일치하지 않습니다."};
constexpr Text errorNewer{"The export was made by a newer EndfieldHUD.", "该导出数据由较新版本的 EndfieldHUD 创建。", "該匯出資料由較新版本的 EndfieldHUD 建立。",
    "このエクスポートは新しいバージョンの EndfieldHUD で作成されました。", "이 내보내기는 최신 버전의 EndfieldHUD로 만들어졌습니다."};
constexpr Text errorTooLarge{"The export is larger than the import limit.", "导出数据超过了导入上限。", "匯出資料超過了匯入上限。",
    "エクスポートが読み込みの上限を超えています。", "내보낸 데이터가 가져오기 한도를 초과합니다."};
constexpr Text errorUnavailable{"A file or folder could not be read or written, or the disk is full.", "无法读取或写入文件或文件夹，或磁盘空间不足。",
    "無法讀取或寫入檔案或資料夾，或磁碟空間不足。", "ファイルやフォルダを読み書きできないか、ディスクの空き容量が不足しています。",
    "파일이나 폴더를 읽거나 쓸 수 없거나 디스크 공간이 부족합니다."};
constexpr Text errorConflict{"Some files were in use or changed. Close programs using EndfieldHUD data and try again.",
    "部分文件正在使用或已更改。请关闭使用 EndfieldHUD 数据的程序后重试。", "部分檔案正在使用或已變更。請關閉使用 EndfieldHUD 資料的程式後重試。",
    "一部のファイルが使用中か変更されました。EndfieldHUD のデータを使っているプログラムを閉じてから再試行してください。",
    "일부 파일이 사용 중이거나 변경되었습니다. EndfieldHUD 데이터를 사용하는 프로그램을 닫고 다시 시도하세요."};
constexpr Text errorCancelled{"The import was cancelled.", "导入已取消。", "匯入已取消。", "読み込みはキャンセルされました。", "가져오기가 취소되었습니다."};
constexpr Text errorAlready{"macOS data was already imported on this PC.", "此电脑已导入过 macOS 数据。", "此電腦已匯入過 macOS 資料。",
    "この PC には macOS のデータがすでに読み込まれています。", "이 PC에는 이미 macOS 데이터를 가져왔습니다."};
constexpr Text errorGeneric{"The import could not be completed.", "无法完成导入。", "無法完成匯入。", "読み込みを完了できませんでした。", "가져오기를 완료할 수 없습니다."};
constexpr Text waiting{"Waiting", "等待中", "等待中", "待機中", "대기 중"};
constexpr Text checking{"Checking…", "检查中…", "檢查中…", "確認中…", "확인 중…"};
constexpr Text checked{"Checked", "已检查", "已檢查", "確認済み", "확인됨"};
constexpr Text ready{"Ready", "可导入", "可匯入", "読み込み可", "가져오기 가능"};
constexpr Text readyRelink{"Ready · {0} to relink", "可导入 · {0} 项待关联", "可匯入 · {0} 項待連結", "読み込み可 · 再リンク {0}", "가져오기 가능 · 재연결 {0}"};
constexpr Text readyNotes{"Ready · with notes", "可导入 · 有提示", "可匯入 · 有提示", "読み込み可 · 注意あり", "가져오기 가능 · 참고"};
constexpr Text notInExport{"Not in export", "未包含", "未包含", "含まれていません", "포함되지 않음"};
constexpr Text newer{"Newer · skipped", "版本较新 · 跳过", "版本較新 · 略過", "新しい形式 · スキップ", "최신 형식 · 건너뜀"};
constexpr Text unreadable{"Unreadable · skipped", "无法读取 · 跳过", "無法讀取 · 略過", "読み取り不可 · スキップ", "읽을 수 없음 · 건너뜀"};
constexpr Text imported{"Imported", "已导入", "已匯入", "読み込み済み", "가져옴"};
constexpr Text importedRelink{"Imported · {0} to relink", "已导入 · {0} 项待关联", "已匯入 · {0} 項待連結", "読み込み済み · 再リンク {0}", "가져옴 · 재연결 {0}"};
constexpr Text skipped{"Skipped", "已跳过", "已略過", "スキップ", "건너뜀"};
constexpr Text previousData{"Previous data", "原有数据", "原有資料", "以前のデータ", "이전 데이터"};
constexpr Text summonHotkey{"Summon hotkey", "呼出快捷键", "呼出快速鍵", "呼び出しショートカット", "호출 단축키"};
constexpr Text setAgain{"Set again in Hotkeys", "请在快捷键中重新设置", "請在快速鍵中重新設定", "ショートカットで再設定", "단축키에서 다시 설정"};
constexpr Text signInAgain{"Sign in again", "请重新登录", "請重新登入", "再度サインイン", "다시 로그인"};
constexpr Text reminders{"Reminders rescheduled", "提醒将重新安排", "提醒將重新安排", "リマインダーを再設定", "알림 다시 예약"};
constexpr Text relinkTitle{"Needs relinking", "需要重新关联", "需要重新連結", "再リンクが必要", "다시 연결 필요"};
constexpr Text relinkMessage{"macOS file references cannot open on Windows. Relink each item in its module; nothing is matched automatically.",
    "macOS 的文件引用无法在 Windows 上打开。请在对应模块中逐项重新关联，不会自动匹配。", "macOS 的檔案參照無法在 Windows 上開啟。請在對應模組中逐項重新連結，不會自動配對。",
    "macOS のファイル参照は Windows では開けません。各モジュールで項目ごとに再リンクしてください。自動では照合されません。",
    "macOS 파일 참조는 Windows에서 열 수 없습니다. 각 모듈에서 항목별로 다시 연결하세요. 자동으로 연결되지 않습니다."};
constexpr Text relinkList{"Relink List", "重新关联列表", "重新連結清單", "再リンク一覧", "재연결 목록"};
constexpr Text relinkEmpty{"Nothing needs relinking.", "没有需要重新关联的项目。", "沒有需要重新連結的項目。", "再リンクが必要な項目はありません。", "다시 연결할 항목이 없습니다."};
constexpr Text done{"Done", "完成", "完成", "完了", "완료"};
constexpr Text busyImport{"Another import is still finishing. Try again in a moment.", "另一项导入仍在完成中，请稍后再试。", "另一項匯入仍在完成中，請稍後再試。",
    "別の読み込みを終了しています。しばらくしてからもう一度お試しください。", "다른 가져오기를 마무리하는 중입니다. 잠시 후 다시 시도하세요."};
}

const Text& errorText(std::optional<mig::MacImportErrorCode> code) {
    using C = mig::MacImportErrorCode;
    if (!code) return T::errorGeneric;
    switch (*code) {
    case C::invalid: return T::errorInvalid;
    case C::newerVersion: return T::errorNewer;
    case C::tooLarge: return T::errorTooLarge;
    case C::unavailable: return T::errorUnavailable;
    case C::conflict: return T::errorConflict;
    case C::cancelled: return T::errorCancelled;
    case C::corrupt: return T::errorCorrupt;
    case C::alreadyImported: return T::errorAlready;
    }
    return T::errorGeneric;
}
bool importedStatus(Status s) { return s == Status::imported || s == Status::importedWithWarnings; }
bool rejectedStatus(Status s) { return s == Status::rejectedNewer || s == Status::rejectedInvalid; }
std::string relinkStoreTitle(std::string_view store, Language l) {
    if (store == mig::relinkNotesMedia) return macImportStoreTitle(S::notes, l);
    if (store == mig::relinkArchiveMedia) return macImportStoreTitle(S::archive, l);
    if (store == mig::relinkReaderBook) return macImportStoreTitle(S::reader, l);
    if (store == mig::relinkAppShortcut) return macImportStoreTitle(S::appShortcuts, l);
    if (store == mig::relinkShelfItem) return macImportStoreTitle(S::fileShelf, l);
    return std::string(store);
}
std::string lastComponent(std::string_view path) {
    while (!path.empty() && (path.back() == '/' || path.back() == '\\')) path.remove_suffix(1);
    const auto slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}
} // namespace

std::string_view macImportStepName(MacImportStep step) noexcept {
    switch (step) {
    case MacImportStep::hidden: return "hidden";
    case MacImportStep::intro: return "intro";
    case MacImportStep::choosing: return "choosing";
    case MacImportStep::staging: return "staging";
    case MacImportStep::cancelling: return "cancelling";
    case MacImportStep::replace: return "replace";
    case MacImportStep::review: return "review";
    case MacImportStep::restarting: return "restarting";
    case MacImportStep::committing: return "committing";
    case MacImportStep::report: return "report";
    case MacImportStep::relink: return "relink";
    case MacImportStep::failed: return "failed";
    }
    return "hidden";
}
std::string macImportEntryTitle(Language l) { return pick(T::entry, l); }
std::string macImportStoreTitle(S store, Language l) {
    switch (store) {
    case S::settings: return catalog("Settings", "设置", l);
    case S::notes: return catalog("Notes", "便笺", l);
    case S::archive: return catalog("Archive", "档案库", l);
    case S::profile: return catalog("Personal Profile", "个人名片", l);
    case S::fileShelf: return catalog("Temporary File Shelf", "文件暂存架", l);
    case S::reader: return catalog("E-Reader", "阅读器", l);
    case S::calendar: return catalog("Calendar", "日历", l);
    case S::worldMap: return catalog("Map", "地图", l);
    case S::appShortcuts: return catalog("+ Add App", "+ 添加应用", l);
    case S::eventLog: return catalog("Event Log", "事件日志", l);
    case S::account: return catalog("Account Linking", "账户绑定", l);
    case S::centerLogo: return catalog("Center logo", "中心标志", l);
    }
    return {};
}

MacImportFlow::MacImportFlow(app::UtilityExecutor& utility, fs::path destination, std::function<mig::MacImportPlatform()> platform, Host host)
    : utility_(utility), route_(utility.makeRoute()), destination_(std::move(destination)), platform_(std::move(platform)), host_(std::move(host)),
      alive_(std::make_shared<bool>(true)) {
    service_ = std::make_unique<mig::MacImportService>(utility_, [this] { serviceChanged(); });
    rebuild();
}
MacImportFlow::~MacImportFlow() { shutdown(); }
void MacImportFlow::shutdown() noexcept {
    if (!alive_) return;
    *alive_ = false;
    alive_.reset();
    // A queued discard of a declined handoff still runs (it only removes the
    // private folder); its completion never reaches this destroyed flow.
    utility_.invalidate(route_, false);
    // Dropping the reference: a session accepted by the restart handoff stays
    // alive in the host's work; otherwise its destructor removes its folder.
    handedOff_.reset();
    service_.reset();
}
bool MacImportFlow::inProcess() const { return host_.inProcess && host_.inProcess(); }
bool MacImportFlow::busy() const noexcept { return (service_ && service_->busy()) || discarding_; }

void MacImportFlow::setLanguage(Language language) {
    if (language == Language::system) language = Language::english;
    if (language == language_) return;
    language_ = language;
    rebuild();
}
void MacImportFlow::open() {
    if (!alive_) return;
    if (step_ == MacImportStep::hidden) {
        notice_.clear(); detail_.clear(); first_ = 0;
        step_ = MacImportStep::intro;
    }
    rebuild();
    if (host_.changed) host_.changed();
}
void MacImportFlow::hide() {
    step_ = MacImportStep::hidden;
    notice_.clear(); detail_.clear(); list_.clear(); first_ = 0;
    outcome_.reset(); relink_.clear(); relink_.shrink_to_fit(); recorded_ = false; replace_ = false; declined_ = false;
    rebuild();
}
// Escape: the left (cancel/back) action; on the final report it is Done.
void MacImportFlow::escape() {
    if (step_ == MacImportStep::report) activate(1);
    else if (content_.enabled[0]) activate(0);
}
void MacImportFlow::choose() {
    step_ = MacImportStep::intro;
    if (busy()) { notice_ = pick(T::busyImport, language_); rebuild(); return; }
    notice_.clear();
    if (!host_.chooseFolder || !host_.chooseFolder(pick(T::pickerTitle, language_), pick(T::pickerAccept, language_))) {
        notice_ = pick(T::pickerBusy, language_);
        rebuild();
        return;
    }
    step_ = MacImportStep::choosing;
    rebuild();
}
bool MacImportFlow::scroll(std::ptrdiff_t rows) {
    if (list_.size() <= macImportCardRows || !rows) return false;
    const auto maximum = static_cast<std::ptrdiff_t>(list_.size() - macImportCardRows);
    const auto next = std::clamp(static_cast<std::ptrdiff_t>(first_) + rows, std::ptrdiff_t{0}, maximum);
    if (next == static_cast<std::ptrdiff_t>(first_)) return false;
    first_ = static_cast<std::size_t>(next);
    rebuild(false);
    if (host_.changed) host_.changed();
    return true;
}

void MacImportFlow::activate(unsigned button) {
    if (!alive_ || button > 1 || !content_.enabled[button]) return;
    switch (step_) {
    case MacImportStep::intro:
        if (button == 0) hide();
        else choose();
        break;
    case MacImportStep::choosing:
        if (button == 0 && host_.cancelChoice) host_.cancelChoice(); // the cancelled completion returns to the intro
        break;
    case MacImportStep::staging: if (button == 0) cancelImport(); break;
    case MacImportStep::replace:
        if (button == 0) hide();
        else begin(true);
        break;
    case MacImportStep::review:
        if (button == 0) cancelImport();
        else confirm();
        break;
    case MacImportStep::committing:
        // Honoured until the new root is switched in; the service then
        // reports cancelled (root unchanged) or completed.
        if (button == 0) service_->cancel();
        break;
    case MacImportStep::report:
        if (button == 0) { step_ = MacImportStep::relink; first_ = 0; rebuild(); break; }
        if (recorded_ && host_.forgetOutcome) host_.forgetOutcome();
        hide();
        break;
    case MacImportStep::relink:
        if (button == 0) { step_ = MacImportStep::report; first_ = 0; rebuild(); break; }
        if (recorded_ && host_.forgetOutcome) host_.forgetOutcome();
        hide();
        break;
    case MacImportStep::failed:
        if (recorded_ && host_.forgetOutcome) host_.forgetOutcome();
        recorded_ = false;
        if (button == 0) { hide(); break; }
        outcome_.reset(); detail_.clear(); notice_.clear(); first_ = 0;
        step_ = MacImportStep::intro;
        choose(); // choose a folder again
        break;
    case MacImportStep::hidden: case MacImportStep::cancelling: case MacImportStep::restarting: break;
    }
    if (host_.changed) host_.changed();
}

void MacImportFlow::folderChosen(bool cancelled, bool failed, std::vector<std::string> paths) {
    if (!alive_ || step_ != MacImportStep::choosing) return;
    step_ = MacImportStep::intro;
    if (cancelled) notice_.clear();
    else if (failed) notice_ = pick(T::pickerUnavailable, language_);
    else if (paths.size() != 1 || paths.front().empty()) notice_ = pick(T::chooseOne, language_);
    else {
        auto root = utf8Path(paths.front());
        // Choosing the manifest itself means its folder.
        if (root.filename() == "manifest.json") root = root.parent_path();
        exportRoot_ = std::move(root);
        notice_.clear();
        begin(false);
        return;
    }
    rebuild();
    if (host_.changed) host_.changed();
}

void MacImportFlow::begin(bool replace) {
    replace_ = replace;
    mig::MacImportOptions options;
    options.replaceExistingImport = replace;
    detail_.clear(); notice_.clear(); first_ = 0; declined_ = false;
    step_ = MacImportStep::staging;
    bool started = false;
    try { started = service_->begin(exportRoot_, destination_, platform_(), options); }
    catch (const std::exception& error) { failed(mig::MacImportOutcome::State::failed, std::nullopt, error.what()); return; }
    if (!started) { step_ = MacImportStep::intro; notice_ = pick(T::busyImport, language_); }
    rebuild();
    if (host_.changed) host_.changed();
}

void MacImportFlow::cancelImport() {
    if (handedOff_) { discardHandedOff(); return; }
    step_ = MacImportStep::cancelling;
    service_->cancel();
    serviceChanged();
}
void MacImportFlow::discardHandedOff() {
    step_ = MacImportStep::cancelling;
    discarding_ = true;
    if (handedOff_) {
        std::weak_ptr<bool> alive = alive_;
        auto session = handedOff_;
        if (utility_.submit(route_, [session] { session->discard(); }, [this, alive](std::exception_ptr) {
                if (const auto token = alive.lock(); !token || !*token) return;
                discarding_ = false;
                if (step_ == MacImportStep::cancelling) hide();
                if (host_.changed) host_.changed();
            })) handedOff_.reset();
        // Full queue: retried from utilityCompleted(), never from a timer.
    }
    rebuild();
}
void MacImportFlow::utilityCompleted() {
    if (!alive_) return;
    if (service_) service_->queueCapacityAvailable();
    if (discarding_ && handedOff_) discardHandedOff();
}

void MacImportFlow::confirm() {
    const mig::MacImportSummary* summary = handedOff_ ? &handedOff_->summary() : service_->summary();
    if (!summary) return;
    const auto rejected = summary->rejected();
    if (!inProcess()) {
        if (!host_.restart || !host_.restartAvailable || !host_.restartAvailable()) { rebuild(); return; }
        if (!handedOff_) {
            if (!rejected.empty() && !service_->acceptRejected(rejected)) { rebuild(); return; }
            handedOff_ = service_->handOff();
            if (!handedOff_) { rebuild(); return; }
        }
        declined_ = false;
        if (host_.restart(handedOff_)) step_ = MacImportStep::restarting;
        else declined_ = true; // stays reviewable: confirm again or cancel (discard on the worker)
        rebuild();
        return;
    }
    if (!rejected.empty() && !service_->acceptRejected(rejected)) { rebuild(); return; }
    if (service_->commit()) step_ = MacImportStep::committing;
    rebuild();
}
void MacImportFlow::restartDeclined() {
    if (step_ != MacImportStep::restarting) return;
    step_ = MacImportStep::review;
    declined_ = true;
    rebuild();
    if (host_.changed) host_.changed();
}

void MacImportFlow::serviceChanged() {
    if (!alive_ || !service_) return;
    using St = mig::MacImportService::State;
    const auto state = service_->state();
    switch (step_) {
    case MacImportStep::staging:
        if (state == St::ready) { step_ = MacImportStep::review; first_ = 0; }
        else if (state == St::failed || state == St::cancelled) {
            const auto code = service_->errorCode();
            if (code == mig::MacImportErrorCode::alreadyImported && !replace_) { step_ = MacImportStep::replace; break; }
            failed(state == St::cancelled ? mig::MacImportOutcome::State::cancelled : mig::MacImportOutcome::State::failed, code, service_->error());
            return;
        }
        break;
    case MacImportStep::cancelling:
        // The last staging step can finish before it sees the cancellation:
        // discard that reviewed session on the worker as well.
        if (state == St::ready && !service_->busy()) service_->cancel();
        else if (!discarding_ && !service_->busy() && state != St::staging && state != St::committing) { hide(); if (host_.changed) host_.changed(); return; }
        break;
    case MacImportStep::committing:
        if (state == St::completed) { finishCommit(); return; }
        if (state == St::failed || state == St::cancelled) {
            failed(state == St::cancelled ? mig::MacImportOutcome::State::cancelled : mig::MacImportOutcome::State::failed, service_->errorCode(), service_->error());
            return;
        }
        break;
    default: break;
    }
    rebuild();
    if (host_.changed) host_.changed();
}

void MacImportFlow::finishCommit() {
    const auto* summary = service_->summary();
    const auto& result = service_->result();
    if (!summary || !result) { failed(mig::MacImportOutcome::State::failed, std::nullopt, "The import finished without a result"); return; }
    outcome_ = mig::completedMacImportOutcome(*summary, *result, host_.unixNow ? host_.unixNow() : 0.0);
    relink_.clear();
    for (const auto& item : summary->relink) if (item.state == mig::RelinkState::unresolved) relink_.push_back(item);
    recorded_ = false;
    step_ = MacImportStep::report;
    first_ = 0;
    rebuild();
    if (host_.changed) host_.changed();
}
void MacImportFlow::failed(mig::MacImportOutcome::State state, std::optional<mig::MacImportErrorCode> code, std::string error) {
    const auto* summary = service_ ? service_->summary() : nullptr;
    outcome_ = mig::failedMacImportOutcome(state, code, std::move(error), summary, host_.unixNow ? host_.unixNow() : 0.0);
    detail_ = outcome_->state == mig::MacImportOutcome::State::failed ? outcome_->error : std::string{};
    recorded_ = false;
    step_ = MacImportStep::failed;
    first_ = 0;
    rebuild();
    if (host_.changed) host_.changed();
}
void MacImportFlow::showOutcome(mig::MacImportOutcome outcome, std::vector<mig::MacRelinkItem> relink) {
    if (!alive_) return;
    // A running import keeps its card; the note stays for the next opening.
    if (step_ != MacImportStep::hidden) return;
    const bool completed = outcome.state == mig::MacImportOutcome::State::completed;
    detail_ = outcome.state == mig::MacImportOutcome::State::failed ? outcome.error : std::string{};
    outcome_ = std::move(outcome);
    relink_ = std::move(relink);
    recorded_ = true;
    first_ = 0;
    step_ = completed ? MacImportStep::report : MacImportStep::failed;
    rebuild();
    if (host_.changed) host_.changed();
}

// --- Content ------------------------------------------------------------------
void MacImportFlow::reviewRows() {
    list_.clear();
    const mig::MacImportSummary* summary = handedOff_ ? &handedOff_->summary() : service_->summary();
    if (!summary) return;
    for (const auto store : mig::macImportStores) {
        const auto& r = summary->store(store);
        MacImportRow row{macImportStoreTitle(store, language_), {}, MacImportTone::primary};
        switch (r.status) {
        case Status::imported: row.value = r.relinkItems ? format(T::readyRelink, language_, {number(r.relinkItems)}) : pick(T::ready, language_); break;
        case Status::importedWithWarnings:
            row.value = r.relinkItems ? format(T::readyRelink, language_, {number(r.relinkItems)}) : pick(T::readyNotes, language_);
            row.tone = MacImportTone::accent;
            break;
        case Status::notInExport: row.value = pick(T::notInExport, language_); row.tone = MacImportTone::secondary; break;
        case Status::rejectedNewer: row.value = pick(T::newer, language_); row.tone = MacImportTone::alert; break;
        case Status::rejectedInvalid: row.value = pick(T::unreadable, language_); row.tone = MacImportTone::alert; break;
        case Status::pending: row.value = pick(T::waiting, language_); row.tone = MacImportTone::secondary; break;
        }
        list_.push_back(std::move(row));
    }
}
void MacImportFlow::reportRows() {
    list_.clear();
    if (!outcome_) return;
    const auto& o = *outcome_;
    if (!o.backup.empty()) list_.push_back({pick(T::previousData, language_), lastComponent(o.backup), MacImportTone::secondary});
    if (o.customShortcutUntranslated) list_.push_back({pick(T::summonHotkey, language_), pick(T::setAgain, language_), MacImportTone::accent});
    const auto account = std::find_if(o.stores.begin(), o.stores.end(), [](const auto& s) { return s.store == S::account; });
    if (account != o.stores.end() && importedStatus(account->status))
        list_.push_back({macImportStoreTitle(S::account, language_), pick(T::signInAgain, language_), MacImportTone::accent});
    if (o.remindersNeedReconcile) list_.push_back({macImportStoreTitle(S::calendar, language_), pick(T::reminders, language_), MacImportTone::secondary});
    for (const auto& s : o.stores) {
        MacImportRow row{macImportStoreTitle(s.store, language_), {}, MacImportTone::primary};
        if (importedStatus(s.status)) row.value = s.relinkItems ? format(T::importedRelink, language_, {number(s.relinkItems)}) : pick(T::imported, language_);
        else if (rejectedStatus(s.status)) { row.value = pick(T::skipped, language_); row.tone = MacImportTone::alert; }
        else { row.value = pick(T::notInExport, language_); row.tone = MacImportTone::secondary; }
        list_.push_back(std::move(row));
    }
}
void MacImportFlow::relinkRows() {
    list_.clear();
    list_.reserve(relink_.size());
    for (const auto& item : relink_) {
        auto name = !item.displayName.empty() ? item.displayName : !item.lastKnownPath.empty() ? lastComponent(item.lastKnownPath) : item.recordID;
        list_.push_back({std::move(name), relinkStoreTitle(item.store, language_), MacImportTone::primary});
    }
}

// The full list behind the visible rows (content events only, never frames).
void MacImportFlow::buildList() {
    const auto l = language_;
    const auto progress = service_ ? service_->progress() : mig::MacImportProgress{};
    using P = mig::MacImportPhase;
    list_.clear();
    switch (step_) {
    case MacImportStep::intro: case MacImportStep::choosing: case MacImportStep::cancelling:
        if (!notice_.empty()) break;
        for (const auto store : mig::macImportStores) list_.push_back({macImportStoreTitle(store, l), {}, MacImportTone::secondary});
        break;
    case MacImportStep::staging: {
        const bool validating = progress.phase == P::validating || progress.phase == P::staged;
        for (std::size_t n = 0; n < mig::macImportStores.size(); ++n) {
            const auto& value = !validating || n > progress.validatedStores ? T::waiting : n == progress.validatedStores ? T::checking : T::checked;
            list_.push_back({macImportStoreTitle(mig::macImportStores[n], l), pick(value, l),
                             validating && n < progress.validatedStores ? MacImportTone::primary : MacImportTone::secondary});
        }
        break;
    }
    case MacImportStep::review: case MacImportStep::restarting: case MacImportStep::committing: reviewRows(); break;
    case MacImportStep::report: reportRows(); break;
    case MacImportStep::relink: relinkRows(); break;
    case MacImportStep::hidden: case MacImportStep::replace: case MacImportStep::failed: break;
    }
}

void MacImportFlow::rebuild(bool list) {
    const auto l = language_;
    if (list) buildList();
    MacImportCardContent next;
    const auto cancel = catalog("Cancel", "取消", l);
    const auto progress = service_ ? service_->progress() : mig::MacImportProgress{};
    using P = mig::MacImportPhase;
    switch (step_) {
    case MacImportStep::hidden: break;
    case MacImportStep::intro:
    case MacImportStep::choosing:
        next.title = pick(T::introTitle, l);
        next.message = pick(step_ == MacImportStep::choosing ? T::choosingMessage : T::introMessage, l);
        next.detail = notice_;
        next.buttons = {cancel, pick(T::chooseFolder, l)};
        next.enabled = {true, step_ == MacImportStep::intro};
        break;
    case MacImportStep::staging: {
        next.title = pick(T::stagingTitle, l);
        if (progress.phase == P::opening) next.message = pick(T::stagingOpening, l);
        else if (progress.phase == P::verifying) next.message = format(T::stagingVerifying, l, {megabytes(progress.verifiedBytes), megabytes(progress.exportBytes)});
        else next.message = format(T::stagingValidating, l, {number(progress.validatedStores), number(mig::macImportStores.size())});
        double fraction = 0;
        if (progress.phase == P::verifying && progress.exportBytes) fraction = .85 * static_cast<double>(progress.verifiedBytes) / static_cast<double>(progress.exportBytes);
        else if (progress.phase == P::validating || progress.phase == P::staged)
            fraction = .85 + .15 * static_cast<double>(progress.validatedStores) / static_cast<double>(mig::macImportStores.size());
        next.progress = std::clamp(fraction, 0.0, 1.0);
        next.buttons = {cancel, pick(T::import, l)};
        next.enabled = {true, false};
        break;
    }
    case MacImportStep::cancelling:
        next.title = pick(T::cancellingTitle, l);
        next.message = pick(T::cancellingMessage, l);
        next.buttons = {cancel, pick(T::import, l)};
        next.enabled = {false, false};
        break;
    case MacImportStep::replace:
        next.title = pick(T::replaceTitle, l);
        next.message = pick(T::replaceMessage, l);
        next.buttons = {cancel, pick(T::importAgain, l)};
        break;
    case MacImportStep::review:
    case MacImportStep::restarting:
    case MacImportStep::committing: {
        const mig::MacImportSummary* summary = handedOff_ ? &handedOff_->summary() : service_ ? service_->summary() : nullptr;
        std::size_t readyCount{}, rejectedCount{};
        if (summary) for (const auto& r : summary->stores) { readyCount += importedStatus(r.status); rejectedCount += rejectedStatus(r.status); }
        const bool restart = !inProcess();
        const bool available = !restart || (host_.restart && host_.restartAvailable && host_.restartAvailable());
        if (step_ == MacImportStep::review) {
            next.title = pick(T::reviewTitle, l);
            if (declined_) next.message = pick(T::restartDeclined, l);
            else if (!readyCount) next.message = pick(T::reviewNothing, l);
            else next.message = join(l, {format(T::reviewReady, l, {number(readyCount), number(mig::macImportStores.size())}),
                                         rejectedCount ? format(T::reviewRejected, l, {number(rejectedCount)}) : std::string{},
                                         pick(!restart ? T::reviewInProcess : available ? T::reviewRestart : T::reviewNoRestart, l)});
            next.buttons = {cancel, pick(rejectedCount ? T::skipImport : restart ? T::importRestart : T::import, l)};
            next.enabled = {true, readyCount > 0 && available};
        } else if (step_ == MacImportStep::restarting) {
            next.title = pick(T::restartingTitle, l);
            next.message = pick(T::restartingMessage, l);
            next.buttons = {cancel, pick(T::importRestart, l)};
            next.enabled = {false, false};
        } else {
            next.title = pick(T::committingTitle, l);
            const bool activating = progress.phase == P::activating || progress.phase == P::committed;
            next.message = activating ? pick(T::committingActivate, l)
                                      : format(T::committingCarry, l, {megabytes(progress.carriedBytes), megabytes(progress.carryBytes)});
            next.progress = activating ? 1.0 : progress.carryBytes ? .9 * static_cast<double>(progress.carriedBytes) / static_cast<double>(progress.carryBytes) : 0.0;
            next.buttons = {cancel, pick(T::import, l)};
            next.enabled = {!activating, false};
        }
        break;
    }
    case MacImportStep::report: {
        std::size_t importedCount{};
        if (outcome_) for (const auto& s : outcome_->stores) importedCount += importedStatus(s.status);
        const auto version = outcome_ && !outcome_->sourceVersion.empty() ? outcome_->sourceVersion : std::string("\xE2\x80\x94");
        next.title = pick(T::completeTitle, l);
        next.message = join(l, {format(T::completeMessage, l, {number(importedCount), version}),
                                relink_.empty() ? std::string{} : format(T::completeRelink, l, {number(relink_.size())})});
        next.buttons = {relink_.empty() ? pick(T::relinkList, l) : pick(T::relinkList, l) + " (" + number(relink_.size()) + ")", pick(T::done, l)};
        next.enabled = {!relink_.empty(), true};
        break;
    }
    case MacImportStep::relink:
        next.title = pick(T::relinkTitle, l);
        next.message = pick(T::relinkMessage, l);
        if (relink_.empty()) next.detail = pick(T::relinkEmpty, l);
        next.buttons = {catalog("Back", "返回", l), pick(T::done, l)};
        break;
    case MacImportStep::failed: {
        const bool cancelled = outcome_ && outcome_->state == mig::MacImportOutcome::State::cancelled;
        next.title = pick(cancelled ? T::cancelledTitle : T::failedTitle, l);
        next.message = join(l, {cancelled ? std::string{} : pick(errorText(outcome_ ? outcome_->code : std::nullopt), l), pick(T::unchanged, l)});
        next.detail = detail_;
        next.buttons = {catalog("Close", "关闭", l), pick(T::chooseFolder, l)};
        break;
    }
    }
    first_ = list_.size() > macImportCardRows ? std::min(first_, list_.size() - macImportCardRows) : 0;
    if (next.detail.empty()) {
        for (std::size_t n = 0; n < macImportCardRows && first_ + n < list_.size(); ++n) next.rows[n] = list_[first_ + n];
        if (list_.size() > macImportCardRows)
            next.position = number(first_ + 1) + "\xE2\x80\x93" + number(std::min(first_ + macImportCardRows, list_.size())) + " / " + number(list_.size());
    }
    if (next != content_) { content_ = std::move(next); ++revision_; }
}
} // namespace endfield::areas
