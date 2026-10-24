#include "modules/orbipom_canvas.hpp"
#include "modules/hud_design_badges.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace endfield::modules {
namespace d = design;
using d::Channel;
using d::Point;
using d::Rect;
namespace {
constexpr std::array<OrbiPomAsset, 15> assets{{
    {"OrbiPom/level-1.png", "c6e40bf723a0ff63e247616ff608128b9926320a33004d133351c81792afe099", 130, 145},
    {"OrbiPom/level-2.png", "c427f5d98af01154b8502711ab12a2ea850bd54cd75bad174bab835a4c660181", 148, 180},
    {"OrbiPom/level-3.png", "54682200e3325aa8bcddb1cd8fca3e95375c990d4c05a8012aaceb919981006f", 217, 205},
    {"OrbiPom/level-4.png", "5cb064a3cecdda942700b3f7c433840fc8e7c38cfd463999a5821e158e9b50e0", 213, 201},
    {"OrbiPom/level-5.png", "75de0721acd15569123808db245849d46802fc899b1c7af1c24b596ac2531677", 213, 237},
    {"OrbiPom/level-6.png", "b32c6540d485ddf6627233c88be2baa0f5fa40be1f020dc014c0eb0833d7443f", 224, 253},
    {"OrbiPom/level-7.png", "b865cdf42d87da05e8a211d03d93c63bb525dc47c3a2cc5defb680e5335283c2", 237, 266},
    {"OrbiPom/level-8.png", "2192bee10c754ecb9b17391336aa8b28e0cd9dc75076ac362dce572e2b7937b6", 257, 294},
    {"OrbiPom/level-9.png", "06571247101c34956052dee0215d1598b34865dc1996d914950d0f03fbef887f", 428, 312},
    {"OrbiPom/level-10.png", "b7f56f809f2e2df18648fbe69b26b674ae707b66716bd5949e9c1b42960cae17", 307, 310},
    {"OrbiPom/level-11.png", "3f44a0dad2c7318f5d90c28493434fab7f7b58e558c8fe9026fe9c0aa0e39db4", 376, 393},
    {"OrbiPom/skill-clear.png", "eced5a3bbfbe94e2a819ce4e2ce95d1fd3ad4e928dba8bd69cd589389fb24474", 115, 116},
    {"OrbiPom/skill-wind.png", "23321a1657ab729b813af7b469fe554f71cc44628dfbe3610b3296e1bc569196", 116, 116},
    {"OrbiPom/skill-shake.png", "1a20d4ec832c78946615d916454e35ca89a58dd5f838f54613d16b8d24589c5b", 116, 116},
    {"OrbiPom/skill-swap.png", "741a23bb6f0716ef33eacf22ddcb32c5f775589c8cd1012a7571c2a3fbcb6985", 116, 115}}};
d::ImageSource source(const OrbiPomAsset& a) { return {d::ImageSource::Kind::asset, std::string(a.file), std::string(a.sha256), 0}; }
constexpr std::array<OrbiPomSkill, 4> allSkills{OrbiPomSkill::clear, OrbiPomSkill::wind, OrbiPomSkill::shake, OrbiPomSkill::swap};
std::size_t skillIndex(OrbiPomSkill s) noexcept { return std::size_t(s); }
bool contains(Rect r, Point p) noexcept { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
// OrbiPomCanvas.skillName / skillIcon (:500-511).
std::pair<std::string_view, std::string_view> skillName(OrbiPomSkill s) {
    switch (s) {
    case OrbiPomSkill::clear: return {"Eliminate", "消除"};
    case OrbiPomSkill::wind: return {"Wind", "风场"};
    case OrbiPomSkill::shake: return {"Shake", "震动"};
    case OrbiPomSkill::swap: break;
    }
    return {"Swap", "交换"};
}
d::Icon skillIcon(OrbiPomSkill s) {
    switch (s) {
    case OrbiPomSkill::clear: return d::Icon::trash;
    case OrbiPomSkill::wind: return d::Icon::right;
    case OrbiPomSkill::shake: return d::Icon::refresh;
    case OrbiPomSkill::swap: break;
    }
    return d::Icon::reset;
}
std::optional<OrbiPomSkill> skillNamed(std::string_view id) {
    for (std::size_t i = 0; i < OrbiPomCanvas::skillIDs.size(); ++i) if (OrbiPomCanvas::skillIDs[i] == id) return allSkills[i];
    return std::nullopt;
}
// The board stays dark in both themes: its overlay title keeps light ink (:513-514).
d::TextStyle overlayTitle(const d::Palette& p) { return d::type::emptyTitle(p).sized(22).colored(d::fixed::microTagText); }
// String(format: "%.1f", max(0, seconds)).
std::string oneDecimal(double seconds) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", std::max(0.0, seconds));
    return buffer;
}
d::Layer& textLayer(d::Builder& b, std::string_view id, std::string value, const d::TextStyle& style, Rect frame,
    d::TextAlignment alignment = d::TextAlignment::left) {
    return d::makeText(b, id, std::move(value), style, frame, alignment);
}
}

std::span<const OrbiPomAsset> orbiPomAssets() noexcept { return assets; }
d::ImageSource orbiPomLevelImage(int level) noexcept { return source(assets[std::size_t(std::clamp(level, 1, 11) - 1)]); }
d::ImageSource orbiPomSkillImage(OrbiPomSkill s) noexcept { return source(assets[11 + skillIndex(s)]); }

double OrbiPomCanvas::pieceSide(int level) noexcept {
    constexpr std::array<double, 11> sizes{24, 32, 40, 56, 64, 72, 84, 96, 128, 160, 192};
    return sizes[std::size_t(std::clamp(level, 1, 11) - 1)] * .8 * unit;
}
Point OrbiPomCanvas::world(Point p) noexcept { return {(p.x - board.x) / unit, (p.y - board.y) / unit - dropZone}; }
Point OrbiPomCanvas::boardPoint(double x, double y) noexcept { return {x * unit, (y + dropZone) * unit}; }

// OrbiPomPiece (:756-811): one piece on the board, the drop ghost or the NEXT
// preview: the level's original artwork fitted to its sprite box; a skill
// target is marked by a yellow 1.5 px ring on the level's collision circle.
// The container is placed numerically (position, rotation, scale, opacity);
// the image is set once per level and size.
class OrbiPomCanvas::Piece {
public:
    // The original collision circle per level: radius and centre as fractions of the sprite box.
    struct Circle { double r, x, y; };
    static constexpr std::array<Circle, 11> circles{{
        {.4136, .4970, .6078}, {.4185, .4976, .5897}, {.3965, .4908, .6011}, {.4546, .5463, .5289},
        {.4425, .5064, .5464}, {.4373, .4904, .5411}, {.4374, .4956, .5639}, {.4410, .5012, .5516},
        {.3287, .4991, .5464}, {.4510, .4942, .5473}, {.4451, .4895, .5491}}};
    // The sprite box of a level in board px.
    static double side(int level) noexcept { return pieceSide(level); }

    Piece(d::Builder& b, std::string id, bool partRoot) : env_(&b.env) {
        layer_ = &b.layer(id);
        art_ = &b.layer(id + ".art");
        mark_ = &b.layer(id + ".mark");
        layer_->setAnimatable();                    // placed per tick
        if (partRoot) layer_->setPartRoot(true);    // a body is its own part: a new body loads only itself
        mark_->setAnimatable(d::SurfaceRole::lazy); // shown while marked (isHidden on the Mac)
        mark_->setValue(Channel::opacity, 0);
        layer_->add(*art_); layer_->add(*mark_);
    }
    d::Layer& layer() const noexcept { return *layer_; }
    void configure(int level, double side) {
        if (configured_ && configured_->first == level && configured_->second == side) return;
        configured_ = {level, side};
        const auto circle = circles[std::size_t(std::clamp(level, 1, 11) - 1)];
        const double radius = side * circle.r;
        const Rect bounds{0, 0, side, side};
        layer_->setBounds({side, side});
        art_->setFrame(bounds);
        art_->setContent(d::image(art_->id(), bounds, orbiPomLevelImage(level), "resizeAspect"));
        mark_->setFrame(bounds);
        d::StrokeStyle stroke; stroke.width = 1.5;
        mark_->setContent(d::shape(mark_->id(), bounds, d::PathBuilder().ellipse({side * circle.x - radius, side * circle.y - radius, radius * 2, radius * 2}),
            std::nullopt, env_->palette.yellow(), stroke));
    }
    void place(Point point, double angle, double scale, double opacity) {
        layer_->setPosition(point);
        layer_->setValue(Channel::rotation, angle);
        layer_->setValue(Channel::scaleX, scale); layer_->setValue(Channel::scaleY, scale);
        layer_->setValue(Channel::opacity, opacity);
    }
    void setMarked(bool v) { if (v != marked_) { marked_ = v; mark_->setValue(Channel::opacity, v ? 1 : 0); } }
    bool marked() const noexcept { return marked_; }
    std::optional<OrbiPomBody> rendered;          // the body last placed (renderedBodies)
    std::uint64_t seen{};                         // sweep generation of the last tick that listed it
private:
    d::Environment* env_;
    d::Layer *layer_, *art_, *mark_;
    std::optional<std::pair<int, double>> configured_;
    bool marked_{};
};

// OrbiPomSkillButton (:813-860): one row of the column: light chamfered plate
// (8), the original skill icon, the 500 12.5 px label and the cost at the
// right; yellow while its target is being chosen.
class OrbiPomCanvas::SkillButton final : public d::Control {
public:
    SkillButton(d::Builder& b, std::string id, std::string title, OrbiPomSkill skill, d::Size size)
        : Control(b.env, id, title, b.layer(id)) {
        auto& l = layer();
        const Rect bounds{0, 0, size.width, size.height};
        const auto shape = d::chamfer(bounds, 8);
        constexpr double iconSide = 22, costWidth = 30, left = 8 + iconSide + 8;
        l.setBounds(size);
        l.setAnimatable();                              // hover lift, press scale, disabled .4
        plate_ = &b.layer(id + ".plate");
        plate_->setFrame(bounds);
        brighten_ = &b.layer(id + ".brighten");
        brighten_->setFrame(bounds);
        d::setPath(*brighten_, shape, d::white(.07));
        brighten_->setAnimatable(d::SurfaceRole::lazy);
        brighten_->setValue(Channel::opacity, 0);
        auto& picture = b.layer(id + ".icon");
        picture.setFrame({8, (size.height - iconSide) / 2, iconSide, iconSide});
        picture.setContent(d::image(picture.id(), d::localBounds(picture), orbiPomSkillImage(skill), "resizeAspect"));
        auto& name = textLayer(b, id + ".title", std::move(title), d::TextStyle{12.5, d::FontWeight::medium, d::fixed::nearBlack},
            {left, (size.height - 12.5) / 2, size.width - left - costWidth - 10, 12.5});
        cost_ = &textLayer(b, id + ".cost", "", d::TextStyle{10, d::FontWeight::bold, d::rgb(0x666666), 1, 0, false, true},
            {size.width - 10 - costWidth, (size.height - 10) / 2, costWidth, 10}, d::TextAlignment::right);
        for (d::Layer* child : {plate_, brighten_, &picture, &name, cost_}) l.add(*child);
        renderPlate();
        setTarget(bounds, shape);
    }
    const std::string& cost() const noexcept { return costValue_; }
    void setCost(std::string v) {
        if (v == costValue_) return;
        costValue_ = std::move(v);
        d::editText(*cost_, [&](d::TextBlock& block) { block.setText(costValue_); });
    }
    bool isOn() const noexcept { return on_; }
    void setOn(bool v) { if (v != on_) { on_ = v; renderPlate(); } }
    void hoverChanged() override { d::applyHoverMotion(*this, 2, 1, .965, brighten_); }
    void pressChanged() override { d::applyHoverMotion(*this, 2, 1, .965, brighten_); }
    void enabledChanged() override { layer().setValue(Channel::opacity, enabled() ? 1 : .4); hoverChanged(); }
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::toggle; }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::toggle);
        item.on = on_;
        out.push_back(std::move(item));
    }
private:
    d::Layer *plate_, *brighten_, *cost_;
    std::string costValue_;
    bool on_{};
    void renderPlate() {
        const auto colors = on_ ? std::vector<d::Color>{palette().accent().primaryButton().begin(), palette().accent().primaryButton().end()}
                                : std::vector<d::Color>{d::fixed::lightButton.begin(), d::fixed::lightButton.end()};
        plate_->setContent(d::plate(plate_->id(), d::localBounds(*plate_), colors, {}, 8));
    }
};

// The "?" instructions (OrbiPomGuide, :603-723; spec section 2.17 pop-over,
// two columns): the goal, the controls and the lose line on the left; scoring,
// SP and the skills on the right. Every rule is the original controller's
// (orbipom.js): merges score 1, 3, 6 ... 66, 12 merges give 1 SP up to 3, a 5 s
// countdown above the red line, the skill costs and Swap's charge from SP
// spent on the other skills.
namespace {
class GuideColumn {
public:
    // Without a builder the column only measures (the pop-over's height is
    // the taller column; it is made before its content).
    GuideColumn(d::Environment& env, d::Builder* b, std::string id, double width) : env_(&env), b_(b), id_(std::move(id)), width_(width) {
        if (b_) layer_ = &b_->layer(id_);
    }
    d::Layer& layer() const noexcept { return *layer_; }
    double height() const noexcept { return height_; }
    static d::TextStyle body(const d::Palette& p) { return {11.5, d::FontWeight::regular, p.token(d::ThemeToken::menuText), 1.45}; }
    static d::TextStyle keyStyle(const d::Palette& p) { return {11.5, d::FontWeight::regular, p.token(d::ThemeToken::steel), 1.45}; }
    static d::TextStyle detail(const d::Palette& p) { return {11, d::FontWeight::regular, p.token(d::ThemeToken::description), 1.4}; }
    static constexpr double keyWidth = 76, iconSide = 22;
    void heading(std::string value) {
        if (height_ > 0) height_ += 14;
        add(std::move(value), d::type::label(palette()).colored(palette().accentInk()), {0, height_, width_, 10});
        height_ += 10 + 7;
    }
    void paragraph(std::string value) {
        const double lines = linesHeight(value, body(palette()), width_);
        add(std::move(value), body(palette()), {0, height_, width_, lines});
        height_ += lines;
    }
    void key(std::string name, std::string value) {
        const double valueWidth = width_ - keyWidth - 6;
        const double nameHeight = linesHeight(name, keyStyle(palette()), keyWidth), valueHeight = linesHeight(value, body(palette()), valueWidth);
        add(std::move(name), keyStyle(palette()), {0, height_, keyWidth, nameHeight});
        add(std::move(value), body(palette()), {keyWidth + 6, height_, valueWidth, valueHeight});
        height_ += std::max(nameHeight, valueHeight) + 2;
    }
    // The skill's icon, name and cost on one line, what it does under them.
    void skill(OrbiPomSkill s, std::string name, std::string cost, std::string what) {
        height_ += 8;
        const double left = iconSide + 8, textWidth = width_ - left, top = height_;
        if (b_) {
            auto& icon = b_->layer(id_ + ".icon." + std::to_string(skillIndex(s)));
            icon.setFrame({0, top, iconSide, iconSide});
            icon.setContent(d::image(icon.id(), d::localBounds(icon), orbiPomSkillImage(s), "resizeAspect"));
            layer_->add(icon);
        }
        add(std::move(name), d::TextStyle{12, d::FontWeight::medium, palette().token(d::ThemeToken::textOnDark)}, {left, top + 1, textWidth - 80, 12});
        add(uppercase(cost), d::TextStyle{10, d::FontWeight::bold, palette().accentInk(), 1, 0, false, true}, {width_ - 80, top + 2, 80, 10}, d::TextAlignment::right);
        const double lines = linesHeight(what, detail(palette()), textWidth);
        add(std::move(what), detail(palette()), {left, top + 17, textWidth, lines});
        height_ = top + std::max(iconSide, 17 + lines);
    }
private:
    d::Environment* env_;
    d::Builder* b_;
    std::string id_;
    double width_, height_{};
    d::Layer* layer_{};
    std::size_t serial_{};
    const d::Palette& palette() const noexcept { return env_->palette; }
    std::string uppercase(std::string_view v) const { return env_->text.uppercase ? env_->text.uppercase(v) : d::asciiUppercase(v); }
    void add(std::string value, const d::TextStyle& style, Rect frame, d::TextAlignment alignment = d::TextAlignment::left) {
        if (!b_) return;
        d::TextBlock block(std::move(value), style, alignment);
        block.maximumLines = 0; block.truncates = false;
        block.setLineFrame(frame);
        auto& label = b_->layer(id_ + ".text." + std::to_string(serial_++));
        label.setText(std::move(block));
        layer_->add(label);
    }
    // The wrapped height of `text` at `width` (Column.height, :717-721).
    double linesHeight(const std::string& value, const d::TextStyle& style, double width) const {
        const d::TextRun run{value, style};
        const auto lines = env_->text.lineCount(std::span<const d::TextRun>(&run, 1), width, 0);
        return double(std::max<std::size_t>(1, lines)) * std::ceil(style.size * style.lineHeight);
    }
};
}


OrbiPomCanvas::OrbiPomCanvas(d::Context& context, OrbiPomSession& session, std::function<bool()> foreground)
    : Canvas(core::Module::minigame, context), session_(&session), foregroundProvider_(std::move(foreground)) {}
// deinit: the retained session pauses (and saves its best) when the canvas goes.
OrbiPomCanvas::~OrbiPomCanvas() {
    try { session_->pause(true); } catch (...) { /* Owner persistence callbacks must not throw during retirement. */ }
}

// --- Lifecycle and clock (:95-155) ----------------------------------------------------
void OrbiPomCanvas::didActivate() {
    foreground_ = foregroundProvider_ ? foregroundProvider_() : true;
    if (!session_->hasRuntime() || session_->snapshot().state == "idle") startGame(false);
    lastSkillUses_ = session_->snapshot().skillUseCount; lastState_ = session_->snapshot().state;
    if (session_->error()) showError();
    reconcileClock();
}
void OrbiPomCanvas::didDeactivate() {
    pointerPressed_ = false;
    if (restartConfirmation_) { restartConfirmation_ = false; context().overlay().dismissConfirm(); }
    reconcileClock();
}
void OrbiPomCanvas::willLeave() { pointerPressed_ = false; stopClock(); session_->pause(true); }
void OrbiPomCanvas::setForeground(bool value, double now) {
    env().now = std::max(env().now, now);
    foreground_ = value;
    reconcileClock();
}
// NSWorkspace willSleep (foreground false) / didWake (the provider again).
void OrbiPomCanvas::systemSuspended(bool suspended, double now) {
    setForeground(suspended ? false : (foregroundProvider_ ? foregroundProvider_() : true), now);
}
void OrbiPomCanvas::setLowPowerVisualMode(bool value, double now) {
    if (value == lowPower_) return;
    env().now = std::max(env().now, now);
    lowPower_ = value;
    reconcileClock();
}
bool OrbiPomCanvas::shouldRun() const noexcept {
    return isActive() && foreground_ && !session_->manuallyPaused() && !restartConfirmation_ && !rulesPresented_ && !session_->error();
}
// reconcileClock (:118-136): the session pauses whenever the clock may not
// run; the tick runs while playing (an interval change restarts it).
void OrbiPomCanvas::reconcileClock() {
    const bool run = shouldRun();
    if (session_->hasRuntime() && session_->snapshot().paused == run) session_->pause(!run);
    const bool wantsTimer = run && session_->snapshot().isPlaying() && automaticallySchedulesFrames;
    if (!wantsTimer || (timer_ && std::abs(timerInterval_ - frameInterval()) > .001)) stopClock();
    if (wantsTimer && !timer_) { timer_ = now(); timerInterval_ = frameInterval(); }
    render();
}
std::optional<double> OrbiPomCanvas::nextWakeTime(double) const {
    if (!timer_) return std::nullopt;
    return *timer_ + timerInterval_;
}
void OrbiPomCanvas::wake(double now) {
    env().now = std::max(env().now, now);
    if (!timer_) return;
    const double elapsed = now - *timer_;
    timer_ = now;
    advance(elapsed);
}
void OrbiPomCanvas::advance(double seconds) {
    if (!shouldRun()) return;
    session_->advance(seconds);
    render();
    if (!session_->snapshot().canAdvance() || session_->error()) reconcileClock();
}
void OrbiPomCanvas::startGame(bool announceGame) {
    (void)session_->start();
    lastSkillUses_ = 0; lastState_ = session_->snapshot().state; lastSkill_.reset();
    if (session_->error()) showError();
    if (announceGame) toast(d::Icon::reset, "New game", "新的一局");
    reconcileClock();
}
void OrbiPomCanvas::showError() { toast(d::Icon::warn, "Notice", "提示", session_->error().value_or("")); }

// --- Actions (:157-223) -----------------------------------------------------------------
void OrbiPomCanvas::perform(std::string_view id) {
    if (!isActive()) return;
    if (id == "start") startGame(false);
    else if (id == "pause") togglePause();
    else if (id == "rules") { if (rulesPresented_) closePopover(); else openRules(); }
    else if (id == "restart") requestRestart();
    else if (id == "cancelRestart") {
        if (!restartConfirmation_) return;
        restartConfirmation_ = false; context().overlay().dismissConfirm(); reconcileClock();
    } else if (id == "confirmRestart") {
        if (restartConfirmation_) { restartConfirmation_ = false; context().overlay().dismissConfirm(); }
        startGame(true);
    } else if (id == "cancelSkill") { session_->cancelSkill(); render(); }
    else if (const auto skill = skillNamed(id)) {
        if (session_->snapshot().skill == skill) { session_->cancelSkill(); render(); return; }
        if (!session_->snapshot().canUse(*skill)) return;
        if (session_->activate(*skill)) lastSkill_ = skill;
        render();
    }
}
void OrbiPomCanvas::togglePause() {
    if (!session_->snapshot().isPlaying()) return;
    session_->setManuallyPaused(!session_->manuallyPaused());
    reconcileClock();
}
void OrbiPomCanvas::requestRestart() {
    if (!session_->snapshot().isPlaying()) { startGame(true); return; }
    restartConfirmation_ = true;
    reconcileClock();
    d::ConfirmRequest request;
    request.id = "minigameRestart";
    request.title = text("Restart the game?", "重新开始游戏？");
    request.message = text("The current score is lost.", "当前得分将会丢失。");
    request.confirmTitle = text("Restart", "重新开始");
    request.icon = d::Icon::reset;
    confirm(std::move(request), [this] { restartConfirmation_ = false; startGame(true); },
        [this] { restartConfirmation_ = false; reconcileClock(); });
}

// The "?" toggles the instructions (Esc, its close button or a press outside
// close them too); play holds while they are open (:189-205).
void OrbiPomCanvas::openRules() {
    const auto& p = env().palette;
    constexpr double width = 566, gutter = 20, columnWidth = (width - 24 - gutter) / 2;
    const auto uppercase = [&](std::string_view v) { return env().text.uppercase ? env().text.uppercase(v) : d::asciiUppercase(v); };
    // Columns are measured first (the pop-over's height is the taller one),
    // then filled into the pop-over's content.
    const auto lay = [&](d::Builder* b, d::Layer* content) {
        GuideColumn left(env(), b, "orbipom.guide.left", columnWidth), right(env(), b, "orbipom.guide.right", columnWidth);
        left.heading(text("Goal", "目标"));
        left.paragraph(text("Drop pieces into the box. Two identical pieces that touch merge into the next, larger piece. Two of the largest (level 11) merge and vanish.",
            "将山团团投入容器。两个相同的山团团接触后，会合成更大一级的山团团。两个最大的山团团（第 11 级）合成后会消失。"));
        left.heading(text("Controls", "操作"));
        left.key(text("Aim", "瞄准"), text("Move the pointer over the box, or the arrow keys", "在容器上移动指针，或使用方向键"));
        left.key(text("Drop", "投放"), text("Click, or Return", "点击，或按 Return 键"));
        left.key(text("Skill", "技能"), text("Click a skill, or 1–4", "点击技能，或按 1–4"));
        left.key(text("Cancel skill", "取消技能"), text("Esc, or click the skill again", "按 Esc，或再次点击该技能"));
        left.key(text("Pause", "暂停"), text("Space or P", "空格键或 P"));
        left.heading(text("Game over", "游戏结束"));
        left.paragraph(text("Pieces resting above the red dashed line start a 5-second countdown. Get them back below the line in time, or the game ends.",
            "山团团停在红色虚线上方时，会开始 5 秒倒计时。倒计时结束前让它们回到线下，否则游戏结束。"));
        right.heading(text("Score", "计分"));
        right.paragraph(text("Bigger merges score more: 1, 3, 6, 10 … up to 66 points. New pieces come from the five smallest sizes; NEXT shows the one after the current piece. BEST keeps your highest score.",
            "合成的山团团越大，得分越高：1、3、6、10……最高 66 分。新的山团团来自最小的五种；NEXT 显示当前之后的一个。BEST 记录最高分。"));
        right.heading(text("SP and skills", "能量与技能"));
        right.paragraph(text("Each merge fills one SP cell; 12 cells give 1 SP, up to 3. Choose a skill, then click a piece or the box to use it.",
            "每次合成填充一格能量，12 格积满获得 1 点 SP，最多 3 点。选择技能后，点击山团团或容器即可发动。"));
        const std::array<std::string, 4> costs{text("1 SP", "1 SP"), text("2 SP", "2 SP"), text("3 SP", "3 SP"), text("6 SP spent", "消耗 6 SP")};
        const std::array<std::string, 4> details{text("Removes the piece you pick.", "移除选中的山团团。"),
            text("A rising wind fills the box for a few seconds and lifts the pieces.", "上升气流充满容器数秒，托起山团团。"),
            text("Shakes the box from side to side.", "左右摇晃容器。"),
            text("Pick two pieces to trade places. It charges with the SP you spend on the other skills.", "选择两个山团团交换位置。在其他技能上消耗 SP 即可充能。")};
        for (std::size_t i = 0; i < allSkills.size(); ++i) {
            const auto name = skillName(allSkills[i]);
            right.skill(allSkills[i], text(name.first, name.second), costs[i], details[i]);
        }
        const double height = std::max(left.height(), right.height());
        if (!b) return height;
        left.layer().setFrame({0, 0, columnWidth, left.height()});
        right.layer().setFrame({columnWidth + gutter, 0, columnWidth, right.height()});
        auto& rule = b->layer("orbipom.guide.rule");
        rule.setFrame({columnWidth + gutter / 2 - .5, 2, 1, height - 4});
        d::setBackground(rule, p.token(d::ThemeToken::separator));
        content->add(left.layer()); content->add(rule); content->add(right.layer());
        return height;
    };
    auto popover = std::make_unique<d::Popover>(env(), uppercase(text("How to play", "玩法说明")), std::nullopt, width, lay(nullptr, nullptr));
    (void)lay(&popover->builder(), &popover->content());
    openPopover(std::move(popover), rulesButton_, std::nullopt, {});
    rulesPresented_ = true; pointerPressed_ = false;
    if (rulesButton_) rulesButton_->setOn(true);
    reconcileClock();
}
// closePopover override (:199-205): however the pop-over closed.
void OrbiPomCanvas::didClosePopover() {
    if (!rulesPresented_) return;
    rulesPresented_ = false;
    if (rulesButton_) rulesButton_->setOn(false);
    reconcileClock();
}

// --- Pointer and keys (:225-301) ------------------------------------------------------------
// The original only aims and drops inside its container; the drop zone above
// it counts as its top edge unless a skill is choosing a target.
Point OrbiPomCanvas::gamePoint(Point point) const noexcept {
    const Point p = world(point);
    if (session_->snapshot().skill) return p;
    return {p.x, std::min(worldHeight, std::max(0.0, p.y))};
}
void OrbiPomCanvas::aim(Point point) {
    if (restartConfirmation_) return;
    const Point p = gamePoint(point);
    keyboardPoint_ = p;
    session_->move(p);
    render();
}
void OrbiPomCanvas::handlePointerMoved(std::optional<Point> point) {
    if (!point || !contains(board, *point)) return;
    aim(*point);
}
bool OrbiPomCanvas::handlePointerDown(Point point, const d::PointerInput&) {
    if (!contains(board, point)) return false;
    pointerPressed_ = !restartConfirmation_;
    aim(point);
    return true;
}
void OrbiPomCanvas::handlePointerDragged(Point point) { if (pointerPressed_ && contains(board, point)) aim(point); }
void OrbiPomCanvas::handlePointerUp(Point point) {
    const bool pressed = pointerPressed_;
    pointerPressed_ = false;
    if (!pressed || !contains(board, point) || restartConfirmation_) return;
    session_->pointerUp(gamePoint(point));
    render();
}
// Space pauses (spec section 1.12); Return drops at the keyboard aim, the
// arrows aim, 1-4 pick a skill, P pauses, Esc cancels a selected skill.
bool OrbiPomCanvas::handleKey(const d::KeyInput& key) {
    if (key.commandModifiers()) return false;
    const auto& s = session_->snapshot();
    switch (key.virtualKey) {
    case d::key::space: if (!key.repeat) togglePause(); return true;
    case d::key::escape:
        if (!s.skill) return false;
        perform("cancelSkill"); return true;
    case d::key::enter:
        if (!s.canAdvance()) return true;
        if (s.skill) session_->pointerUp(keyboardPoint_); else (void)session_->drop();
        render(); return true;
    case d::key::left: case d::key::right: case d::key::down: case d::key::up:
        if (!s.skill) keyboardPoint_.x = s.previewX;
        if (key.virtualKey == d::key::left) keyboardPoint_.x -= 6;
        else if (key.virtualKey == d::key::right) keyboardPoint_.x += 6;
        else if (key.virtualKey == d::key::down) keyboardPoint_.y += 6;
        else keyboardPoint_.y -= 6;
        keyboardPoint_.x = std::min(worldWidth, std::max(0.0, keyboardPoint_.x));
        keyboardPoint_.y = std::min(worldHeight, std::max(-dropZone, keyboardPoint_.y));
        session_->move(keyboardPoint_); render();
        return true;
    default: break;
    }
    const char32_t c = key.character >= U'A' && key.character <= U'Z' ? key.character + 32 : key.character;
    if (c == U'p') { if (!key.repeat) togglePause(); return true; }
    if (c >= U'1' && c <= U'4') {
        if (!key.repeat) perform(skillIDs[std::size_t(c - U'1')]);
        return true;
    }
    return false;
}

// --- Page (:303-448) ------------------------------------------------------------------------
void OrbiPomCanvas::buildPage(d::Page& page) {
    bodies_.clear(); pool_.clear(); ghostOwned_.reset(); nextOwned_.reset(); rendered_.reset(); skillButtons_ = {}; dangerShown_ = false; dangerPulse_.reset(); dangerCounting_.reset();
    auto& rules = page.make<d::IconButton>("rules", text("How to play", "玩法说明"), d::Icon::help, d::IconButton::Style::dark, true);
    rules.onActivate = [this] { perform("rules"); };
    rules.setOn(rulesPresented_);
    rulesButton_ = &rules;
    header().configure({"MINIGAME"}, "MOD-06", title("Minigame", "小游戏"), "SCORE", false, {&rules});
    buildBoard(page);
    buildColumn(page);
    render();
}
void OrbiPomCanvas::buildBoard(d::Page& page) {
    const auto& p = env().palette;
    auto& b = page.builder();
    const Rect bounds{0, 0, board.width, board.height};
    auto& face = page.makeLayer("orbipom.board");
    face.setFrame(board); face.setMasksToBounds(true);
    d::setBackground(face, d::rgb(0x141414));
    auto& grid = page.makeLayer("orbipom.grid");
    grid.setFrame(bounds);
    d::setPath(grid, d::tickLadder(board.width, board.height, 20), d::white(.06));
    // The wind field: the Mac redraws a rect from the wind surface to the
    // bottom; here one full-height surface follows the surface numerically
    // (its overflow below the board is clipped) and the bottom edge's inner
    // half is the static line under it.
    wind_ = &page.makeLayer("orbipom.wind");
    wind_->setAnimatable();
    wind_->setFrame(bounds);
    wind_->setContent(d::shape(wind_->id(), bounds, d::PathBuilder().rect(bounds), d::rgb(0x22A6F2, .10), d::rgb(0x22A6F2, .45)));
    wind_->setHidden(true);
    windEdge_ = &page.makeLayer("orbipom.wind.edge");
    windEdge_->setAnimatable();
    windEdge_->setFrame({0, board.height - .5, board.width, .5});
    d::setBackground(*windEdge_, d::rgb(0x22A6F2, .45));
    windEdge_->setHidden(true);
    guide_ = &page.makeLayer("orbipom.guide");
    guide_->setAnimatable();
    guide_->setFrame({-.5, 20, 1, board.height - 20});
    d::setBackground(*guide_, d::withAlpha(p.yellow(), .25));
    guide_->setHidden(true);
    // The red line (always shown): white-authored, its colour is the tint so
    // the countdown pulse is a number per tick.
    danger_ = &page.makeLayer("orbipom.danger");
    danger_->setAnimatable();
    danger_->setFrame(bounds);
    pieces_ = &page.makeLayer("orbipom.pieces");
    pieces_->setFrame(bounds);
    pieces_->setPartRoot(true);
    ghostOwned_ = std::make_unique<Piece>(b, "orbipom.ghost", false);
    ghost_ = ghostOwned_.get();
    ghost_->layer().setValue(Channel::opacity, .5); ghost_->layer().setHidden(true);
    countdown_ = &textLayer(b, "orbipom.countdown", "", d::TextStyle{15, d::FontWeight::bold, d::fixed::red, 1, 0, false, true},
        {board.width - 6 - 80, 22, 80, 15}, d::TextAlignment::right);
    countdown_->setHidden(true);
    overlay_ = &page.makeLayer("orbipom.overlay");
    overlay_->setAnimatable();
    overlay_->setFrame(bounds);
    d::setBackground(*overlay_, d::black(.6));
    overlay_->setHidden(true);
    overlayText_ = &textLayer(b, "orbipom.overlay.text", "", overlayTitle(p), {0, (board.height - 22) / 2, board.width, 22}, d::TextAlignment::center);
    overlay_->add(*overlayText_);
    auto& ring = page.makeLayer("orbipom.ring");
    ring.setFrame(bounds);
    d::setBorder(ring, std::nullopt, p.token(d::ThemeToken::ringField), 1);
    for (d::Layer* layer : {&grid, wind_, windEdge_, guide_, danger_, pieces_, &ghost_->layer(), countdown_, overlay_, &ring}) face.add(*layer);
    page.layer().add(face);
    page.stagger(face);
    renderDanger(std::nullopt, false);
}
// The HUD column (spec section 3.11), narrowed to the board's right: SCORE,
// NEXT, SP, the four skills stacked, then Pause with an icon-only Restart.
void OrbiPomCanvas::buildColumn(d::Page& page) {
    const auto& p = env().palette;
    auto& b = page.builder();
    const double x = column.x, width = column.width;
    double y = column.y;
    auto& score = page.own<d::StatPlate>("orbipom.score", width, d::type::statValue(p).sized(32).weighted(d::FontWeight::light));
    score.setValueMinimumScale(.7);
    auto& best = textLayer(b, "orbipom.best", "", d::type::label(p), {52, 9, width - 64, 10}, d::TextAlignment::right);
    score.layer().setOrigin({x, y});
    score.layer().add(best);
    page.layer().add(score.layer());
    scorePlate_ = &score; bestText_ = &best;
    page.stagger(score.layer());
    y += score.layer().bounds().height + 6;

    auto& next = page.own<d::StatPlate>("orbipom.next", width, d::type::statValue(p).sized(32));
    auto& well = page.makeLayer("orbipom.next.well");
    nextOwned_ = std::make_unique<Piece>(b, "orbipom.next.piece", false);
    next.layer().setOrigin({x, y});
    well.setFrame({12, 22, width - 24, 38});
    well.add(nextOwned_->layer());
    next.layer().add(well);
    page.layer().add(next.layer());
    next.set("NEXT", "");
    nextWell_ = &well; nextPiece_ = nextOwned_.get();
    page.stagger(next.layer());
    y += next.layer().bounds().height + 6;

    auto& sp = page.makeLayer("orbipom.sp");
    auto& label = textLayer(b, "orbipom.sp.label", "SP", d::type::label(p), {0, 0, 60, 10});
    auto& energy = textLayer(b, "orbipom.sp.energy", "", d::type::label(p).colored(p.legible(d::fixed::cyan)), {width - 80, 0, 80, 10}, d::TextAlignment::right);
    auto& bar = page.own<d::Cells>("orbipom.sp.cells", 12, width, false, 10.);
    sp.setFrame({x, y, width, 26});
    bar.layer().setOrigin({0, 16});
    sp.add(label); sp.add(energy); sp.add(bar.layer());
    page.layer().add(sp);
    energyText_ = &energy; cells_ = &bar;
    page.stagger(sp);
    y += 26 + 8;

    constexpr double skillHeight = 34, skillGap = 6;
    auto& skills = page.makeLayer("orbipom.skills");
    const double count = double(allSkills.size());
    skills.setFrame({x, y, width, count * skillHeight + (count - 1) * skillGap});
    page.layer().add(skills);
    for (std::size_t index = 0; index < allSkills.size(); ++index) {
        const auto skill = allSkills[index];
        const auto name = skillName(skill);
        auto& button = page.make<SkillButton>(std::string(skillIDs[index]), text(name.first, name.second), skill, d::Size{width, skillHeight});
        button.onActivate = [this, index] { perform(skillIDs[index]); };
        button.layer().setOrigin({0, double(index) * (skillHeight + skillGap)});
        skills.add(button.layer());
        skillButtons_[index] = &button;
        page.add(button);
    }
    page.stagger(skills);
    y += skills.bounds().height + 8;

    auto& actions = page.makeLayer("orbipom.actions");
    auto& restart = page.make<d::IconButton>("restart", text("Restart", "重新开始"), d::Icon::reset, d::IconButton::Style::dark, false, false, 32.);
    const double flexible = width - 6 - 32;
    auto& pause = page.make<d::Button>("pause", text("Pause", "暂停"), d::Icon::pause, d::Button::Kind::primary);
    auto& resume = page.make<d::Button>("resume", text("Resume", "继续"), d::Icon::play, d::Button::Kind::primary);
    pause.place({0, 0}, flexible); resume.place({0, 0}, flexible);
    restart.layer().setOrigin({width - 32, 0});
    pause.onActivate = [this] { togglePause(); };
    resume.onActivate = [this] { togglePause(); };
    restart.onActivate = [this] { requestRestart(); };
    actions.setFrame({x, y, width, 32});
    actions.add(pause.layer()); actions.add(resume.layer()); actions.add(restart.layer());
    page.layer().add(actions);
    pauseButton_ = &pause; resumeButton_ = &resume;
    page.add(std::vector<d::Control*>{&pause, &resume, &restart});
    page.stagger(actions);
}

// --- Rendering (in place; the page is never rebuilt per tick, :450-484) ------------------------
bool OrbiPomCanvas::Rendered::sameControls(const Rendered& o) const noexcept {
    return state == o.state && paused == o.paused && manual == o.manual && confirm == o.confirm && controlsEnergy == o.controlsEnergy
        && charge == o.charge && skill == o.skill && phase == o.phase && error == o.error;
}
void OrbiPomCanvas::render() {
    if (!pieces_) return;
    const auto& s = session_->snapshot();
    announce(s);
    const auto best = std::max({session_->bestScore(), s.highScore, s.score});
    std::optional<std::string> overlay;
    if (session_->error()) overlay = "UNAVAILABLE";
    else if (s.state == "over") overlay = "GAME OVER";
    else if (session_->manuallyPaused() || restartConfirmation_) overlay = "PAUSED";
    const std::optional<std::string> danger = s.isPlaying() && s.dangerSeconds ? std::optional(oneDecimal(*s.dangerSeconds)) : std::nullopt;
    const int progress = s.energy >= 3 ? 12 : int(std::min(12.0, std::max(0.0, std::floor(s.energyProgress))));
    const auto previous = std::move(rendered_);
    rendered_ = Rendered{s.score, best, s.energy, progress, s.nextLevel, s.currentLevel, danger, overlay,
        s.state, s.paused, session_->manuallyPaused(), restartConfirmation_, s.energy, s.swapCharge, s.skill, s.skillPhase, session_->error()};
    if (!previous || previous->score != s.score) scorePlate_->set("SCORE", d::format::grouped(s.score));
    if (!previous || previous->best != best) d::editText(*bestText_, [&](d::TextBlock& block) { block.setText("BEST " + d::format::grouped(best)); });
    if (!previous || previous->energy != s.energy) d::editText(*energyText_, [&](d::TextBlock& block) { block.setText(d::format::pad2(s.energy) + "/03"); });
    if (!previous || previous->progress != progress || previous->energy != s.energy)
        cells_->set(progress, d::Cells::Tone{d::fixed::cyan, d::fixed::cyan}, false, previous.has_value());
    if ((!previous || previous->next != s.nextLevel) && nextWell_) {
        nextPiece_->configure(s.nextLevel, std::min(38.0, Piece::side(s.nextLevel)));
        nextPiece_->layer().setPosition({nextWell_->bounds().width / 2, nextWell_->bounds().height / 2});
    }
    if (!previous || previous->overlay != overlay) renderOverlay(overlay);
    if (!previous || previous->danger != danger || dangerShown_ != (danger && s.canAdvance())) renderDanger(danger, danger && s.canAdvance());
    if (!previous || !previous->sameControls(*rendered_)) renderControls(s);
    renderBoard(s, !previous || previous->ghost != s.currentLevel);
    renderPulse();
}
// Toasts for results: skills that were cast, and the end of a game (:486-498).
void OrbiPomCanvas::announce(const OrbiPomSnapshot& s) {
    if (s.skill) lastSkill_ = s.skill;
    if (s.skillUseCount > lastSkillUses_ && lastSkill_) {
        const auto name = skillName(*lastSkill_);
        toast(skillIcon(*lastSkill_), name.first, name.second, text("Skill activated", "技能已发动"));
    }
    lastSkillUses_ = s.skillUseCount;
    if (s.state == "over" && lastState_ == "playing") toast(d::Icon::x, "Game over", "游戏结束", d::format::grouped(s.score));
    lastState_ = s.state;
}
// `[ TITLE ]` with yellow brackets. The Mac's fade (opacity to 1 over .16 s)
// starts from the overlay's own opacity, which is already 1 (isHidden only
// changes): nothing fades there, and nothing does here (the same call).
void OrbiPomCanvas::renderOverlay(const std::optional<std::string>& title) {
    const auto& p = env().palette;
    overlay_->setHidden(!title);
    d::editText(*overlayText_, [&](d::TextBlock& block) {
        const auto style = overlayTitle(p);
        const auto bracket = style.colored(p.yellow());
        block.runs = {{"[ ", bracket}, {title.value_or(""), style}, {" ]", bracket}};
    });
    if (title) d::motion::animate(*overlay_, Channel::opacity, 1, .16, clock(), d::easing::ease);
}
// The red line pulses between #E5383B and #661B1D while a countdown runs (:525-543).
void OrbiPomCanvas::renderDanger(const std::optional<std::string>& value, bool pulsing) {
    dangerShown_ = pulsing;
    countdown_->setHidden(!value);
    d::editText(*countdown_, [&](d::TextBlock& block) { block.setText(value.value_or("")); });
    // The line changes only between its idle (dashed) and countdown (solid) styles.
    const bool counting = value.has_value();
    if (!dangerCounting_ || *dangerCounting_ != counting) dangerCounting_ = counting; else { pulse(pulsing); return; }
    const Rect bounds{0, 0, board.width, board.height};
    const double redLine = boardPoint(0, 8).y;
    d::StrokeStyle stroke;
    stroke.width = value ? 1.5 : 1;
    if (!value) stroke.dash = {5, 4};
    danger_->setContent(d::shape(danger_->id(), bounds, d::PathBuilder().move({0, redLine}).line({board.width, redLine}), std::nullopt, d::white(1), stroke));
    pulse(pulsing);
}
void OrbiPomCanvas::pulse(bool pulsing) {
    if (!pulsing || env().reduceMotion) {
        dangerPulse_.reset();
        danger_->setTint(*dangerCounting_ ? d::fixed::red : d::rgb(0xE5383B, .45));
        return;
    }
    if (!dangerPulse_) dangerPulse_ = now();
}
// The CABasicAnimation strokeColor red -> #661B1D, 1/3 s easeInOut,
// autoreverses, repeats forever: sampled at the tick (it runs only while the
// clock does).
void OrbiPomCanvas::renderPulse() {
    if (!dangerPulse_) return;
    const double half = 1. / 3, phase = std::fmod(std::max(0.0, now() - *dangerPulse_), 2 * half) / half;
    const double eased = d::easing::easeInOut.value(phase <= 1 ? phase : 2 - phase);
    const d::Color from = d::fixed::red, to = d::rgb(0x661B1D);
    danger_->setTint(d::Color{from.r + (to.r - from.r) * eased, from.g + (to.g - from.g) * eased, from.b + (to.b - from.b) * eased, 1});
}
void OrbiPomCanvas::renderControls(const OrbiPomSnapshot& s) {
    const bool paused = session_->manuallyPaused();
    pauseButton_->layer().setHidden(paused); resumeButton_->layer().setHidden(!paused);
    pauseButton_->setEnabled(s.isPlaying() && !restartConfirmation_);
    resumeButton_->setEnabled(s.isPlaying() && !restartConfirmation_);
    for (std::size_t index = 0; index < allSkills.size(); ++index) {
        const auto skill = allSkills[index];
        auto& button = *skillButtons_[index];
        button.setCost(skill == OrbiPomSkill::swap ? std::to_string(std::min(6, s.swapCharge)) + "/6" : std::to_string(orbiPomEnergyCost(skill)));
        button.setOn(s.skill == skill);
        button.setEnabled(!restartConfirmation_ && (s.skill == skill || s.canUse(skill)));
    }
}
OrbiPomCanvas::Piece& OrbiPomCanvas::piece(std::int64_t id, int level, double side) {
    // A released piece is reused before a new layer is made (the page arena
    // never shrinks); a game makes thousands of bodies.
    Piece* found{};
    for (auto& candidate : pool_) if (!candidate->layer().parent()) { found = candidate.get(); break; }
    if (!found) {
        pool_.push_back(std::make_unique<Piece>(page().builder(), "orbipom.piece." + std::to_string(pool_.size()), true));
        found = pool_.back().get();
    }
    found->rendered.reset();
    found->setMarked(false);
    found->configure(level, side);
    pieces_->add(found->layer());
    bodies_[id] = found;
    return *found;
}
void OrbiPomCanvas::renderBoard(const OrbiPomSnapshot& s, bool ghostChanged) {
    // Bodies that left go first (their layers are reused), then every body is
    // configured once and placed when it changed (:559-580).
    ++sweep_;
    for (const auto& body : s.bodies) if (const auto it = bodies_.find(body.id); it != bodies_.end()) it->second->seen = sweep_;
    for (auto it = bodies_.begin(); it != bodies_.end();) {
        if (it->second->seen == sweep_) { ++it; continue; }
        it->second->layer().removeFromParent();
        it = bodies_.erase(it);
    }
    for (const auto& body : s.bodies) {
        const auto it = bodies_.find(body.id);
        Piece& p = it != bodies_.end() ? *it->second : piece(body.id, body.level, body.size * unit);
        if (p.rendered != body) {
            p.place(boardPoint(body.x, body.y), body.angle, body.scale, body.opacity);
            p.rendered = body;
        }
        p.setMarked(std::find(s.selectedBodyIDs.begin(), s.selectedBodyIDs.end(), body.id) != s.selectedBodyIDs.end() || s.hoverBodyID == body.id);
    }
    const bool showGhost = s.previewVisible && s.canAdvance() && !s.skill;
    if (ghostChanged) ghost_->configure(s.currentLevel, s.previewSize * unit);
    ghost_->layer().setHidden(!showGhost);
    if (showGhost) ghost_->place(boardPoint(s.previewX, s.previewY), 0, s.previewScale, .5);
    guide_->setHidden(!showGhost);
    guide_->setPosition({boardPoint(s.previewX, 0).x, 20 + (board.height - 20) / 2});
    wind_->setHidden(!s.windSurfaceY);
    windEdge_->setHidden(!s.windSurfaceY || boardPoint(0, *s.windSurfaceY).y >= board.height);
    if (s.windSurfaceY) wind_->setPosition({board.width / 2, boardPoint(0, *s.windSurfaceY).y + board.height / 2});
}
} // namespace endfield::modules
