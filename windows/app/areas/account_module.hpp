#pragma once
// Account Linking module area (cmake/modules/account.cmake registers it as
// "account"). The factory builds the one tools::AccountLinkingOwner of the
// application: the signed community client over WinHTTP, the DPAPI credential
// vault and owner-only Account\profile-cache.json below the versioned data
// root, the official WebView2 login (when the build has the SDK and the
// Evergreen runtime is installed), the Account module face and the header
// sanity / Work Mode wallet with its recovery popover. The wallet and popover
// are composed through floatingEntries() above module content; the face is the
// presented Module::account. Profile sync reaches the Personal Profile area
// through AccountProfileLink (app/areas/account_profile_link.hpp).
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
