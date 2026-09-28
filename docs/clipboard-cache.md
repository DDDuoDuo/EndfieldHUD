# Clipboard Cache / 剪贴板缓存

Open the HUD with the configured summon shortcut (**Ctrl + backtick** by default), then select **Clipboard Cache**. Copying in another application adds an entry while EndfieldHUD is running, even when its HUD is closed.

- Plain text and URLs appear as one-line previews, never full contents. Images use cached thumbnails. Copied Finder items show filenames and retain references to the originals.
- Click a row to put that item back on the system clipboard, then paste normally in the destination app. The HUD stays open; copying back does not add another history entry.
- Use the pin and delete controls on each row. **Clear unpinned** preserves pinned entries. Deleting a clipboard entry never removes its source file or clears the current system clipboard.
- Scroll or use the page arrows to browse. Up/Down selects a row, Return copies it, and Delete removes it. Keys 1–6 copy a row on the current page.

The default capacity is **10 total entries**, including pins. New captures evict the oldest unpinned entries first. If every slot is pinned, the cache explains that an item must be unpinned or removed. Duplicate captures reuse the existing item and pin state. `ClipboardStore.setCapacity(_:)` supports a future capacity setting; there is no capacity control in Settings yet.

History, including pins, lasts for the current app session and is not saved to disk. Closing the HUD or changing modules preserves it; quitting the app clears it. Files are not duplicated. File references can become unavailable if their originals are moved or deleted.

## Watcher and image handling

A single one-second timer with a 250 ms tolerance checks [`NSPasteboard.changeCount`](https://developer.apple.com/documentation/appkit/nspasteboard/changecount). Unchanged ticks never request pasteboard items, types, strings, images, or files. The timer stops for system sleep, display sleep, or an inactive user session, and performs one catch-up check on resume. No per-window watcher or frame callback is used.

The first launch records a baseline without importing the previous clipboard. Copy-back acknowledges its own change count. Like any interval-based clipboard history, several copies between two checks can overwrite one another before capture; the cache cannot recover those intermediate contents.

Each captured image retains its encoded representation for copy-back and creates one thumbnail of at most 96 pixels per side. Row redraws reuse that thumbnail. Bounds prevent unbounded payload retention: 1 MiB text, 64 MiB encoded image, 100 megapixels per image, 512 file references per entry, and 128 MiB total encoded payload. Large payloads may reduce the number retained below 10. Pinned entries remain protected; an entry is skipped if it cannot fit. Concealed, transient and automatically generated pasteboard items are skipped when their source marks them as such.

The store, watcher, canvas and AppKit interaction bridge are separate. Only the center content joins the existing section transition; the outer shell remains attached. Inactive clipboard content does not redraw for background captures.

## 中文

按召唤快捷键（默认 **Ctrl + 反引号**）打开浮层，再选择**剪贴板缓存**。应用运行期间，即使浮层关闭，也会记录新的复制内容。

- 支持纯文本、网址、图片和 Finder 文件。仅显示单行预览、缩略图或文件名；点击项目即可复制回系统剪贴板，再到目标应用中粘贴。
- 每行可固定或删除；**清空未固定**保留固定项目。删除缓存不会删除原文件，也不会清空当前系统剪贴板。
- 默认最多 **10 个项目（包含固定项目）**。新内容优先替换最早的未固定项目；全部固定时会提示先取消固定或移除项目。
- 滚动或点击箭头翻页；上下键选择，回车复制，Delete 删除，数字 1–6 复制当前页对应项目。
- 历史与固定状态仅保存在本次应用会话的内存中，退出应用后清空；切换模块或关闭浮层不会清空。文件只保留引用，不复制原文件。
- 每秒检查一次变更计数，内容未变时不反序列化；睡眠或会话暂停时停止。图片缩略图只生成一次。极短时间内连续多次复制可能只捕获最后一次。
- 为限制内存，单条文本上限 1 MiB、编码图片上限 64 MiB、文件引用上限 512 个，总编码内容上限 128 MiB；大内容可能使保留数量少于 10。容量已封装为可调整的模型属性，设置界面将在后续加入。
