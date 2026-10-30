/*
 * Probe for git-for-windows/git#3707. Not part of Git.
 *
 * Git for Windows fills `struct stat` from three sources:
 *   dir   NtQueryDirectoryFile(FileFullDirectoryInformation)  (FSCache)
 *   path  GetFileAttributesExW()                               (mingw_lstat)
 *   hnd   GetFileInformationByHandle()                         (mingw_stat/fstat)
 * Candidates exposing ChangeTime:
 *   nt    RtlDosPathNameToNtPathName_U_WithStatus + NtQueryFullAttributesFile
 *   ntrel RtlDosPathNameToRelativeNtPathName_U_WithStatus + same
 *   hex   GetFileInformationByHandleEx(FileBasicInfo + FileStandardInfo)
 *
 * Usage: probe-3707 parity <dir>
 *        probe-3707 scenario <dir>
 *        probe-3707 perf <dir>
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef LONG NTSTATUS;
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)

typedef struct {
	USHORT Length, MaximumLength;
	PWSTR Buffer;
} USTR;

typedef struct {
	ULONG Length;
	HANDLE RootDirectory;
	USTR *ObjectName;
	ULONG Attributes;
	PVOID SecurityDescriptor;
	PVOID SecurityQualityOfService;
} OBJATTR;

typedef struct {
	LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
	LARGE_INTEGER AllocationSize, EndOfFile;
	ULONG FileAttributes;
} NETOPEN;

typedef struct {
	USTR RelativeName;
	HANDLE ContainingDirectory;
	PVOID CurDirRef;
} RELNAME;

typedef struct {
	union { NTSTATUS Status; PVOID Pointer; } u;
	ULONG_PTR Information;
} IOSB;

typedef struct {
	ULONG NextEntryOffset, FileIndex;
	LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
	LARGE_INTEGER EndOfFile, AllocationSize;
	ULONG FileAttributes, FileNameLength, EaSize;
	WCHAR FileName[1];
} FULLDIR;

#define P_OBJ_CASE_INSENSITIVE 0x40
#define P_FileFullDirectoryInformation 2
#define P_FileNetworkOpenInformation 34
#define P_STATUS_NO_MORE_FILES ((NTSTATUS)0x80000006L)

static NTSTATUS (NTAPI *pNtQueryFullAttributesFile)(OBJATTR *, NETOPEN *);
static NTSTATUS (NTAPI *pRtlDosPathNameToNtPathName_U_WithStatus)(PCWSTR,
		USTR *, PWSTR *, RELNAME *);
static NTSTATUS (NTAPI *pRtlDosPathNameToRelativeNtPathName_U_WithStatus)(
		PCWSTR, USTR *, PWSTR *, RELNAME *);
static VOID (NTAPI *pRtlReleaseRelativeName)(RELNAME *);
static VOID (NTAPI *pRtlFreeUnicodeString)(USTR *);
static ULONG (NTAPI *pRtlNtStatusToDosError)(NTSTATUS);
static NTSTATUS (NTAPI *pNtQueryDirectoryFile)(HANDLE, HANDLE, PVOID, PVOID,
		IOSB *, PVOID, ULONG, ULONG, BOOLEAN, USTR *, BOOLEAN);
static NTSTATUS (NTAPI *pNtQueryInformationFile)(HANDLE, IOSB *, PVOID,
		ULONG, ULONG);

static void load(void)
{
	HMODULE h = GetModuleHandleW(L"ntdll.dll");
#define L(f) do { FARPROC fp = GetProcAddress(h, #f); \
		if (!fp) { printf("FATAL missing %s\n", #f); exit(2); } \
		memcpy(&p##f, &fp, sizeof(fp)); } while (0)
	L(NtQueryFullAttributesFile);
	L(RtlDosPathNameToNtPathName_U_WithStatus);
	L(RtlDosPathNameToRelativeNtPathName_U_WithStatus);
	L(RtlReleaseRelativeName);
	L(RtlFreeUnicodeString);
	L(RtlNtStatusToDosError);
	L(NtQueryDirectoryFile);
	L(NtQueryInformationFile);
#undef L
}

/* Result of one query, normalized for comparison. */
struct res {
	int ok;
	DWORD err;		/* Win32 error if !ok */
	NTSTATUS st;		/* NTSTATUS if available */
	DWORD attr;
	LONGLONG ctime, atime, wtime, chtime, size;
	int has_ch;
};

static void q_gfaex(const wchar_t *p, struct res *r)
{
	WIN32_FILE_ATTRIBUTE_DATA d;

	memset(r, 0, sizeof(*r));
	if (!GetFileAttributesExW(p, GetFileExInfoStandard, &d)) {
		r->err = GetLastError();
		return;
	}
	r->ok = 1;
	r->attr = d.dwFileAttributes;
	r->ctime = ((LONGLONG)d.ftCreationTime.dwHighDateTime << 32) |
		d.ftCreationTime.dwLowDateTime;
	r->wtime = ((LONGLONG)d.ftLastWriteTime.dwHighDateTime << 32) |
		d.ftLastWriteTime.dwLowDateTime;
	r->size = ((LONGLONG)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

static void from_netopen(const NETOPEN *n, struct res *r)
{
	r->ok = 1;
	r->attr = n->FileAttributes;
	r->ctime = n->CreationTime.QuadPart;
	r->atime = n->LastAccessTime.QuadPart;
	r->wtime = n->LastWriteTime.QuadPart;
	r->chtime = n->ChangeTime.QuadPart;
	r->size = n->EndOfFile.QuadPart;
	r->has_ch = 1;
}

static void q_nt(const wchar_t *p, struct res *r)
{
	USTR name;
	OBJATTR oa;
	NETOPEN n;
	NTSTATUS st;

	memset(r, 0, sizeof(*r));
	st = pRtlDosPathNameToNtPathName_U_WithStatus(p, &name, NULL, NULL);
	if (!NT_SUCCESS(st)) {
		/* like GetFileAttributesExW() */
		r->st = st;
		r->err = ERROR_PATH_NOT_FOUND;
		return;
	}
	memset(&oa, 0, sizeof(oa));
	oa.Length = sizeof(oa);
	oa.ObjectName = &name;
	oa.Attributes = P_OBJ_CASE_INSENSITIVE;
	st = pNtQueryFullAttributesFile(&oa, &n);
	pRtlFreeUnicodeString(&name);
	if (!NT_SUCCESS(st))
		goto fail;
	from_netopen(&n, r);
	return;
fail:
	r->st = st;
	r->err = pRtlNtStatusToDosError(st);
}

static void q_ntrel(const wchar_t *p, struct res *r)
{
	USTR name;
	RELNAME rel;
	OBJATTR oa;
	NETOPEN n;
	NTSTATUS st;

	memset(r, 0, sizeof(*r));
	st = pRtlDosPathNameToRelativeNtPathName_U_WithStatus(p, &name, NULL,
							      &rel);
	if (!NT_SUCCESS(st)) {
		/* like GetFileAttributesExW() */
		r->st = st;
		r->err = ERROR_PATH_NOT_FOUND;
		return;
	}
	memset(&oa, 0, sizeof(oa));
	oa.Length = sizeof(oa);
	oa.Attributes = P_OBJ_CASE_INSENSITIVE;
	if (rel.RelativeName.Length) {
		oa.ObjectName = &rel.RelativeName;
		oa.RootDirectory = rel.ContainingDirectory;
	} else {
		oa.ObjectName = &name;
	}
	st = pNtQueryFullAttributesFile(&oa, &n);
	pRtlReleaseRelativeName(&rel);
	pRtlFreeUnicodeString(&name);
	if (!NT_SUCCESS(st))
		goto fail;
	from_netopen(&n, r);
	return;
fail:
	r->st = st;
	r->err = pRtlNtStatusToDosError(st);
}

static HANDLE open_attr(const wchar_t *p)
{
	return CreateFileW(p, FILE_READ_ATTRIBUTES,
			   FILE_SHARE_READ | FILE_SHARE_WRITE |
			   FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			   FILE_FLAG_BACKUP_SEMANTICS, NULL);
}

static int h_old(HANDLE h, struct res *r)
{
	BY_HANDLE_FILE_INFORMATION d;

	memset(r, 0, sizeof(*r));
	if (!GetFileInformationByHandle(h, &d)) {
		r->err = GetLastError();
		return -1;
	}
	r->ok = 1;
	r->attr = d.dwFileAttributes;
	r->ctime = ((LONGLONG)d.ftCreationTime.dwHighDateTime << 32) |
		d.ftCreationTime.dwLowDateTime;
	r->wtime = ((LONGLONG)d.ftLastWriteTime.dwHighDateTime << 32) |
		d.ftLastWriteTime.dwLowDateTime;
	r->size = ((LONGLONG)d.nFileSizeHigh << 32) | d.nFileSizeLow;
	return 0;
}

static int h_ex(HANDLE h, struct res *r)
{
	FILE_BASIC_INFO b;
	FILE_STANDARD_INFO s;

	memset(r, 0, sizeof(*r));
	if (!GetFileInformationByHandleEx(h, FileBasicInfo, &b, sizeof(b)) ||
	    !GetFileInformationByHandleEx(h, FileStandardInfo, &s, sizeof(s))) {
		r->err = GetLastError();
		return -1;
	}
	r->ok = 1;
	r->attr = b.FileAttributes;
	r->ctime = b.CreationTime.QuadPart;
	r->atime = b.LastAccessTime.QuadPart;
	r->wtime = b.LastWriteTime.QuadPart;
	r->chtime = b.ChangeTime.QuadPart;
	r->size = s.EndOfFile.QuadPart;
	r->has_ch = 1;
	return 0;
}

static int h_nt(HANDLE h, struct res *r)
{
	NETOPEN n;
	IOSB io;
	NTSTATUS st;

	memset(r, 0, sizeof(*r));
	st = pNtQueryInformationFile(h, &io, &n, sizeof(n),
				     P_FileNetworkOpenInformation);
	if (!NT_SUCCESS(st)) {
		r->st = st;
		r->err = pRtlNtStatusToDosError(st);
		return -1;
	}
	from_netopen(&n, r);
	return 0;
}

static void q_hex(const wchar_t *p, struct res *r)
{
	HANDLE h = open_attr(p);

	if (h == INVALID_HANDLE_VALUE) {
		memset(r, 0, sizeof(*r));
		r->err = GetLastError();
		return;
	}
	h_ex(h, r);
	CloseHandle(h);
}

/* Look up `name` in directory `dir` the way the FSCache does. */
static void q_dir(const wchar_t *dir, const wchar_t *name, struct res *r)
{
	static LONGLONG buf[64 * 1024 / sizeof(LONGLONG)];
	IOSB io;
	NTSTATUS st;
	HANDLE h;
	BOOLEAN restart = TRUE;
	size_t nlen = wcslen(name);

	memset(r, 0, sizeof(*r));
	h = CreateFileW(dir, FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		r->err = GetLastError();
		return;
	}
	for (;;) {
		FULLDIR *e;

		st = pNtQueryDirectoryFile(h, NULL, NULL, NULL, &io, buf,
					   sizeof(buf),
					   P_FileFullDirectoryInformation,
					   FALSE, NULL, restart);
		restart = FALSE;
		if (!NT_SUCCESS(st))
			break;
		e = (FULLDIR *)buf;
		for (;;) {
			if (e->FileNameLength / sizeof(WCHAR) == nlen &&
			    !_wcsnicmp(e->FileName, name, nlen)) {
				r->ok = 1;
				r->attr = e->FileAttributes;
				r->ctime = e->CreationTime.QuadPart;
				r->atime = e->LastAccessTime.QuadPart;
				r->wtime = e->LastWriteTime.QuadPart;
				r->chtime = e->ChangeTime.QuadPart;
				r->size = e->EndOfFile.QuadPart;
				r->has_ch = 1;
				CloseHandle(h);
				return;
			}
			if (!e->NextEntryOffset)
				break;
			e = (FULLDIR *)((BYTE *)e + e->NextEntryOffset);
		}
	}
	CloseHandle(h);
	r->st = st;
	r->err = st == P_STATUS_NO_MORE_FILES ? ERROR_FILE_NOT_FOUND :
		pRtlNtStatusToDosError(st);
}

static void print_res(const char *tag, const struct res *r)
{
	if (!r->ok)
		printf("    %-5s ERR err=%lu st=0x%08lx\n", tag,
		       (unsigned long)r->err, (unsigned long)r->st);
	else
		printf("    %-5s OK  attr=0x%05lx C=%lld W=%lld Ch=%lld size=%lld\n",
		       tag, (unsigned long)r->attr, r->ctime, r->wtime,
		       r->chtime, r->size);
}

/* Compare what mingw_lstat() uses: success, error, attr, C, W, size. */
static int same_as_gfaex(const struct res *a, const struct res *b)
{
	if (a->ok != b->ok)
		return 0;
	if (!a->ok)
		return a->err == b->err;
	return a->attr == b->attr && a->ctime == b->ctime &&
		a->wtime == b->wtime && a->size == b->size;
}

/* ---------------------------------------------------------------- parity */

static void parity_one(const wchar_t *p)
{
	struct res g, n, nr;

	q_gfaex(p, &g);
	q_nt(p, &n);
	q_ntrel(p, &nr);
	printf("  [%s] '%ls'\n", same_as_gfaex(&g, &n) &&
	       same_as_gfaex(&g, &nr) ? "SAME" : "DIFF", p);
	print_res("gfaex", &g);
	print_res("nt", &n);
	print_res("ntrel", &nr);
}

static void write_file(const wchar_t *p, const char *s)
{
	HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
			       FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD n;

	if (h == INVALID_HANDLE_VALUE) {
		printf("FATAL create '%ls' err=%lu\n", p,
		       (unsigned long)GetLastError());
		exit(2);
	}
	WriteFile(h, s, (DWORD)strlen(s), &n, NULL);
	CloseHandle(h);
}

static int cmd_parity(const wchar_t *base)
{
	wchar_t abs[MAX_PATH], buf[2 * MAX_PATH], longdir[1024];
	HANDLE locked, delpend;
	FILE_DISPOSITION_INFO del = { TRUE };
	int i;

	if (!SetCurrentDirectoryW(base)) {
		printf("FATAL chdir err=%lu\n", (unsigned long)GetLastError());
		return 2;
	}
	CreateDirectoryW(L"pt", NULL);
	CreateDirectoryW(L"pt\\sub", NULL);
	write_file(L"pt\\file.txt", "AAAA");
	write_file(L"pt\\locked.txt", "AAAA");
	write_file(L"pt\\delpend.txt", "AAAA");
	write_file(L"pt\\file.txt:ads", "ADS");
	printf("symlink file: %d\n", CreateSymbolicLinkW(L"pt\\lnk_file",
	       L"file.txt", SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE));
	printf("symlink dir: %d\n", CreateSymbolicLinkW(L"pt\\lnk_dir",
	       L"sub", SYMBOLIC_LINK_FLAG_DIRECTORY |
	       SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE));
	printf("symlink dangling: %d\n", CreateSymbolicLinkW(
	       L"pt\\lnk_dangling", L"nowhere.txt",
	       SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE));
	printf("junction: %d\n",
	       _wsystem(L"mklink /J pt\\junction pt\\sub >NUL") == 0);

	locked = CreateFileW(L"pt\\locked.txt", GENERIC_READ, 0, NULL,
			     OPEN_EXISTING, 0, NULL);
	delpend = CreateFileW(L"pt\\delpend.txt", DELETE, FILE_SHARE_READ |
			      FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			      OPEN_EXISTING, 0, NULL);
	printf("locked handle: %d, delete-pending: %d\n",
	       locked != INVALID_HANDLE_VALUE,
	       delpend != INVALID_HANDLE_VALUE &&
	       SetFileInformationByHandle(delpend, FileDispositionInfo, &del,
					  sizeof(del)));

	GetFullPathNameW(L"pt\\file.txt", MAX_PATH, abs, NULL);

	parity_one(L"pt\\file.txt");
	parity_one(L"pt/file.txt");
	parity_one(abs);
	swprintf(buf, ARRAYSIZE(buf), L"\\\\?\\%ls", abs);
	parity_one(buf);
	parity_one(L"pt\\file.txt.");
	parity_one(L"pt\\file.txt ");
	parity_one(L"PT\\FILE.TXT");
	parity_one(L"pt\\file.txt:ads");
	parity_one(L"pt\\missing.txt");
	parity_one(L"pt\\missingdir\\x.txt");
	parity_one(L"pt\\file.txt\\x");
	parity_one(L"pt\\fi*e.txt");
	parity_one(L"pt");
	parity_one(L"pt\\sub");
	parity_one(L".");
	parity_one(L"..");
	parity_one(L"C:\\");
	parity_one(L"pt\\lnk_file");
	parity_one(L"pt\\lnk_dir");
	parity_one(L"pt\\lnk_dangling");
	parity_one(L"pt\\junction");
	parity_one(L"pt\\locked.txt");
	parity_one(L"pt\\delpend.txt");
	parity_one(L"C:\\pagefile.sys");
	parity_one(L"D:\\pagefile.sys");
	parity_one(L"NUL");
	/* abs is "X:\...", turn it into "\\localhost\X$\..." */
	swprintf(buf, ARRAYSIZE(buf), L"\\\\localhost\\%lc$%ls", abs[0],
		 abs + 2);
	parity_one(buf);
	swprintf(buf, ARRAYSIZE(buf), L"\\\\?\\UNC\\localhost\\%lc$%ls",
		 abs[0], abs + 2);
	parity_one(buf);

	/* a path longer than MAX_PATH */
	swprintf(longdir, ARRAYSIZE(longdir), L"\\\\?\\%ls", abs);
	*wcsrchr(longdir, L'\\') = L'\0';
	for (i = 0; i < 6; i++) {
		wcscat(longdir, L"\\0123456789012345678901234567890123456789");
		CreateDirectoryW(longdir, NULL);
	}
	wcscat(longdir, L"\\deep.txt");
	write_file(longdir, "AAAA");
	parity_one(longdir);
	/* same, relative ("pt\...") and without prefix */
	parity_one(longdir + 4 + wcslen(abs) - wcslen(L"pt\\file.txt"));

	if (locked != INVALID_HANDLE_VALUE)
		CloseHandle(locked);
	if (delpend != INVALID_HANDLE_VALUE)
		CloseHandle(delpend);
	return 0;
}

/* -------------------------------------------------------------- scenario */

struct snap {
	struct res dir, path, hnd;
};

static void take(const wchar_t *dir, const wchar_t *name, struct snap *s)
{
	wchar_t p[MAX_PATH];

	swprintf(p, ARRAYSIZE(p), L"%ls\\%ls", dir, name);
	q_dir(dir, name, &s->dir);
	q_nt(p, &s->path);
	q_hex(p, &s->hnd);
}

static const char *eq3(LONGLONG a, LONGLONG b, LONGLONG c)
{
	return a == b && b == c ? "same" : "DIFF";
}

/* proposed st_ctim: the latest of CreationTime, LastWriteTime, ChangeTime */
static LONGLONG latest(const struct res *r)
{
	LONGLONG t = r->chtime;

	if (r->ctime > t)
		t = r->ctime;
	if (r->wtime > t)
		t = r->wtime;
	return t;
}

static void show(const char *step, const struct snap *s,
		 const struct snap *prev)
{
	printf("  %-22s latest=%s Ch[dir,path,hnd]=%s W=%s C=%s size=%s",
	       step,
	       eq3(latest(&s->dir), latest(&s->path), latest(&s->hnd)),
	       eq3(s->dir.chtime, s->path.chtime, s->hnd.chtime),
	       eq3(s->dir.wtime, s->path.wtime, s->hnd.wtime),
	       eq3(s->dir.ctime, s->path.ctime, s->hnd.ctime),
	       eq3(s->dir.size, s->path.size, s->hnd.size));
	if (prev)
		printf(" | vs prev: latest %s, Ch %s, W %s, C %s",
		       latest(&s->path) == latest(&prev->path) ?
		       "kept" : "MOVED",
		       s->path.chtime == prev->path.chtime ? "kept" : "MOVED",
		       s->path.wtime == prev->path.wtime ? "kept" : "MOVED",
		       s->path.ctime == prev->path.ctime ? "kept" : "MOVED");
	printf("\n");
	if (!s->dir.ok || !s->path.ok || !s->hnd.ok ||
	    s->dir.chtime != s->path.chtime || s->path.chtime != s->hnd.chtime) {
		print_res("dir", &s->dir);
		print_res("path", &s->path);
		print_res("hnd", &s->hnd);
	}
	if (s->path.ok && !s->path.chtime)
		printf("    ChangeTime is 0\n");
}

static void pause_ms(void)
{
	Sleep(60);
}

static int cmd_scenario(const wchar_t *arg)
{
	wchar_t base[MAX_PATH], top[MAX_PATH], dir[MAX_PATH];
	wchar_t f[MAX_PATH], f2[MAX_PATH], f3[MAX_PATH];
	struct snap s0, s, prev, d0, d1, d2;
	FILETIME w0;
	HANDLE h;
	DWORD n;
	char rd[16];
	size_t len;

	/* "F:\" -> "F:", so that "%ls\\probe3707" is "F:\\probe3707" */
	wcscpy(base, arg);
	len = wcslen(base);
	while (len && base[len - 1] == L'\\')
		base[--len] = L'\0';
	swprintf(top, ARRAYSIZE(top), L"%ls\\probe3707", base);
	swprintf(dir, ARRAYSIZE(dir), L"%ls\\work", top);
	swprintf(f, ARRAYSIZE(f), L"%ls\\a.txt", dir);
	swprintf(f2, ARRAYSIZE(f2), L"%ls\\a2.txt", dir);
	swprintf(f3, ARRAYSIZE(f3), L"%ls\\b.txt", dir);
	CreateDirectoryW(top, NULL);
	if (!CreateDirectoryW(dir, NULL) &&
	    GetLastError() != ERROR_ALREADY_EXISTS) {
		printf("SKIP cannot create '%ls' err=%lu\n", dir,
		       (unsigned long)GetLastError());
		return 0;
	}
	DeleteFileW(f);
	DeleteFileW(f2);
	DeleteFileW(f3);
	printf("scenario in '%ls'\n", dir);

	write_file(f, "AAAA");
	take(dir, L"a.txt", &s0);
	show("create", &s0, NULL);
	w0.dwLowDateTime = (DWORD)s0.path.wtime;
	w0.dwHighDateTime = (DWORD)(s0.path.wtime >> 32);
	prev = s0;

	pause_ms();
	h = CreateFileW(f, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	WriteFile(h, "BBBB", 4, &n, NULL);
	CloseHandle(h);
	take(dir, L"a.txt", &s);
	show("overwrite same size", &s, &prev);
	prev = s;

	pause_ms();
	h = CreateFileW(f, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ |
			FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, 0, NULL);
	SetFileTime(h, NULL, NULL, &w0);
	CloseHandle(h);
	take(dir, L"a.txt", &s);
	show("restore mtime", &s, &prev);
	printf("    mtime restored: %s\n",
	       s.path.wtime == s0.path.wtime ? "yes" : "NO");
	prev = s;

	pause_ms();
	h = CreateFileW(f, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			0, NULL);
	ReadFile(h, rd, sizeof(rd), &n, NULL);
	CloseHandle(h);
	take(dir, L"a.txt", &s);
	show("read", &s, &prev);
	printf("    atime %s\n", s.path.atime == prev.path.atime ?
	       "kept" : "MOVED");
	prev = s;

	/*
	 * NTFS updates LastAccessTime at most once an hour: age it first,
	 * so that the next read updates it (if updates are enabled).
	 */
	pause_ms();
	{
		ULARGE_INTEGER t;
		FILETIME a;

		GetSystemTimeAsFileTime(&a);
		t.LowPart = a.dwLowDateTime;
		t.HighPart = a.dwHighDateTime;
		t.QuadPart -= 3ULL * 3600 * 10000000;
		a.dwLowDateTime = t.LowPart;
		a.dwHighDateTime = t.HighPart;
		h = CreateFileW(f, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ |
				FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
				OPEN_EXISTING, 0, NULL);
		SetFileTime(h, NULL, &a, NULL);
		CloseHandle(h);
	}
	take(dir, L"a.txt", &s);
	show("age atime by 3h", &s, &prev);
	prev = s;
	pause_ms();
	h = CreateFileW(f, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			0, NULL);
	ReadFile(h, rd, sizeof(rd), &n, NULL);
	CloseHandle(h);
	take(dir, L"a.txt", &s);
	show("read after aging", &s, &prev);
	printf("    atime %s\n", s.path.atime == prev.path.atime ?
	       "kept" : "MOVED");
	prev = s;

	pause_ms();
	SetFileAttributesW(f, FILE_ATTRIBUTE_NORMAL); /* clears archive */
	take(dir, L"a.txt", &s);
	show("clear archive bit", &s, &prev);
	prev = s;

	pause_ms();
	{
		wchar_t cmd[2 * MAX_PATH];

		/* S-1-1-0 is "Everyone", whatever the locale */
		swprintf(cmd, ARRAYSIZE(cmd),
			 L"icacls \"%ls\" /grant *S-1-1-0:R >NUL 2>&1", f);
		printf("  icacls exit=%d\n", _wsystem(cmd));
	}
	take(dir, L"a.txt", &s);
	show("ACL change", &s, &prev);
	prev = s;

	pause_ms();
	MoveFileExW(f, f2, 0);
	MoveFileExW(f2, f, 0);
	take(dir, L"a.txt", &s);
	show("rename away and back", &s, &prev);
	prev = s;

	pause_ms();
	DeleteFileW(f);
	write_file(f, "CCCC");
	h = CreateFileW(f, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ |
			FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, 0, NULL);
	SetFileTime(h, NULL, NULL, &w0);
	CloseHandle(h);
	take(dir, L"a.txt", &s);
	show("delete+recreate", &s, &prev);
	printf("    tunneled CreationTime: %s, mtime restored: %s\n",
	       s.path.ctime == s0.path.ctime ? "yes" : "no",
	       s.path.wtime == s0.path.wtime ? "yes" : "NO");

	/* the directory's own stat data, as the untracked cache sees it */
	take(top, L"work", &d0);
	show("dir: initial", &d0, NULL);
	pause_ms();
	h = CreateFileW(f, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	WriteFile(h, "DDDD", 4, &n, NULL);
	CloseHandle(h);
	take(top, L"work", &d1);
	show("dir: child modified", &d1, &d0);
	pause_ms();
	write_file(f3, "x");
	take(top, L"work", &d2);
	show("dir: child added", &d2, &d1);
	return 0;
}

/* ------------------------------------------------------------------ perf */

#define NFILES 1000
#define ROUNDS 31

static wchar_t (*paths)[MAX_PATH];

static int cmp_ll(const void *a, const void *b)
{
	LONGLONG x = *(const LONGLONG *)a, y = *(const LONGLONG *)b;
	return x < y ? -1 : x > y;
}

static void m_gfaex(int i) { struct res r; q_gfaex(paths[i], &r); }
static void m_nt(int i) { struct res r; q_nt(paths[i], &r); }
static void m_ntrel(int i) { struct res r; q_ntrel(paths[i], &r); }
static void m_open_old(int i)
{
	struct res r;
	HANDLE h = open_attr(paths[i]);
	h_old(h, &r);
	CloseHandle(h);
}
static void m_open_ex(int i)
{
	struct res r;
	HANDLE h = open_attr(paths[i]);
	h_ex(h, &r);
	CloseHandle(h);
}
static HANDLE *handles;
static void m_fstat_old(int i) { struct res r; h_old(handles[i], &r); }
static void m_fstat_ex(int i) { struct res r; h_ex(handles[i], &r); }
static void m_fstat_nt(int i) { struct res r; h_nt(handles[i], &r); }

static const struct {
	const char *name;
	void (*fn)(int);
} methods[] = {
	{ "lstat GetFileAttributesExW", m_gfaex },
	{ "lstat NtQueryFullAttributesFile", m_nt },
	{ "lstat NtQueryFullAttributesFile rel", m_ntrel },
	{ "stat open+GetFileInformationByHandle", m_open_old },
	{ "stat open+GetFileInformationByHandleEx x2", m_open_ex },
	{ "fstat GetFileInformationByHandle", m_fstat_old },
	{ "fstat GetFileInformationByHandleEx x2", m_fstat_ex },
	{ "fstat NtQueryInformationFile", m_fstat_nt },
};
#define NMETHODS (sizeof(methods) / sizeof(*methods))

static int cmd_perf(const wchar_t *base, int absolute)
{
	static LONGLONG t[NMETHODS][ROUNDS];
	LARGE_INTEGER freq, a, b;
	wchar_t d[MAX_PATH];
	size_t m;
	int i, k;

	if (!SetCurrentDirectoryW(base))
		return 2;
	paths = calloc(NFILES, sizeof(*paths));
	handles = calloc(NFILES, sizeof(*handles));
	wcscpy(d, L"perf");
	CreateDirectoryW(d, NULL);
	for (i = 0; i < 4; i++) {
		wcscat(d, L"\\subdirectory");
		CreateDirectoryW(d, NULL);
	}
	for (i = 0; i < NFILES; i++) {
		wchar_t rel[MAX_PATH];

		swprintf(rel, MAX_PATH, L"%ls\\file%04d.txt", d, i);
		write_file(rel, "0123456789");
		if (absolute)
			GetFullPathNameW(rel, MAX_PATH, paths[i], NULL);
		else
			wcscpy(paths[i], rel);
		handles[i] = open_attr(paths[i]);
	}
	QueryPerformanceFrequency(&freq);
	for (m = 0; m < NMETHODS; m++) /* warm up */
		for (i = 0; i < NFILES; i++)
			methods[m].fn(i);
	for (k = 0; k < ROUNDS; k++)
		for (m = 0; m < NMETHODS; m++) {
			size_t mm = (m + k) % NMETHODS;

			QueryPerformanceCounter(&a);
			for (i = 0; i < NFILES; i++)
				methods[mm].fn(i);
			QueryPerformanceCounter(&b);
			t[mm][k] = (b.QuadPart - a.QuadPart) * 1000000000LL /
				freq.QuadPart / NFILES;
		}
	printf("perf: %s paths, %d files, %d rounds, ns per call (min/median)\n",
	       absolute ? "absolute" : "relative", NFILES, ROUNDS);
	for (m = 0; m < NMETHODS; m++) {
		qsort(t[m], ROUNDS, sizeof(LONGLONG), cmp_ll);
		printf("  %-44s %8lld %8lld\n", methods[m].name, t[m][0],
		       t[m][ROUNDS / 2]);
	}
	for (i = 0; i < NFILES; i++)
		CloseHandle(handles[i]);
	return 0;
}

int wmain(int argc, wchar_t **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	load();
	if (argc == 3 && !wcscmp(argv[1], L"parity"))
		return cmd_parity(argv[2]);
	if (argc == 3 && !wcscmp(argv[1], L"scenario"))
		return cmd_scenario(argv[2]);
	if (argc == 3 && !wcscmp(argv[1], L"perf"))
		return cmd_perf(argv[2], 0) || cmd_perf(argv[2], 1);
	fprintf(stderr, "usage: probe-3707 (parity|scenario|perf) <dir>\n");
	return 1;
}
