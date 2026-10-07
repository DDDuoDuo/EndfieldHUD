![EndfieldHUD](docs/media/readme/endfield-hud-title.png)

<p align="center">
  <a href="https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0"><img src="docs/media/readme/version-badge.svg" alt="リリース v1.2.0" height="38"></a>
  <a href="https://github.com/DDDuoDuo/EndfieldHUD/issues"><img src="docs/media/readme/bug-badge.svg" alt="不具合を報告" height="38"></a>
  <a href="https://space.bilibili.com/223936961"><img src="docs/media/readme/bilibili-badge.svg" alt="DDDuoDuo の Bilibili" height="38"></a>
</p>

<p align="center">
  <a href="README.md">English</a> · <a href="README.zh-CN.md">简体中文</a> · <a href="README.zh-TW.md">繁體中文</a> · <a href="README.ja.md">日本語</a>
</p>

Mac に『アークナイツ：エンドフィールド』をちょっと。ショートカットやメニューバーから、奥行きのある動く HUD を開けます。メモ、ファイル、音楽、ゲームの情報、毎日使うツールをひとまとめに。

<a href="https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0"><img src="docs/media/readme/release-v1.2.0.png" alt="リリース v1.2.0" width="326"></a>

[PKG をダウンロード](https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.2.0/EndfieldHUD-1.2.0-build18-Installer.pkg) · [DMG をダウンロード](https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.2.0/EndfieldHUD-1.2.0-build18-macOS.dmg) · [すべてのリリース](https://github.com/DDDuoDuo/EndfieldHUD/releases)

<img src="docs/media/readme/en-01-installation.png" alt="インストール" width="326">

0. すでに EndfieldHUD を使っている場合は、メニューバーの **更新を確認**をクリックします。
1. DMG を開いて **EndfieldHUD.app** を **Applications（アプリケーション）** にドラッグするか、PKG インストーラを実行します。
2. 「アプリケーション」から EndfieldHUD を開くと、メニューバーにアイコンが表示されます。
3. **Ctrl + バッククォート（`）** で HUD を開きます。キーは **ショートカット**で変更できます。

macOS にブロックされた場合は、**システム設定 → プライバシーとセキュリティ → このまま開く**から確認して開いてください。アプリはまだ Apple の公証を受けていません。

左右のボタンで画面を切り替え、右側をスクロールするとほかの画面も選べます。**Esc** は編集中の項目を先に閉じ、その後 HUD を閉じます。HUD を閉じてもアプリは動き続けます。終了するには赤い電源ボタンを押してください。

![HUD の開閉](docs/media/readme/opening.gif)

<img src="docs/media/readme/en-02-requirements.png" alt="動作環境" width="326">

Apple シリコン：**macOS 11 以降**。Intel：**macOS 10.15.4 以降**。一部の機能には、より新しい OS や対応するハードウェアが必要です。

必要な機能を使うときに、次の権限を許可してください。

| 権限 | 用途 |
| --- | --- |
| アクセシビリティ | 作業モードからコントロールセンターを通じて macOS の集中モードを切り替えます。許可なしでもタイマーは使えます。 |
| システムオーディオ録音 | macOS 14.2 以降の実験的なアプリ別音量調整に使います。音声は保存しません。 |
| オートメーション | 対応する音楽アプリの操作に使います。macOS に確認されたときに許可してください。 |
| 通知 | 更新のお知らせとカレンダーのリマインダーに使います。 |
| ファイルとフォルダ | 自分で選んだファイル、画像、動画を開くために使います。 |

権限は **システム設定 → プライバシーとセキュリティ**で確認できます。通知は専用の設定画面があります。

<img src="docs/media/readme/en-03-functions.png" alt="機能" width="326">

![HUD の画面切り替え](docs/media/readme/modules.gif)

| ツール | できること |
| --- | --- |
| メモ | 文字や絵、チェックリスト、画像、動画を追加できます。パネルをピン留めすれば、ほかの画面でも表示できます。 |
| 一時ファイルシェルフ | ファイルをドロップしてプレビューし、そのままドラッグして取り出せます。元のファイルは移動しません。 |
| クリップボード | 最近コピーしたテキスト、リンク、画像、ファイルを見返せます。履歴はアプリ終了時に消えます。 |
| アーカイブ | 文書を自分のカテゴリで整理できます。文字の装飾や画像・動画にも対応しています。 |
| リーダー | 小説や PDF を読み、しおりを付けたり拡大したりできます。 |
| メディア編集 | 画像や動画の切り抜き、色などの調整、フィルター、ステッカーを使えます。 |
| プロジェクション | 画面いっぱいのキャンバスに描けます。ドットの背景も選べます。 |
| 再生中 | アルバムアートや取得できる歌詞を表示し、再生を操作できます。 |
| 音量 | 音声デバイスを切り替え、対応する音量を調整できます。 |
| 作業モード・カレンダー | タイマーやストップウォッチを使い、予定とリマインダーを登録できます。 |
| マップ・ミニゲーム | オフラインの地形図を見てピンを置いたり、Merge! OrbiPom! で遊んだりできます。 |
| 個人 ID・アカウント連携 | 自分のカードをカスタマイズできます。ゲームアカウントを連携すると、プロフィール情報や理性の回復カウントダウンを表示できます。 |
| バッテリー・ストレージ・アクティビティモニタ | 充電状態、ディスク容量、CPU、RAM、実行中のアプリを確認できます。 |
| アプリのショートカット | 好きな名前とアイコンでアプリを追加できます。 |

![Merge! OrbiPom! のプレイ映像](docs/media/readme/minigame.gif)

アカウント連携は任意です。

**表示**では、テーマ、傾き、時計、アイコン、アニメーションを変更できます。HUD は英語、簡体字中国語、繁体字中国語、日本語、韓国語に対応しています。

メモや設定はこの Mac に保存されます。連携したコミュニティアカウントの認証情報は macOS のキーチェーンに保存します。更新は GitHub から届きます。メニューバーか **このアプリについて**で更新の確認や設定ができます。

<img src="docs/media/readme/en-04-credits.png" alt="クレジット" width="326">

制作：[DDDuoDuo](https://github.com/DDDuoDuo)。『アークナイツ：エンドフィールド』、[QinAnze](https://github.com/QinAnze/zmd-charge)、[llynxxx](https://www.bilibili.com/video/BV1DBaP6yEHs/) から着想を得ています。

非公式のファンプロジェクトです。ゲームの画像やブランドの権利は HYPERGRYPH および各権利者に帰属します。オリジナルのコードは [MIT ライセンス](LICENSE)で公開しています。第三者の素材やライブラリには、それぞれの利用条件が適用されます。[クレジットと出典の一覧](CREDITS.md)

[不具合を報告](https://github.com/DDDuoDuo/EndfieldHUD/issues)
