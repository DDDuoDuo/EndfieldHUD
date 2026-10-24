// The Notes centre on the redesign: the port of Sources/NotesCanvas.swift
// (see the header). Line references are that file's (Mac 1.3.0).
//
// Removed with the v1.2.0 centre (DOCS-100, DOCS-122): the NOTES section
// heading, the "Could not save: ..." / "Notes storage unavailable" status
// line (errors are toasts now, DOCS-103, DOCS-126), the four 92 x 31 toolbar
// plates with their 2 px press and the retained media source chooser and
// shelf picker (DOCS-124, DOCS-125, DOCS-250: centre menus on the Media button).
#include "modules/notes_canvas.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
namespace d = design;
namespace {
using d::Channel; using d::Point; using d::Rect;
using Document = ehud::data::NotesCentreDocument;
constexpr double formHeight = 32, rowPitch = d::TaskCard::height + d::metric::rowGap;
bool inside(Rect r, Point p) noexcept { return r.width > 0 && r.height > 0 && p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
std::string pad2(std::size_t v) { return d::format::pad2(static_cast<std::int64_t>(v)); }

// NotesAddButton (:821-849): the add bar's light chamfered button: the light
// gradient with an 8 px chamfer, a 16 px icon and a 13 px medium label (#111)
// centred as a group 8 px apart; hover lifts it 2 px and fades in a white .07
// brighten layer; no press scale; disabled .35.
class NotesAddButton final : public d::Control {
public:
    NotesAddButton(d::Builder& b, std::string id, std::string title, d::Icon icon, d::Size size) : Control(b.env, id, title, b.layer(id)) {
        const Rect bounds{0, 0, size.width, size.height};
        const auto shape = d::chamfer(bounds, 8);
        d::Layer& l = layer();
        l.setAnimatable();                               // the hover lift, the disabled opacity
        l.setBounds(size);
        d::Layer& plate = b.layer(this->id() + ".add.plate");
        plate.setFrame(bounds);
        plate.setContent(d::plate(plate.id(), bounds, d::fixed::lightButton, {}, 8));
        brighten_ = &b.layer(this->id() + ".add.brighten");
        brighten_->setFrame(bounds);
        brighten_->setContent(d::shape(brighten_->id(), bounds, shape, d::white(.07)));
        brighten_->setAnimatable();
        brighten_->setValue(Channel::opacity, 0);
        const d::TextStyle style{13, d::FontWeight::medium, d::fixed::nearBlack};
        const double textWidth = std::ceil(env().text.width(title, style)), start = (size.width - 16 - 8 - textWidth) / 2;
        d::Layer& glyph = d::makeIcon(b, this->id() + ".add.icon", icon, 16, d::fixed::nearBlack);
        glyph.setFrame({start, (size.height - 16) / 2, 16, 16});
        d::Layer& text = d::makeText(b, this->id() + ".add.text", std::move(title), style, {start + 24, (size.height - 13) / 2, textWidth + 1, 13});
        for (d::Layer* part : {&plate, brighten_, &glyph, &text}) l.add(*part);
        setTarget(bounds, shape);
    }
    void hoverChanged() override { d::applyHoverMotion(*this, 2, 1, 1, brighten_); }
    void enabledChanged() override { layer().setValue(Channel::opacity, enabled() ? 1 : .35); }
private:
    d::Layer* brighten_;
};
}

NotesCanvas::NotesCanvas(d::Context& context, ehud::data::NotesCentreStore& store, NotesCanvasOptions options)
    : Canvas(core::Module::notes, context), store_(&store), options_(std::move(options)) {
    timers_ = timerGroup();
}
NotesCanvas::~NotesCanvas() { if (noteArea_) noteArea_->dispose(); }

// --- Lifecycle (:169-190) --------------------------------------------------------
// Storage problems are shown once, the first time Notes opens.
void NotesCanvas::didActivate() {
    attachNote();
    if (reportedLoadProblem_) return;
    std::optional<std::string> error;
    if (!options_.storageAvailable) error = options_.storageError.value_or(text("Notes storage is unavailable.", "便笺存储不可用。"));
    else if (store_->loadIssue() == ehud::data::NotesCentreStore::LoadIssue::setAside)
        error = text("The centre notes could not be read and were set aside.", "无法读取中央便笺，原文件已另存。");
    if (!error) return;
    reportedLoadProblem_ = true;
    toast(d::Icon::warn, "Notice", "提示", std::move(error));
}
void NotesCanvas::didDeactivate() {
    cancelReorder();
    saveNote();
    if (noteArea_) noteArea_->suspend();
}
void NotesCanvas::willLeave() { saveNote(); }
void NotesCanvas::recordingModeChanged() {
    endEditing(true);
    Canvas::recordingModeChanged();
}
bool NotesCanvas::inputLocked() const { return Canvas::inputLocked() || reorder_.has_value(); }

// --- Page (:192-264) ----------------------------------------------------------------
std::string NotesCanvas::counterText() const {
    const auto& doc = document();
    return doc.tab == Document::Tab::todo ? pad2(doc.doneCount()) + "/" + pad2(doc.tasks.size()) : "01";
}
double NotesCanvas::addBarTop(bool folded) const noexcept { return d::metric::headerHeight + (folded ? 6 + 2 : expandedBodyHeight_ + 10); }
// foldButton (:258-265). The Mac makes a new button for each Hide / Show; the
// page's one "notes.fold" is re-titled in place here (layer ids are unique).
d::Button& NotesCanvas::foldButton(d::Page& page, bool folded) {
    if (!foldButton_) foldButton_ = &page.make<d::Button>("notes.fold", std::string(), folded ? d::Icon::eye : d::Icon::eyeoff,
        d::Button::Kind::secondary, d::Button::Height::compact);
    auto& button = *foldButton_;
    button.setTitle(folded ? text("Show", "显示") : text("Hide", "隐藏"));
    button.setIcon(folded ? d::Icon::eye : d::Icon::eyeoff);
    button.place({0, 0});
    button.onActivate = [this, folded] { setFolded(!folded); };
    return button;
}
void NotesCanvas::buildPage(d::Page& page) {
    cards_.clear(); builtSlots_.clear(); tabs_ = nullptr; formatBar_ = nullptr; foldButton_ = nullptr; reorder_.reset(); taskList_ = nullptr;
    if (editingTask_ && !editingTask_->field->editing()) editingTask_.reset();
    const auto& doc = document();
    const bool folded = doc.isFolded && options_.storageAvailable;
    std::vector<d::Control*> actions;
    if (options_.storageAvailable) actions.push_back(&foldButton(page, folded));
    header().configure({"NOTES"}, "MOD-01", title("Notes", "便笺"), counterText(), false, actions);
    d::Layer& clip = page.makeLayer("notes.body");
    d::Layer& content = page.makeLayer("notes.body.content");
    clip.setMasksToBounds(true);
    clip.add(content); page.layer().add(clip);
    std::vector<d::Layer*> staggered;
    double contentHeight{};
    bool showsNote = false;
    if (!options_.storageAvailable) {
        d::Layer& empty = d::emptyState::bilingual(page.builder(), "notes.unavailable", "NOTES UNAVAILABLE", "便笺存储不可用", {586, 250});
        empty.setOrigin({2, 4}); content.add(empty);
        staggered.push_back(&empty);
        contentHeight = 4 + 250 + 2;
    } else {
        auto& bar = page.makeTabs("notes.tabs", {{text("TEXT", "文字"), d::Icon::text}, {text("TODO", "待办"), d::Icon::todo}},
            doc.tab == Document::Tab::text ? 0 : 1, 586);
        bar.setNews(1, doc.hasDot());
        bar.onSelect = [this](int index, double direction) { selectTab(index == 0 ? Document::Tab::text : Document::Tab::todo, direction); };
        bar.layer().setOrigin({2, 4}); content.add(bar.layer());
        page.add(bar.controls());
        tabs_ = &bar;
        if (!folded) page.tabs = &bar;
        staggered.push_back(&bar.layer());
        constexpr double top = 4 + 34 + 10;
        showsNote = doc.tab == Document::Tab::text;
        const double height = showsNote ? buildNote(page, content, top, staggered) : buildTasks(page, content, top, staggered);
        contentHeight = top + height + 2;
    }
    if ((!showsNote || env().recordingMode) && noteArea_) noteArea_->suspend();
    expandedBodyHeight_ = contentHeight;
    // The fold (:238-244): a clip 6 px high when folded, the content faded
    // out and raised 8 px. Here the clip keeps its expanded bounds and a
    // top-anchored mask (the wipe channel) shows 6 / height of it.
    clip.setFrame({0, d::metric::headerHeight, d::metric::boxWidth, contentHeight});
    clip.setAnimatable();
    clip.wipeOrigin = d::WipeOrigin::top;
    clip.setValue(Channel::wipe, folded ? 6 / contentHeight : 1);
    content.setFrame({0, 0, d::metric::boxWidth, contentHeight});
    content.setAnimatable();
    content.setValue(Channel::opacity, folded ? 0 : 1);
    content.setValue(Channel::translateY, folded ? -8 : 0);
    body_ = &clip; bodyContent_ = &content;
    d::Layer& addBar = buildAddBar(page);
    addBar.setOrigin({0, addBarTop(false)});
    addBar.setAnimatable();
    addBar.setValue(Channel::translateY, addBarTop(folded) - addBarTop(false));
    if (!folded) for (d::Layer* layer : staggered) page.stagger(*layer);
    for (d::Layer* layer : addBar.children()) page.stagger(*layer);
    if (env().recordingMode) { const std::array<d::Layer*, 1> redacted{&content}; d::redaction::playEntrance(redacted, clock()); }
    if (showsNote && isActive() && !folded) attachNote();
}

// --- TODO (:266-346) ------------------------------------------------------------------
double NotesCanvas::slotY(int index) noexcept { return 4 + index * rowPitch; }
double NotesCanvas::buildTasks(d::Page& page, d::Layer& content, double top, std::vector<d::Layer*>& staggered) {
    const auto& doc = document();
    const bool redacted = env().recordingMode;
    double listHeight{};
    if (doc.tasks.empty()) {
        listHeight = 126;
        d::Layer& empty = d::emptyState::bilingual(page.builder(), "notes.tasks.empty", "NO TASKS", "待办为空", {586, listHeight});
        empty.setOrigin({2, top}); content.add(empty);
        staggered.push_back(&empty);
    } else {
        const auto count = doc.tasks.size();
        const double natural = double(count) * d::TaskCard::height + double(count - 1) * d::metric::rowGap;
        listHeight = std::min(listLimit, natural);
        // 4 px of headroom keep the orange dot (-3, -3) unclipped.
        auto& list = page.scrollArea("notes.tasks", {2, top - 4, 586, listHeight + 4}, &content);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& task = doc.tasks[index];
            const std::string id = task.id;
            auto& card = page.make<d::TaskCard>("task:" + id, task.text, task.isChecked, doc.showsDot(task), redacted, 580);
            card.onToggle = [this, id](bool done) { setTask(id, done); };
            card.onDelete = [this, id] { deleteTask(id); };
            // The card's left edge: drag to reorder; a press opens the item
            // menu (edit, orange dot, move, delete), which is also how the
            // keyboard and screen readers reach those actions.
            auto& grip = page.make<d::DragGrip>("task:" + id + ":grip", text("Item options", "事项选项") + " " + (redacted ? std::string() : task.text),
                d::Size{14, d::TaskCard::height}, Point{4, 25});
            auto* gripPointer = &grip;
            grip.onActivate = [this, id, gripPointer] { openTaskMenu(id, gripPointer, std::nullopt); };
            grip.onDrag = [this, id, gripPointer](d::DragGrip::Phase phase, Point point) {
                const auto rootPoint = d::convert(point, gripPointer->layer(), root());
                if (rootPoint) dragTask(id, phase, *rootPoint);
            };
            card.layer().setOrigin({0, slotY(int(index))});
            grip.layer().setOrigin({0, 0});
            card.layer().add(grip.layer());
            list.content().add(card.layer());
            page.add(card.controls()); page.add(grip);
            staggered.push_back(&card.layer());
            cards_.emplace_back(id, &card);
            builtSlots_.emplace_back(id, int(index));
        }
        list.setContentHeight(4 + natural);
        page.restoreOffset(list);
        taskList_ = &list;
        if (editingTask_) if (auto* card = taskCard(editingTask_->id)) placeTaskField(*editingTask_->field, *card);
    }
    const double formTop = top + listHeight + 8;
    d::Layer& form = page.makeLayer("notes.addForm");
    auto& add = page.make<d::Button>("notes.addTask", text("Add", "添加"), d::Icon::plus, d::Button::Kind::primary);
    add.onActivate = [this] { submitTask(); };
    d::TextField& field = taskField(586 - 8 - add.preferredWidth());
    form.setFrame({2, formTop, 586, formHeight});
    field.layer().setOrigin({0, 0});
    form.add(field.layer());
    content.add(form);
    add.place({586 - add.preferredWidth(), 0});
    form.add(add.layer());
    registerField(field);
    page.add(add);
    staggered.push_back(&form);
    return listHeight + 8 + formHeight;
}
// taskField (:335-346): the add-item field survives re-renders with its
// draft and focus (canvas-owned; a new width resizes it).
d::TextField& NotesCanvas::taskField(double width) {
    if (addField_) { addField_->setSize({width, formHeight}); return *addField_; }
    d::TextFieldOptions o;
    o.maximumLength = Document::maximumTaskLength;
    o.inset = {12, 0};
    auto& field = canvasBuilder().make<d::TextField>("notes.newTask", text("Add item", "添加事项"), "", text("+ Add item", "+ 添加事项"),
        d::Size{width, formHeight}, o);
    field.onReturn = [this] { submitTask(); };
    addField_ = &field;
    return field;
}
d::TaskCard* NotesCanvas::taskCard(const std::string& id) const {
    const auto it = std::find_if(cards_.begin(), cards_.end(), [&](const auto& c) { return c.first == id; });
    return it == cards_.end() ? nullptr : it->second;
}
int NotesCanvas::builtSlot(const std::string& id) const {
    const auto it = std::find_if(builtSlots_.begin(), builtSlots_.end(), [&](const auto& s) { return s.first == id; });
    return it == builtSlots_.end() ? 0 : it->second;
}

// The item menu (:348-369): grip press, right-click.
void NotesCanvas::openTaskMenu(const std::string& id) { openTaskMenu(id, nullptr, std::nullopt); }
void NotesCanvas::openTaskMenu(const std::string& id, const d::Control* anchor, std::optional<Point> at) {
    const auto& tasks = document().tasks;
    const auto it = std::find_if(tasks.begin(), tasks.end(), [&](const auto& t) { return t.id == id; });
    auto* card = taskCard(id);
    if (it == tasks.end() || !card) return;
    if (menuOpen()) { closeMenu(); return; }
    const auto index = std::size_t(it - tasks.begin()), count = tasks.size();
    std::vector<d::Menu::Item> items(5);
    items[0].id = "edit"; items[0].title = text("Edit", "编辑"); items[0].enabled = !env().recordingMode;
    items[1].id = "dot"; items[1].title = text("Orange dot", "橙色标记"); items[1].selected = document().flagged.contains(id);
    items[2].id = "up"; items[2].title = text("Move up", "上移"); items[2].separatorBefore = true; items[2].enabled = index > 0;
    items[3].id = "down"; items[3].title = text("Move down", "下移"); items[3].enabled = index + 1 < count;
    items[4].id = "delete"; items[4].title = text("Delete", "删除"); items[4].danger = true; items[4].separatorBefore = true;
    const Rect anchorRect = at ? Rect{at->x, at->y, 0, 0} : anchor ? anchor->rect(root()) : card->rect(root());
    openMenu("notes.task", std::move(items), anchorRect, anchor, 168, [this, id](const d::Menu::Item& item) {
        if (item.id == "edit") beginTaskEdit(id);
        else if (item.id == "dot") setDot(id, !document().flagged.contains(id));
        else if (item.id == "up") moveTask(id, -1);
        else if (item.id == "down") moveTask(id, 1);
        else deleteTask(id);
    });
}
void NotesCanvas::setDot(const std::string& id, bool on) {
    save([&](Document& doc) { if (on) doc.flagged.insert(id); else doc.flagged.erase(id); });
    refreshDots();
}
// The orange dots in place: one per flagged open card, one on the TODO tab.
void NotesCanvas::refreshDots() {
    const auto& doc = document();
    for (const auto& task : doc.tasks) if (auto* card = taskCard(task.id)) card->setAttention(doc.showsDot(task));
    if (tabs_) tabs_->setNews(1, doc.hasDot());
}
void NotesCanvas::moveTask(const std::string& id, int step) {
    const auto& tasks = document().tasks;
    const auto it = std::find_if(tasks.begin(), tasks.end(), [&](const auto& t) { return t.id == id; });
    if (it == tasks.end()) return;
    const int index = int(it - tasks.begin());
    const int target = std::clamp(index + step, 0, int(tasks.size()) - 1);
    if (target == index) return;
    save([&](Document& doc) { auto task = doc.tasks[std::size_t(index)]; doc.tasks.erase(doc.tasks.begin() + index); doc.tasks.insert(doc.tasks.begin() + target, std::move(task)); });
    rerender();
    if (taskList_) taskList_->reveal({0, slotY(target), 580, d::TaskCard::height});
}

// --- TODO: editing an item in place (:392-430) --------------------------------------
void NotesCanvas::beginTaskEdit(const std::string& id) {
    if (env().recordingMode) return;
    const auto& tasks = document().tasks;
    const auto it = std::find_if(tasks.begin(), tasks.end(), [&](const auto& t) { return t.id == id; });
    auto* card = taskCard(id);
    if (it == tasks.end() || !card) return;
    if (editingTask_) editingTask_->field->endEditing(true);
    // One field per task, canvas-owned (it may outlive a re-render while it
    // edits); the Mac makes a new one each time, with the same options.
    const std::string fieldID = "task:" + id + ":edit";
    d::TextField* field{};
    if (const auto found = editFields_.find(fieldID); found != editFields_.end()) { field = found->second; field->setText(it->text); }
    else {
        d::TextFieldOptions o;
        o.light = true;
        o.maximumLength = Document::maximumTaskLength;
        o.selectsAllOnBegin = true;
        o.style = d::TaskCard::titleStyle();
        o.inset = {8, 0};
        field = &canvasBuilder().make<d::TextField>(fieldID, text("Edit item", "编辑事项"), it->text, "", d::Size{580 - d::TaskCard::sideWidth - 18, 24}, o);
        editFields_.emplace(fieldID, field);
    }
    field->onCommit = [this, id](const std::string& value) { finishTaskEdit(id, value); };
    field->onCancel = [this, id] { finishTaskEdit(id, std::nullopt); };
    editingTask_ = EditingTask{id, field};
    placeTaskField(*field, *card);
    registerField(*field);
    refreshRouter();
    field->beginEditing();
}
void NotesCanvas::placeTaskField(d::TextField& field, d::TaskCard& card) {
    field.layer().removeFromParent();
    field.layer().setOrigin({10, 11});
    card.layer().add(field.layer());
}
// A non-blank edit replaces the title; Esc (nullopt) or a blank one keeps it.
void NotesCanvas::finishTaskEdit(const std::string& id, std::optional<std::string> value) {
    if (!editingTask_ || editingTask_->id != id) return;
    d::TextField* field = editingTask_->field;
    editingTask_.reset();
    field->layer().removeFromParent();
    const std::string trimmed = d::trimmedWhitespace(value.value_or(std::string()));
    if (!trimmed.empty()) {
        const std::string title = env().characters.prefix(trimmed, Document::maximumTaskLength);
        save([&](Document& doc) { for (auto& task : doc.tasks) if (task.id == id) task.text = title; });
    }
    if (isActive()) rerender();
}

// --- TODO: reordering by the grip (:432-497) -------------------------------------------
void NotesCanvas::dragTask(const std::string& id, d::DragGrip::Phase phase, Point point) {
    if (!taskList_) return;
    auto& list = *taskList_;
    const auto local = d::convert(point, root(), list.content());
    if (!local) return;
    switch (phase) {
    case d::DragGrip::Phase::began: {
        auto* card = taskCard(id);
        const auto& tasks = document().tasks;
        const auto it = std::find_if(tasks.begin(), tasks.end(), [&](const auto& t) { return t.id == id; });
        if (!card || it == tasks.end()) return;
        closePopups();
        if (editingTask_) editingTask_->field->endEditing(true);
        Reorder state{id, local->y - slotY(int(it - tasks.begin())), int(it - tasks.begin()), {}};
        for (const auto& task : document().tasks) state.order.push_back(task.id);
        reorder_ = std::move(state);
        card->layer().setZPosition(10);
        d::motion::animate(card->layer(), Channel::scaleX, 1.015, d::duration::hover, clock());
        d::motion::animate(card->layer(), Channel::scaleY, 1.015, d::duration::hover, clock());
        if (context().onLockParallax) context().onLockParallax();
        break;
    }
    case d::DragGrip::Phase::moved: {
        auto* card = taskCard(id);
        if (!reorder_ || reorder_->id != id || !card) return;
        auto& state = *reorder_;
        // Past the list's edges the list scrolls under the card.
        const double visible = list.layer().bounds().height;
        const auto edgePoint = d::convert(point, root(), list.layer());
        const double edge = edgePoint ? edgePoint->y : 0;
        if (edge < 0) { list.setOffset(std::max(0.0, list.offset() + edge * .5)); list.noteScroll(); }
        else if (edge > visible) { list.setOffset(std::min(list.maximumOffset(), list.offset() + (edge - visible) * .5)); list.noteScroll(); }
        const double y = std::clamp(local->y - state.grab, slotY(0), slotY(int(state.order.size()) - 1));
        card->layer().setValue(Channel::translateY, y - slotY(builtSlot(id)));
        const int target = std::clamp(int(std::round((y - 4) / rowPitch)), 0, int(state.order.size()) - 1);
        if (target == state.index) return;
        state.order.erase(state.order.begin() + state.index);
        state.order.insert(state.order.begin() + target, id);
        state.index = target;
        for (std::size_t slot = 0; slot < state.order.size(); ++slot) {
            const auto& other = state.order[slot];
            if (other == id) continue;
            if (auto* otherCard = taskCard(other)) d::motion::animate(otherCard->layer(), Channel::translateY, slotY(int(slot)) - slotY(builtSlot(other)), .18, clock());
        }
        break;
    }
    case d::DragGrip::Phase::ended: {
        if (!reorder_ || reorder_->id != id) return;
        const Reorder state = std::move(*reorder_);
        reorder_.reset();
        if (auto* card = taskCard(id)) {
            d::motion::animate(card->layer(), Channel::translateY, slotY(state.index) - slotY(builtSlot(id)), .18, clock());
            d::motion::animate(card->layer(), Channel::scaleX, 1, .18, clock());
            d::motion::animate(card->layer(), Channel::scaleY, 1, .18, clock());
            card->layer().setZPosition(0);
        }
        std::vector<std::string> current;
        for (const auto& task : document().tasks) current.push_back(task.id);
        if (state.order == current) return;
        save([&](Document& doc) {
            std::vector<ehud::data::NotesCentreTask> moved;
            for (const auto& taskID : state.order)
                for (const auto& task : doc.tasks) if (task.id == taskID) { moved.push_back(task); break; }
            if (moved.size() == doc.tasks.size()) doc.tasks = std::move(moved);
        });
        // The page order (accessibility) follows once the card has landed.
        after(env().reduceMotion ? 0 : .2, timers_, [this] {
            if (isActive() && !reorder_ && document().tab == Document::Tab::todo) rerender();
        });
        break;
    }
    }
}
void NotesCanvas::dragTask(const std::string& id, int toSlot) {
    auto* card = taskCard(id);
    if (!card) return;
    const auto start = d::convert(Point{7, 30}, card->layer(), root());
    if (!start) return;
    const auto& tasks = document().tasks;
    const auto it = std::find_if(tasks.begin(), tasks.end(), [&](const auto& t) { return t.id == id; });
    const double distance = double(toSlot - (it == tasks.end() ? 0 : int(it - tasks.begin()))) * rowPitch;
    dragTask(id, d::DragGrip::Phase::began, *start);
    dragTask(id, d::DragGrip::Phase::moved, {start->x, start->y + distance});
    dragTask(id, d::DragGrip::Phase::ended, {start->x, start->y + distance});
}
// A lifted card returns to its slot (the module was left mid-drag).
void NotesCanvas::cancelReorder() {
    if (!reorder_) return;
    reorder_.reset();
    for (const auto& [id, card] : cards_) {
        d::Layer& l = card->layer();
        l.setValue(Channel::translateY, 0);
        l.setValue(Channel::scaleX, 1); l.setValue(Channel::scaleY, 1);
        l.setZPosition(0);
    }
}

// --- TEXT (:499-615) ------------------------------------------------------------------------
// The TEXT tab: the note box (format bar, then the 200 px rich text area).
double NotesCanvas::buildNote(d::Page& page, d::Layer& content, double top, std::vector<d::Layer*>& staggered) {
    const auto& doc = document();
    d::Layer& box = page.makeLayer("notes.note");
    box.setFrame({2, top, 586, 43 + 200});
    d::setBackground(box, d::rgb(0x1C1C1C));
    auto& bar = page.own<NotesFormatBar>(*this, "notes.format", noteFormatState(), options_.families);
    bar.setOnFormat([this](const core::notes::FormatChange& change) { applyFormat(change); });
    formatBar_ = &bar;
    d::Layer& rule = page.makeLayer("notes.note.rule");
    rule.setFrame({0, 42, 586, 1});
    d::setBackground(rule, env().palette.token(d::ThemeToken::separator));
    bar.layer().setOrigin({8, 8});
    box.add(bar.layer()); box.add(rule);
    content.add(box);
    page.add(bar.controls());
    if (env().recordingMode) {
        d::Layer& block = d::redaction::block(page.builder(), "notes.note.redacted", doc.text, {586, 200});
        block.setOrigin({0, 43});
        box.add(block);
    } else {
        auto& area = noteEditor();
        area.formatsSelection = [this](Point point) { return formatBar_ && formatBar_->formatsSelection(point, root()); };
        area.setSize({586, 200});
        area.layer().removeFromParent();
        area.layer().setOrigin({0, 43});
        box.add(area.layer());
        page.add(area);
    }
    // CALayer draws the box's 1 px ringField border above its sublayers.
    d::Layer& ring = page.makeLayer("notes.note.ring");
    ring.setFrame({0, 0, 586, 43 + 200});
    d::setBorder(ring, std::nullopt, env().palette.token(d::ThemeToken::ringField), 1);
    box.add(ring);
    staggered.push_back(&box);
    return 43 + 200;
}
// noteEditor (:534-558): the TEXT editor, created once with the stored text
// and runs; typing continues in the last character's style (the base style
// when empty), the caret at the end.
ArchiveBodyArea& NotesCanvas::noteEditor() {
    if (noteArea_) return *noteArea_;
    const auto& doc = document();
    auto& area = canvasBuilder().make<ArchiveBodyArea>("notes.text", text("Edit text note", "编辑文字便笺"), text("Start writing", "开始书写"),
        std::uint32_t(2 * 1024 * 1024));
    area.setFramed(false);
    area.setHost(options_.noteEditor);
    const auto base = doc.style.textStyle();
    auto text16 = archiveUTF16(doc.text);
    auto runs = archive_rich_text::loaded(text16, doc.richRuns(text16), base);
    const auto typing = runs.runs.empty() ? base : runs.runs.back().style;
    const auto length = std::uint32_t(text16.size());
    area.load(std::move(text16), std::move(runs), typing);
    area.document().setSelection({{length, length}});
    area.onChange = [this] { noteEdited_ = true; if (noteArea_) noteArea_->textChanged(); };
    area.onSelectionChange = [this] { if (formatBar_) formatBar_->show(noteFormatState()); };
    area.onEndEditing = [this] { saveNote(); };
    area.onFocusChange = [this](bool focused) {
        if (focused) { if (context().onLockParallax) context().onLockParallax(); }
        else saveNote();
        if (formatBar_) formatBar_->show(noteFormatState());
        if (context().onInputStateChange) context().onInputStateChange();
    };
    // Ctrl+B / Ctrl+I / Ctrl+U report to the bar's onFormat (ArchiveBodyArea.formatBar).
    area.onFormatShortcut = [this](core::notes::FormatKind kind) { core::notes::FormatChange change; change.kind = kind; applyFormat(change); };
    noteArea_ = &area;
    return area;
}
// attachNote (:561-565): projected while the TEXT tab is on screen and interactive.
void NotesCanvas::attachNote() {
    const auto& doc = document();
    if (!isActive() || !noteArea_ || doc.tab != Document::Tab::text || doc.isFolded || env().recordingMode || !noteArea_->layer().parent()) return;
    noteArea_->attach();
}
// noteFormatState (:567-573): the selection or typing style while editing,
// else the first character's.
NotesFormatState NotesCanvas::noteFormatState() const {
    const auto& doc = document();
    core::notes::TextStyle style;
    if (noteArea_) style = archive_rich_text::displayedStyle(noteArea_->document(), noteArea_->isFocused());
    else if (doc.richText && !doc.richText->runs.empty()) style = doc.richText->runs.front().style;
    else style = doc.style.textStyle();
    return {archive_rich_text::family(style, options_.fonts), style.fontSize, style.bold, style.italic, style.underline, style.strikethrough,
        archive_rich_text::hex(style.color)};
}
// applyFormat (:575-585): to the selection while editing (the typing style at
// a caret), otherwise to the whole note.
void NotesCanvas::applyFormat(const core::notes::FormatChange& change) {
    if (!noteArea_ || env().recordingMode) return;
    auto& area = *noteArea_;
    const bool focused = area.isFocused();
    area.applyFormat(archive_rich_text::stored(change), archive_rich_text::formatRange(area.document(), focused), focused);
    noteEdited_ = true;
    saveNote();
    if (formatBar_) formatBar_->show(noteFormatState());
}
// saveNote (:587-593): only when edited and never during an IME composition.
void NotesCanvas::saveNote() {
    if (!noteEdited_ || !noteArea_ || noteArea_->hasMarkedText() || noteArea_->document().composition()) return;
    noteEdited_ = false;
    const auto& document = noteArea_->document();
    auto value = archiveUTF8(document.text());
    const auto rich = document.coveringRichText();
    save([&](Document& doc) { doc.setText(value, rich); });
}

// --- Text input routing (:617-669) ------------------------------------------------------------
bool NotesCanvas::editingOtherText() const { return noteArea_ && noteArea_->isFocused(); }
void NotesCanvas::endOtherEditing(bool) { if (noteArea_) noteArea_->blur(); }
bool NotesCanvas::interceptPointerDown(Point point, const d::PointerInput& input) {
    if (!isActive() || transitioning()) return false;
    // The note keeps focus while the format bar edits its selection.
    if (noteArea_ && noteArea_->isFocused() && !menuOpen() && !popoverOpen() && !(formatBar_ && formatBar_->formatsSelection(point, root())))
        noteArea_->blur();
    // A double-click on a task card edits its title in place.
    if (input.clickCount != 2 || menuOpen() || popoverOpen()) return false;
    const auto id = taskID(point);
    auto* card = id ? taskCard(*id) : nullptr;
    if (!card) return false;
    const auto controls = card->controls();
    if (inside(controls[1]->rect(root()), point) || inside(controls[2]->rect(root()), point)) return false;
    beginTaskEdit(*id);
    return true;
}
bool NotesCanvas::handleScroll(Point point, Point delta) {
    if (!noteArea_ || !d::isVisible(noteArea_->layer(), root()) || !inside(noteArea_->rect(root()), point)) return false;
    noteArea_->scroll(delta.y);
    return true;
}
bool NotesCanvas::handleRightPointerDown(Point point) {
    const auto id = taskID(point);
    if (!id) return false;
    openTaskMenu(*id, nullptr, point);
    return true;
}
// The task card under a box point, when the list shows it there.
std::optional<std::string> NotesCanvas::taskID(Point point) const {
    if (document().tab != Document::Tab::todo || !taskList_ || !d::isVisible(taskList_->layer(), root())) return std::nullopt;
    const auto local = d::convert(point, root(), taskList_->layer());
    if (!local || local->y < 0 || !inside(d::localBounds(taskList_->layer()), *local)) return std::nullopt;
    for (const auto& [id, card] : cards_) if (inside(card->rect(root()), point)) return id;
    return std::nullopt;
}

// --- Add bar (:671-690) --------------------------------------------------------------------------
d::Layer& NotesCanvas::buildAddBar(d::Page& page) {
    d::Layer& bar = page.makeLayer("notes.addBar");
    const double width = (d::metric::boxWidth - 3 * 8) / 4;
    struct Kind { const char* id; std::string title; d::Icon icon; };
    const std::array<Kind, 4> kinds{{{"text", text("Text", "文字"), d::Icon::text}, {"todo", text("TODO", "待办"), d::Icon::todo},
        {"image", text("Media", "媒体"), d::Icon::image}, {"drawing", text("Drawing", "画画"), d::Icon::pen}}};
    bar.setFrame({0, 0, d::metric::boxWidth, 40});
    page.layer().add(bar);
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const auto& kind = kinds[index];
        auto& button = page.make<NotesAddButton>(std::string("notes.new.") + kind.id, kind.title, kind.icon, d::Size{width, 40});
        button.setEnabled(options_.storageAvailable);
        button.onActivate = [this, action = std::string("tool:") + kind.id] { if (options_.onTool) options_.onTool(action); };
        button.layer().setOrigin({double(index) * (width + 8), 0});
        bar.add(button.layer());
        page.add(button);
        if (std::string_view(kind.id) == "image") mediaButton_ = &button;
    }
    addBar_ = &bar;
    return bar;
}

// --- Actions (:692-757) ----------------------------------------------------------------------------
bool NotesCanvas::save(const std::function<void(Document&)>& change) {
    try { return store_->update(change); }
    catch (const std::exception& error) {
        toast(d::Icon::warn, "Notice", "提示", text("Could not save: ", "无法保存：") + error.what());
        return false;
    }
}
void NotesCanvas::selectTab(Document::Tab tab, double direction) {
    if (tab == document().tab) return;
    saveNote();
    save([&](Document& doc) { doc.tab = tab; });
    navigate(direction);
}
// Hide/Show (:706-728): the area folds to nothing over .32 s (fading in .24 s,
// rising 8 px) and the add bar slides up to 2 px under the header.
void NotesCanvas::setFolded(bool folded) {
    endEditing(true); closePopups();
    saveNote();
    save([&](Document& doc) { doc.isFolded = folded; });
    header().configure({"NOTES"}, "MOD-01", title("Notes", "便笺"), counterText(), false, {&foldButton(page(), folded)});
    page().tabs = folded ? nullptr : tabs_;
    if (folded) { if (noteArea_) noteArea_->suspend(); } else attachNote();
    const auto c = clock();
    if (body_ && expandedBodyHeight_ > 0) d::motion::animate(*body_, Channel::wipe, folded ? 6 / expandedBodyHeight_ : 1, .32, c);
    if (bodyContent_) {
        d::motion::animate(*bodyContent_, Channel::opacity, folded ? 0 : 1, .24, c, d::easing::ease);
        d::motion::animate(*bodyContent_, Channel::translateY, folded ? -8 : 0, .32, c);
    }
    if (addBar_) d::motion::animate(*addBar_, Channel::translateY, addBarTop(folded) - addBarTop(false), .32, c);
    refreshRouter();
    if (context().onInputStateChange) context().onInputStateChange();
}
void NotesCanvas::setTask(const std::string& id, bool done) {
    save([&](Document& doc) { for (auto& task : doc.tasks) if (task.id == id) task.isChecked = done; });
    header().setCounter(counterText());
    refreshDots();
    if (context().onInputStateChange) context().onInputStateChange();
}
void NotesCanvas::deleteTask(const std::string& id) {
    save([&](Document& doc) { std::erase_if(doc.tasks, [&](const auto& t) { return t.id == id; }); doc.flagged.erase(id); });
    rerender();
}
// Enter or Add: a non-blank item joins the list, which scrolls to it, and
// the field takes focus again for the next one.
void NotesCanvas::submitTask() {
    if (!addField_) return;
    auto& field = *addField_;
    if (field.editing()) field.endEditing(true);
    const std::string value = d::trimmedWhitespace(field.text());
    if (value.empty()) return;
    ehud::data::NotesCentreTask task;
    task.text = env().characters.prefix(value, Document::maximumTaskLength);
    save([&](Document& doc) { doc.tasks.push_back(task); });
    field.setText("");
    rerender();
    if (taskList_) taskList_->setOffset(taskList_->maximumOffset());
    after(0, timers_, [this] { if (isActive() && addField_) addField_->beginEditing(); });
}
void NotesCanvas::submitTask(const std::string& value) { if (addField_) { addField_->setText(value); submitTask(); } }

// --- Media and status (:759-798) ---------------------------------------------------------------------
void NotesCanvas::presentMediaSources(Point point) {
    if (!isActive() || !mediaButton_) return;
    std::vector<d::Menu::Item> items(2);
    items[0].id = "finder"; items[0].title = options_.chooseFileTitle ? options_.chooseFileTitle(env().language) : text("Choose in Finder", "从 Finder 选择");
    items[1].id = "shelf"; items[1].title = text("Choose from Shelf", "从暂存架选择");
    openMenu("notes.media", std::move(items), *mediaButton_, std::nullopt, [this, point](const d::Menu::Item& item) {
        if (item.id == "finder") { if (options_.onChooseFiles) options_.onChooseFiles(point); }
        else if (options_.onChooseShelf) options_.onChooseShelf(point);
    });
}
void NotesCanvas::presentShelfMedia(std::vector<NotesShelfChoice> choices, std::function<void(const std::string&)> onSelect) {
    if (!isActive() || !mediaButton_) return;
    if (choices.empty()) { toast(d::Icon::folder, "Shelf empty", "暂存架为空"); return; }
    std::vector<d::Menu::Item> items;
    for (auto& choice : choices) { d::Menu::Item item; item.id = choice.id; item.title = choice.title; item.enabled = choice.supported && choice.available; items.push_back(std::move(item)); }
    openMenu("notes.shelf", std::move(items), *mediaButton_, 260, [select = std::move(onSelect)](const d::Menu::Item& item) {
        if (select && ehud::data::validUUID(item.id)) select(item.id);
    });
}
void NotesCanvas::workspaceDidAct(std::string_view action) {
    if (!isActive()) return;
    if (action == "createdText") toast(d::Icon::text, "New text note", "已在桌面创建便笺");
    else if (action == "createdTODO") toast(d::Icon::todo, "New TODO note", "已在桌面创建便笺");
    else if (action == "createdMedia") toast(d::Icon::image, "New media note", "已在桌面创建便笺");
    else if (action == "createdDrawing") toast(d::Icon::pen, "New drawing note", "已在桌面创建便笺");
}
void NotesCanvas::reportStatus(const std::optional<std::string>& status) {
    const auto previous = std::exchange(reportedStatus_, status);
    if (!isActive() || !status || status == previous) return;
    toast(d::Icon::warn, "Notice", "提示", *status);
}
} // namespace endfield::modules
