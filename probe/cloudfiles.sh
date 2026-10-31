#!/bin/sh
# Git 2.56 and this Git, each on a repository inside its own Cloud Files
# sync root: dehydrate the files like OneDrive's "Free up space", then
# run "git status" twice and count the files the provider had to send.
NEW="$(cygpath -u "$GITHUB_WORKSPACE")/bin-wrappers/git"
OLD="/c/Program Files/Git/cmd/git.exe"
PY="$(cygpath -u "$PYEXE")"
P="$(cygpath -u "$GITHUB_WORKSPACE")/probe"
CFX="$(cygpath -u "$GITHUB_WORKSPACE")/cfprobe.exe"
files=
for i in $(seq 0 19)
do
	files="$files repo\\f$(printf %04d $i)"
done
fetched () { grep -c '^FETCH_DATA' "$1" || :; }
for which in OLD NEW
do
	eval "GITX=\"\$$which\""
	root="/c/cf-$which"
	rm -rf "$root" && mkdir -p "$root" && cd "$root" || exit 1
	"$GITX" -c init.defaultBranch=main init -q repo
	"$GITX" -C repo config core.fscache true
	"$PY" -I "$P/mk.py" make repo 20
	"$GITX" -C repo add . &&
	"$GITX" -C repo -c user.name=t -c user.email=t@example.com \
		commit -qm one
	sleep 2
	"$GITX" -C repo update-index -q --refresh
	echo "== $which: $("$GITX" version)"
	echo "$which: stat-dirty before: $("$GITX" -C repo diff-files --name-only | wc -l)"
	"$CFX" "$(cygpath -w "$root")" 90 $files >"$root.log" 2>&1 &
	for t in $(seq 1 60)
	do
		grep -q '^READY' "$root.log" && break
		sleep 1
	done
	grep -v '^FETCH_DATA' "$root.log" | grep -E 'f000[01] |Cf|READY|cannot'
	echo "$which: stat-dirty after dehydration: $("$GITX" -C repo diff-files --name-only | wc -l)"
	n0=$(fetched "$root.log")
	echo "$which: status 1: [$("$GITX" -C repo status --porcelain | tr '\n' ' ')]"
	n1=$(fetched "$root.log")
	echo "$which: status 1 made the provider send $((n1 - n0)) of 20 files"
	echo "$which: stat-dirty after status 1: $("$GITX" -C repo diff-files --name-only | wc -l)"
	echo "$which: status 2: [$("$GITX" -C repo status --porcelain | tr '\n' ' ')]"
	n2=$(fetched "$root.log")
	echo "$which: status 2 made the provider send $((n2 - n1)) files"
	echo "$which: stat-dirty after status 2: $("$GITX" -C repo diff-files --name-only | wc -l)"
	grep -m 2 '^FETCH_DATA' "$root.log"
	cd /
done
wait
