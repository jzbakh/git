#!/bin/sh
# Placeholders whose provider is not running (OneDrive closed): does
# "git status" still work? For Git 2.56.0, option 1 and option 2, with
# and without the FSCache, for in-sync placeholders (provider killed)
# and for freed ones (provider disconnected cleanly).
W="$(cygpath -u "$GITHUB_WORKSPACE")"
P="$W/probe"
PY="$(cygpath -u "$PYEXE")"
OLD="/c/Program Files/Git/cmd/git.exe"
export GIT_CONFIG_COUNT=2 GIT_CONFIG_KEY_0=safe.directory \
	GIT_CONFIG_VALUE_0='*' GIT_CONFIG_KEY_1=core.autocrlf \
	GIT_CONFIG_VALUE_1=false
waitfor () {	# waitfor LOG LINE
	t=0
	while [ $t -lt 120 ] && ! grep -q "^$2\$" "$1" 2>/dev/null
	do
		sleep 1
		t=$((t + 1))
	done
}
setup () {	# setup DIR
	rm -rf "$1" && mkdir -p "$1/repo" &&
	"$PY" -I "$P/mk.py" make "$1/repo" 5 &&
	"$OLD" -C "$1/repo" -c init.defaultBranch=main init -q &&
	"$OLD" -C "$1/repo" add . &&
	"$OLD" -C "$1/repo" -c user.name=t -c user.email=t@example.com \
		commit -qm one &&
	sleep 2 &&
	"$OLD" -C "$1/repo" update-index -q --refresh
}
try () {	# try LABEL DIR
	for g in "$OLD" "$W/opt1/git.exe" "$W/opt2/git.exe"
	do
		case "$g" in
		"$OLD") n="Git 2.56.0";;
		*opt1*) n="option 1";;
		*) n="option 2";;
		esac
		for fscache in true false
		do
			out=$("$g" -C "$2/repo" -c core.fscache=$fscache \
				status --porcelain 2>&1)
			echo "$1, $n, core.fscache=$fscache: exit $?," \
				"output [$(echo "$out" | tr '\n' ' ')]"
		done
	done
}
setup /c/off1 || exit 1
"$W/cfprobe.exe" 'C:\off1' 600 -n 5 >/c/off1.log 2>&1 &
pid=$!
waitfor /c/off1.log READY1
kill -9 $pid 2>/dev/null
sleep 2
tasklist //FI "IMAGENAME eq cfprobe.exe" | grep -c cfprobe || :
"$W/phattr.exe" 'C:\off1\repo' f0000
try "in-sync, provider killed" /c/off1
setup /c/off2 || exit 1
"$W/cfprobe.exe" 'C:\off2' 1 -n 5 >/c/off2.log 2>&1 &
pid=$!
waitfor /c/off2.log READY1
: >/c/off2/go
waitfor /c/off2.log DONE
wait $pid
"$W/phattr.exe" 'C:\off2\repo' f0000
try "freed, provider disconnected" /c/off2
echo "-- provider logs"
grep -E 'Cf|READY|DONE|FETCH|cannot' /c/off1.log /c/off2.log
