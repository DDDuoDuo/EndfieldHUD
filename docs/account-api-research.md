# Account API evidence and boundary

Initial research on 2026-10-05 used unauthenticated public HTML/static JavaScript and public source repositories. Subsequent account-owner tests are recorded separately below; research did not inspect browser cookies, credentials, successful account payloads or game mutations. These are the community websites' observed interfaces, not a published/stable developer API. The owner has verified Mainland Endfield self-profile retrieval and Arknights sanity matching 森空岛. Global sign-in and the complete production HUD flow remain unverified.

## Official sources

- [CN Endfield Protocol Terminal](https://game.skland.com/endfield/game-data) and [Global terminal](https://game.skport.com/endfield/game-data). The Global page explicitly describes Awakening Day, Authority Level, owned operators and weapons.
- CN request/signing and binding implementation: [vendor_src_libs-CihRQtpo.js](https://assets.skland.com/_static_assets/game-tools/vendor_src_libs-CihRQtpo.js).
- Global equivalent: [vendor_src_libs-Bbn0E-NL.js](https://assets.skport.com/_static_assets/game-tools/vendor_src_libs-Bbn0E-NL.js).
- CN card endpoint/codec: [dist-BZImVwlH.js](https://assets.skland.com/_static_assets/game-tools/dist-BZImVwlH.js); Global equivalent: [dist-DLBTnXys.js](https://assets.skport.com/_static_assets/game-tools/dist-DLBTnXys.js).
- CN model selects role and supplies community user ID: [home-YECLuEWJ.js](https://assets.skland.com/_static_assets/game-tools/home-YECLuEWJ.js), [gameData-C203mJZF2.js](https://assets.skland.com/_static_assets/game-tools/gameData-C203mJZF2.js). Global equivalent: [home-DJYZC_6y.js](https://assets.skport.com/_static_assets/game-tools/home-DJYZC_6y.js).
- CN presentation of wake date, collection counts and stamina: [GameData-CKtD4-ed.js](https://assets.skland.com/_static_assets/game-tools/GameData-CKtD4-ed.js).

Cross-checks against implementation source, rather than summaries: [NoneBot Skland request implementation at 886d19d](https://github.com/FrostN0v0/nonebot-plugin-skland/blob/886d19decf32a7bfb2980adba9254b41d1aadb2f/nonebot_plugin_skland/api/request.py), [Endfield schema](https://github.com/FrostN0v0/nonebot-plugin-skland/blob/886d19decf32a7bfb2980adba9254b41d1aadb2f/nonebot_plugin_skland/schemas/endfield/card.py), [binding schema](https://github.com/FrostN0v0/nonebot-plugin-skland/blob/886d19decf32a7bfb2980adba9254b41d1aadb2f/nonebot_plugin_skland/schemas/binding.py), [Arknights status schema](https://github.com/FrostN0v0/nonebot-plugin-skland/blob/886d19decf32a7bfb2980adba9254b41d1aadb2f/nonebot_plugin_skland/schemas/arknights/models/status.py). Global [endfield-auto at 12af701](https://github.com/torikushiii/endfield-auto/blob/12af7017464d2cee1792de6486b88b18d105d11f/skport/endfield/index.ts) confirms the separate Global host and card fields. Its account-token/check-in workflows are outside this implementation.

## Implemented requests

CN host is `zonai.skland.com`; Global host is `zonai.skport.com`. Each region is independent. All requests are GET:

| Path | Parameters / purpose |
| --- | --- |
| `/web/v1/auth/refresh` | Refresh an existing scoped session with its signing/device context; response `data.token` becomes signingToken. Fresh sign-in uses the already-issued pair directly. No passport token. |
| `/api/v1/game/player/binding` | Read `data.list`, supported `appCode`, `bindingList`, nested Endfield `roles`/`defaultRole`. |
| `/api/v1/game/endfield/card/detail` | Normal authenticated self-profile: `roleId`, `serverId` only. The credential identifies the account. No `userId` or added `sk-game-role` header. |
| `/api/v1/game/player/info` | Arknights binding `uid` plus its `channelMasterId` when present. Legacy bindings without channel metadata retain UID-only behavior. |
| `/web/v1/user`, `/web/v1/game/endfield/card/detail` | Retained explicit diagnostic community-target flow; not used by the normal self-profile client. |

The Endfield binding account `uid` differs from nested game `roleId`. Stable identity includes region, game, binding UID, role ID and server. Global Arknights is never invented: the UI can only select games/roles the regional service actually returns. Ban/deletion flags remain unavailable roles. The normal client uses the owner-verified self-profile contract directly; errors never cause automatic switching to another route or identity.

Both official signing implementations concatenate path, raw GET query (without `?`), timestamp, then ordered JSON header fields `platform`, `timestamp`, `dId`, `vName`. Values are `3`, Unix seconds as a string, the device ID issued to this official community session, and `1.0.0`. Apply HMAC-SHA256 with the signing token, lowercase-hex encode that digest, then MD5 the hex text. The credential-refresh response can calibrate regional clock offset. The same issued device ID is sent in the `dId` request header; EndfieldHUD does not generate a device fingerprint.

## Sparse field mapping

| HUD data | Endfield `data.detail` | Arknights `data.status` |
| --- | --- | --- |
| Game UID | `base.roleId` | `uid` |
| Name / avatar | `base.name`, `base.avatarUrl` | `name`, `avatar.url` when present |
| Wake / registration date | `base.createTime` seconds; terminal labels Awakening Day | `registerTs` seconds |
| Permission / exploration level | `base.level`, `base.worldLevel` | `level`; no Endfield exploration equivalent |
| Operators / weapons / documents | `base.charNum`, `base.weaponNum`, `base.docNum` | `charCnt`; other two unavailable |
| Experience | `base.exp` | `exp.current` |
| Stamina | `dungeon.curStamina`, `dungeon.maxStamina` | `ap.current`, `ap.max` |
| Recovery timestamp | `dungeon.maxTs` | `ap.completeRecoveryTime` |

Official Endfield codec maps `maxTs` to `nextSanityRecoverTs`; despite that property name, its UI displays the remaining time to the limit and treats zero as already at the limit. It is a full-recovery deadline. The HUD locally projects recovery from that deadline, response time and the verified game interval of 432 seconds per point; Arknights uses its separate 360-second interval. Reported over-cap values remain intact. Missing/sentinel deadlines do not fabricate recovery. Optional response timestamps align server/local clocks, and local `observedAt` records receipt. There is no established public push protocol; spending or gaining sanity in-game needs a fresh sample.

Absent, null, malformed, negative-sentinel and invalid-date fields stay nil. Explicit zero counts remain zero. A partial response cannot fabricate missing inventory counts. Returned UID mismatch is rejected. `dailyMission` is activity, not stamina; displayed character arrays are not a total-operator substitute. Avatar URLs are HTTPS metadata only; the separately bounded artwork loader controls permitted CDN hosts. No game identity writes occur in the API client. Manual UID and wake-date override behavior belongs to the profile integration.

## Runtime and tests

`HypergryphAccountAPI` uses ephemeral URLSession with cookies, URL credential storage and cache disabled, 15-second request / 20-second resource timeouts, and an 8 MiB streamed response limit. It refuses every redirect, validates TLS normally, and invalidates each finite request session after completion/cancellation. Normal HUD errors use fixed local text; an isolated, opt-in callback can display only a redacted top-level provider explanation, as detailed below. The endpoint allowlist contains no attendance, configuration, profile-edit, grant, or passport-token routes. There is no timer or background polling in the client.

`HypergryphAccountAPITests` intercepts every request with URLProtocol. Its independent Python-generated signature vectors cover empty and nonempty queries. Fixtures cover both regional flows, sparse and over-cap data, actual role identities, wrong-UID rejection, numeric representations, errors, advertised/streamed limits, redirect refusal, cancellation, and invalid-input no-network behavior. The current focused API/projection suite passes 296 assertions, with Login policy/bridge/Keychain fixtures passing 236 and controller lifecycle/cadence fixtures passing 124. Earlier counts below describe historical stages. The owner has validated Mainland Endfield and Arknights reads; Global live validation is deferred until after release. Untested provider fields and variants are not inferred from those successes.

## Sign-in repair follow-up (2026-10-05)

The user-supplied [endfield-wallpaper auth implementation at c9328b1](https://github.com/Entropy-Increase-Team/endfield-wallpaper/blob/c9328b105c189cd6035a692f65c0dc2d25b56ca9/assets/js/auth.js) uses `end-api.shallow.ink`: anonymous UUID authorization followed by a relay QR/framework token, or an API key. It is not a direct Hypergryph login example. The [relay's own description](https://end.shallow.ink/zh-CN/dashboard/about) identifies its unofficial backend. EndfieldHUD keeps first-party requests; no relay credentials, code or dependency were imported.

The [official CN community refresh client](https://bbs.hycdn.cn/skland-fe-static/skland-bbs/7905.3be0d11f.js) distinguishes code 10000 (signing-token recovery), 10003 (server-clock correction), 10002 (expired community cred) and 10001 (business/real-name failure). HTTP 401/403 JSON is now decoded within the same transfer cap so these codes survive classification. Each signed request has at most one recovery/retry; repeated or mixed errors stop. A transient replacement signing key is retained for at most one credential SHA-256 identity per region; switching identities discards the previous entry. Nothing new polls or persists.

Official SDK sources: [Hypergryph](https://web.hycdn.cn/hg_account_web_sdk/lib/hg-account-web-sdk.min.js), [CN login module](https://web.hycdn.cn/hg_account_web_sdk/lib/3.4.0/796.9eed9aa2b9f8de4470cf.js), [GRYPHLINE](https://web-static.hg-cdn.com/gl_account_web_sdk/gl-account-web-sdk.min.js). The global SDK creates a blank child before asynchronous navigation and uses opener callbacks. The old delegate loaded that request into the parent and returned nil. The repaired delegate returns WebKit's configured child, retains its ephemeral store and opener, validates the source and destination, bounds child windows to two, and releases them on cancellation/completion. Owned first-party main frames can supply the exact scoped key through writes, storage/lifecycle events or finite navigation rechecks. There is no storage enumeration or credential polling.

A fresh, visible, unauthenticated WebKit probe also reproduced a viewport defect: at 860 points wide, the CN site's login button lies at x942–998. Fitting the desktop page to a 1280 CSS-pixel viewport places it at x774–812, visibly inside the window. Default user-agent and existing official navigation policy loaded both public homepages; neither was established as the cause. The CN phone/password/QR form rendered in a visible probe. Offscreen probes were throttled and cannot establish a site failure. Real sign-in, captcha completion and account handoff were not attempted.

A repeatable local custom-scheme WebKit fixture passed 16 assertions for returned-child configuration, opener identity, cross-window postMessage, close callbacks and scoped storage events. This checks platform mechanics without external networking; it does not substitute for a live account test.

## Post-login device-context failure (2026-10-05)

The account owner reported successful CN web sign-in followed by a closed login window and a disconnected HUD. A fresh native fixture using the actual production Login class confirmed that its injected bridge, native origin/nonce checks and completion callback work. A separate public request using only a synthetic invalid credential reproduced HTTP 200 / provider code 10001, with device information rejected before account validation. No real credential was used in that probe.

Tracing the current official CN and Global clients established that successful community authorization caches a raw signing key at `SK_TOKEN_CACHE_KEY` before saving raw `SK_OAUTH_CRED_KEY`. Both also cache their already-issued device context at `SK_SHUMEI_DEVICE_ID_KEY` as `{id,timestamp}`. The prior bridge discarded the signing/device context and attempted an unsigned credential-only bootstrap refresh. The repaired bridge transfers exactly those three scoped values, reads only `.id` from the bounded device object, and waits for a complete session. No passport storage, cookies, forms, device generation or third-party relay is involved.

The native credential record has an additive optional device ID for older Keychain payload compatibility. Login, resumed refresh, binding reads, card reads and token recovery retain and sign that exact context. Its persistence remains inside the existing scoped Keychain item; ordinary profile/cache files and diagnostic logs exclude it. Initial signed refresh can correct clock skew once, and token-cache identities include device context as well as the credential.

The official websites use login dialogs rather than a dedicated login route. Following explicit Connect consent, a bounded observer activates only the official header login controls once: CN `header .header-right .header-button` with the exact login label, and Global's account avatar then its logged-out login item. The observer ends after the control is activated or after 20 seconds; it never fills or submits a credential form. Evidence: [CN header](https://bbs.hycdn.cn/skland-fe-static/skland-bbs/4269.e0a2e555.js), [Global header](https://static.skport.com/skport-fe-static/skport-bbs/3265.5e008280.js).

A separate interactive Account Test app uses these actual source classes with fresh ephemeral WebKit and read-only API calls. It stores neither account sessions nor profile data, and records only fixed lifecycle/stage tags, safe counts/booleans and numeric error codes. A real account-owner test is still required to establish authenticated success.

## Owner-tested profile-only failure (2026-10-05)

The owner's isolated Mainland China test completed official login, signed token refresh, game bindings and community user lookup. The Endfield card request alone returned raw provider code **10001**. This establishes the earlier session handoff repair for those successful phases, not end-to-end profile sync. The owner also reported that the ordinary game-data webpage asks them to scan a QR code and open the mobile app; this is not evidence of missing game data or a failed account.

A fresh audit of the [CN card client](https://assets.skland.com/_static_assets/game-tools/dist-BZImVwlH.js) and [network client](https://assets.skland.com/_static_assets/game-tools/vendor_src_libs-CihRQtpo.js) found one mismatch: EndfieldHUD sent `sk-language: zh-cn`, while the mainland client does not set this header. `zh-cn` is a passport SDK locale, not the game-tools language enum. Mainland now omits this header; Global keeps its valid `en` value. Whether this mismatch caused the observed failure still needs an owner retest.

The role ID, server ID, community user ID, ordered query and signature algorithm match current official sources. The initial audit also treated the shared client's game-role header as a card header; the later instance-level trace below corrects that claim. The [game-data model](https://assets.skland.com/_static_assets/game-tools/gameData-C203mJZF2.js) returns early when outside the mobile app; its natural in-app flow selects `/api/v1/game/endfield/card/detail`. Both that route and `/web/v1/game/endfield/card/detail` are exported by the official card client and accept the same query object. The normal HUD retains the web route pending live evidence. An explicit constructor option permits the isolated tester to compare the official app route; production never switches routes automatically after a permission refusal.

Optional diagnostics reduce failed responses to a fixed endpoint category, numeric code and message category (device, parameters, role, permission, identity, authentication or other). Raw provider text, URLs, headers and identifiers never reach the callback or log. The callback is nil in the HUD, adds no network calls or polling, and leaves existing error handling intact. Code 10001 remains a generic provider failure, not an invented expired-session or real-name error.

The follow-up `build/account-profile-test` helper tests the corrected normal route, with a finite read-only app-route comparison for unresolved 10001 errors other than recognized permission/identity restrictions. Both use the same owner-authorized in-memory session. The original `build/account-live-test/result.log` is preserved. No authenticated retry was run by the assistant and no real profile or Keychain data was read. The focused API suite passes **184 assertions**, including mainland/global headers, both signed route contracts and diagnostic data minimization.

## Both card routes rejected; preserve the service explanation (2026-10-05)

The owner's next test reached both official card routes after successful login,
refresh, binding and community lookup. Both returned 10001, classified as
`other`. Thus removing the mainland language header and comparing routes did
not establish a fix. The category-only diagnostic discarded the explanation
needed to distinguish a service restriction from a request bug.

Three user-supplied references were inspected at pinned revisions:

- [bhaoo/endfield-gacha, 97cc974](https://github.com/bhaoo/endfield-gacha/blob/97cc9742ed73ceac7e4cb9f64ca60a596696125a/src-tauri/src/lib.rs): passport/OAuth/U8 credentials for gacha records, not SKLAND community profile/stamina. No account-scope change or code was imported.
- [erzaozi/skland-plugin, 09bc5e4](https://github.com/erzaozi/skland-plugin/blob/09bc5e467eed3b72216e2f629d8d3e4cf6061d45/components/Code.js): explicitly filters original Arknights bindings and uses `/api/v1/game/player/info`; it has no Endfield card route.
- [fxquarter/astrbot_plugin_arknights_sanity, 9bca538](https://github.com/fxquarter/astrbot_plugin_arknights_sanity/blob/9bca538b17d914835cfa86b3f48c4daca24a8be3/api_process.py): also filters original Arknights and uses binding UID/channel master ID for its player-info route. Those identities are not substitutes for Endfield's nested role/server/community identity.

The exact official JavaScript signature function was independently evaluated
against actual native requests sent over a local loopback connection: **8
synthetic cases passed**, covering both regions/routes, numeric and leading-zero
IDs, and opaque device bytes. This rules out query/wire rewriting for those
fixtures; it does not prove acceptance by the live game service. A separate
fixture exposed `NSNumber → Double → Int` rounding above 2^53. Numeric IDs now
preserve their exact integer digits (including UInt64 values); string IDs stay
unchanged. Whether the owner's response uses affected numeric IDs is unknown.

The new isolated Service Reason Test uses only the normal web profile route.
An opt-in callback displays a redacted top-level provider error sentence in the
owner's window, never in diagnostic logs. It rejects non-string/oversized
messages, redacts the request's credential/signing/device material and query
IDs, removes URLs/emails/long opaque values/numeric identifiers, strips markup
and control characters, and limits the final plain text to 512 characters. It
never serializes successful or nested response data. The callback is unset in
the HUD. Fixed-category diagnostics additionally recognize signature and
authorization wording that previously became `other`.

Verification: **193 focused API assertions**, **23 isolated tester lifecycle
assertions**, strict tester signature verification and hidden UI preview pass.
The old helper logs are preserved. No real Keychain/profile data was accessed,
no authenticated request was initiated by the assistant, and no production
endpoint or account permission was changed. At this stage the exact service
reason was pending; the owner's subsequent result is recorded below.

## Current owner result and card-client correction (2026-10-05)

The owner supplied the exact service explanation: **10001 — 操作失败，请稍后重试**.
The same account's profile and sanity work in the official mobile app. These
observations rule out claiming that the owner has no game data or that the
mobile app cannot retrieve it; they do not identify the cause of the native
request failure. Login, refresh, bindings and community lookup have succeeded,
while neither isolated card-route comparison has established profile success.

A new unauthenticated fetch began at the live
[game-data HTML](https://game.skland.com/endfield/game-data), then followed
[index-7eguj7hd.js](https://assets.skland.com/_static_assets/game-tools/index-7eguj7hd.js),
[bootstrap-q5E3z2qL.js](https://assets.skland.com/_static_assets/game-tools/bootstrap-q5E3z2qL.js),
the [route/model registry](https://assets.skland.com/_static_assets/game-tools/vendor_sk_chimera-Cyc7UHuw.js),
and its registered [game-data page](https://assets.skland.com/_static_assets/game-tools/game-data-Bck5yEcw.js).
That graph still selects the models and card routes listed above. This was a
fresh entry-to-route trace, not a lookup of remembered asset filenames.

The correction concerns **two separate `OneFetch` instances**. The game-data
model's `setGameRole` calls the networking module's exported
`setGameFetchOptions`, which changes the shared `gameFetch`. The card service
in `dist-BZImVwlH.js` creates its own client and `getInfo` forwards the
`roleId`, `serverId`, `userId` query to that client. Its `sk-game-role` setters
belong to attendance and cost-calculator code, not this card-loading path.
EndfieldHUD therefore removes its extra card `sk-game-role` header. The
earlier statement that this header matched the profile request was incorrect.
The Endfield identity mapping remains unchanged.

The [Global game-data model](https://assets.skport.com/_static_assets/game-tools/gameData-EjZqJgv62.js)
confirms the same separation: its role setter imports `a` (the shared
`setGameFetchOptions`) from `vendor_src_libs-Bbn0E-NL.js`, while profile
`getInfo` comes from the separate `dist-DLBTnXys.js` card client. Its SHA-256 is
`7dafb3e715334cbeeef20df9e7e3afe45575cd5ba2155659e299d7fe0df2d6e6`.
The card-header correction therefore applies to both regions. A Global
default-binding write occurs only after an explicit role change in the
official UI; it is not a profile-read prerequisite and is not adopted.

The same trace found no `credentials: include` on the card request. Browser
fetch defaults omit cookies between `game.skland.com` and `zonai.skland.com`;
there is no evidence to expand native cookie access. Removing the mainland
language header did not solve the owner's failure. The subsequent owner test
also failed after removing the extra card header and using the fresh pair,
as recorded below. Neither change is a confirmed cause or live fix.

## Shared SKLAND flow adopted from both user references

These comparisons concern shared SKLAND authentication and request behavior,
even where the reference's final game-data endpoint is for original Arknights.
No AGPL source code or dependency was copied.

| Step | erzaozi/skland-plugin (`09bc5e4`) | fxquarter/astrbot_plugin_arknights_sanity (`9bca538`) | EndfieldHUD decision |
| --- | --- | --- | --- |
| Community scope | [Grant and credential exchange](https://github.com/erzaozi/skland-plugin/blob/09bc5e467eed3b72216e2f629d8d3e4cf6061d45/components/Code.js#L90): appCode `4ca99fa6b56cc2ba`, type 0, kind 1 | [Same exchange](https://github.com/fxquarter/astrbot_plugin_arknights_sanity/blob/9bca538b17d914835cfa86b3f48c4daca24a8be3/api_process.py#L148) | The official community page uses this same scope. Keep its issued scoped session; do not add passport-token collection. |
| First data requests | [Fresh pair, then bindings](https://github.com/erzaozi/skland-plugin/blob/09bc5e467eed3b72216e2f629d8d3e4cf6061d45/components/Code.js#L203) | [Store the returned cred/token pair](https://github.com/fxquarter/astrbot_plugin_arknights_sanity/blob/9bca538b17d914835cfa86b3f48c4daca24a8be3/api_process.py#L189), then [use it for requests](https://github.com/fxquarter/astrbot_plugin_arknights_sanity/blob/9bca538b17d914835cfa86b3f48c4daca24a8be3/api_process.py#L293) | Adopt fresh `cred` + signing token + issued device context → bindings → community identity → profile directly. Remove the unnecessary refresh between completed sign-in and those first reads. |
| Later credential recovery | Regenerates a pair in `isAvailable` | [Refresh-by-cred fallback](https://github.com/fxquarter/astrbot_plugin_arknights_sanity/blob/9bca538b17d914835cfa86b3f48c4daca24a8be3/api_process.py#L194) and bounded recovery | Preserve the existing scoped-session refresh/recovery for resumed or aged sessions. Do not regenerate passport grants on every data read. |
| Clock and signed query | [Server timestamp/configured skew](https://github.com/erzaozi/skland-plugin/blob/09bc5e467eed3b72216e2f629d8d3e4cf6061d45/components/Code.js#L45), query without `?` | [Same query-byte convention](https://github.com/fxquarter/astrbot_plugin_arknights_sanity/blob/9bca538b17d914835cfa86b3f48c4daca24a8be3/api_process.py#L243) | Keep existing regional server-clock correction and tested wire-query signing. No additional clock polling. |
| Identity and game data | [Binding UID](https://github.com/erzaozi/skland-plugin/blob/09bc5e467eed3b72216e2f629d8d3e4cf6061d45/components/Code.js#L224) | [Binding UID/channel master ID](https://github.com/fxquarter/astrbot_plugin_arknights_sanity/blob/9bca538b17d914835cfa86b3f48c4daca24a8be3/api_process.py#L406) | Preserve Endfield's official nested role ID, server ID and community user ID. Original Arknights IDs do not replace them. |

The same community appCode and grant type are visible in the
[official CN login module](https://bbs.hycdn.cn/skland-fe-static/skland-bbs/3158.cd61aa37.js)
and [fresh game-tools OAuth configuration](https://assets.skland.com/_static_assets/game-tools/vendor_sk_pandora-4j21Uk6y.js).
Thus using the official page's fresh pair follows the references' shared
credential flow. Their legacy/app-specific header variants and classification
of every 10001 as authentication failure are not adopted over the current
official Endfield contract and the owner's concrete service explanation.

The header correction and direct fresh-pair flow are implemented changes.
**193 focused API assertions**, **122 controller assertions**, and **30
isolated tester lifecycle assertions** pass. The owner's Card Client Test
version 2 then returned two total roles and one available Endfield role, but
the normal web card still returned **10001 — 操作失败，请稍后重试**. This test
used both corrections; neither is established as the cause of the failure or
a live fix. Profile retrieval remains unresolved. No successful profile response,
Global sign-in, or end-to-end sync is claimed. No new background polling,
credential service, account mutation or replacement of local profile data is
introduced, and the original Chinese roadmap requirements remain unchanged.

The subsequent current-client audit found no token-cache serialization or
card-specific signing-scope mismatch. In the [active networking
module](https://assets.skland.com/_static_assets/game-tools/vendor_src_libs-CihRQtpo.js),
`setSignToken` stores the token string directly in `SK_TOKEN_CACHE_KEY`; its
constructor reads that string directly. Clock calibration is a separate
`SK_TIME_CACHE_KEY` object. The [card client's
initializer](https://assets.skland.com/_static_assets/game-tools/dist-BZImVwlH.js)
uses this same `OneFetch` implementation and waits for login readiness before
adding the community cred. It does not assign an independent card signing key
or secret. HTTP 401/code 10000 triggers that client's ordinary scoped refresh;
10003 corrects its clock. This evidence does not establish a new cause for the
owner's 10001 response or justify collecting broader authentication material.
## Authenticated self-profile comparison (2026-10-05)

After the fresh-session/header correction the owner still received 10001 with
`操作失败，请稍后重试`; bindings contained two roles but only one available
Endfield role. This does not support an ambiguous-role explanation.

An additional Endfield-specific comparison found a distinct self-profile
contract. [otae-bot-entari's tested personal API](https://github.com/otae-1204/otae-bot-entari/blob/d4fc0769c6da7c87f7cce9e5f9450549331d6494/docs/skland_endfield_personal_api.md#L321-L339)
uses `/api/v1/game/endfield/card/detail` with only `roleId` and `serverId`;
its query script adds `userId` only for a specified other-user lookup.
[skland-kit's Endfield request type](https://github.com/AEtherside/skland-kit/blob/bbe0d470d4f00ce1f465c4c7d381000dc3335711/src/types/client.ts#L108-L115)
also takes role/server without userId (its client separately sends a role
header). Other implementations include userId, so omission is a justified
comparison, not proof that userId is universally invalid.

An explicit `.authenticatedSelf` diagnostic mode makes that two-parameter
request, skips the unneeded community-user lookup and still checks the returned
role identity. Normal HUD routing remains unchanged pending the owner test;
errors never trigger route switching. The focused API suite passes **231
assertions**, including both regions, exact signature bytes, missing-community
identity, invalid servers, wrong returned roles and no fallback after 10001.
No third-party relay or imported session is used.

## Owner-confirmed self-profile success and Arknights support (2026-10-05)

The owner confirmed that the Self Profile Test succeeds and reports all
requested field groups present: sanity, avatar URL, authority level, awakening
date, operators, weapons and files. The normal API now defaults to that exact
two-parameter self-profile contract. Fresh login needs bindings followed by
profile, with neither an initial signing refresh nor a community-user lookup.
This establishes a working request for this Mainland account; it does not
establish that explicit userId queries fail for every account or region.

Arknights has its own player-info endpoint and header display. Inspection found
that its decoded `channelMasterId` was not forwarded to the request; it is now
sent with the UID as in the user's
[fxquarter reference](https://github.com/fxquarter/astrbot_plugin_arknights_sanity/blob/9bca538b17d914835cfa86b3f48c4daca24a8be3/api_process.py#L392-L414).
The AP decoder follows the
[official game-tools codec](https://assets.skland.com/_static_assets/game-tools/dist-BZImVwlH.js):
preserve over-cap values and unavailable recovery sentinels; otherwise account
for completed six-minute recovery intervals using server time and last-AP time.
The recovery-deadline calculation in the reference projects supplies the
fallback when last-AP time is absent. This happens during decoding only, adds
no timer or polling, and does not apply Arknights' interval to Endfield.

The owner subsequently confirmed that the isolated Arknights Test's current/max
sanity matches 森空岛. This independently verifies its own player-info path;
it is not inferred from Endfield success. The tester used only a returned
binding, the same official scoped login and memory-only retry. Its 42 isolated
lifecycle/selection checks and the 267 API assertions pass. Global account
linking and the full HUD's real-account persistence/UI flow remain unverified.


## v1.2.0 local recovery and Global audit (2026-10-05)

The user requested calculated sanity between manual/API updates, then withdrew
the earlier thirty-minute cadence and asked the app to choose an interval.
The controller now schedules successful-sample refresh every **600 seconds**
while the account module or game header is visible, with a five-second manual
repeat guard and existing bounded failure backoff. This is a conservative app
policy, not a documented provider limit. It reuses the visible HUD clock and
existing services; closed overlays have no account timer or network polling.
A fresh persisted sample avoids a needless credential read on reopening.

Endfield’s **432 seconds per point** is supported by the extracted game’s
[`DungeonConst.json` at 2597a52](https://github.com/XiaBei-cy/EndfieldData/blob/2597a522ba5af048623d0b7e4d276812be43ba3b/TableCfg/DungeonConst.json)
(`staminaRecoverDuration: 432`, `staminaRecoverValue: 1`) and the independent
[earlier game table at a1df392](https://github.com/UPON-2021/EndFieldData/blob/a1df392f03760e28a1d4c3e80bba9d1b5cef0f9e/TableCfg/DungeonConst.json).
The owner’s reference video independently shows 42/360, next recovery 00:49
and full recovery 38:03:13: subtracting the next-point delay leaves exactly
317 intervals of 432 seconds. The official
[Endfield display module](https://assets.skland.com/_static_assets/game-tools/GameData-CKtD4-ed.js)
uses `maxTs` as the limit countdown. Arknights’ official codec and the earlier
references above establish its separate 360-second interval.

The projection advances server time by elapsed local time since receipt,
calculates missing points with the game-specific full-recovery deadline, and
preserves the last authoritative value as a lower bound. It never lowers
over-cap sanity. Unknown/zero/negative deadlines stay unknown; clock rollback
does not fabricate gained points. Sleep/reopening catches up without loops.
The existing timestamps remain Codable-compatible, and projected values are
not written every second. API samples still determine changes made in-game.
Fixtures cover the next-point boundary, repeated intervals, both games, full
recovery, over-cap, unknown deadlines, two-day elapsed time, device/server clock
skew, backward local time and snapshot round trips.

Fresh public sources inspected for Global:

- [SKPORT main module](https://static.skport.com/skport-fe-static/skport-bbs/main.11b750a6.js), [login](https://static.skport.com/skport-fe-static/skport-bbs/1436.25671ce5.js), [credential refresh](https://static.skport.com/skport-fe-static/skport-bbs/4994.95ba1c62.js), [header](https://static.skport.com/skport-fe-static/skport-bbs/3265.5e008280.js). The scoped key names and avatar/popover login selectors remain unchanged.
- [Current GRYPHLINE SDK entry](https://web-api.gryphline.com/static/gl_web_sdk/sdk.entry.js) lists `https://web-api.skport.com` and `https://web-api.gryphline.net` in `SUPPORT_HOSTS`. The latter exact host is added only to Global document navigation. Neither can provide a credential: that still requires an owned `www.skport.com` main frame.
- [Global game networking](https://assets.skport.com/_static_assets/game-tools/vendor_src_libs-Bbn0E-NL.js) retains separate region host, scoped signing/device context, `sk-language: en` and the binding/card flow. The owner-verified authenticated self-profile contract remains the normal route; no fallback is added after service denial.

Added Global fixtures transfer a complete community context; keep a long role
ID exact; select a returned server ID of 3 without assuming Mainland defaults;
preserve unavailable roles; verify origin/language/no-cookie headers, two-read
binding/self-profile sequence, exact query signing and server-time recovery.
Login fixtures test each current first-party callback in both directions and
reject its use as a credential origin or Mainland callback. Tests use only
synthetic credentials and intercepted networking. Live Global authentication,
captcha variants and real profile data remain **unverified**, intentionally
deferred to the owner’s post-publication test.
