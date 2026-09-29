#!/bin/bash
set -euo pipefail

# Exercise an already packaged app on a disposable graphical macOS CI runner.
# This never rebuilds, re-signs, removes quarantine, or grants permissions. The
# existing --ui-test harness uses temporary stores and skips login/updater setup.
if [ "${CI:-}" != "true" ] || [ "${RUNNER_ENVIRONMENT:-}" = "self-hosted" ]; then
    printf 'This launch check requires disposable CI; it must not run in a user session.\n' >&2
    exit 2
fi
if [ "$(uname -s)" != "Darwin" ] || [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    printf 'Usage on macOS CI: %s APP_PATH [REPORT_DIRECTORY]\n' "$0" >&2
    exit 2
fi

APP="$(cd "$1" && pwd -P)"
REPORT_DIRECTORY="${2:-build/distributed-launch}"
python3 - "$APP" "$REPORT_DIRECTORY" "${ENDFIELD_LAUNCH_TIMEOUT:-75}" <<'PY'
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import uuid

app = Path(sys.argv[1]).resolve()
reports = Path(sys.argv[2]).resolve()
binary = app / "Contents/MacOS/EndfieldHUD"
timeout = float(sys.argv[3])
if not 15 <= timeout <= 180:
    raise SystemExit("ENDFIELD_LAUNCH_TIMEOUT must be between 15 and 180 seconds")
if not binary.is_file() or not os.access(binary, os.X_OK):
    raise SystemExit(f"Missing app executable: {binary}")
if reports == app or app in reports.parents:
    raise SystemExit("Reports must be outside the app bundle")
reports.mkdir(parents=True, exist_ok=True)


def capture(name, command, seconds=20):
    """Every diagnostic is bounded and writes only its own command output."""
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, errors="replace", timeout=seconds)
        output, status = result.stdout, result.returncode
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b""
        if isinstance(output, bytes):
            output = output.decode("utf-8", "replace")
        output += f"\nDiagnostic timed out after {seconds} seconds.\n"
        status = 124
    except OSError as error:
        output, status = str(error) + "\n", 127
    (reports / name).write_text(output)
    return status


def app_processes():
    # Do not save the full process list: unrelated runner processes are not part
    # of this report. Full arguments distinguish our UUID-tagged app instance.
    try:
        listing = subprocess.run(["/bin/ps", "-axww", "-o", "pid=,command="],
                                 capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.TimeoutExpired):
        return {}
    found = {}
    for line in listing.splitlines():
        fields = line.strip().split(None, 1)
        if len(fields) == 2 and fields[0].isdigit() and fields[1].startswith(str(binary)):
            found[int(fields[0])] = fields[1]
    return found


def owned_processes(token):
    argument = "--distributed-launch-token=" + token
    return [pid for pid, command in app_processes().items() if argument in command.split()]


def stop_owned(process, token):
    # Never kill by application name or bundle identifier. LaunchServices owns
    # its app child; only the exact executable plus our UUID proves ownership.
    for sig in (signal.SIGTERM, signal.SIGKILL):
        for pid in owned_processes(token):
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                pass
        if process.poll() is None:
            try:
                process.send_signal(sig)
            except ProcessLookupError:
                pass
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass
        if not owned_processes(token) and process.poll() is not None:
            break


def run_launch(mode):
    token = str(uuid.uuid4())
    stdout_path = reports / f"{mode}.stdout.log"
    stderr_path = reports / f"{mode}.stderr.log"
    launcher_path = reports / f"{mode}.launcher.log"
    arguments = ["--ui-test", "--lifecycle-smoke-test", "--distributed-launch-token=" + token]
    started = time.monotonic()
    observed_pids = set()
    timed_out = False
    with stdout_path.open("w") as stdout, stderr_path.open("w") as stderr, launcher_path.open("w") as launcher:
        if mode == "launchservices":
            command = ["/usr/bin/open", "-n", "-W", "-a", str(app),
                       "--stdout", str(stdout_path), "--stderr", str(stderr_path), "--args", *arguments]
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=launcher, stderr=launcher)
        else:
            process = subprocess.Popen([str(binary), *arguments], stdin=subprocess.DEVNULL,
                                       stdout=stdout, stderr=stderr)
        try:
            while process.poll() is None:
                observed_pids.update(owned_processes(token))
                if time.monotonic() - started >= timeout:
                    timed_out = True
                    break
                time.sleep(0.25)
            if timed_out:
                for pid in owned_processes(token):
                    capture(f"{mode}.{pid}.process.log",
                            ["/bin/ps", "-p", str(pid), "-o", "pid,ppid,state,etime,time,rss,vsz,command"])
                    capture(f"{mode}.{pid}.sample-command.log",
                            ["/usr/bin/sample", str(pid), "2", "-file", str(reports / f"{mode}.{pid}.sample.txt")], 10)
            returncode = process.poll()
        finally:
            stop_owned(process, token)
    output = stdout_path.read_text(errors="replace")
    passed = not timed_out and returncode == 0 and bool(re.search(r"^PASS: \d+ HUD lifecycle assertions;", output, re.M))
    result = {"mode": mode, "passed": passed, "timed_out": timed_out,
              "returncode": returncode, "elapsed_seconds": round(time.monotonic() - started, 2),
              "observed_app_pids": sorted(observed_pids)}
    print(json.dumps(result), flush=True)
    return result


metadata = {}
for name, command in [
    ("os.log", ["/usr/bin/sw_vers"]),
    ("architecture.log", ["/usr/bin/uname", "-m"]),
    ("bundle.log", ["/usr/bin/plutil", "-p", str(app / "Contents/Info.plist")]),
    ("signature.log", ["/usr/bin/codesign", "--display", "--verbose=4", str(app)]),
    ("signature-verification.log", ["/usr/bin/codesign", "--verify", "--deep", "--strict", "--all-architectures", "--verbose=4", str(app)]),
    ("libraries.log", ["/usr/bin/otool", "-L", str(binary)]),
    ("binary-sha256.log", ["/usr/bin/shasum", "-a", "256", str(binary)]),
]:
    metadata[name] = capture(name, command)

results = []
failure = None
if metadata["signature-verification.log"] != 0:
    failure = "The supplied bundle failed code-signature verification; it was not launched."
elif app_processes():
    failure = "The supplied app is already running; no existing process was changed."
else:
    for mode in ("launchservices", "direct"):
        if app_processes():
            failure = "An app process remains without proven ownership; no further launch or termination was attempted."
            break
        results.append(run_launch(mode))

if failure or not all(result["passed"] for result in results):
    # Restrict retained system logs to this app and the trust services involved
    # in launch assessment. No private-data logging or elevated access is used.
    predicate = '(eventMessage CONTAINS[c] "EndfieldHUD") AND (process == "syspolicyd" OR process == "amfid" OR process == "taskgated-helper" OR sender == "AppleSystemPolicy" OR sender == "AppleMobileFileIntegrity")'
    capture("launch-policy.log", ["/usr/bin/log", "show", "--last", "5m", "--style", "compact", "--info", "--predicate", predicate], 20)

summary = {"app": str(app), "metadata_exit_codes": metadata, "launches": results,
           "failure": failure, "passed": failure is None and len(results) == 2 and all(result["passed"] for result in results),
           "scope": "Unmodified distributed binary; isolated lifecycle fixtures. This does not certify Gatekeeper approval or production permission flows."}
(reports / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
if failure:
    print(failure, file=sys.stderr)
print(f"Distributed launch reports: {reports}", flush=True)
raise SystemExit(0 if summary["passed"] else 1)
PY
