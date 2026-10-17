#!/usr/bin/env python3
"""Hidden-coverage fixture for the Account Linking area.

Selects, from the Mac oracle fixture (tests/fixtures/hypergryph-account-source.json,
generated from the unchanged Sources/Hypergryph*.swift by
tools/hypergryph_account_reference.sh), the synthetic inputs the EndfieldHUD
hidden module coverage replays through the production account owner:

  * a fake official-site host: mainland navigation, popup and bridge-message
    cases with the Mac policy verdicts (HypergryphAccountLogin), ending in the
    accepted synthetic session;
  * the synthetic community envelopes the signed client receives for each
    fixed endpoint, and the Mac-decoded roles and snapshots they produce;
  * the expired-session envelope (HTTP 401, code 10002).

Every value is synthetic (SYNTHETIC-*/synthetic-* credentials, oracle role
IDs). Nothing here contacts a network host or names a real account.

  python3 -I windows/tools/hypergryph_account_coverage_fixture.py \
      windows/tests/fixtures/hypergryph-account-source.json \
      windows/tests/fixtures/hypergryph-account-coverage.json
"""
import hashlib
import json
import sys


def scenario(source, name):
    return next(s for s in source["api"] if s["name"] == name)


def main(source_path, output_path):
    raw = open(source_path, "rb").read()
    source = json.loads(raw)
    login = source["login"]
    nonce = login["nonce"]
    region = "mainland"

    # Navigation and popup verdicts (Mac permitsNavigation / permitsPopup).
    navigation = [row for row in login["navigation"] if row["region"] == region and row["url"]]
    allowed = [row for row in navigation if row["permits"]][:4]
    blocked = [row for row in navigation if not row["permits"]][:6]
    site = "https://www.skland.com/"
    popups = [row for row in login["popup"] if row["region"] == region and row["source"] == site and row["url"]]
    popup_allowed = [row for row in popups if row["permits"]][:2]
    popup_blocked = [row for row in popups if not row["permits"]][:4]

    # Bridge messages for this nonce: rejected variants first, then the first
    # accepted synthetic session (the fake page's successful sign-in).
    sessions = [row for row in login["sessions"] if row["region"] == region and row["nonce"] == nonce]
    rejected = [row for row in sessions if row["session"] is None][:6]
    accepted = next(row for row in sessions if row["session"] is not None)
    messages = [{"body": row["body"], "frame": row["frame"], "page": row["page"], "mainFrame": row["mainFrame"],
                 "session": row["session"]} for row in rejected + [accepted]]

    # Community envelopes (authenticated self route, no /web/v1/user).
    refresh = scenario(source, "refresh-cred")["steps"][0]
    bindings = scenario(source, "signing-recovery")["steps"][1]
    profiles = scenario(source, "profiles")["steps"]
    endfield, arknights = profiles[0], profiles[1]
    assert bindings["op"] == "bindings" and len(bindings["responses"]) == 1 and "ok" in bindings["result"]
    assert endfield["input"]["role"]["game"] == "endfield" and arknights["input"]["role"]["game"] == "arknights"
    assert endfield["now"] == arknights["now"] == bindings["now"]
    expired = next(st for st in scenario(source, "errors")["steps"]
                   if st["responses"] and st["responses"][0].get("status") == 401 and st["result"] == {"error": "authenticationExpired"})

    def response(row):
        return {"status": row["status"], "body": row["body"]}

    fixture = {
        "provenance": {
            "generator": "windows/tools/hypergryph_account_coverage_fixture.py",
            "source": "windows/tests/fixtures/hypergryph-account-source.json",
            "sourceSHA256": hashlib.sha256(raw).hexdigest(),
            "sourceAuthority": source["provenance"].get("sourceAuthority"),
        },
        "now": bindings["now"],
        "login": {
            "region": region,
            "nonce": nonce,
            "site": site,
            "navigation": [{"url": row["url"], "mainFrame": row["mainFrame"], "permits": row["permits"]} for row in allowed + blocked],
            "popups": [{"url": row["url"], "source": row["source"], "permits": row["permits"]} for row in popup_allowed + popup_blocked],
            "messages": messages,
        },
        "responses": {
            "/web/v1/auth/refresh": response(refresh["responses"][0]),
            "/api/v1/game/player/binding": response(bindings["responses"][0]),
            "/api/v1/game/endfield/card/detail": response(endfield["responses"][0]),
            "/api/v1/game/player/info": response(arknights["responses"][0]),
        },
        "expired": response(expired["responses"][0]),
        "expected": {
            "roles": bindings["result"]["ok"],
            "endfield": endfield["result"]["ok"],
            "arknights": arknights["result"]["ok"],
        },
    }
    with open(output_path, "w", encoding="utf-8") as out:
        json.dump(fixture, out, ensure_ascii=False, indent=1, sort_keys=True)
        out.write("\n")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
