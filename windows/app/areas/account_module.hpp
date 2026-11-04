#pragma once
// Account Linking module area (cmake/modules/account.cmake registers it as
// "account"). The factory builds the one tools::AccountLinkingOwner of the
// application: the signed community client over WinHTTP, the DPAPI credential
// vault and owner-only Account\profile-cache.json below the versioned data
// root, the official WebView2 login (when the build has the SDK and the
// Evergreen runtime is installed), the Account page (modules::AccountCanvas,
// the Mac 1.3.0 HUDAccountCanvas, on the design library: a DesignOwnerCore
// over the HUD's one design Context, marked as a design screen) and the
// header sanity / Work Mode wallet with its recovery popover. The wallet and
// popover are composed through floatingEntries() above module content; the
// page is the presented Module::account. Profile sync reaches the Personal
// Profile area through AccountProfileLink (app/areas/account_profile_link.hpp);
// the daily boot log's SANITY line reads this area's controller
// (modules::boot::BootLogSources::sanity).
//
// UI Automation: the ModuleOwner hooks list one AutomationTree
// (native/hud_design_automation.hpp) under this owner's group of the HUD
// window's root provider: the page's accessibility items while it takes input
// (the Mac's HUDDesignCanvas.accessibilityItems: buttons, segments, pop-ups,
// toggles, field chips, an open menu's rows), then the wallet's stand-ins
// with the open popover's countdowns as the toggle's value. A UIA action
// performs the click's action and posts the area's registered message, so the
// host refreshes frames, deadlines and the tree exactly as after a routed
// click.
// Without the Evergreen WebView2 Runtime, Connect shows the localized
// explanation page linking to Microsoft's download page
// (native/hypergryph_account_webview.hpp); nothing is bundled or downloaded.
//
// Hidden coverage builds the same owner over synthetic providers (an
// in-process community host replaying oracle envelopes, an in-memory vault,
// a fake official-site host driving the real LoginSession policy, a private
// profile store in the area's coverage root) and a pinned synthetic wall
// clock: no socket, browser, DPAPI blob, real account or user profile is used.
//
// The factory lives in endfield::areas: app.cmake's generated table names it
// from inside namespace endfield::app, whose local `areas` table would make
// a qualified endfield::app::areas::... name ambiguous.
#include "app/application.hpp"
#include <memory>

namespace endfield::areas {
std::unique_ptr<app::ModuleOwner> accountModule(const app::ApplicationModuleContext&);
}
