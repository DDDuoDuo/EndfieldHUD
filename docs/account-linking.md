# Account linking (roadmap batch 10)

The original Chinese requirement remains unchanged in `implementation-roadmap.txt`. The user's 2026-10-05 request supersedes its older lock for Player ID and awakening date: both remain editable, and explicit local edits take priority over later syncs.

## Scope

- Mainland China: official 森空岛 session, Hypergryph account.
- Global: official SKPORT session, GRYPHLINE account.
- Region-separated credentials, roles and cached snapshots. Endfield card sync is opt-in; avatar sync is a separate opt-in. Mainland is the initial region.
- Endfield fields: role ID, name, account creation date (used for 苏醒日), authority/exploration levels, operator/weapon/document counts, and avatar when the service supplies a supported URL. Missing fields preserve local values.
- Header choices: Work Mode minutes, Endfield sanity, Arknights sanity, or hidden. Endfield is the initial game choice; unlinked accounts show Work Mode remaining/total minutes. Game choices only use roles actually returned by that region's binding service. A global Endfield session is not assumed to provide a mainland Arknights account.
- UID is additive: the original locally generated identity remains in the profile archive; the displayed game ID and the user's explicit override are separate optional fields. Name/levels/counters follow the selected Endfield role while profile sync is on. Tag, introduction, portrait, background, Player ID and awakening date remain editable.
- Selecting a personal portrait after enabling game-avatar sync turns that option off. Unlinking removes that region's Keychain session and game cache; the already-imported personal card stays local.

## Authentication and API evidence

This is an **unofficial integration**, not a registered Hypergryph OAuth application or a guaranteed public SDK. Authentication runs in an explicitly opened, ephemeral WebKit window on the official site. After consent, the app observes three specific community-session keys in owned, exact-community-origin main frames: `SK_OAUTH_CRED_KEY`, `SK_TOKEN_CACHE_KEY`, and the issued `.id` in `SK_SHUMEI_DEVICE_ID_KEY`. An approved child returning to that origin follows the same rule. It waits for complete signing/device context before closing sign-in. It never reads existing browser cookies, passwords, passport tokens or other applications' sessions. The adapter allows the official page’s phone/email login routes; external Google/Apple redirects currently show an unsupported-route message.

Fresh sign-in uses that complete issued pair directly for binding lookup
and authenticated self-profile retrieval. It no longer refreshes the signing
token before the first reads. This follows the shared SKLAND flow in both
[erzaozi/skland-plugin](https://github.com/erzaozi/skland-plugin/blob/09bc5e467eed3b72216e2f629d8d3e4cf6061d45/components/Code.js#L203)
and [fxquarter/astrbot_plugin_arknights_sanity](https://github.com/fxquarter/astrbot_plugin_arknights_sanity/blob/9bca538b17d914835cfa86b3f48c4daca24a8be3/api_process.py#L148).
Their mainland community appCode/type/kind match the official page's scope.
Existing scoped refresh remains available for resumed or aged sessions;
Endfield's selected role/server mapping is preserved. No passport-token
collection or third-party code is added.

Official sources inspected on 2026-10-05:

- [森空岛](https://www.skland.com/) and [SKPORT](https://www.skport.com/).
- [CN login implementation](https://bbs.hycdn.cn/skland-fe-static/skland-bbs/3158.cd61aa37.js), [CN credential refresh](https://bbs.hycdn.cn/skland-fe-static/skland-bbs/7905.3be0d11f.js).
- [Global login implementation](https://static.skport.com/skport-fe-static/skport-bbs/1436.25671ce5.js), [Global credential refresh](https://static.skport.com/skport-fe-static/skport-bbs/4994.95ba1c62.js).
- [Official Endfield card](https://game.skland.com/endfield/game-data). Exact profile/signature evidence and fixture coverage are recorded in `account-api-research.md`.
- [小黑盒 mini-app authentication](https://docs.xiaoheihe.cn/hb_sdk/guide/auth) describes Heybox identity, not a public Endfield profile API. [Its privacy policy](https://api.xiaoheihe.cn/account/privacy_introduce/) describes authorized game-account binding. No public primary evidence established how its Endfield integration is implemented; EndfieldHUD does not depend on or impersonate 小黑盒.

## Resource and lifecycle limits

The account controller is retained by the existing overlay owner. There is no account timer or background polling service. The existing visible HUD clock checks for an automatic refresh ten minutes after the last successful sample, only while the account panel or game sanity header is visible. This is an app scheduling policy, not a claimed publisher rate limit. Reopening with a fresh cached sample uses it without reading Keychain or making a request. Manual refresh has a five-second repeat guard. Bindings are reused for five minutes, signing material for twenty minutes, repeated failures back off up to fifteen minutes, and expired authentication requires reconnecting. Signing-token or clock errors get one bounded recovery attempt before surfacing failure; they are not mistaken for an expired community session. Closing the HUD cancels requests; stale callbacks cannot apply data. Explicit Keychain commits and unlink cleanup use separate per-region revisions, so closing cannot leave a deleted session usable or erase a newer login. Replacement logins clear the previous role identity before retrying bindings. Between requests, sanity and recovery countdowns are calculated locally from the saved snapshot: one point per 432 seconds for Endfield and 360 seconds for Arknights. The game configuration and the supplied reference video agree on Endfield’s interval. Response time anchors the calculation when the device clock differs from the server; elapsed sleep/closed time is handled on the next existing HUD tick. Missing recovery deadlines stay unknown, and purchased over-cap sanity is preserved. Spending sanity or gaining it in-game requires the next API refresh; local recovery is not a live game-state feed.

Only scoped community credentials, their signing key and issued device context enter Keychain (`WhenUnlockedThisDeviceOnly`, not synchronizable). Account JSON contains preferences, roles and sparse snapshots, never credentials; its file permissions are 0600. Existing corrupt, oversized or unsupported-version cache files are preserved byte-for-byte; the current session can continue in memory without overwriting them. Errors are fixed local text, never raw server messages. Event Log accepts only four action names (linked, unlinked, synced, settings); automatic refreshes stay silent and identifiers or credentials never enter events. API requests use a fixed read-only endpoint list, HTTPS, bounded response sizes, no redirects, cookies, credential store or disk URL cache. There are no attendance/check-in/account mutation endpoints.

The avatar request has no account headers, rejects redirects/unknown hosts, caps transfer at 4 MiB, and converts to at most 512 px on a utility queue. WebKit exists only during explicit sign-in and is discarded on completion/cancel. Its owned sign-in panel stays above the HUD on the selected display; completing or cancelling it restores HUD focus only while the overlay remains visible. The header reuses three existing game textures (about 6.7 KiB) plus a compressed subset of 33 original HarmonyOS Sans SC Medium numeric/time glyphs (54,122 bytes before compression). Its right edge mirrors the ENDFIELDHUD heading around the HUD center. It rasterizes a single number image only when the value or display scale changes; no full font atlas or new renderer is added.

## Verification boundary

Unit and native fixtures use temporary profiles, a memory credential vault and intercepted networking. The owner has confirmed Mainland Endfield self-profile retrieval, including sanity, avatar, levels, awakening date and collection counts, and separately confirmed Arknights sanity matching 森空岛. Global public frontend contracts and offline regional fixtures are checked; live Global sign-in/profile validation is deferred until after publication at the owner’s request. The full production HUD’s real-account persistence/UI path, untested captcha/SMS/email variants, publisher rate limits and arbitrary live avatar URLs remain unverified. Official frontend changes may require adapter updates; failure retains the last local data.

## Sign-in window repair history (2026-10-05)

The following records the investigation before the owner-confirmed success below; pending results in this history are not the current status.

Connect now automatically opens the official site’s login dialog after consent. Its desktop page also fits the account window so fallback controls stay visible, and resizes without a timer. Failed loads leave an explicit Retry control. Approved SDK popups keep their supplied WebKit configuration and opener callback; closing a child returns to the parent. Only two child windows can exist, all using the login session's ephemeral store. Lifecycle/storage notifications detect credentials written by another owned page without polling. All WebKit views are released when the attempt ends.

Public homepage and CN login-form rendering were checked in a disposable unauthenticated window. Both reported user failure paths are addressed by code and isolated fixtures, but successful end-to-end account login still requires the account owner's review. The user's running HUD, real account, Keychain and profile data were not used for this verification.

The owner's subsequent Mainland China test confirmed login, signed refresh and
binding/community lookup. Endfield profile retrieval still returned 10001.
The mainland request now omits an unsupported language header found during a
fresh official-client audit. A separate profile diagnostic tester compares the
official read-only routes and reports only fixed error categories; normal HUD
requests keep the web route until the result establishes a needed change.
End-to-end profile sync and Global sign-in remain unverified. See
`account-api-research.md` for the current evidence and testing boundary.

The owner then confirmed that both official profile routes still return 10001.
The next isolated Service Reason Test displays the redacted provider explanation
only in its window; fixed-tag logs still exclude it. Normal HUD error messages
and persistence are unchanged. A separately reproduced large-numeric-ID
rounding defect is fixed, but it is not asserted to explain this live failure.
At that stage profile sync remained unverified while the service reason was
pending; the subsequent owner results are recorded below.

## Verification before self-profile success (2026-10-05)

These intermediate failures were resolved by the authenticated self-profile contract described below.

The owner subsequently reported the provider sentence **操作失败，请稍后重试**
with code **10001**, and confirmed that the same account's profile and sanity
work in the official mobile app. Both isolated native card routes had failed;
the language-header correction alone did not resolve the issue. This is not
evidence that the account lacks game data.

A fresh trace from the live official game-data HTML exposed a separate card
request client: the game's shared `sk-game-role` setting does not configure
that client. The same separation is confirmed in the Global model. The earlier
claim that our added card header matched was corrected, and that extra header
was removed for both regions. The normal HUD still uses the official web
card route with `roleId`, `serverId` and community `userId`; no cookies, extra
permissions or mutation prerequisites were introduced.

The owner's Card Client Test version 2 used the direct fresh-pair flow and
card-header correction. It returned two roles, including one available Endfield
role, but the normal web card still failed with **10001 — 操作失败，请稍后重试**.
Neither correction is a confirmed cause or live fix; profile retrieval remains
unresolved. The focused API suite passes **193 assertions**, controller tests
pass **122**, and isolated tester lifecycle tests pass **30**. Successful
Endfield profile retrieval and Global linking remain unverified. Full source comparisons and the historical
test sequence are preserved in `account-api-research.md`. Original Chinese
requirements and existing local profile compatibility remain unchanged.
An isolated `Self Profile Test` now compares the documented own-account
contract: `/api/v1/game/endfield/card/detail` with roleId/serverId only, avoiding
the optional community-user target. It uses the same scoped official session
and retains role validation, cancellation and the ten-minute RAM-only retry.
This explicit diagnostic mode does not change the normal HUD route or introduce
automatic fallback after service errors. Its live outcome remains pending;
see `account-api-research.md` for the implementation references and counterexample.

## Current verification status (2026-10-05)

The owner has now confirmed Mainland Endfield self-profile success, including
sanity, avatar, level, awakening date and all three collection counts. The HUD
uses this same self-profile request by default and skips the unnecessary
community-user lookup. Local schema and manual-profile overrides are unchanged.

Arknights sanity is implemented for bindings actually returned by 森空岛. Select
**账户绑定 → 顶部显示 → 明日方舟** to show it without changing the selected
Endfield personal-card role. The request retains the binding's server channel,
and AP decoding accounts for recovery using the official codec. It uses the
existing visible refresh schedule, with no added background worker. The owner
confirmed that the Arknights tester's current/max sanity matches 森空岛. Both
Mainland game endpoints are now verified; Global linking and the full HUD's
real-account persistence/UI flow remain pending. The Arknights tester passes 42 isolated checks. Current focused test counts and the v1.2.0 recovery/Global audit are recorded in `account-api-research.md`.

## v1.2.0 Global and recovery update

The current SKPORT homepage, login/header modules, GRYPHLINE SDK and game-tools
modules were rechecked without an account. Their three community-session keys
and signing context remain compatible. The official SDK’s exact fallback host
`web-api.gryphline.net` is now accepted for Global account navigation, alongside
its existing first-party hosts. It cannot supply community credentials and is
not accepted in the Mainland flow. The existing explicit-consent, ephemeral
WebKit, nonce, popup limits and region boundaries remain unchanged.

Global intercepted fixtures exercise complete credential handoff, actual server
selection, unavailable roles, signed self-profile lookup, no extra user lookup,
server-clock alignment, and Mainland/Global isolation. The live Global test is
intentionally deferred until after release; offline passing tests are not a
claim of owner-authenticated Global success.
