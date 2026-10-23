#!/usr/bin/env bash
# Tags a Rack 3D release and pushes the tag, which makes GitHub Actions build the Windows zip and publish a release.
#   bash scripts/release.sh v0.1.0          a release
#   bash scripts/release.sh v0.2.0-beta.1   a pre-release (any version with a hyphen)
# Options (before the version):
#   --remote NAME   remote to push to (default: the one whose URL contains math0ne/rack-3d, else origin)
#   --dry-run       check everything and show what would happen, without tagging
#
# It refuses unless the working tree is clean and the current commit is already on the remote's main branch, so a
# release is always a commit others can see. Run the self-tests first (tests/rack3d/run.sh and run-app.sh): CI can
# only run the unit tests.
set -euo pipefail
cd "$(dirname "$0")/.."

remote=""
dryRun=0
while [ $# -gt 0 ]; do
	case "$1" in
		--remote) remote="$2"; shift 2 ;;
		--dry-run) dryRun=1; shift ;;
		-*) echo "unknown option $1"; exit 2 ;;
		*) break ;;
	esac
done
version="${1:-}"

if [ -z "$remote" ]; then
	remote="$(git remote -v | awk '/math0ne\/rack-3d/ {print $1; exit}')"
	remote="${remote:-origin}"
fi

# Rack 3D's releases are the version tags on its own remote. A checkout of upstream Rack also has VCV's v2.x tags
# locally, so local tags aren't used.
releases() {
	git ls-remote --tags --refs "$remote" 'v*' | sed 's#.*refs/tags/##' | sort -rV
}

if ! [[ "$version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]]; then
	echo "usage: $0 [--remote NAME] [--dry-run] vMAJOR.MINOR.PATCH[-PRERELEASE]"
	echo "releases on $remote: $(releases | head -n 3 | tr '\n' ' ')"
	exit 2
fi

fail() { echo "release: $*" >&2; exit 1; }

[ -z "$(git status --porcelain --untracked-files=no)" ] || fail "commit or stash your changes first"
git rev-parse -q --verify "refs/tags/$version" > /dev/null && fail "tag $version already exists locally"
git fetch -q "$remote" main
releases | grep -qx "$version" && fail "$version is already released on $remote"
head="$(git rev-parse HEAD)"
git merge-base --is-ancestor "$head" "$remote/main" || fail "$(git rev-parse --short HEAD) isn't on $remote/main yet; push it first"

# The newest earlier release this commit builds on, for the change list
previous=""
for tag in $(releases); do
	git fetch -q "$remote" "refs/tags/$tag:refs/tags/$tag" 2>/dev/null || true
	if git merge-base --is-ancestor "$tag" "$head" 2>/dev/null; then
		previous="$tag"
		break
	fi
done

kind=release
[[ "$version" == *-* ]] && kind=pre-release
echo "Releasing $version ($kind) at $(git log -1 --format='%h %s' "$head")"
if [ -n "$previous" ]; then
	echo "Changes since $previous:"
	git log --format='  %h %s' "$previous..$head"
else
	echo "First release on $remote."
fi

if [ "$dryRun" = 1 ]; then
	echo "Dry run: would tag $version and push it to $remote"
	exit 0
fi
git tag -a "$version" -m "Rack 3D $version" "$head"
git push "$remote" "refs/tags/$version"
url="$(git remote get-url "$remote" | sed -E 's#^git@github.com:#https://github.com/#; s#\.git$##')"
echo "Pushed $version. The build and release: $url/actions"
