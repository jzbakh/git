#!/bin/sh
#
# st_ctim as the FSCache, lstat() and stat() compute it after
# metadata-only changes, and whether Git finds the same stat data with
# and without the FSCache. Not part of Git.
#
# GIT_BUILD: the build directory, PROBE: ctprobe.exe (Windows path)

# pass /c, /grant, ... to native programs as they are
export MSYS2_ARG_CONV_EXCL='*'
G="$GIT_BUILD/git.exe"
fail=

check () {
	echo "-- $1 ($2)"
	"$PROBE" show "$(cygpath -w "$PWD")" "$2" || fail="$fail; $root: $1 (probe)"
	GIT_TEST_FSCACHE=1 "$G" update-index -q --refresh >/dev/null
	GIT_TEST_FSCACHE=0 "$G" diff-files --quiet
	r1=$?
	GIT_TEST_FSCACHE=0 "$G" update-index -q --refresh >/dev/null
	GIT_TEST_FSCACHE=1 "$G" diff-files --quiet
	r2=$?
	echo "    refreshed with the FSCache, compared without: exit $r1;" \
	     "refreshed without, compared with: exit $r2"
	test $r1 = 0 && test $r2 = 0 || fail="$fail; $root: $1 (git)"
}

"$G" version
for root in /c/ctest "$(cygpath -u "$RUNNER_TEMP")/ctest"
do
	rm -rf "$root" && mkdir -p "$root" && cd "$root" || exit 1
	echo
	echo "== $(cygpath -w "$root")"
	"$G" init -q &&
	for i in 1 2 3 4 5 6 7
	do
		awk -v i=$i 'BEGIN { for (n = 0; n < 2000; n++)
			print "line", n, "of file", i }' >f$i || exit 1
	done &&
	"$G" add . &&
	"$G" -c user.name=t -c user.email=t@example.com commit -q -m one ||
	exit 1
	sleep 2

	check "after commit" f1

	attrib +R "$(cygpath -w f1)"
	check "read-only attribute set" f1

	icacls "$(cygpath -w f2)" /grant '*S-1-1-0:(R)' >/dev/null
	check "ACL changed" f2

	"$PROBE" ea "$(cygpath -w f3)"
	check "extended attribute added" f3

	compact /c /exe:xpress4k "$(cygpath -w f4)" | tail -n 2
	fsutil reparsepoint query "$(cygpath -w f4)" | head -n 2
	check "WOF-compressed (compact /exe)" f4

	compact /u /exe "$(cygpath -w f4)" | tail -n 2
	check "WOF compression removed" f4

	compact /c "$(cygpath -w f5)" | tail -n 2
	check "NTFS-compressed (compact /c)" f5

	cp f6 tmp6 && touch -r f6 tmp6 && mv -f tmp6 f6
	check "replaced by a renamed copy, same content and mtime" f6

	echo "-- rewritten with the same size and mtime (f7)"
	cp -p f7 ref7 &&
	sed s/line/LINE/ f7 >tmp7 && cat tmp7 >f7 && rm tmp7 &&
	touch -r ref7 f7 && rm ref7
	"$PROBE" show "$(cygpath -w "$PWD")" f7
	echo "    status with the FSCache: [$(GIT_TEST_FSCACHE=1 "$G" status --porcelain f7)]"
	echo "    status without the FSCache: [$(GIT_TEST_FSCACHE=0 "$G" status --porcelain f7)]"
done

echo
if test -n "$fail"
then
	echo "DIFFERENCES${fail}"
	exit 1
fi
echo "st_ctim was the same in all three code paths, and Git found the"
echo "same stat data with and without the FSCache, after every change."
