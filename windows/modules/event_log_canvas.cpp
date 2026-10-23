#include "modules/event_log_canvas.hpp"
#include "modules/charge_indicator.hpp"
#include "modules/display_center_logo.hpp"
#include "modules/work_mode_canvas.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace endfield::modules {
namespace d = design;
namespace {
std::optional<std::string_view> value(const EventMetadata& m, std::string_view key) {
    const auto it = m.find(key);
    if (it == m.end()) return std::nullopt;
    return std::string_view(it->second);
}
// Int(String): optional sign and decimal digits only (Swift's Int.init?(_:)).
std::optional<std::int64_t> integer(std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::size_t at = text[0] == '+' || text[0] == '-' ? 1 : 0;
    if (at == text.size()) return std::nullopt;
    std::int64_t out{};
    const auto parsed = std::from_chars(text.data() + (text[0] == '+' ? 1 : 0), text.data() + text.size(), out);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    for (; at < text.size(); ++at) if (text[at] < '0' || text[at] > '9') return std::nullopt;
    return out;
}
// NowPlayingSource.title (Sources/NowPlayingController.swift:15-22): fixed
// names, the same in every language.
std::optional<std::string> playbackSourceTitle(std::string_view key) {
    if (key == "music") return "Music";
    if (key == "spotify") return "Spotify";
    if (key == "netease") return "网易云音乐";
    if (key == "qqMusic") return "QQ 音乐";
    if (key == "kugou") return "酷狗音乐";
    if (key == "system") return "Now Playing";
    return std::nullopt;
}
// HUDClockStyle.allCases order (Sources/HUDClockStyle.swift:5).
constexpr std::array<std::string_view, 5> clockStyles{"digital", "split", "dial", "rail", "stacked"};
}

// --- EventLogFilter (:7-34) ---------------------------------------------------------
EventLogFilter eventLogFilterOf(EventKind kind) noexcept {
    switch (kind) {
    case EventKind::workStarted: case EventKind::workPaused: case EventKind::workResumed: case EventKind::workReset: case EventKind::workCompleted:
        return EventLogFilter::timer;
    case EventKind::shelfAdded: case EventKind::shelfRemoved: case EventKind::shelfCleared: return EventLogFilter::shelf;
    case EventKind::powerConnected: case EventKind::powerDisconnected: case EventKind::batteryStateChanged: case EventKind::audioDeviceConnected:
    case EventKind::audioDeviceDisconnected: case EventKind::displayConnected: case EventKind::displayDisconnected:
        return EventLogFilter::devices;
    default: return EventLogFilter::other;
    }
}
bool eventLogFilterIncludes(EventLogFilter filter, EventKind kind) noexcept { return filter == EventLogFilter::all || eventLogFilterOf(kind) == filter; }
std::string eventLogFilterTitle(EventLogFilter filter, core::Language language) {
    switch (filter) {
    case EventLogFilter::all: return core::localized("All", "全部", language);
    case EventLogFilter::timer: return core::localized("Timer", "计时", language);
    case EventLogFilter::shelf: return core::localized("Shelf", "暂存架", language);
    case EventLogFilter::devices: return core::localized("Devices", "设备", language);
    case EventLogFilter::other: return core::localized("Other", "其他", language);
    }
    return {};
}
d::Chip::Variant eventLogFilterChip(EventLogFilter filter) noexcept {
    return filter == EventLogFilter::timer ? d::Chip::Variant::yellow : filter == EventLogFilter::devices ? d::Chip::Variant::cyan : d::Chip::Variant::dark;
}

// --- EventLogText.line (:38-211) --------------------------------------------------------
std::string eventLogLine(const SystemEvent& event, core::Language language) {
    const auto& m = event.metadata;
    const auto t = [&](std::string_view en, std::string_view zh) { return core::localized(en, zh, language); };
    const auto with = [&](std::string_view en, std::string_view zh, std::string_view argument) {
        const std::array<std::string_view, 1> args{argument};
        return core::localized(en, zh, language, args);
    };
    const auto joined = [&](std::string phrase, const std::optional<std::string>& extra) {
        if (!extra || extra->empty()) return phrase;
        const std::array<std::string_view, 2> args{phrase, *extra};
        return core::localized("{0}, {1}", "{0}，{1}", language, args);
    };
    const bool stopwatch = value(m, "kind") == "stopwatch";
    std::optional<std::string> duration;
    if (!stopwatch) if (const auto seconds = value(m, "seconds")) if (const auto n = integer(*seconds)) duration = WorkModeCanvas::durationTitle(double(*n), language);
    std::optional<std::string> percentage;
    if (const auto p = value(m, "percentage")) percentage = std::string(*p) + "%";
    const auto action = value(m, "action");
    switch (event.kind) {
    case EventKind::overlayOpened: return t("HUD opened", "界面已打开");
    case EventKind::moduleOpened: return t("Module opened", "模块已打开");
    case EventKind::appShortcutOpened:
        if (const auto app = value(m, "app")) return with("Opened {0}", "已打开 {0}", *app);
        return t("App shortcut opened", "已打开应用快捷方式");
    case EventKind::clipboardCopied: {
        const auto kind = value(m, "kind");
        if (kind == "text") return t("Copied text", "已复制文本");
        if (kind == "url") return t("Copied a link", "已复制链接");
        if (kind == "image") return t("Copied an image", "已复制图像");
        if (kind == "files") return t("Copied files", "已复制文件");
        return t("Copied from the clipboard", "已从剪贴板复制");
    }
    case EventKind::shelfAdded:
        if (const auto name = value(m, "filename")) return with("Added {0}", "已添加 {0}", *name);
        return t("Added to the shelf", "已添加到暂存架");
    case EventKind::shelfRemoved:
        if (const auto name = value(m, "filename")) return with("Removed {0}", "已移除 {0}", *name);
        return t("Removed from the shelf", "已从暂存架移除");
    case EventKind::shelfCleared:
        if (const auto count = value(m, "count")) return with("Shelf cleared, {0} items", "暂存架已清空，共 {0} 项", *count);
        return t("Shelf cleared", "暂存架已清空");
    case EventKind::workStarted:
        return stopwatch ? t("Stopwatch started", "秒表开始") : joined(t("Countdown started", "倒计时开始"), duration);
    case EventKind::workPaused:
        return stopwatch ? t("Stopwatch paused", "秒表暂停") : joined(t("Countdown paused", "倒计时暂停"), duration);
    case EventKind::workResumed:
        return stopwatch ? t("Stopwatch resumed", "秒表继续") : joined(t("Countdown resumed", "倒计时继续"), duration);
    case EventKind::workReset:
        return stopwatch ? t("Stopwatch reset", "秒表重置") : joined(t("Countdown reset", "倒计时重置"), duration);
    case EventKind::workCompleted: return joined(t("Countdown finished", "倒计时结束"), duration);
    case EventKind::powerConnected: return joined(t("Power connected", "电源已连接"), percentage);
    case EventKind::powerDisconnected: return joined(t("Power disconnected", "电源已断开"), percentage);
    case EventKind::batteryStateChanged: {
        const auto state = value(m, "state");
        std::string phrase;
        if (state == "charging") phrase = t("Charging started", "开始充电");
        else if (state == "full") phrase = t("Battery full", "电池已充满");
        else if (state == "connected") phrase = t("Plugged in, not charging", "已接通电源，未在充电");
        else if (state == "battery") phrase = t("On battery power", "正在使用电池");
        else if (state == "noBattery" || state == "unavailable") phrase = t("Battery unavailable", "电池不可用");
        else phrase = t("Battery state changed", "电池状态已变化");
        return joined(std::move(phrase), percentage);
    }
    case EventKind::audioDeviceConnected: case EventKind::displayConnected:
        if (const auto device = value(m, "device")) return with("{0} connected", "{0} 已连接", *device);
        return event.kind == EventKind::audioDeviceConnected ? t("Audio device connected", "音频设备已连接") : t("Display connected", "显示器已连接");
    case EventKind::audioDeviceDisconnected: case EventKind::displayDisconnected:
        if (const auto device = value(m, "device")) return with("{0} disconnected", "{0} 已断开", *device);
        return event.kind == EventKind::audioDeviceDisconnected ? t("Audio device disconnected", "音频设备已断开") : t("Display disconnected", "显示器已断开");
    case EventKind::displaySettingsChanged: {
        const auto field = value(m, "field"), setting = value(m, "value");
        if (field == "clockStyle" && setting) {
            const auto it = std::find(clockStyles.begin(), clockStyles.end(), *setting);
            const std::string number = d::format::pad2(it == clockStyles.end() ? 1 : (it - clockStyles.begin()) + 1);
            return with("Clock style {0}", "时钟样式 {0}", number);
        }
        if (field == "centerLogo" && setting == "customImported") return t("Center logo imported", "已导入中心标志");
        if (field == "centerLogo" && setting) {
            const auto choice = centerLogoChoice(*setting);
            return with("Center logo: {0}", "中心标志：{0}", choice ? centerLogoTitle(*choice, language) : std::string(*setting));
        }
        if (field == "alertMetric" && setting) {
            const auto metric = chargeMetricFromKey(*setting);
            return with("Battery alert reading: {0}", "电池提醒数据：{0}", metric ? chargeMetricTitle(*metric, language) : std::string(*setting));
        }
        return t("Display setting changed", "显示设置已更改");
    }
    case EventKind::profileCropChanged: {
        const auto target = value(m, "target");
        if (target == "background") return t("Profile background cropped", "已裁剪名片背景");
        if (target == "thumbnail") return t("Profile picture cropped", "已裁剪头像");
        if (target == "both") return t("Profile background and picture cropped", "已裁剪名片背景和头像");
        return t("Profile crop changed", "名片裁剪已更改");
    }
    case EventKind::mapPinStyleChanged: return t("Map pin style changed", "地图标记样式已更改");
    case EventKind::mapRecentered: return t("Map recentered", "地图已居中");
    case EventKind::noteAction:
        if (action == "createdText") return t("Text note added", "已添加文字便笺");
        if (action == "createdTODO") return t("Checklist added", "已添加待办清单");
        if (action == "createdMedia") return t("Media note added", "已添加媒体便笺");
        if (action == "createdDrawing") return t("Drawing added", "已添加画画便笺");
        if (action == "editedText") return t("Note edited", "便笺已编辑");
        if (action == "formattedText") return t("Note formatted", "便笺格式已更改");
        if (action == "editedTODO") return t("Checklist edited", "待办清单已编辑");
        if (action == "drawingEdited") return t("Drawing edited", "绘图已编辑");
        if (action == "deletedNote") return t("Note deleted", "便笺已删除");
        if (action == "mediaPlayback") return t("Note media played", "已播放便笺媒体");
        return t("Note changed", "便笺已更改");
    case EventKind::playbackAction: {
        std::string phrase;
        if (action == "playPause") phrase = t("Play / pause", "播放 / 暂停");
        else if (action == "previous") phrase = t("Previous track", "上一首");
        else if (action == "next") phrase = t("Next track", "下一首");
        else if (action == "seek") phrase = t("Playback position changed", "播放进度已调整");
        else phrase = t("Playback changed", "播放已更改");
        const auto source = value(m, "source");
        return joined(std::move(phrase), source ? playbackSourceTitle(*source) : std::nullopt);
    }
    case EventKind::projectionAction:
        if (action == "drawingEdited") return t("Projection drawing edited", "投影绘图已编辑");
        if (action == "erased") return t("Projection drawing erased", "投影绘图已擦除");
        if (action == "brushChanged") return t("Projection brush changed", "投影画笔已更改");
        if (action == "backgroundChanged") return t("Projection background changed", "投影背景已更改");
        if (action == "mediaAdded") return t("Media added to the projection", "已向投影添加媒体");
        if (action == "mediaRemoved") return t("Media removed from the projection", "已从投影移除媒体");
        if (action == "cleared") return t("Projection cleared", "投影已清空");
        return t("Projection changed", "投影已更改");
    case EventKind::archiveAction:
        if (action == "created") return t("Archive document created", "已创建档案文档");
        if (action == "edited") return t("Archive document edited", "档案文档已编辑");
        if (action == "deleted") return t("Archive document deleted", "档案文档已删除");
        if (action == "mediaAdded") return t("Media added to a document", "已向文档添加媒体");
        if (action == "mediaRemoved") return t("Media removed from a document", "已从文档移除媒体");
        if (action == "categoryCreated") return t("Archive category created", "已创建档案分类");
        if (action == "categoryChanged") return t("Archive category changed", "档案分类已更改");
        if (action == "categoryDeleted") return t("Archive category deleted", "档案分类已删除");
        return t("Archive changed", "档案已更改");
    case EventKind::readerAction:
        // "progress" is no longer recorded (MODS-747) but old records still read.
        if (action == "imported") return t("Book added to the reader", "已向阅读器添加书籍");
        if (action == "deleted") return t("Book removed from the reader", "已从阅读器移除书籍");
        if (action == "bookmarked") return t("Bookmark changed", "书签已更改");
        if (action == "progress") return t("Reading progress saved", "阅读进度已保存");
        if (action == "settings") return t("Reading settings changed", "阅读设置已更改");
        return t("Reader changed", "阅读器已更改");
    case EventKind::mediaAssemblyAction:
        if (action == "imported") return t("Media imported", "已导入媒体");
        if (action == "edited") return t("Media edited", "媒体已编辑");
        if (action == "exported") return t("Media exported", "媒体已导出");
        return t("Media changed", "媒体已更改");
    case EventKind::calendarAction:
        if (action == "created") return t("Calendar event added", "已添加日历事项");
        if (action == "edited") return t("Calendar event edited", "日历事项已编辑");
        if (action == "deleted") return t("Calendar event deleted", "日历事项已删除");
        return t("Calendar changed", "日历已更改");
    case EventKind::minigameAction:
        if (action == "started") return t("Minigame started", "小游戏已开始");
        if (action == "restarted") return t("Minigame restarted", "小游戏已重新开始");
        if (action == "finished") return t("Minigame finished", "小游戏已结束");
        return t("Minigame played", "已进行小游戏");
    case EventKind::accountAction:
        if (action == "linked") return t("Account linked", "账户已绑定");
        if (action == "unlinked") return t("Account unlinked", "账户已解绑");
        if (action == "synced") return t("Profile refreshed", "名片已刷新");
        if (action == "settings") return t("Account settings changed", "账户设置已更改");
        return t("Account changed", "账户已更改");
    }
    return {};
}

EventLogTime eventLogTime(double createdAt, double now, const EventLogLocalTime& local) {
    const auto date = local ? local(createdAt) : std::nullopt;
    if (!date) return {"--", std::nullopt};
    EventLogTime out{d::format::pad2(date->hour) + ":" + d::format::pad2(date->minute), std::nullopt};
    const auto today = local(now);
    if (today && today->year == date->year && today->month == date->month && today->day == date->day) return out;
    out.day = d::format::pad2(date->month) + "." + d::format::pad2(date->day);
    return out;
}

// --- The canvas (:217-431) --------------------------------------------------------------
EventLogCanvas::EventLogCanvas(d::Context& context, EventLogCallbacks store, EventLogLocalTime localTime, std::function<double()> now)
    : Canvas(core::Module::eventLog, context), store_(std::move(store)), localTime_(std::move(localTime)), now_(std::move(now)) {}

std::vector<SystemEvent> EventLogCanvas::events() const {
    std::vector<SystemEvent> out;
    if (!store_.snapshot) return out;
    for (auto& event : store_.snapshot().events)
        if (event.kind != EventKind::overlayOpened && event.kind != EventKind::moduleOpened) out.push_back(std::move(event));
    return out;
}
std::optional<std::string> EventLogCanvas::status() const { return store_.snapshot ? store_.snapshot().status : std::nullopt; }
double EventLogCanvas::listOffsetForTesting() const noexcept { return list_ ? list_->offset() : 0; }

void EventLogCanvas::show(double now) {
    Canvas::show(now);
    refresh();
}
void EventLogCanvas::didActivate() {
    lastStatus_ = status();
    observing_ = true;
    refresh();
}
void EventLogCanvas::didDeactivate() { observing_ = false; }
// The store's failure appearing while the page is open shows once as a toast
// (owner decision: copy the Mac; one already present at activation is the
// baseline and stays silent, MODS-716).
void EventLogCanvas::storeChanged() {
    if (!observing_) return;
    const auto current = status();
    if (current && current != lastStatus_ && isActive()) toast(d::Icon::warn, "Event log", "事件日志", *current);
    lastStatus_ = current;
    refresh();
}
// New records re-render in place; the row at the top stays in view (:270-273).
void EventLogCanvas::refresh() {
    if (!renderedLanguage() || transitioning()) return;
    if (renderedEvents_ && events() == *renderedEvents_) return;
    rerender();
}

void EventLogCanvas::buildPage(d::Page& page) {
    const auto all = events();
    const auto anchor = topVisibleAnchor();
    renderedEvents_ = all;
    shown_.clear();
    for (const auto& event : all) if (eventLogFilterIncludes(filter_, event.kind)) shown_.push_back(event);
    rows_.clear(); parked_.clear(); list_ = nullptr; listPage_ = nullptr;
    auto& clear = page.make<d::Button>("eventLog.clear", text("Clear", "清空"), d::Icon::trash, d::Button::Kind::secondary, d::Button::Height::compact);
    clear.place({0, 0});
    clear.setEnabled(!all.empty());
    clear.onActivate = [this] { confirmClear(); };
    header().configure({"EVENT LOG"}, "MOD-14", title("Event Log", "事件日志"), d::format::pad2(std::int64_t(all.size())), false, {&clear});
    std::vector<d::Tabs::Title> titles;
    for (std::size_t n = 0; n < eventLogFilterCount; ++n) titles.push_back({eventLogFilterTitle(EventLogFilter(n), env().language), std::nullopt});
    auto& tabs = page.makeTabs("eventLog.filter", std::move(titles), int(filter_), d::metric::boxWidth);
    tabs.onSelect = [this](int index, double direction) {
        select(index >= 0 && index < int(eventLogFilterCount) ? EventLogFilter(index) : EventLogFilter::all, direction);
    };
    tabs.layer().setOrigin({0, d::metric::headerHeight});
    page.layer().add(tabs.layer());
    page.add(tabs.controls()); page.tabs = &tabs; page.stagger(tabs.layer());
    if (shown_.empty()) {
        auto& empty = d::emptyState::bilingual(page.builder(), "eventLog.empty", "NO EVENTS", "日志为空");
        empty.setOrigin({0, listTop});
        page.layer().add(empty);
        page.stagger(empty);
        listOffset_ = 0;
        return;
    }
    const double contentHeight = double(shown_.size()) * pitch - 6;
    // Not one of the page's scroll areas: the canvas scrolls it itself (handleScroll).
    auto& area = page.own<d::ScrollArea>("eventLog.list", d::Rect{0, listTop, d::metric::boxWidth, std::min(listHeight, contentHeight)});
    page.layer().add(area.layer());
    area.setContentHeight(contentHeight);
    rowWidth_ = area.contentWidth();
    double offset = listOffset_;
    if (anchor) {
        const auto it = std::find_if(shown_.begin(), shown_.end(), [&](const SystemEvent& e) { return e.id == anchor->first; });
        if (it != shown_.end()) offset = double(it - shown_.begin()) * pitch + anchor->second;
    }
    area.setOffset(offset);
    listOffset_ = area.offset();
    list_ = &area; listPage_ = &page;
    updateRows(page, true);
}

std::optional<std::pair<std::string, double>> EventLogCanvas::topVisibleAnchor() const {
    if (!list_ || list_->offset() <= 0) return std::nullopt;
    const auto index = std::size_t(list_->offset() / pitch);
    if (index >= shown_.size()) return std::nullopt;
    return std::pair{shown_[index].id, list_->offset() - double(index) * pitch};
}

// Builds the rows in view (plus one either side) and drops the rest (:322-339).
void EventLogCanvas::updateRows(d::Page& page, bool initial) {
    if (!list_) return;
    const long long first = std::max(0LL, (long long)std::floor(list_->offset() / pitch) - 1);
    const long long last = std::min((long long)shown_.size() - 1, (long long)std::ceil((list_->offset() + list_->layer().bounds().height) / pitch));
    const auto wanted = [&](std::size_t index) { return (long long)index >= first && (long long)index <= last; };
    for (auto it = rows_.begin(); it != rows_.end();) {
        if (wanted(it->first)) { ++it; continue; }
        d::RowPlate& row = *it->second;
        row.clearInteractionState(); page.remove(row);
        row.layer().removeFromParent();
        parked_.emplace(it->first, &row);
        it = rows_.erase(it);
    }
    for (long long index = first; index <= last; ++index) {
        const auto n = std::size_t(index);
        if (rows_.contains(n)) continue;
        d::RowPlate* row{};
        if (const auto kept = parked_.find(n); kept != parked_.end()) { row = kept->second; parked_.erase(kept); }
        else row = &makeRow(page, shown_[n]);
        row->layer().setOrigin({0, double(n) * pitch});
        row->layer().setPartRoot(true);
        list_->content().add(row->layer());
        if (initial) page.stagger(row->layer());
        page.add(*row);
        rows_.emplace(n, row);
    }
    if (!initial) refreshRouter();
}

d::RowPlate& EventLogCanvas::makeRow(d::Page& page, const SystemEvent& event) {
    const auto language = env().language;
    const auto category = eventLogFilterOf(event.kind);
    const std::string sentence = eventLogLine(event, language);
    const std::string id = "eventLog.row:" + event.id;
    auto& row = page.make<d::RowPlate>(id, "", std::nullopt, d::RowPlate::Variant::standard, "›", rowWidth_, rowHeight);
    const auto time = eventLogTime(event.createdAt, now_ ? now_() : 0, localTime_);
    const std::string categoryTitle = eventLogFilterTitle(category, language);
    row.label = time.clock + (time.day ? ", " + *time.day : std::string()) + ", " + categoryTitle;
    row.readout = sentence;
    auto& builder = page.builder();
    auto& chip = page.own<d::Chip>(id + ".chip", categoryTitle, eventLogFilterChip(category));
    // An earlier day adds its date under the time.
    if (time.day) {
        auto& date = d::makeText(builder, id + ".date", *time.day,
            d::TextStyle{.size = 9.5, .weight = d::FontWeight::regular, .color = d::fixed::steel2, .tabular = true}, {18, 23, 52, 9.5});
        row.layer().add(date);
    }
    auto& clock = d::makeText(builder, id + ".clock", time.clock,
        d::TextStyle{.size = 11, .weight = d::FontWeight::bold, .color = d::rgb(0x444444), .tabular = true}, {18, time.day ? 9.0 : 15.5, 52, 11});
    chip.layer().setOrigin({80, 12});
    auto& line = d::makeText(builder, id + ".line", sentence,
        d::TextStyle{.size = 13, .weight = d::FontWeight::regular, .color = d::fixed::nearBlack, .lineHeight = 1.3}, {166, 21 - 8.45, rowWidth_ - 166 - 12, 16.9});
    row.layer().add(clock); row.layer().add(chip.layer()); row.layer().add(line);
    return row;
}

// --- Actions (:376-424) ------------------------------------------------------------------
void EventLogCanvas::select(EventLogFilter next, double direction) {
    if (next == filter_) return;
    filter_ = next; listOffset_ = 0; list_ = nullptr; listPage_ = nullptr;
    navigate(direction);
}
void EventLogCanvas::confirmClear() {
    const auto count = events().size();
    if (count == 0) return;
    const std::string number = std::to_string(count);
    const std::array<std::string_view, 1> args{number};
    d::ConfirmRequest request;
    request.id = "eventLog.clear"; request.title = text("Clear the event log?", "清空事件日志？");
    request.message = text("{0} entries", "{0} 条记录", args); request.confirmTitle = text("Clear", "清空");
    request.danger = true; request.icon = d::Icon::trash;
    confirm(std::move(request), [this] {
        listOffset_ = 0; list_ = nullptr; listPage_ = nullptr;
        if (store_.clear) store_.clear();
        refresh();
        toast(d::Icon::trash, "Log cleared", "日志已清空");
    });
}
// The wheel over the list scrolls it and brings the next rows in. While a
// filter swap runs the Mac has no list (`select` dropped it); the page built
// for the swap here is not scrolled before it lands.
bool EventLogCanvas::handleScroll(d::Point point, d::Point delta) {
    if (!list_ || transitioning()) return false;
    const auto local = d::convert(point, root(), list_->layer());
    const auto b = list_->layer().bounds();
    if (!local || local->x < 0 || local->y < 0 || local->x >= b.width || local->y >= b.height) return false;
    scrollList(delta.y);
    return true;
}
void EventLogCanvas::scrollList(double delta) {
    if (!list_ || !listPage_ || std::isnan(delta)) return;
    list_->setOffset(std::isinf(delta) ? (delta > 0 ? list_->maximumOffset() : 0) : list_->offset() + delta);
    listOffset_ = list_->offset();
    updateRows(*listPage_, false);
}
// Down / Up one row, Page Down / Page Up one view, End / Home.
bool EventLogCanvas::handleKey(const d::KeyInput& key) {
    if (!list_ || transitioning() || key.commandModifiers() || key.shift) return false;
    constexpr double view = listHeight - 30;
    constexpr double infinity = std::numeric_limits<double>::infinity();
    switch (key.virtualKey) {
    case d::key::down: scrollList(pitch); break;
    case d::key::up: scrollList(-pitch); break;
    case d::key::pageDown: scrollList(view); break;
    case d::key::pageUp: scrollList(-view); break;
    case d::key::end: scrollList(infinity); break;
    case d::key::home: scrollList(-infinity); break;
    default: return false;
    }
    return true;
}
} // namespace endfield::modules
