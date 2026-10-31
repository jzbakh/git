#!/bin/sh
# usage: fscheck.sh GIT NAME=DIR...
# Per directory: is the stat data stable, does an attribute change make
# the entry stat-dirty, and are a same-size rewrite and a delete+recreate,
# both with the mtime restored, noticed.
GITX="$1"
shift
P="$(cd "$(dirname "$0")" && pwd)"
PY="$(cygpath -u "$PYEXE")"
export GIT_CONFIG_COUNT=2 GIT_CONFIG_KEY_0=safe.directory \
	GIT_CONFIG_VALUE_0='*' GIT_CONFIG_KEY_1=core.autocrlf \
	GIT_CONFIG_VALUE_1=false
dirty () { "$GITX" diff-files --name-only "$@" | wc -l; }
st () { "$GITX" status --porcelain -- "$1" | tr '\n' ' '; }
"$GITX" version
for spec in "$@"
do
	name=${spec%%=*}
	dir=${spec#*=}
	r="$dir/fsc-$(basename "$GITX")-$$"
	if ! mkdir -p "$r" 2>/dev/null
	then
		printf '%-9s SKIP (cannot create %s)\n' "$name" "$r"
		continue
	fi
	(
		cd "$r" &&
		"$GITX" -c init.defaultBranch=main init -q . &&
		"$PY" -I "$P/mk.py" make . 200 &&
		"$GITX" add . &&
		"$GITX" -c user.name=t -c user.email=t@example.com \
			commit -qm one &&
		sleep 2 &&
		{ "$GITX" update-index -q --refresh || :; } &&
		a=$(dirty) &&
		sleep 2 &&
		b=$(dirty) &&
		"$PY" -I "$P/mk.py" attr-toggle f0002 &&
		c=$(dirty f0002) &&
		{ "$GITX" update-index -q --refresh || :; } &&
		sleep 2 &&
		"$PY" -I "$P/mk.py" rewrite-keep-mtime f0001 &&
		"$PY" -I "$P/mk.py" recreate-keep-mtime f0003 &&
		"$PY" -I "$P/mk.py" rewrite-older-ctime f0004 &&
		d=$(st f0001) &&
		e=$(st f0003) &&
		f=$(st f0004) &&
		printf '%-9s stable:%s/%s attr:%s rewrite:[%s] recreate:[%s] older-C:[%s]\n' \
			"$name" "$a" "$b" "$c" "$d" "$e" "$f"
	) || printf '%-9s FAILED\n' "$name"
done
