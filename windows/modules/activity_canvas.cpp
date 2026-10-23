// Sources/ActivityMonitorCanvas.swift, statement by statement (line numbers cite it).
#include "modules/activity_canvas.hpp"
#include "modules/telemetry_format.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
namespace d = design;
namespace {
std::string join(const std::vector<std::string>& parts, std::string_view separator) {
    std::string out;
    for (const auto& part : parts) { if (!out.empty()) out += separator; out += part; }
    return out;
}
}

// ---------------------------------------------------------------------------
// ActivityGraph.paths (:240-260)
// ---------------------------------------------------------------------------
ActivityGraphPaths activityGraphPaths(std::span<const std::optional<double>> values, d::Size size) {
    const auto window = values.last(std::min(values.size(), activityGraphSamples));
    ActivityGraphPaths out;
    std::vector<d::Point> run;
    const auto flush = [&] {
        if (run.size() > 1) {
            out.line.move(run.front());
            for (std::size_t n = 1; n < run.size(); ++n) out.line.line(run[n]);
            out.area.move({run.front().x, size.height});
            for (const auto& point : run) out.area.line(point);
            out.area.line({run.back().x, size.height});
            out.area.close();
        }
        run.clear();
    };
    const std::size_t offset = activityGraphSamples - window.size();
    for (std::size_t index = 0; index < window.size(); ++index) {
        const auto& value = window[index];
        if (!value || !std::isfinite(*value)) { flush(); continue; }
        const double x = size.width * double(offset + index) / double(activityGraphSamples - 1);
        const double y = size.height - std::min(100., std::max(0., *value)) * .42 * size.height / 44;
        run.push_back({x, y});
    }
    flush();
    return out;
}

// ---------------------------------------------------------------------------
// ActivityAppIcons (:566-620)
// ---------------------------------------------------------------------------
struct ActivityAppIcons::State { ActivityAppIcons* owner{}; };
ActivityAppIcons::ActivityAppIcons(Loader loader) : loader_(std::move(loader)), state_(std::make_shared<State>(State{this})) {}
ActivityAppIcons::~ActivityAppIcons() { state_->owner = nullptr; clear(); }
void ActivityAppIcons::clear() {
    if (loader_.release) for (const auto& [_, image] : images_) if (image) loader_.release(*image);
    images_.clear();
}
// scale didSet: a render-scale change empties the cache and the queue (a
// batch in flight for the old scale is dropped when it returns).
void ActivityAppIcons::setScale(double scale) {
    if (scale == scale_) return;
    scale_ = scale;
    clear(); queue_.clear();
    ++generation_; inFlight_ = false;
}
unsigned ActivityAppIcons::pixels() const noexcept { return unsigned(std::ceil(side * std::max(1., scale_))); }
ActivityAppIcons::Icon ActivityAppIcons::icon(const ActivityApp& item) {
    if (item.identity.iconKey.empty() || !loader_.load) return {Icon::Kind::generic, {}};
    if (const auto found = images_.find(item.identity.id); found != images_.end())
        return found->second ? Icon{Icon::Kind::image, *found->second} : Icon{Icon::Kind::generic, {}};
    if (std::none_of(queue_.begin(), queue_.end(), [&](const Request& r) { return r.id == item.identity.id; }))
        queue_.push_back({item.identity.id, item.identity.iconKey, pixels()});
    return {};
}
void ActivityAppIcons::schedule() {
    if (inFlight_ || queue_.empty() || !loader_.load) return;
    std::vector<Request> next(queue_.begin(), queue_.begin() + std::ptrdiff_t(std::min(batch, queue_.size())));
    queue_.erase(queue_.begin(), queue_.begin() + std::ptrdiff_t(next.size()));
    inFlight_ = true;
    const std::weak_ptr<State> weak = state_;
    const auto generation = generation_;
    Done done = [weak, generation, next](std::vector<std::optional<d::ImageSource>> results) {
        const auto state = weak.lock();
        return state && state->owner && state->owner->loaded(generation, next, std::move(results));
    };
    if (!loader_.load(next, std::move(done))) {
        // Not accepted now (executor backpressure): the next lookup retries.
        queue_.insert(queue_.begin(), next.begin(), next.end());
        inFlight_ = false;
    }
}
bool ActivityAppIcons::loaded(std::uint64_t generation, const std::vector<Request>& requests, std::vector<std::optional<d::ImageSource>> results) {
    if (generation != generation_) return false;          // loaded for another scale: not taken
    results.resize(requests.size());
    inFlight_ = false;
    if (images_.size() + requests.size() > capacity) clear();
    std::vector<std::string> ids;
    for (std::size_t n = 0; n < requests.size(); ++n) {
        auto& slot = images_[requests[n].id];
        if (slot && loader_.release) loader_.release(*slot);
        slot = std::move(results[n]);
        ids.push_back(requests[n].id);
        ++loads_;
    }
    if (onLoad) onLoad(ids);
    schedule();
    return true;
}

// ---------------------------------------------------------------------------
// ActivityRow (:265-381): dark chamfered plate, colour bar, name and caption,
// the live graph and a single- or two-line readout.
// ---------------------------------------------------------------------------
class ActivityCanvas::Row final : public d::Control {
public:
    enum class Metric : int { gpu, cpu, ram, network, disk };
    static constexpr d::Size size{d::metric::boxWidth, 64};
    Row(d::Builder& b, Metric metric, std::string name, std::string caption)
        : Control(b.env, "activity.row." + std::to_string(int(metric)), name, b.layer("activity.row." + std::to_string(int(metric)))), metric_(metric) {
        const auto& p = palette();
        // Read when the row is built: the accent follows the theme colour,
        // and both take their light-surface variants in the light theme.
        const d::Color yellow = p.accentInk(), cyan = p.legible(d::fixed::cyan);
        const d::Color color = metric == Metric::ram || metric == Metric::network ? cyan : yellow;
        const d::Color secondaryColor = metric == Metric::network ? yellow : cyan;
        const bool two = int(metric) >= 3;
        if (two) traces_.push_back({secondaryColor, .25});
        traces_.push_back({color, .35});
        const d::TextStyle top{two ? 14. : 19., d::FontWeight::regular, two ? yellow : color, two ? 1.45 : 1.1, 0, false, true};
        const d::TextStyle bottom = two ? top.colored(cyan) : d::TextStyle{10.5, d::FontWeight::regular, p.token(d::ThemeToken::steel), 1.5};
        const std::string id = this->id();
        const d::Rect bounds{0, 0, size.width, size.height};
        const auto shape = d::chamfer(bounds, d::metric::rowChamfer);
        d::Layer& l = layer();
        l.setAnimatable();                                   // translateX on hover
        l.setBounds(size);
        d::Layer& plate = b.layer(id + ".plate");
        plate.setFrame(bounds);
        const std::array<d::Color, 2> stops{p.tone(0x232323), p.tone(0x1A1A1A)};
        plate.setContent(d::plate(plate.id(), bounds, stops, {}, d::metric::rowChamfer));
        // The inset ring follows the box; the chamfer clips its corner.
        d::Layer& ring = b.layer(id + ".ring");
        ring.setFrame(bounds);
        const double cut = d::metric::rowChamfer;
        d::PathBuilder edge;
        edge.move({bounds.width - cut, .5}).line({.5, .5}).line({.5, bounds.height - .5}).line({bounds.width - .5, bounds.height - .5}).line({bounds.width - .5, cut});
        ring.setContent(d::shape(ring.id(), bounds, edge, std::nullopt, p.token(d::ThemeToken::ringTab)));
        brighten_ = &b.layer(id + ".brighten");
        brighten_->setFrame(bounds); brighten_->setAnimatable();
        brighten_->setContent(d::shape(brighten_->id(), bounds, shape, p.foreground(.025)));
        brighten_->setValue(d::Channel::opacity, 0);
        sheen_ = std::make_unique<d::Sheen>(b, id + ".sheen", .07);
        sheen_->setShape(plate, size);
        d::Layer& bar = b.layer(id + ".bar");
        bar.setFrame({0, 10, 3, bounds.height - 20});
        d::setBackground(bar, color);
        d::Layer& nameText = d::makeText(b, id + ".name", name, d::TextStyle{15, d::FontWeight::medium, p.tone(0xF2F2F2), 1.15}, {18, 15.5, 118, 17.25});
        d::Layer& subtitle = d::makeText(b, id + ".caption", caption,
            d::TextStyle{9.5, d::FontWeight::regular, p.token(d::ThemeToken::steel), 1, .1, true}, {18, 37.75, 118, 9.5});
        // The graph and the readouts change with every sample: their own
        // surfaces, so a sample re-rasterizes only them (the Mac re-paths
        // CAShapeLayers and re-sets two text layers).
        graph_ = &b.layer(id + ".graph");
        graph_->setFrame({148, 10, activityGraphSize.width, activityGraphSize.height});
        graph_->setDecorative(true);                         // HUDDecorativeContentLayer
        graph_->setAnimatable();
        primary_ = &d::makeText(b, id + ".primary", "", top, two ? d::Rect{456, 11.7, 120, 20.3} : d::Rect{456, 13.6, 120, 20.9}, d::TextAlignment::right);
        secondary_ = &d::makeText(b, id + ".secondary", "", bottom, two ? d::Rect{456, 32, 120, 20.3} : d::Rect{456, 34.5, 120, 15.75}, d::TextAlignment::right);
        primary_->setAnimatable(); secondary_->setAnimatable();
        for (d::Layer* child : {&plate, &ring, brighten_, &sheen_->layer(), &bar, &nameText, &subtitle, graph_, primary_, secondary_}) l.add(*child);
        renderGraph({});
        setTarget(bounds, shape);
    }
    Metric metric() const noexcept { return metric_; }
    const std::vector<std::string>& readoutLines() const noexcept { return lines_; }
    // update(history:latest:) (:336-370): paths and readouts from the
    // retained history (oldest first) and the latest sample.
    void update(std::span<const ActivitySnapshot> history, const ActivitySnapshot& value, core::Language language) {
        const auto peak = [](std::initializer_list<const std::vector<std::optional<double>>*> series) {
            double maximum = 0;
            for (const auto* values : series) for (const auto& v : *values) if (v && std::isfinite(*v) && *v >= 0) maximum = std::max(maximum, *v);
            return std::max(1000., maximum * 1.12);
        };
        const auto scaled = [](std::vector<std::optional<double>> values, double ceiling) {
            for (auto& v : values) if (v) *v = *v / ceiling * 100;
            return values;
        };
        const auto series = [&](auto read) { std::vector<std::optional<double>> out; out.reserve(history.size()); for (const auto& s : history) out.push_back(read(s)); return out; };
        std::vector<std::string> lines;
        switch (metric_) {
        case Metric::gpu:
        case Metric::cpu: {
            const bool gpu = metric_ == Metric::gpu;
            renderGraph({series([&](const ActivitySnapshot& s) { return gpu ? s.gpuPercent : s.cpuPercent; })});
            lines = {telemetry::percent(gpu ? value.gpuPercent : value.cpuPercent), "0\xE2\x80\x93" "100%"};
            break;
        }
        case Metric::ram:
            renderGraph({series([](const ActivitySnapshot& s) -> std::optional<double> {
                if (!s.memory || !s.memory->total) return std::nullopt;
                return double(s.memory->used) / double(s.memory->total) * 100;
            })});
            lines = {telemetry::bytes(value.memory ? std::optional(double(value.memory->used)) : std::nullopt),
                     "/ " + telemetry::bytes(value.memory ? std::optional(double(value.memory->total)) : std::nullopt)};
            break;
        case Metric::network: {
            const auto up = series([](const ActivitySnapshot& s) { return s.upload; }), down = series([](const ActivitySnapshot& s) { return s.download; });
            const double ceiling = peak({&up, &down});
            renderGraph({scaled(up, ceiling), scaled(down, ceiling)});
            lines = {"\xE2\x86\x91 " + telemetry::rate(value.upload), "\xE2\x86\x93 " + telemetry::rate(value.download)};
            break;
        }
        case Metric::disk: {
            const auto read = series([](const ActivitySnapshot& s) { return s.diskRead; }), write = series([](const ActivitySnapshot& s) { return s.diskWrite; });
            const double ceiling = peak({&read, &write});
            renderGraph({scaled(write, ceiling), scaled(read, ceiling)});
            lines = {core::localized("R ", "读 ", language) + telemetry::rate(value.diskRead), core::localized("W ", "写 ", language) + telemetry::rate(value.diskWrite)};
            break;
        }
        }
        if (lines == lines_) return;
        lines_ = lines;
        d::editText(*primary_, [&](d::TextBlock& block) { block.setText(lines[0]); });
        d::editText(*secondary_, [&](d::TextBlock& block) { block.setText(lines[1]); });
    }
    void hoverChanged() override {
        const bool on = hovered() && enabled();
        d::motion::animate(layer(), d::Channel::translateX, on ? 2 : 0, d::duration::hover, clock());
        d::motion::animate(*brighten_, d::Channel::opacity, on ? 1 : 0, d::duration::hover, clock(), d::easing::ease);
        sheen_->play(on, clock());
    }
    bool pointerDown(d::Point) override { return false; }
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::text; }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::text);
        item.value = join(lines_, ", ");
        out.push_back(std::move(item));
    }
private:
    Metric metric_;
    d::Layer *brighten_, *graph_, *primary_, *secondary_;
    std::unique_ptr<d::Sheen> sheen_;
    std::vector<std::pair<d::Color, double>> traces_;        // back to front: the secondary series first
    std::vector<std::string> lines_;
    // ActivityGraph (:197-236): the #2E2E2E grid (four vertical lines and the
    // middle one), then per series an area (.35, secondary .25) and a 1.6 px
    // round line; re-pathed without animation.
    void renderGraph(const std::vector<std::vector<std::optional<double>>>& series) {
        const auto& p = palette();
        const d::Rect bounds{0, 0, activityGraphSize.width, activityGraphSize.height};
        auto node = d::frameNode(graph_->id(), bounds);
        d::PathBuilder grid;
        for (int index = 1; index <= 4; ++index) grid.rect({std::round(bounds.width * double(index) / 5) - .5, 0, 1, bounds.height});
        grid.rect({0, bounds.height / 2 - .5, bounds.width, 1});
        d::Json::Array children{d::shape("grid", bounds, grid, p.tone(0x2E2E2E))};
        for (std::size_t index = 0; index < traces_.size(); ++index) {
            const auto paths = index < series.size() ? activityGraphPaths(series[index]) : ActivityGraphPaths{};
            auto area = d::shape("area." + std::to_string(index), bounds, paths.area, traces_[index].first);
            area["opacity"] = traces_[index].second;
            d::StrokeStyle stroke; stroke.width = 1.6; stroke.cap = "round"; stroke.join = "round";
            children.push_back(std::move(area));
            children.push_back(d::shape("line." + std::to_string(index), bounds, paths.line, std::nullopt, traces_[index].first, stroke));
        }
        d::addChildren(node, std::move(children));
        graph_->setContent(std::move(node));
    }
};

// ---------------------------------------------------------------------------
// SortColumn (:384-414): steel, the active one accent ink with ▼ / ▲.
// ---------------------------------------------------------------------------
class ActivityCanvas::SortColumn final : public d::Control {
public:
    SortColumn(d::Builder& b, ActivitySort key, std::string title, d::Rect frame, d::TextAlignment alignment)
        : Control(b.env, "activity:sort:" + std::string(activitySortKey(key)), title, b.layer("activity.sort." + std::string(activitySortKey(key)))),
          key_(key), title_(std::move(title)) {
        const auto& p = palette();
        style_ = d::TextStyle{10, d::FontWeight::regular, p.token(d::ThemeToken::steel), 1, .08, true};
        layer().setFrame({frame.x, frame.y - 8, frame.width, frame.height + 16});
        text_ = &d::makeText(b, layer().id() + ".text", title_, style_, {0, 8, frame.width, frame.height}, alignment);
        layer().add(*text_);
        setTarget(d::localBounds(layer()));
    }
    ActivitySort key() const noexcept { return key_; }
    void render(bool active, bool descending) {
        const auto& p = palette();
        d::editText(*text_, [&](d::TextBlock& block) {
            block.runs = {{title_ + (active ? (descending ? " \xE2\x96\xBC" : " \xE2\x96\xB2") : ""), style_.colored(active ? p.accentInk() : p.token(d::ThemeToken::steel))}};
        });
        const auto language = env().language;
        label = core::localized("Sort by ", "排序：", language) + title_
            + (active ? (descending ? core::localized(", descending", "，降序", language) : core::localized(", ascending", "，升序", language)) : "");
    }
private:
    ActivitySort key_;
    std::string title_;
    d::TextStyle style_;
    d::Layer* text_;
};

// ---------------------------------------------------------------------------
// AppTable (:419-558): a scroll area of light rows virtualised over a small
// pool, rebound to items by index on samples and scrolls.
// ---------------------------------------------------------------------------
class ActivityCanvas::AppTable final {
public:
    struct Column { ActivitySort key; std::string title; double x, width; };
    static std::vector<Column> columns(core::Language language) {
        return {{ActivitySort::name, core::localized("App", "应用", language), 18, 274}, {ActivitySort::gpu, "GPU", 300, 60},
                {ActivitySort::cpu, "CPU", 368, 60}, {ActivitySort::memory, core::localized("RAM", "RAM", language), 436, 66},
                {ActivitySort::threads, core::localized("Threads", "线程", language), 510, 62}};
    }
    static constexpr double rowHeight = 40, rowWidth = 584, pitch = rowHeight + d::metric::rowGap;
    static std::size_t poolSize() { return std::size_t(std::ceil(330 / pitch)) + 1; } // 9

    struct Row {
        d::RowPlate* plate{};
        d::Readout* readout{};
        // The app's own icon, or the generic tile for an app without one.
        d::Layer *icon{}, *tile{}, *name{};
        std::vector<d::Layer*> numbers;
        std::optional<ActivityApp> item;
        std::optional<ActivitySort> sortKey;
        std::optional<std::string> iconID;  // the app whose icon the row shows
    };

    AppTable(d::Page& page, d::ScrollArea& area, std::vector<SortColumn*> columns, ActivityAppIcons& icons)
        : page_(&page), area_(&area), columns_(std::move(columns)), icons_(&icons) {
        icons.setScale(page.builder().env.text.boxScale);
    }
    d::Page& page() const noexcept { return *page_; }
    d::ScrollArea& area() const noexcept { return *area_; }
    const std::vector<std::unique_ptr<Row>>& rows() const noexcept { return rows_; }
    std::vector<Row*> visibleRows() const {
        std::vector<Row*> out;
        for (const auto& row : rows_) if (!row->plate->layer().hidden()) out.push_back(row.get());
        return out;
    }
    std::function<void(const std::vector<Row*>&)> onRowsAdded; // new pooled rows need routing (hover, accessibility)

    void update(std::vector<ActivityApp> next, bool waiting, ActivitySort sortKey, bool descending) {
        items_ = std::move(next);
        for (auto* column : columns_) column->render(column->key() == sortKey, descending);
        area_->setContentHeight(std::max(0., double(items_.size()) * pitch - d::metric::rowGap));
        const auto wanted = std::min(poolSize(), items_.size());
        if (rows_.size() < wanted) {
            std::vector<Row*> added;
            for (auto index = rows_.size(); index < wanted; ++index) {
                rows_.push_back(makeRow(index));
                area_->content().add(rows_.back()->plate->layer());
                added.push_back(rows_.back().get());
            }
            if (onRowsAdded) onRowsAdded(added);
        }
        const bool showsEmpty = items_.empty() && !waiting;
        auto& b = page_->builder();
        if (showsEmpty && !empty_) {
            d::Layer& state = d::emptyState::bilingual(b, "activity.apps.empty." + std::to_string(++emptyCount_), "NO APP READINGS", "暂无应用读数",
                {d::metric::boxWidth, 250});
            state.setOrigin({0, 0});
            area_->content().add(state);
            empty_ = &state;
        } else if (!showsEmpty && empty_) {
            empty_->removeFromParent(); empty_ = nullptr;
        }
    }
    // bind(sortKey:) (:535-549): places the pool over the visible slice.
    void bind(ActivitySort sortKey) {
        const auto first = std::size_t(std::max(0., std::floor(area_->offset() / pitch)));
        for (std::size_t offset = 0; offset < rows_.size(); ++offset) {
            auto& row = *rows_[offset];
            const auto index = first + offset;
            if (index >= items_.size()) { row.plate->layer().setHidden(true); continue; }
            row.plate->layer().setHidden(false);
            const d::Point origin{0, double(index) * pitch};
            if (row.plate->layer().frame().x != origin.x || row.plate->layer().frame().y != origin.y) row.plate->layer().setOrigin(origin);
        }
        for (std::size_t offset = 0; offset < rows_.size() && first + offset < items_.size(); ++offset) show(*rows_[offset], items_[first + offset], sortKey);
        icons_->schedule();
    }
    // Icons that finished loading go to the visible rows showing their apps.
    void showIcons(const std::vector<std::string>& ids) {
        for (const auto& row : rows_) {
            if (row->plate->layer().hidden() || !row->item) continue;
            const auto& id = row->item->identity.id;
            if (std::find(ids.begin(), ids.end(), id) == ids.end() || row->iconID == id) continue;
            showIcon(*row, icons_->icon(*row->item), id);
        }
    }
private:
    d::Page* page_;
    d::ScrollArea* area_;
    std::vector<SortColumn*> columns_;
    ActivityAppIcons* icons_;
    std::vector<std::unique_ptr<Row>> rows_;
    std::vector<ActivityApp> items_;
    d::Layer* empty_{};
    unsigned emptyCount_{};

    std::unique_ptr<Row> makeRow(std::size_t index) {
        auto& b = page_->builder();
        const auto& p = b.env.palette;
        auto row = std::make_unique<Row>();
        const std::string id = "activity.app." + std::to_string(index);
        row->plate = &page_->make<d::RowPlate>(id, "", std::nullopt, d::RowPlate::Variant::compact, "\xE2\x80\xBA", rowWidth, rowHeight);
        d::Layer& readoutFrame = b.layer(id + ".readout");
        readoutFrame.setFrame(d::localBounds(row->plate->layer()));
        row->readout = &b.controls.make<d::Readout>(b.env, id + ".readout", "", "", readoutFrame);
        const d::Rect frame{18, 11, ActivityAppIcons::side, ActivityAppIcons::side};
        row->icon = &b.layer(id + ".icon");
        row->icon->setFrame(frame); row->icon->setHidden(true);
        row->tile = &b.layer(id + ".tile");
        row->tile->setFrame(frame); d::setBackground(*row->tile, d::rgb(0x1B1B1B)); row->tile->setHidden(true);
        d::Layer& glyph = d::makeIcon(b, id + ".tile.glyph", d::Icon::apps, 12, p.yellow());
        glyph.setFrame({(frame.width - 12) / 2, (frame.height - 12) / 2, 12, 12});
        row->tile->add(glyph);
        row->name = &d::makeText(b, id + ".name", "", d::TextStyle{13.5, d::FontWeight::medium, d::fixed::ink}, {46, 14.25, 246, 13.5});
        const auto all = columns(b.env.language);
        for (std::size_t n = 1; n < all.size(); ++n)
            row->numbers.push_back(&d::makeText(b, id + ".number." + std::to_string(n), "", numberStyle(), {all[n].x, 15.25, all[n].width, 11.5}, d::TextAlignment::right));
        d::Layer& plate = row->plate->layer();
        for (d::Layer* child : {row->tile, row->icon, row->name}) plate.add(*child);
        for (d::Layer* number : row->numbers) plate.add(*number);
        plate.add(readoutFrame);
        return row;
    }
    static d::TextStyle numberStyle() { return {11.5, d::FontWeight::bold, d::rgb(0x555555), 1, 0, false, true}; }
    // Sets the icon when the row shows another app or its icon arrived; a
    // pending icon leaves the slot empty until it does.
    static void showIcon(Row& row, const ActivityAppIcons::Icon& value, const std::string& id) {
        using Kind = ActivityAppIcons::Icon::Kind;
        row.iconID = value.kind == Kind::pending ? std::nullopt : std::optional(id);
        if (value.kind == Kind::image) row.icon->setContent(d::image(row.icon->id(), d::localBounds(*row.icon), value.image));
        else row.icon->clearContent();
        row.icon->setHidden(value.kind != Kind::image);
        row.tile->setHidden(value.kind != Kind::generic);
    }
    void show(Row& row, const ActivityApp& item, ActivitySort sortKey) {
        if (row.iconID != item.identity.id) showIcon(row, icons_->icon(item), item.identity.id);
        if (row.item == item && row.sortKey == sortKey) return;
        row.item = item; row.sortKey = sortKey;
        const auto language = page_->builder().env.language;
        const std::array<std::string, 4> values{telemetry::percent(item.gpuPercent), telemetry::percent(item.cpuPercent, std::nullopt),
            telemetry::memory(item.memoryBytes), item.threads ? std::to_string(*item.threads) : std::string(telemetry::missing)};
        d::editText(*row.name, [&](d::TextBlock& block) { block.setText(item.identity.name); });
        const auto all = columns(language);
        std::vector<std::string> spoken;
        for (std::size_t index = 0; index < row.numbers.size(); ++index) {
            const bool sorted = all[index + 1].key == sortKey;
            d::editText(*row.numbers[index], [&](d::TextBlock& block) { block.runs = {{values[index], numberStyle().colored(sorted ? d::fixed::nearBlack : d::rgb(0x555555))}}; });
            spoken.push_back(all[index + 1].title + " " + values[index]);
        }
        row.readout->label = item.identity.name;
        row.readout->value = join(spoken, ", ");
    }
};

// ---------------------------------------------------------------------------
// ActivityMonitorCanvas
// ---------------------------------------------------------------------------
ActivityCanvas::ActivityCanvas(d::Context& context, ActivityState& state, Demand demand, ActivityAppIcons::Loader loader)
    : Canvas(core::Module::activityMonitor, context), state_(&state), demand_(std::move(demand)), icons_(std::move(loader)) {}
ActivityCanvas::~ActivityCanvas() {
    icons_.onLoad = nullptr;
    if (observing_) { state_->onSystem = nullptr; state_->onApps = nullptr; }
    if (reading_ && demand_) try { demand_(false, false, now()); } catch (...) {}
}

// MARK: Lifecycle (:40-65)
void ActivityCanvas::didActivate() {
    if (!observing_) {
        state_->onSystem = [this] { if (!showingApps_) updateOverview(); };
        state_->onApps = [this] { if (showingApps_) updateApps(); };
        observing_ = true;
    }
    if (showingApps_) state_->setSort(sortKey_, sortDescending_);
    reading_ = true;
    if (demand_) demand_(true, showingApps_, now());
}
void ActivityCanvas::stopReading() {
    if (observing_) { state_->onSystem = nullptr; state_->onApps = nullptr; observing_ = false; }
    if (reading_ && demand_) demand_(false, false, now());
    reading_ = false;
}

// MARK: Actions (:69-110)
bool ActivityCanvas::perform(std::string_view actionID, double now) {
    env().now = now;
    if (actionID == "activity:overview" || actionID == "activity:apps") {
        const bool next = actionID == "activity:apps";
        if (next == showingApps_ || !page().tabs) return next == showingApps_;
        page().tabs->select(next ? 1 : 0, true);
        return true;
    }
    constexpr std::string_view prefix = "activity:sort:";
    if (!actionID.starts_with(prefix)) return false;
    for (auto key : {ActivitySort::name, ActivitySort::cpu, ActivitySort::memory, ActivitySort::network, ActivitySort::disk, ActivitySort::upload,
                     ActivitySort::download, ActivitySort::diskRead, ActivitySort::diskWrite, ActivitySort::gpu, ActivitySort::threads})
        if (activitySortKey(key) == actionID.substr(prefix.size())) { sort(key); return true; }
    return false;
}
void ActivityCanvas::showApps(bool next, double direction) {
    if (next == showingApps_) return;
    leavingTab_ = showingApps_ ? 1 : 0;
    showingApps_ = next;
    if (next) state_->setSort(sortKey_, sortDescending_);
    if (isActive() && demand_) demand_(true, next, now());
    navigate(direction);
}
// Spec §3.25: the active column flips; a new one starts descending, except
// App (A-Z). The rows then re-stagger in from below.
void ActivityCanvas::sort(ActivitySort key) {
    if (!showingApps_ || !table_) return;
    sortDescending_ = key == sortKey_ ? !sortDescending_ : key != ActivitySort::name;
    sortKey_ = key;
    state_->setSort(key, sortDescending_);
    updateApps();
    std::vector<d::Layer*> layers;
    for (auto* row : table_->visibleRows()) layers.push_back(&row->plate->layer());
    d::motion::stagger(layers, {0, 12}, clock());
}
bool ActivityCanvas::handleScroll(d::Point point, d::Point delta) {
    if (!showingApps_ || !table_ || !table_->area().layer().frame().contains(point)) return false;
    if (table_->area().scroll(delta.y)) table_->bind(sortKey_);
    return true;
}

// MARK: Pages (:114-190)
void ActivityCanvas::buildPage(d::Page& page) {
    rows_.clear(); table_.reset();
    const int selected = showingApps_ ? 1 : 0;
    auto& tabs = page.makeTabs("activityTabs", {{text("Overview", "概览"), std::nullopt}, {text("Apps", "应用"), std::nullopt}},
        leavingTab_.value_or(selected), d::metric::boxWidth);
    tabs.select(selected, false);
    leavingTab_.reset();
    tabs.onSelect = [this](int index, double direction) { showApps(index == 1, direction); };
    tabs.layer().setOrigin({0, d::metric::headerHeight});
    page.layer().add(tabs.layer());
    page.add(tabs.controls()); page.tabs = &tabs; page.stagger(tabs.layer());
    if (showingApps_) buildApps(page); else buildOverview(page);
}
void ActivityCanvas::buildOverview(d::Page& page) {
    header().configure({"ACTIVITY"}, "SYS-11", title("Activity", "活动监视器"), "LIVE", false);
    const std::array<std::pair<std::string, std::string>, 5> labels{{
        {"GPU", text("TOTAL LOAD", "总负载")}, {"CPU", text("TOTAL LOAD", "总负载")}, {text("RAM", "RAM"), text("USED / TOTAL", "已用 / 总计")},
        {text("Network", "网络"), text("UP / DOWN", "上传 / 下载")}, {text("Disk I/O", "磁盘 I/O"), text("READ / WRITE", "读取 / 写入")}}};
    for (int index = 0; index < 5; ++index) {
        auto& row = page.make<Row>(Row::Metric(index), labels[std::size_t(index)].first, labels[std::size_t(index)].second);
        row.layer().setOrigin({0, 125 + double(index) * (Row::size.height + d::metric::rowGap)});
        page.layer().add(row.layer());
        rows_.push_back(&row);
    }
    for (auto* row : rows_) { page.add(*row); page.stagger(row->layer()); }
    updateOverview();
}
void ActivityCanvas::updateOverview() {
    if (rows_.empty()) return;
    const auto history = state_->history();
    const auto window = history.last(std::min(history.size(), activityGraphSamples));
    for (auto* row : rows_) row->update(window, state_->snapshot(), env().language);
}
void ActivityCanvas::buildApps(d::Page& page) {
    const auto& snapshot = state_->appSnapshot();
    header().configure({"ACTIVITY"}, "SYS-11", title("Activity", "活动监视器"), d::format::pad2(std::int64_t(snapshot.items.size())), false);
    std::vector<SortColumn*> columns;
    d::Layer& head = page.makeLayer("activity.columns");
    head.setFrame(d::metric::box);
    for (const auto& column : AppTable::columns(env().language)) {
        auto& made = page.make<SortColumn>(column.key, column.title, d::Rect{column.x, 133, column.width, 10},
            column.key == ActivitySort::name ? d::TextAlignment::left : d::TextAlignment::right);
        head.add(made.layer());
        columns.push_back(&made);
    }
    page.layer().add(head);
    for (auto* column : columns) {
        column->onActivate = [this, key = column->key()] { sort(key); };
        page.add(*column);
    }
    auto& area = page.scrollArea("apps", {0, 155, d::metric::boxWidth, 330});
    table_ = std::make_unique<AppTable>(page, area, columns, icons_);
    icons_.onLoad = [this](const std::vector<std::string>& ids) { if (table_) table_->showIcons(ids); };
    page.stagger(head);
    table_->update(snapshot.items, snapshot.timestamp == 0, sortKey_, sortDescending_);
    page.restoreOffset(area);
    table_->bind(sortKey_);
    for (const auto& row : table_->rows()) { page.add(*row->plate); page.add(*row->readout); }
    for (auto* row : table_->visibleRows()) page.stagger(row->plate->layer());
    table_->onRowsAdded = [this, table = table_.get()](const std::vector<AppTable::Row*>& added) {
        if (table_.get() != table) return;
        for (auto* row : added) { table->page().add(*row->plate); table->page().add(*row->readout); }
        refreshRouter();
    };
}
void ActivityCanvas::updateApps() {
    if (!table_) return;
    const auto& snapshot = state_->appSnapshot();
    header().setCounter(d::format::pad2(std::int64_t(snapshot.items.size())));
    table_->update(snapshot.items, snapshot.timestamp == 0, sortKey_, sortDescending_);
    table_->bind(sortKey_);
}
} // namespace endfield::modules
