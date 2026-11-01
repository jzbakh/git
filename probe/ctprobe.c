/*
 * st_ctim of DIR\NAME as each code path of Git for Windows computes it
 * with the pull request's rule: the FSCache (directory listing),
 * mingw_lstat() (path query) and mingw_stat()/mingw_fstat() (handle
 * query), with placeholders exposed as git.exe does.
 *   ctprobe show DIR NAME
 *   ctprobe ea FILE        (add an extended attribute to FILE)
 * Not part of Git.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

typedef struct {
	ULONG NextEntryOffset;
	ULONG FileIndex;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER EndOfFile;
	LARGE_INTEGER AllocationSize;
	ULONG FileAttributes;
	ULONG FileNameLength;
	ULONG EaSize;
	WCHAR FileName[1];
} full_dir_info;

typedef struct {
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER AllocationSize;
	LARGE_INTEGER EndOfFile;
	ULONG FileAttributes;
} net_open_info;

typedef struct {
	ULONG NextEntryOffset;
	UCHAR Flags;
	UCHAR EaNameLength;
	USHORT EaValueLength;
	CHAR EaName[1];
} full_ea_info;

typedef NTSTATUS (NTAPI *qdir_fn)(HANDLE, HANDLE, PVOID, PVOID,
				  PIO_STATUS_BLOCK, PVOID, ULONG, int,
				  BOOLEAN, PUNICODE_STRING, BOOLEAN);
typedef NTSTATUS (NTAPI *qfull_fn)(POBJECT_ATTRIBUTES, net_open_info *);
typedef NTSTATUS (NTAPI *dos2nt_fn)(PCWSTR, PUNICODE_STRING, PWSTR *,
				    PVOID);
typedef VOID (NTAPI *freeus_fn)(PUNICODE_STRING);
typedef CHAR (NTAPI *smode_fn)(CHAR);
typedef NTSTATUS (NTAPI *setea_fn)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG);

static qdir_fn qdir;
static qfull_fn qfull;
static dos2nt_fn dos2nt;
static freeus_fn freeus;
static smode_fn smode;
static setea_fn setea;

#define LOAD(var, type, name) \
	(var = (type)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), name))

struct times {
	int ok;
	unsigned long attr;
	long long c, w, ch;
};

/* the pull request's filetimes_to_ctim() */
static long long ctim(const struct times *t)
{
	if (!(t->attr & FILE_ATTRIBUTE_REPARSE_POINT) &&
	    t->ch > t->c && t->ch > t->w)
		return t->ch;
	return t->c;
}

/* as compat/win32/fscache.c lists a directory */
static void listing(const wchar_t *dir, const wchar_t *name,
		    struct times *t)
{
	static char buf[64 * 1024];
	IO_STATUS_BLOCK iosb;
	HANDLE h = CreateFileW(dir, FILE_LIST_DIRECTORY,
			       FILE_SHARE_READ | FILE_SHARE_WRITE |
			       FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			       FILE_FLAG_BACKUP_SEMANTICS, NULL);
	size_t len = wcslen(name);

	t->ok = 0;
	if (h == INVALID_HANDLE_VALUE)
		return;
	while (NT_SUCCESS(qdir(h, NULL, NULL, NULL, &iosb, buf, sizeof(buf),
			       2 /* FileFullDirectoryInformation */, FALSE,
			       NULL, FALSE))) {
		full_dir_info *di = (full_dir_info *)buf;

		for (;;) {
			if (di->FileNameLength == len * sizeof(WCHAR) &&
			    !_wcsnicmp(di->FileName, name, len)) {
				t->ok = 1;
				t->attr = di->FileAttributes;
				t->c = di->CreationTime.QuadPart;
				t->w = di->LastWriteTime.QuadPart;
				t->ch = di->ChangeTime.QuadPart;
			}
			if (!di->NextEntryOffset)
				break;
			di = (full_dir_info *)((char *)di +
					       di->NextEntryOffset);
		}
	}
	CloseHandle(h);
}

/* as mingw_lstat() */
static void path_query(const wchar_t *path, struct times *t)
{
	UNICODE_STRING us;
	OBJECT_ATTRIBUTES oa;
	net_open_info ni;

	t->ok = 0;
	if (!NT_SUCCESS(dos2nt(path, &us, NULL, NULL)))
		return;
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, NULL,
				   NULL);
	if (NT_SUCCESS(qfull(&oa, &ni))) {
		t->ok = 1;
		t->attr = ni.FileAttributes;
		t->c = ni.CreationTime.QuadPart;
		t->w = ni.LastWriteTime.QuadPart;
		t->ch = ni.ChangeTime.QuadPart;
	}
	freeus(&us);
}

/* as mingw_stat() and mingw_fstat() */
static void handle_query(const wchar_t *path, struct times *t)
{
	FILE_BASIC_INFO b;
	HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE |
			       FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			       FILE_FLAG_BACKUP_SEMANTICS, NULL);

	t->ok = 0;
	if (h == INVALID_HANDLE_VALUE)
		return;
	if (GetFileInformationByHandleEx(h, FileBasicInfo, &b, sizeof(b))) {
		t->ok = 1;
		t->attr = b.FileAttributes;
		t->c = b.CreationTime.QuadPart;
		t->w = b.LastWriteTime.QuadPart;
		t->ch = b.ChangeTime.QuadPart;
	}
	CloseHandle(h);
}

static void print(const char *what, const struct times *t)
{
	if (!t->ok) {
		printf("    %-7s FAILED\n", what);
		return;
	}
	printf("    %-7s attr=0x%08lx C=%lld W=%lld Ch=%lld -> st_ctim=%s\n",
	       what, t->attr, t->c, t->w, t->ch,
	       ctim(t) == t->ch && ctim(t) != t->c ? "ChangeTime" :
	       "CreationTime");
}

static int show(const wchar_t *dir, const wchar_t *name)
{
	wchar_t path[MAX_PATH];
	struct times d, p, h;
	int same;

	swprintf(path, MAX_PATH, L"%ls\\%ls", dir, name);
	listing(dir, name, &d);
	path_query(path, &p);
	handle_query(path, &h);
	print("FSCache", &d);
	print("lstat", &p);
	print("stat", &h);
	same = d.ok && p.ok && h.ok && ctim(&d) == ctim(&p) &&
	       ctim(&p) == ctim(&h);
	printf("    st_ctim from the three code paths: %s\n",
	       same ? "same" : "DIFFERENT");
	return !same;
}

static int add_ea(const wchar_t *path)
{
	char buf[64] = { 0 };
	full_ea_info *ea = (full_ea_info *)buf;
	IO_STATUS_BLOCK iosb;
	NTSTATUS st;
	HANDLE h = CreateFileW(path, FILE_WRITE_EA, FILE_SHARE_READ |
			       FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
			       NULL);

	if (h == INVALID_HANDLE_VALUE) {
		printf("    open for FILE_WRITE_EA failed: %lu\n",
		       GetLastError());
		return 1;
	}
	ea->EaNameLength = 7;
	ea->EaValueLength = 1;
	memcpy(ea->EaName, "GITTEST\0" "1", 9);
	st = setea(h, &iosb, buf,
		   (ULONG)(offsetof(full_ea_info, EaName) + 7 + 1 + 1));
	CloseHandle(h);
	printf("    NtSetEaFile: 0x%08lx\n", (unsigned long)st);
	return !NT_SUCCESS(st);
}

int wmain(int argc, wchar_t **argv)
{
	if (!LOAD(qdir, qdir_fn, "NtQueryDirectoryFile") ||
	    !LOAD(qfull, qfull_fn, "NtQueryFullAttributesFile") ||
	    !LOAD(dos2nt, dos2nt_fn,
		  "RtlDosPathNameToNtPathName_U_WithStatus") ||
	    !LOAD(freeus, freeus_fn, "RtlFreeUnicodeString") ||
	    !LOAD(setea, setea_fn, "NtSetEaFile")) {
		printf("ntdll function not found\n");
		return 1;
	}
	/* as git.exe does since the second commit, where available */
	if (LOAD(smode, smode_fn, "RtlSetProcessPlaceholderCompatibilityMode"))
		smode(2);
	if (argc == 4 && !wcscmp(argv[1], L"show"))
		return show(argv[2], argv[3]);
	if (argc == 3 && !wcscmp(argv[1], L"ea"))
		return add_ea(argv[2]);
	fprintf(stderr, "usage: ctprobe show DIR NAME | ctprobe ea FILE\n");
	return 1;
}
