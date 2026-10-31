#!/bin/sh
# Count the index entries that each tool finds stat-dirty (and would
# re-hash) when the index was written by another tool.
set -e
NEW="$(cygpath -u "$GITHUB_WORKSPACE")/bin-wrappers/git"
OLD="/c/Program Files/Git/cmd/git.exe"
PY="$(cygpath -u "$PYEXE")"
P="$(cygpath -u "$GITHUB_WORKSPACE")/probe"
G="-c user.name=t -c user.email=t@example.com"

"$NEW" version
"$OLD" version
"$OLD" config --show-origin --get-regexp 'core\.(trustctime|checkstat|fscache)' || :
"$PY" -I "$P/lg2.py" version .

dirty () { "$1" -C "$2" diff-files --name-only | wc -l; }
lg2 () {	# entries libgit2 re-hashes and rewrites
	"$NEW" -C "$1" ls-files --debug | grep ctime >"$1.before"
	"$PY" -I "$P/lg2.py" update-index "$1"
	"$NEW" -C "$1" ls-files --debug | grep ctime >"$1.after"
	diff "$1.before" "$1.after" | grep -c '^<' || :
}
report () { printf '%-58s %s\n' "$1" "$2"; }

cd "$(cygpath -u "$RUNNER_TEMP")"

# A: files rewritten in place after creation (same content)
"$NEW" -c init.defaultBranch=main init -q a
"$PY" -I "$P/mk.py" make a 500
"$PY" -I "$P/mk.py" big a/big 256
"$NEW" -C a add . && "$NEW" -C a $G commit -qm init
sleep 2
(cd a && "$PY" -I "$P/mk.py" rewrite f* big)
sleep 2
"$NEW" -C a update-index -q --refresh
report "A: entries" "$("$NEW" -C a ls-files | wc -l)"
report "A: index by new Git; stat-dirty for new Git" "$(dirty "$NEW" a)"
report "A: index by new Git; stat-dirty for old Git" "$(dirty "$OLD" a)"
report "A: libgit2 status (1)" "$("$PY" -I "$P/lg2.py" status a)"
report "A: libgit2 status (2)" "$("$PY" -I "$P/lg2.py" status a)"
report "A: index by new Git; re-hashed by libgit2" "$(lg2 a)"
report "A: index by libgit2; stat-dirty for new Git" "$(dirty "$NEW" a)"
report "A: libgit2 status after its own update" "$("$PY" -I "$P/lg2.py" status a)"
"$NEW" -C a update-index -q --refresh
"$OLD" -C a update-index -q --refresh
report "A: index by old Git; stat-dirty for new Git" "$(dirty "$NEW" a)"
"$NEW" -C a config core.trustctime false
report "A: core.trustctime=false; stat-dirty for new Git" "$(dirty "$NEW" a)"
"$NEW" -C a update-index -q --refresh
report "A: core.trustctime=false; stat-dirty for old Git" "$(dirty "$OLD" a)"
report "A: core.trustctime=false; libgit2 status" "$("$PY" -I "$P/lg2.py" status a)"

# B: fresh clone by new Git; C: then a branch switch rewriting half.
# Run on D: (runner temp) and C: (system drive).
bc () {
	mkdir -p "$1" && cd "$1"
	"$NEW" -c init.defaultBranch=main init -q src
	"$PY" -I "$P/mk.py" make src 3000
	"$NEW" -C src add . && "$NEW" -C src $G commit -qm one
	"$NEW" -C src switch -qc other
	"$PY" -I "$P/mk.py" half src 3000
	"$NEW" -C src $G commit -qam two
	"$NEW" -C src switch -q main
	"$NEW" clone -q --no-local src b
	"$NEW" clone -q --no-local src b2
	sleep 2
	report "$2 B: fresh clone; re-hashed by libgit2" "$(lg2 b2)"
	report "$2 B: fresh clone, entries" "$("$NEW" -C b ls-files | wc -l)"
	report "$2 B: index by new Git; stat-dirty for new Git" "$(dirty "$NEW" b)"
	report "$2 B: index by new Git; stat-dirty for old Git" "$(dirty "$OLD" b)"
	"$NEW" -C b switch -q other
	sleep 2
	report "$2 C: after switch (1500 files rewritten); new Git" "$(dirty "$NEW" b)"
	report "$2 C: after switch; stat-dirty for old Git" "$(dirty "$OLD" b)"
	report "$2 C: after switch; re-hashed by libgit2" "$(lg2 b)"
	cd - >/dev/null
}
bc "$(cygpath -u "$RUNNER_TEMP")/bc" "D:"
bc /c/interop-bc "C:"
