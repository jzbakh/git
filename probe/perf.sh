#!/bin/sh
# Cost of exposing placeholders: "git status" on N files with option 1
# (placeholders not exposed) and option 2, with and without the FSCache,
# on regular files and on in-sync placeholders. core.trustCtime=false so
# that both options do the same work, except for exposing placeholders.
W="$(cygpath -u "$GITHUB_WORKSPACE")"
P="$W/probe"
PY="$(cygpath -u "$PYEXE")"
OLD="/c/Program Files/Git/cmd/git.exe"
N=2000
export GIT_CONFIG_COUNT=3 GIT_CONFIG_KEY_0=safe.directory \
	GIT_CONFIG_VALUE_0='*' GIT_CONFIG_KEY_1=core.autocrlf \
	GIT_CONFIG_VALUE_1=false GIT_CONFIG_KEY_2=core.trustCtime \
	GIT_CONFIG_VALUE_2=false
setup () {	# setup DIR
	rm -rf "$1" && mkdir -p "$1/repo" &&
	"$PY" -I "$P/mk.py" make "$1/repo" $N &&
	"$OLD" -C "$1/repo" -c init.defaultBranch=main init -q &&
	"$OLD" -C "$1/repo" add . &&
	"$OLD" -C "$1/repo" -c user.name=t -c user.email=t@example.com \
		commit -qm one &&
	sleep 2 &&
	"$OLD" -C "$1/repo" update-index -q --refresh
}
ms () {	# ms GIT DIR FSCACHE: one "git status", in milliseconds
	t0=$(date +%s%N)
	"$1" -C "$2/repo" -c core.fscache=$3 status --porcelain >/dev/null
	t1=$(date +%s%N)
	echo $(( (t1 - t0) / 1000000 ))
}
median () { sort -n | sed -n 4p; }	# of 7
measure () {	# measure LABEL DIR
	for fscache in true false
	do
		: >/tmp/t1
		: >/tmp/t2
		ms "$W/opt1/git.exe" "$2" $fscache >/dev/null	# warm up
		ms "$W/opt2/git.exe" "$2" $fscache >/dev/null
		for i in 1 2 3 4 5 6 7
		do
			ms "$W/opt1/git.exe" "$2" $fscache >>/tmp/t1
			ms "$W/opt2/git.exe" "$2" $fscache >>/tmp/t2
		done
		echo "$1, core.fscache=$fscache: option 1 $(median </tmp/t1) ms," \
			"option 2 $(median </tmp/t2) ms (medians of 7)"
		echo "  option 1: $(tr '\n' ' ' </tmp/t1)"
		echo "  option 2: $(tr '\n' ' ' </tmp/t2)"
	done
}
setup /c/perf-plain || exit 1
measure "$N regular files" /c/perf-plain
setup /c/perf-cloud || exit 1
"$W/cfprobe.exe" 'C:\perf-cloud' 900 -n $N >/c/perf-cloud.log 2>&1 &
provider=$!
t=0
while [ $t -lt 300 ] && ! grep -q '^READY1$' /c/perf-cloud.log
do
	sleep 1
	t=$((t + 1))
done
echo "placeholders: $(grep -c ' converted ' /c/perf-cloud.log)"
"$W/phattr.exe" 'C:\perf-cloud\repo' f0000 2>/dev/null || :
measure "$N in-sync placeholders" /c/perf-cloud
echo "files sent by the provider: $(grep -c '^FETCH_DATA' /c/perf-cloud.log)"
kill $provider 2>/dev/null || :
