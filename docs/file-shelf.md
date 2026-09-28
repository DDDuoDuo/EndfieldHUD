# Temporary File Shelf / 文件暂存架

Open **Temporary File Shelf** in the right navigation. The shelf shares the HUD’s perspective, shell and section transitions. It accepts normal local Finder files and folders, including images, PDFs, videos, archives and app bundles.

## Controls

- **Add:** select one or more files or folders in the native file chooser.
- **Drag in:** pick up Finder items, then press the configured summon shortcut (**Ctrl + backtick** by default) while holding them. Drop onto the shelf center when that section is open, or onto the **Temporary File Shelf** navigation tile from any section. Dropping on the tile adds the references and switches to the shelf. Finder file references can also be pasted with **Command-V** while the shelf is open.
- **Drag out:** drag a card’s body. **Shift-click** extends the selection across a range of cards; drag a selected card to export the group in one native drag session. The HUD plays its closing animation to reveal Finder or another application. A completed drop leaves it closed; cancelling plays the opening animation to restore it. The original item and shelf entry remain in place. Finder copies only when you actually drop into a destination; other applications receive a native file URL.
- **Quick Look:** double-click a card, select it and press **Space**, or use its preview control.
- **Reveal:** use a card’s Finder control or **Command-R**.
- **Remove:** use the card’s × control or select it and press **Delete**. **Clear** asks for confirmation inside the shelf and removes all references.
- Six cards fit on each page. Use the page arrows or scroll over the collection for additional items.

Cards show the system file icon, filename, type and logical file size. Folder sizes are shown as unavailable because the shelf does not recursively scan their contents. Missing/offline items retain their last metadata and a visible unavailable state; preview and dragging become available again after the source is accessible and the shelf is reopened.

## Storage

`~/Library/Application Support/EndfieldCharge/FileShelf/shelf.json` contains versioned bookmarks and small metadata records. Adding an item never copies its contents. The shelf does not move or delete originals, including when removing a card or clearing everything. There is no automatic expiry.

Security-scoped read-only bookmarks are used when supported, with balanced access for preview and dragging. The unsandboxed local build can fall back to ordinary persistent bookmarks. Bookmark resolution follows moved/renamed files where macOS can resolve them and does not mount offline volumes or show permission prompts. An unrelated file replacing a missing original is not silently substituted. Cross-volume moves and deleted originals may require adding the new location again.

Updates use atomic metadata writes. Corrupt data and unknown newer versions are preserved, and errors appear in the shelf. Metadata refresh happens when entering the shelf, with no background polling. Diagnostic UI/smoke sessions use a separate temporary shelf.

## 中文操作

右侧选择 **文件暂存架**，通过“添加”选择文件或文件夹，也可从 Finder 拖入或在暂存架中使用 **Command-V** 粘贴文件引用。拖动文件时按召唤快捷键（默认 **Ctrl + 反引号**）唤出浮层：暂存架已打开时可放到中央，其他模块中可直接放到右侧**文件暂存架**按钮，加入引用后自动切换到该模块。

按住 **Shift** 点击可选择一段连续卡片，再拖动选中卡片即可同时拖出多个项目。拖出时浮层播放关闭动画，让 Finder 或其他应用接收；取消拖动时播放打开动画恢复。双击或按空格快速查看，**Command-R** 在 Finder 中显示，**Delete** 移除选中的引用。“清空”只清除暂存架，不删除原文件。暂存架保存书签和元数据，不复制文件内容；文件移动或暂时离线后会在下次进入时重新检查。
