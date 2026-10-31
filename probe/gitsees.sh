#!/bin/sh
# What the FSCache, lstat() and stat() of git.exe see of in-sync and
# freed placeholders, in the default placeholder mode and with
# placeholders exposed. core.trustCtime=false keeps "git status" from
# re-hashing (and hydrating) the files, so that every run sees the same
# state.
W="$(cygpath -u "$GITHUB_WORKSPACE")"
G="$W/git.exe"
export GIT_CONFIG_COUNT=3 GIT_CONFIG_KEY_0=safe.directory \
	GIT_CONFIG_VALUE_0='*' GIT_CONFIG_KEY_1=core.autocrlf \
	GIT_CONFIG_VALUE_1=false GIT_CONFIG_KEY_2=core.trustCtime \
	GIT_CONFIG_VALUE_2=false
waitfor () {
	t=0
	while [ $t -lt 120 ] && ! grep -q "^$1\$" /c/gs.log 2>/dev/null
	do
		sleep 1
		t=$((t + 1))
	done
}
show () {
	for fscache in true false
	do
		echo "-- $1, core.fscache=$fscache, default mode"
		GIT_PROBE_ATTRS=1 "$G" -C repo -c core.fscache=$fscache \
			status --porcelain 2>&1 |
		grep -E 'probe .*(f1|f2|by handle)|^.. ' || :
		echo "-- $1, core.fscache=$fscache, placeholders exposed"
		GIT_PROBE_ATTRS=1 GIT_PROBE_EXPOSE=1 "$G" -C repo \
			-c core.fscache=$fscache status --porcelain 2>&1 |
		grep -E 'probe .*(f1|f2|by handle)|^.. ' || :
	done
	for n in git.exe python.exe phattr.exe
	do
		echo "-- $1, phattr as $n"
		"$W/names/$n" 'C:\gs\repo' f1
	done
}
rm -rf /c/gs && mkdir -p /c/gs && cd /c/gs || exit 1
"$G" -c init.defaultBranch=main init -q repo
printf 'one\n' >repo/f1
printf 'two\n' >repo/f2
"$G" -C repo add . &&
"$G" -C repo -c user.name=t -c user.email=t@example.com commit -qm one
sleep 2
"$G" -C repo update-index -q --refresh
show "regular files"
"$W/cfprobe.exe" 'C:\gs' 60 'repo\f1' 'repo\f2' >/c/gs.log 2>&1 &
provider=$!
waitfor READY1
show "in-sync placeholders"
: >/c/gs/go
waitfor READY2
show "freed placeholders"
echo "-- provider log"
cat /c/gs.log
kill $provider 2>/dev/null || :
