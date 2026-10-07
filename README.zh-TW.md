# EndfieldHUD

[English](README.md) · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md)

一款《明日方舟：終末地》風格的 macOS 選單列 App。按下快捷鍵，就能開啟具有層次與動態效果的 HUD，使用便箋、檔案暫存、計時、音量控制和系統狀態等功能。

**整合測試分支：**本分支將新的 Watch 介面與穩定版 1.0.1 的功能及存檔格式整合。測試本分支時，請開啟[本分支通過的 Build and test 建置](https://github.com/DDDuoDuo/EndfieldHUD/actions/workflows/build.yml?query=branch%3Acodex%2Fendfield-hud-integration)，下載其中的 **EndfieldHUD-macOS-…** 產物。下方的正式版下載連結和預覽屬於穩定版，並非本分支。

**[下載 v1.0.1 — EndfieldHUD-1.0.1-build11-macOS.dmg](https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.0.1/EndfieldHUD-1.0.1-build11-macOS.dmg)** · [所有版本](https://github.com/DDDuoDuo/EndfieldHUD/releases)

![EndfieldHUD 電源頁面，使用範例讀數](docs/media/overview.png)

*下方預覽均來自實際 App，使用示範內容與範例讀數，不包含使用者的私人便箋、剪貼簿記錄或檔案。*

## 安裝

1. 下載上方的 DMG。如果舊版正在執行，先從選單列結束。
2. 開啟 DMG，將 **EndfieldHUD.app** 拖入 **Applications（應用程式）**。
3. 退出磁碟映像檔，再從「應用程式」開啟 EndfieldHUD。圖示會出現在選單列。
4. 按 **Ctrl + 反引號（`）** 開啟 HUD。

此版本使用**本機臨時簽署（ad hoc），未經 Apple 公證**。如果 macOS 因無法驗證開發者而阻擋開啟，請先嘗試開啟已安裝的 App；確認信任此下載後，在**系統設定 → 隱私權與安全性 → 強制打開**中僅批准 EndfieldHUD，再確認開啟。較舊的 macOS 使用「系統偏好設定 → 安全性與隱私權」。無須關閉 Gatekeeper。請參閱 [Apple 操作說明](https://support.apple.com/zh-tw/102445)。

App 以 **Apple 晶片 macOS 11 以上**、**Intel macOS 10.15.4 以上**為建置目標，部分功能需要更新的系統。測試主要在 macOS 15.7.4 的 M2 MacBook Air 上進行，其他機型和舊系統的驗證仍有限。

## 先試試這些

- **開啟或隱藏：**按 Ctrl + 反引號，也可使用選單列的**開啟浮層**。Esc 會先離開目前的編輯或選取，再關閉 HUD。
- **切換功能：**點選兩側按鈕；右側清單可捲動。「儲存」和「活動監視器」位於中央下方，左下角名片可開啟個人資料。
- **切換語言：**進入**系統 → 語言**，在有勾選標記的清單中選擇**跟隨系統、English、简体中文、繁體中文或日本語**。四種介面語言立即生效，「跟隨系統」依 macOS 語言顯示。[語言選擇](docs/media/languages.png) · [繁體中文介面](docs/media/traditional-chinese.png)
- **調整外觀：**「顯示」可設定大小、位置、顏色、主題、模糊、透視和減少動態效果；「快速鍵」可變更喚出方式。
- **結束 App：**在選單列選擇**結束 EndfieldHUD**，或點選名片旁的紅色按鈕並確認。隱藏 HUD 後，App 仍會繼續執行。

<details>
<summary>觀看展開、收起、傾斜與頁面切換</summary>

展開、收起與隨滑鼠傾斜：

![HUD 展開、收起與傾斜示範](docs/media/motion.gif)

功能頁面之間的切換：

![HUD 頁面切換示範](docs/media/modules.gif)

</details>

## 功能一覽

點選預覽查看對應頁面，功能名稱連結至詳細說明。

| 頁面 | 可以做什麼 | 預覽 |
| --- | --- | --- |
| 電源 | 查看可讀取的電池與充電狀態；可開啟獨立於主 HUD 的充電提醒。 | [電源](docs/media/overview.png) |
| [便箋](docs/notes-canvas.md) | 新增文字、待辦事項和圖片，自由移動、縮放，並釘選在其他 HUD 頁面。 | [便箋](docs/media/notes.png) |
| [檔案暫存架](docs/file-shelf.md) | 放入檔案或資料夾，快速預覽，再一起拖出。移除暫存項目不會刪除原始檔案。 | [檔案架](docs/media/shelf.png) |
| [剪貼簿](docs/clipboard-cache.md) | 找回最近複製的文字、連結、圖片和檔案，釘選常用項目。記錄在結束 App 後清空。 | [剪貼簿](docs/media/clipboard.png) |
| [音量](docs/audio-and-work-mode.md) | 選擇音訊裝置，調整裝置支援的音量、靜音與左右平衡。個別 App 音量為實驗功能，需要 macOS 14.2+。 | [音量](docs/media/volume.png) |
| [工作模式](docs/audio-and-work-mode.md#work-mode) | 使用倒數計時或碼錶，提供 5／30／60 分鐘預設和自訂時間。隱藏 HUD 後繼續計時。 | [工作模式](docs/media/work-mode.png) |
| [事件記錄](docs/event-log.md) | 瀏覽和篩選本機 HUD 事件，例如計時、檔案暫存操作與裝置變更。 | [事件記錄](docs/media/event-log.png) |
| [儲存](docs/storage-and-activity.md#storage) | 查看已用和可用空間，重新整理讀數，或開啟 macOS 儲存空間設定。 | [儲存](docs/media/storage.png) |
| [活動監視器](docs/storage-and-activity.md) | 查看 CPU、記憶體、網路與磁碟圖表，以及可排序的 App 清單。無法讀取的資料顯示為橫線。 | [活動監視器](docs/media/activity.png) |
| [+ 添加應用程式](docs/app-shortcuts.md) | 為已安裝的 App 儲存捷徑，自訂名稱與圖示，從 HUD 中開啟。 | [App 捷徑](docs/media/apps.png) |
| [個人名片](docs/personal-profile.md) | 編輯名稱、頭像、背景、介紹與日期，查看累計工作模式時間。 | [個人名片](docs/media/profile.png) |
| [地圖](docs/map.md) | 平移、縮放離線地球地圖，新增自己的標記。不會讀取定位，也不載入街道地圖。 | [地圖](docs/media/map.png) |
| 系統 | 選擇語言、顯示器、登入啟動及其他行為。 | [語言選擇](docs/media/languages.png) |
| 顯示 | 調整外觀、App／選單列圖示和電池提醒。大小與位置變更支援限時還原。 | [設定](docs/media/settings.png) |
| 快速鍵 | 直接在 HUD 中設定新的喚出快捷鍵。 | [設定](docs/media/settings.png) |
| 關於 | 查看版本與致謝，檢查更新，設定是否自動安裝更新。 | [設定](docs/media/settings.png) |

## 權限與本機資料

| 功能 | 需要什麼 |
| --- | --- |
| 個別 App 音量 | **macOS 14.2+**；啟用時需要**系統音訊錄製**權限。音訊僅在記憶體中處理，不會儲存。裝置支援有限，請參閱[音量說明](docs/per-app-audio.md)與 [Apple 權限說明](https://support.apple.com/en-in/guide/mac-help/mchl2844ecab/mac)。 |
| 檔案與圖片 | 存取你選擇、貼上或拖入的項目。檔案架儲存參照；圖片便箋與個人名片圖片儲存本機副本。 |
| 更新提醒 | 通知權限可選。即使拒絕通知，選單列和「關於」仍會顯示更新資訊。 |
| 登入啟動 | 先安裝至「應用程式」；macOS 可能要求在登入項目中批准。 |
| 自動專注模式 | 本整合分支在 **macOS 11+** 上優先使用控制中心，需要你授予**輔助使用**權限且系統控制項可辨識；**macOS 13+** 上已設定的捷徑作為備援。穩定版 v1.0.1 下載仍需自行建立這兩個捷徑。請參閱[專注模式設定](docs/audio-and-work-mode.md#work-mode)；沒有自動專注也能使用計時器。 |

便箋、檔案參照、個人名片、地圖標記與事件記錄儲存在本機。剪貼簿記錄只保存在記憶體中，結束後清空。檢查和下載更新會連線至 GitHub，使用 App 無須帳號。[設定說明](docs/settings.md)

## 專案與致謝

EndfieldHUD 由 **DDDuoDuo** 開發，是受《明日方舟：終末地》啟發的**非官方同人專案**，與遊戲製作方無關，也未經其認可。

原創程式碼採用 [MIT 授權](LICENSE)。遊戲圖片、品牌標誌及其他第三方素材保留各自的權利與授權。視覺參考、素材、地圖資料和相依項目的來源請見 [Credits／致謝](CREDITS.md)。

[回報問題](https://github.com/DDDuoDuo/EndfieldHUD/issues)
