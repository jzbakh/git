#!/bin/sh
# OneDrive-like flow, per Git and FSCache setting, each in its own Cloud
# Files sync root: the files become in-sync placeholders and Git records
# them; then "Free up space" unpins and dehydrates them. Count the files
# that "git status" makes the provider send.
NEW="$(cygpath -u "$GITHUB_WORKSPACE")/bin-wrappers/git"
OLD="/c/Program Files/Git/cmd/git.exe"
PY="$(cygpath -u "$PYEXE")"
P="$(cygpath -u "$GITHUB_WORKSPACE")/probe"
CFX="$(cygpath -u "$GITHUB_WORKSPACE")/cfprobe.exe"
export GIT_CONFIG_COUNT=2 GIT_CONFIG_KEY_0=safe.directory \
	GIT_CONFIG_VALUE_0='*' GIT_CONFIG_KEY_1=core.autocrlf \
	GIT_CONFIG_VALUE_1=false
files=
i=0
while [ $i -lt 20 ]
do
	files="$files repo\\f$(printf %04d $i)"
	i=$((i + 1))
done
fetched () { grep -c '^FETCH_DATA' "$1" || :; }
waitfor () {
	t=0
	while [ $t -lt 120 ] && ! grep -q "^$2\$" "$1" 2>/dev/null
	do
		sleep 1
		t=$((t + 1))
	done
}
attrs () {
	"$PY" -I -c 'import os, sys; print(hex(os.lstat(sys.argv[1]).st_file_attributes))' \
		"$1/repo/f0000"
}
for which in OLD NEW
do
	eval "GITX=\"\$$which\""
	for fscache in true false
	do
		tag="$which, core.fscache=$fscache"
		root="/c/cf-$which-$fscache"
		log="$root.log"
		rm -rf "$root" && mkdir -p "$root" && cd "$root" || exit 1
		g () { "$GITX" -C repo -c core.fscache=$fscache "$@"; }
		dirty () { g diff-files --name-only | wc -l; }
		"$GITX" -c init.defaultBranch=main init -q repo
		"$PY" -I "$P/mk.py" make repo 20
		g add . && g -c user.name=t -c user.email=t@example.com commit -qm one
		sleep 2
		g update-index -q --refresh
		"$CFX" "$(cygpath -w "$root")" 60 $files >"$log" 2>&1 &
		waitfor "$log" READY1
		echo "== $tag: $("$GITX" version)"
		echo "$tag: synced placeholder, attributes seen by a process: $(attrs "$root")"
		echo "$tag: stat-dirty: $(dirty)"
		n0=$(fetched "$log")
		g status --porcelain >/dev/null
		n1=$(fetched "$log")
		echo "$tag: status sent $((n1 - n0)) files, then stat-dirty: $(dirty)"
		g status --porcelain >/dev/null
		sleep 2
		: >"$root/go"
		waitfor "$log" READY2
		echo "$tag: after Free up space, attributes seen: $(attrs "$root")"
		echo "$tag: stat-dirty: $(dirty)"
		n2=$(fetched "$log")
		echo "$tag: status 1: [$(g status --porcelain | tr '\n' ' ')]"
		n3=$(fetched "$log")
		echo "$tag: status 1 sent $((n3 - n2)) of 20 files, then stat-dirty: $(dirty)"
		g status --porcelain >/dev/null
		n4=$(fetched "$log")
		echo "$tag: status 2 sent $((n4 - n3)) files, then stat-dirty: $(dirty)"
		grep -E '^Cf|cannot' "$log"
		cd /
	done
done
wait
