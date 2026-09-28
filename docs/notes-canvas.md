# Notes canvas

Open **Notes** in the right navigation. Its add controls stay in the center HUD; note cards can move anywhere across the full overlay screen, outside the circular content area.

- **Text:** press Text to add a note immediately. Type directly in the note. Double-click its body to edit again. Escape or Command-Return saves; Return inserts a new line.
- **TODO:** press TODO to add a checklist immediately. Edit a row and press Return to save. Use **+ Add item**, the checkbox, the up/down arrows, or the row's × to manage the checklist. Scroll over a long checklist to reach more rows.
- **Image:** choose Image to select one or more local images. Images can also be dropped anywhere in the Notes workspace or pasted with Command-V when no text editor is active. To drag from Finder into the full-screen HUD, first visit Notes and close the HUD, pick up an image, then summon the HUD with the configured shortcut (Ctrl + backtick by default) while holding the image.
- Drag a note's header to move it. Text and image bodies can also be dragged. Drag the lower-right corner to resize. Selecting a note brings it forward.
- Pinning keeps a note visible when switching to another HUD section; pinned notes remain movable, resizable and editable. Unpinned notes return when reopening Notes. The header’s × or Delete/Backspace opens a small **× / ✓** confirmation below the selected note: × cancels, ✓ deletes. Closing the HUD hides every note.

Cards retain screen coordinates and share the side buttons’ tilt, mouse travel and response timing without circular clipping. Pinned notes keep the same motion on other sections. Dragging, resizing and native text editing hold the current pose steady; pointer motion resumes afterwards. Adding, changing and removing notes use short depth/reveal animations; hiding the HUD cancels their finite animation tracks. Reduce Motion disables perspective motion. The native text input supports selection, undo, copy/paste and Chinese input methods without opening another editor window. The overlay remembers the last visited section until the app quits.

## Storage

Notes are local to this Mac, in `~/Library/Application Support/EndfieldCharge/Notes/notes.sqlite3`. Each record stores its UUID, type, text/checklist content, image reference, x/y, width/height, z-order, creation time and pinned state. Geometry uses logical overlay-screen coordinates. Existing records remain in the same database. A smaller screen keeps cards accessible through display-only clamping without rewriting their saved position or size.

Changes commit after text editing, a checklist action, or the end of a drag/resize. Closing the overlay, changing sections and quitting finish active edits. SQLite transactions keep failed writes from replacing committed records; failures are shown inside Notes. An unreadable database is preserved instead of replaced.

Imported images are copied into the adjacent `Images` directory as PNG files with a maximum dimension of 1,600 pixels. This canvas is for image notes, not original-resolution archival; keep originals separately when needed. Deleting a source file does not break an imported note. Removing an image note removes its managed copy only after the database commit and only if no note still references it.

There is no network service, polling, or database timer. Diagnostic UI/smoke sessions use a separate temporary directory and do not change saved user notes.

## 中文

在右侧选择**便签**。点击**文字**或**待办**立即添加对应便签；点击**图片**选择图片，也支持拖入或粘贴图片。添加按钮保留在中央，便签可在整个浮层屏幕内移动，不受中央圆形区域限制。

双击文字便签编辑，按 Esc 或 Command-Return 保存。待办事项按 Return 保存；通过复选框、上下箭头、× 和“添加事项”管理清单。长清单可滚动。拖动标题栏移动，拖动右下角调整大小。固定的便签在切换其他模块时仍显示，也能继续移动、缩放和编辑；关闭浮层时一并隐藏。点击标题栏 × 或按 Delete 后，便签下方出现 **× / ✓**：× 取消，✓ 确认删除。编辑时仍可使用中文输入法。添加、修改和删除带短暂过渡，关闭浮层会清理动画。

便签与两侧按钮采用相同的倾斜、鼠标移动幅度和响应速度，固定后在其他模块中也保留相同效果。拖动、缩放和文字编辑时暂时保持当前姿态，完成后继续响应鼠标；开启系统“减少动态效果”时停用透视运动。

便签在原有本机 SQLite 数据库中保存，位置与大小采用整个浮层的逻辑坐标。较小屏幕上的临时位置限制不会覆写已保存的几何数据。关闭浮层和切换模块都会完成当前编辑。导入图片保存独立副本，因此原图移动或删除后仍可显示。
