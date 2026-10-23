#!/usr/bin/env bash
# Runs a Rack 3D developer tool (see include/rack3d/Tools.hpp) against a scratch user folder whose plugins are a
# link to userdata/'s, so your settings and autosave are never touched.
#   bash Rack/scripts/rack3d-tool.sh dump PLUGINS OUT.json
#   bash Rack/scripts/rack3d-tool.sh preview PATCH.vcv OUT_PREFIX
#   bash Rack/scripts/rack3d-tool.sh devices
# Run from the folder holding the Rack checkout (userdata/ and build-tools/ live beside it).
set -uo pipefail
cd "$(dirname "$0")/../.."
root="$PWD"
tooldir="$root/build-tools/userdata"
# Rack allows one instance at a time, so a tool run would quit at once and leave the last run's log behind
if tasklist //fi "imagename eq Rack.exe" 2>/dev/null | grep -qi "Rack.exe"; then
	echo "Rack is already running; close it first (only one Rack instance can run)."; exit 3
fi
mkdir -p "$tooldir"
# Link the installed plugins (a Windows junction; no copy)
if [ ! -e "$tooldir/plugins-win-x64" ]; then
	cmd //c mklink //J "$(cygpath -w "$tooldir/plugins-win-x64")" "$(cygpath -w "$root/userdata/plugins-win-x64")" > /dev/null
fi
echo '{"showTipsOnLaunch": false, "rack3d": {"placementsEnabled": true}}' > "$tooldir/settings.json"

case "$1" in
	dump)
		export RACK3D_TOOL=dump RACK3D_TOOL_PLUGINS="$2" RACK3D_TOOL_OUT="$(cygpath -w "$(realpath -m "$3")")" ;;
	preview)
		export RACK3D_TOOL=preview RACK3D_TOOL_PATCH="$(cygpath -w "$(realpath -m "$2")")" RACK3D_TOOL_OUT="$(cygpath -w "$(realpath -m "$3")")" ;;
	devices)
		export RACK3D_TOOL=devices ;;
	*)
		echo "usage: $0 dump PLUGINS OUT.json | preview PATCH.vcv OUT_PREFIX | devices"; exit 2 ;;
esac

timeout 300 Rack/Rack.exe -u "$(cygpath -w "$tooldir")"
status=$?
grep -E "dump:|preview:|devices:|warn|Exception" "$tooldir/log.txt" | grep -v "Loaded SVG" | tail -n 40
exit $status
