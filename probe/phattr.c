/*
 * Attributes of DIR\NAME as Git for Windows reads them: the FSCache
 * (directory listing), lstat() (path query) and stat() (handle query),
 * in the placeholder compatibility mode the process starts with, then
 * after setting modes 0, 1 and 2.
 *   phattr DIR NAME
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
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

typedef NTSTATUS (NTAPI *qdir_fn)(HANDLE, HANDLE, PVOID, PVOID,
				  PIO_STATUS_BLOCK, PVOID, ULONG, int,
				  BOOLEAN, PUNICODE_STRING, BOOLEAN);
typedef NTSTATUS (NTAPI *qfull_fn)(POBJECT_ATTRIBUTES, net_open_info *);
typedef NTSTATUS (NTAPI *dos2nt_fn)(PCWSTR, PUNICODE_STRING, PWSTR *,
				    PVOID);
typedef VOID (NTAPI *freeus_fn)(PUNICODE_STRING);
typedef CHAR (NTAPI *qmode_fn)(void);
typedef CHAR (NTAPI *smode_fn)(CHAR);

static qdir_fn qdir;
static qfull_fn qfull;
static dos2nt_fn dos2nt;
static freeus_fn freeus;
static qmode_fn qmode;
static smode_fn smode;

#define LOAD(var, type, name) \
	(var = (type)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), name))

/* as compat/win32/fscache.c lists a directory */
static unsigned long listing(const wchar_t *dir, const wchar_t *name,
			     unsigned long *tag)
{
	static char buf[64 * 1024];
	IO_STATUS_BLOCK iosb;
	HANDLE h = CreateFileW(dir, FILE_LIST_DIRECTORY,
			       FILE_SHARE_READ | FILE_SHARE_WRITE |
			       FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			       FILE_FLAG_BACKUP_SEMANTICS, NULL);
	size_t len = wcslen(name);
	unsigned long result = 0xffffffff;

	*tag = 0;
	if (h == INVALID_HANDLE_VALUE)
		return result;
	while (NT_SUCCESS(qdir(h, NULL, NULL, NULL, &iosb, buf, sizeof(buf),
			       2 /* FileFullDirectoryInformation */, FALSE,
			       NULL, FALSE))) {
		full_dir_info *di = (full_dir_info *)buf;

		for (;;) {
			if (di->FileNameLength == len * sizeof(WCHAR) &&
			    !wcsncmp(di->FileName, name, len)) {
				result = di->FileAttributes;
				*tag = di->EaSize;
			}
			if (!di->NextEntryOffset)
				break;
			di = (full_dir_info *)((char *)di +
					       di->NextEntryOffset);
		}
	}
	CloseHandle(h);
	return result;
}

/* as mingw_lstat() */
static unsigned long path_query(const wchar_t *path)
{
	UNICODE_STRING us;
	OBJECT_ATTRIBUTES oa;
	net_open_info ni;
	NTSTATUS st;

	if (!NT_SUCCESS(dos2nt(path, &us, NULL, NULL)))
		return 0xffffffff;
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, NULL,
				   NULL);
	st = qfull(&oa, &ni);
	freeus(&us);
	return NT_SUCCESS(st) ? ni.FileAttributes : 0xffffffff;
}

/* as mingw_stat() */
static unsigned long handle_query(const wchar_t *path)
{
	FILE_BASIC_INFO b;
	HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE |
			       FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			       FILE_FLAG_BACKUP_SEMANTICS, NULL);
	unsigned long result = 0xffffffff;

	if (h == INVALID_HANDLE_VALUE)
		return result;
	if (GetFileInformationByHandleEx(h, FileBasicInfo, &b, sizeof(b)))
		result = b.FileAttributes;
	CloseHandle(h);
	return result;
}

static void show(const wchar_t *dir, const wchar_t *name, const char *what)
{
	wchar_t path[MAX_PATH];
	unsigned long tag, l;

	swprintf(path, MAX_PATH, L"%ls\\%ls", dir, name);
	l = listing(dir, name, &tag);
	printf("  %-16s mode %d: listing 0x%08lx (tag 0x%08lx),"
	       " path 0x%08lx, handle 0x%08lx\n", what, (int)qmode(), l, tag,
	       path_query(path), handle_query(path));
}

int wmain(int argc, wchar_t **argv)
{
	int m;

	if (argc != 3) {
		fprintf(stderr, "usage: phattr DIR NAME\n");
		return 1;
	}
	if (!LOAD(qdir, qdir_fn, "NtQueryDirectoryFile") ||
	    !LOAD(qfull, qfull_fn, "NtQueryFullAttributesFile") ||
	    !LOAD(dos2nt, dos2nt_fn,
		  "RtlDosPathNameToNtPathName_U_WithStatus") ||
	    !LOAD(freeus, freeus_fn, "RtlFreeUnicodeString") ||
	    !LOAD(qmode, qmode_fn,
		  "RtlQueryProcessPlaceholderCompatibilityMode") ||
	    !LOAD(smode, smode_fn,
		  "RtlSetProcessPlaceholderCompatibilityMode")) {
		printf("ntdll function not found\n");
		return 1;
	}
	show(argv[1], argv[2], "as started");
	for (m = 0; m <= 2; m++) {
		char what[32];

		smode((CHAR)m);
		snprintf(what, sizeof(what), "after setting %d", m);
		show(argv[1], argv[2], what);
	}
	return 0;
}
