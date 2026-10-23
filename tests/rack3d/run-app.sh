#!/usr/bin/env bash
# Runs Rack 3D's in-app self-test: launches Rack (a window opens briefly), runs checks against the
# live RackWidget with real modules, and exits non-zero on failure.
# Uses a throwaway user directory so your settings, patches and autosave are never touched.
# Run from an MSYS2 MINGW64 shell in the Rack directory, after `make`:  bash tests/rack3d/run-app.sh
set -uo pipefail
cd "$(dirname "$0")/../.."

userdir=build/tests/userdir
log=build/tests/app.log
rm -rf "$userdir"
mkdir -p "$userdir/plugins/Fundamental"
# The acceptance patch uses Fundamental modules; in dev mode plugins load from the user dir
cp -r plugins/Fundamental/plugin.dll plugins/Fundamental/plugin.json plugins/Fundamental/res "$userdir/plugins/Fundamental/"
# No welcome tips covering the screenshots
echo '{"showTipsOnLaunch": false}' > "$userdir/settings.json"

# -d: development mode (log to stderr, system dir = this dir), -u: user dir.
# timeout guards against a modal dialog or a hang blocking forever.
RACK3D_SELFTEST=1 timeout 120 ./Rack -d -u "$userdir" > "$log" 2>&1
status=$?
grep -E "rack3d|Exception|error" "$log" | sed 's/\x1b\[[0-9;]*m//g'
if [ $status -eq 124 ]; then
	echo "rack3d app self-test: TIMED OUT (see $log)"
fi
exit $status
