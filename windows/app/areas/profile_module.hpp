#pragma once
// Personal Profile module area (app/module_owner.hpp, "Adding a module area").
//
// One ModuleOwner over tools::ProfileOwner, the production owner of the
// Personal Profile page (Module::profile), the bottom-left ID card content and
// the Work Mode lifetime hours (UserProfile.accumulatedWorkSeconds). It:
//   - reads and writes <data root>/Profile through the single ProfileService on
//     the shared UtilityExecutor (no second ProfileStore writer);
//   - observes the shared read-only Work Mode controller's absolute
//     checkpoints (WorkModeController::listenCheckpoints) and loads the stored
//     lifetime against whatever that controller restored at launch;
//   - chooses images through the host's shared file dialog
//     (ApplicationHost::chooseFiles) and the card colour through the modal
//     ChooseColorW picker (native/profile_pickers.hpp) on a registered message;
//   - records profileCropChanged in the Event Log;
//   - publishes native::IdCardFeed ("profile.idCard") for the host's
//     native::IdCardPresenter (native/id_card_presenter.hpp);
//   - attaches its HypergryphAccountController AccountProfileSink to the
//     Account Linking area's profile link ("account.profileLink",
//     app/areas/account_profile_link.hpp) when that area is built.
// Packaged input: resources/profile (ehud_app_resource id "profile"). Without
// it (development previews) the module stays absent. Hidden coverage runs on a
// synthetic record in the area's coverage root with injected choosers.
#include "app/application.hpp"
#include <memory>

namespace endfield::areas {
std::unique_ptr<app::ModuleOwner> profileModule(const app::ApplicationModuleContext&);
}
