import Foundation

/// Translations of the centre redesign's strings, one line per source pair:
/// English, Simplified Chinese (the keys), Traditional Chinese, Japanese and
/// Korean. Toast titles in CJK UIs carry the meaning of the Chinese caption.
extension LocalizationCatalog {
    struct RedesignEntry {
        let entry: Entry
        let korean: String
    }

    private static func R(_ english: String, _ simplifiedChinese: String, _ traditionalChinese: String,
                          _ japanese: String, _ korean: String) -> RedesignEntry {
        RedesignEntry(entry: Entry(english, simplifiedChinese, traditionalChinese, japanese), korean: korean)
    }

    static var redesignKoreanTranslations: [String: String] {
        Dictionary(redesignEntries.map { ($0.entry.english + "\u{1F}" + $0.entry.simplifiedChinese, $0.korean) },
                   uniquingKeysWith: { first, _ in first })
    }

    static let redesignEntries: [RedesignEntry] = [
        // Shared components and the host
        R("Done", "已完成", "已完成", "完了", "완료"),
        R("In progress", "进行中", "進行中", "進行中", "진행 중"),
        R("Notes, shelf files and settings are saved.", "便笺、暂存文件与设置均已保存。", "便箋、暫存檔案與設定均已儲存。",
          "メモ、一時保管ファイル、設定は保存されています。", "메모, 보관 파일과 설정이 저장되어 있습니다."),
        // Settings
        R("System", "系统设置", "系統設定", "システム設定", "시스템 설정"),
        R("Restore default settings?", "恢复默认设置？", "恢復預設設定？", "既定の設定に戻しますか？", "기본 설정으로 복원할까요?"),
        R("Settings restored", "已恢复默认设置", "已恢復預設設定", "既定の設定に戻しました", "기본 설정 복원됨"),
        R("12-hour", "12 小时制", "12 小時制", "12時間制", "12시간제"),
        R("Next clock style", "下一个时钟样式", "下一個時鐘樣式", "次の時計スタイル", "다음 시계 스타일"),
        R("Recording mode", "录制模式", "錄製模式", "録画モード", "녹화 모드"),
        R("Recording mode on", "私密内容已隐藏", "私密內容已隱藏", "非公開の内容を隠しました", "비공개 내용 숨김"),
        R("Recording mode off", "私密内容已恢复", "私密內容已恢復", "非公開の内容を再表示しました", "비공개 내용 복원됨"),
        R("Daily boot log", "每日启动日志", "每日啟動日誌", "毎日の起動ログ", "일일 부팅 로그"),
        R("Facility alerts", "设施警报", "設施警報", "施設アラート", "시설 경보"),
        R("Keep", "保留", "保留", "保持", "유지"),
        R("Revert", "恢复", "還原", "元に戻す", "되돌리기"),
        R("UI scale kept", "已保留界面缩放", "已保留介面縮放", "UIの拡大率を保持しました", "UI 배율 유지됨"),
        R("UI scale reverted", "已恢复界面缩放", "已恢復介面縮放", "UIの拡大率を元に戻しました", "UI 배율 되돌림"),
        R("Position kept", "已保留界面位置", "已保留介面位置", "位置を保持しました", "위치 유지됨"),
        R("Position reverted", "已恢复界面位置", "已恢復介面位置", "位置を元に戻しました", "위치 되돌림"),
        R("Don't display", "不显示", "不顯示", "表示しない", "표시 안 함"),
        R("Bar hidden", "不再自动显示", "不再自動顯示", "自動表示しません", "자동 표시 안 함"),
        R("In use", "使用中", "使用中", "使用中", "사용 중"),
        R("Notice", "提示", "提示", "お知らせ", "알림"),
        R("Import failed", "导入失败", "匯入失敗", "読み込み失敗", "가져오기 실패"),
        R("Open with hotkey", "使用快捷键打开", "使用快捷鍵開啟", "ショートカットで開く", "단축키로 열기"),
        R("Press a shortcut", "请按快捷键", "請按快捷鍵", "ショートカットを押してください", "단축키를 누르세요"),
        R("Hotkey saved", "快捷键已保存", "快捷鍵已儲存", "ショートカットを保存しました", "단축키 저장됨"),
        R("Hotkey restored", "已恢复默认快捷键", "已恢復預設快捷鍵", "既定のショートカットに戻しました", "기본 단축키 복원됨"),
        R("Hotkey unavailable", "快捷键不可用", "快捷鍵無法使用", "ショートカットを使用できません", "단축키 사용 불가"),
        R("Check", "检查", "檢查", "確認", "확인"),
        R("Connecting", "连接中", "連線中", "接続中", "연결 중"),
        R("Reading releases", "读取版本", "讀取版本", "リリースを読み込み中", "릴리스 읽는 중"),
        R("Verifying", "校验中", "驗證中", "検証中", "확인 중"),
        R("Up to date", "已是最新", "已是最新", "最新です", "최신 상태"),
        R("Credits", "致谢", "致謝", "クレジット", "크레딧"),
        R("Opening GitHub", "正在打开 GitHub", "正在開啟 GitHub", "GitHubを開いています", "GitHub 여는 중"),
        R("Opening link", "正在打开链接", "正在開啟連結", "リンクを開いています", "링크 여는 중"),
        // Battery
        R("Battery", "电池状态", "電池狀態", "バッテリー", "배터리"),
        // Facility alerts
        R("LOW DISK SPACE", "存储空间不足", "儲存空間不足", "ディスク容量不足", "저장 공간 부족"),
        R("HIGH MEMORY PRESSURE", "内存压力过高", "記憶體壓力過高", "メモリ負荷が高い", "메모리 압박 높음"),
        R("OVERHEATING", "设备过热", "裝置過熱", "過熱", "과열"),
        R("DISK", "磁盘", "磁碟", "ディスク", "디스크"),
        R("HEAT", "温度", "溫度", "温度", "온도"),
        R("FREE", "可用", "可用", "空き", "여유"),
        R("PRESSURE", "压力", "壓力", "負荷", "압박"),
        R("BATTERY", "电池", "電池", "バッテリー", "배터리"),
        R("THERMAL STATE", "热状态", "熱狀態", "温度状態", "열 상태"),
        R("SERIOUS", "严重", "嚴重", "深刻", "심각"),
        R("CRITICAL", "危急", "危急", "危険", "위급"),
        R("WARNING", "警告", "警告", "警告", "경고"),
        R("Fans at maximum", "风扇全速运转", "風扇全速運轉", "ファンが最大回転中", "팬 최대 속도"),
        R("{0}  {1} left", "{0}  剩余 {1}", "{0}  剩餘 {1}", "{0}  残り {1}", "{0}  {1} 남음"),
        R("{0} / {1} GB in use", "已用 {0} / {1} GB", "已用 {0} / {1} GB", "{0} / {1} GB 使用中", "{0} / {1} GB 사용 중"),
        // Archive
        R("Category {0}", "分类 {0}", "分類 {0}", "カテゴリ {0}", "카테고리 {0}"),
        R("The Keychain could not be read. The document stays classified.", "无法读取钥匙串，文档仍保持机密。",
          "無法讀取鑰匙圈，文件仍保持機密。", "キーチェーンを読み取れませんでした。書類は機密のままです。",
          "키체인을 읽을 수 없습니다. 문서는 기밀로 유지됩니다."),
        R("This document's password is missing from the Keychain. It stays classified.", "钥匙串中缺少此文档的密码，文档仍保持机密。",
          "鑰匙圈中缺少此文件的密碼，文件仍保持機密。", "この書類のパスワードがキーチェーンにありません。書類は機密のままです。",
          "키체인에 이 문서의 암호가 없습니다. 문서는 기밀로 유지됩니다."),
        R("restore access to a classified document", "恢复对机密文档的访问", "恢復對機密文件的存取", "機密書類へのアクセスを復元",
          "기밀 문서에 대한 접근 복원"),
    ]
}
