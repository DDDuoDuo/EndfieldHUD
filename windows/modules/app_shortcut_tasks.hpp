#pragma once
#include "app/utility_executor.hpp"
#include "native/app_shortcut_service.hpp"

namespace endfield::modules {
enum class ShortcutTaskKind {load,inspect,edit,save,remove,launch};
struct ShortcutTask {
    std::uint64_t token{};ShortcutTaskKind kind{};
    std::optional<native::AppShortcutSelection> selection;
    std::optional<ShortcutCandidate> candidate;
    std::optional<ShortcutRecord> record;
    std::optional<std::string> editingID;
    std::string name,icon,newID;double createdAt{};
    std::uintptr_t launchOwner{};
};
struct ShortcutTaskResult {
    std::uint64_t token{};ShortcutTaskKind kind{};
    std::optional<ShortcutFile> file;
    std::optional<ShortcutCandidate> candidate;
    std::optional<native::AppShortcutLaunchReceipt> launched;
    std::optional<std::string> error;
};
// One borrowed FIFO owns repository and explicit OS transactions. submit keeps
// at most one immutable request if that FIFO is full; retry only on a completion
// event. Hiding may cancel reads/inspection, never an accepted write or launch.
// The caller dispatches launch only after the HUD closes. Destruction suppresses
// callbacks but lets already accepted work finish. Flush via the shared owner.
class AppShortcutTasks final {
public:
    using Completion=std::function<void(ShortcutTaskResult)>;
    AppShortcutTasks(app::UtilityExecutor&,std::filesystem::path,ShortcutTextRules,
                    native::AppShortcutOS,Completion);
    ~AppShortcutTasks();
    AppShortcutTasks(const AppShortcutTasks&)=delete;
    AppShortcutTasks&operator=(const AppShortcutTasks&)=delete;
    bool submit(ShortcutTask);
    bool cancel(std::uint64_t);
    void queueCapacityAvailable();
    bool busy()const noexcept;
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};
}
