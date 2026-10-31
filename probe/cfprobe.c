/*
 * Minimal Cloud Files sync provider, like OneDrive's Files On-Demand:
 * what do conversion, unpinning and dehydration ("Free up space") do to
 * the times and attributes Git reads, and does Git then download files?
 *
 * usage: cfprobe ROOT SECONDS FILE...
 * FILEs are relative to ROOT. Their contents are kept in memory, they
 * become in-sync placeholders ("READY1"); once ROOT\go exists, they are
 * unpinned and dehydrated ("READY2"), like OneDrive's "Free up space".
 * Hydration requests are served and logged until SECONDS have passed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <cfapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "cldapi.lib")

#define MAXF 64

static int nfiles;
static wchar_t *names[MAXF];
static char *data[MAXF];
static LONGLONG sizes[MAXF];
static FILE_BASIC_INFO first[MAXF];

static const char *mv(LONGLONG a, LONGLONG b)
{
	return a == b ? "kept " : "MOVED";
}

static void show(int i, const wchar_t *path, const char *stage)
{
	HANDLE h = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ |
			       FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			       OPEN_EXISTING, 0, NULL);
	FILE_BASIC_INFO b;
	FILE_STANDARD_INFO s;
	LONGLONG ch, c, w;

	if (h == INVALID_HANDLE_VALUE ||
	    !GetFileInformationByHandleEx(h, FileBasicInfo, &b, sizeof(b)) ||
	    !GetFileInformationByHandleEx(h, FileStandardInfo, &s, sizeof(s))) {
		printf("  %-11ls %-11s ERROR %lu\n", names[i], stage,
		       (unsigned long)GetLastError());
		if (h != INVALID_HANDLE_VALUE)
			CloseHandle(h);
		return;
	}
	CloseHandle(h);
	if (!strcmp(stage, "start"))
		first[i] = b;
	ch = b.ChangeTime.QuadPart;
	c = b.CreationTime.QuadPart;
	w = b.LastWriteTime.QuadPart;
	printf("  %-11ls %-11s attr=0x%08lx size=%lld vs start: C %s W %s"
	       " Ch %s | Ch>max(C,W): %s\n",
	       names[i], stage, (unsigned long)b.FileAttributes,
	       (long long)s.EndOfFile.QuadPart,
	       mv(c, first[i].CreationTime.QuadPart),
	       mv(w, first[i].LastWriteTime.QuadPart),
	       mv(ch, first[i].ChangeTime.QuadPart),
	       ch > c && ch > w ? "yes" : "no");
}

static void CALLBACK on_fetch(const CF_CALLBACK_INFO *info,
			      const CF_CALLBACK_PARAMETERS *params)
{
	const wchar_t *p = info->NormalizedPath;
	size_t lp = wcslen(p);
	CF_OPERATION_INFO op;
	CF_OPERATION_PARAMETERS par;
	int i, found = -1;
	HRESULT hr;

	for (i = 0; i < nfiles; i++) {
		size_t ln = wcslen(names[i]);

		if (lp > ln && p[lp - ln - 1] == L'\\' &&
		    !_wcsicmp(p + lp - ln, names[i]))
			found = i;
	}
	memset(&op, 0, sizeof(op));
	op.StructSize = sizeof(op);
	op.Type = CF_OPERATION_TYPE_TRANSFER_DATA;
	op.ConnectionKey = info->ConnectionKey;
	op.TransferKey = info->TransferKey;
	memset(&par, 0, sizeof(par));
	par.ParamSize = (ULONG)(FIELD_OFFSET(CF_OPERATION_PARAMETERS,
					     TransferData) +
				sizeof(par.TransferData));
	if (found < 0) {
		par.TransferData.CompletionStatus = (NTSTATUS)0xC0000001L;
		par.TransferData.Offset = params->FetchData.RequiredFileOffset;
		par.TransferData.Length = params->FetchData.RequiredLength;
	} else {
		par.TransferData.CompletionStatus = 0;
		par.TransferData.Buffer = data[found];
		par.TransferData.Offset.QuadPart = 0;
		par.TransferData.Length.QuadPart = sizes[found];
	}
	hr = CfExecute(&op, &par);
	printf("FETCH_DATA %ls offset=%lld length=%lld by %ls -> hr=0x%08lx\n",
	       p, (long long)params->FetchData.RequiredFileOffset.QuadPart,
	       (long long)params->FetchData.RequiredLength.QuadPart,
	       info->ProcessInfo && info->ProcessInfo->ImagePath ?
	       info->ProcessInfo->ImagePath : L"?", (unsigned long)hr);
}

static void CALLBACK on_cancel(const CF_CALLBACK_INFO *info,
			       const CF_CALLBACK_PARAMETERS *params)
{
	(void)params;
	printf("CANCEL_FETCH_DATA %ls\n", info->NormalizedPath);
}

static CF_CALLBACK_REGISTRATION table[] = {
	{ CF_CALLBACK_TYPE_FETCH_DATA, on_fetch },
	{ CF_CALLBACK_TYPE_CANCEL_FETCH_DATA, on_cancel },
	CF_CALLBACK_REGISTRATION_END
};

int wmain(int argc, wchar_t **argv)
{
	static const GUID id = { 0x5c1e3707, 0x1d0b, 0x4c5e,
		{ 0x9a, 0x37, 0x07, 0x37, 0x07, 0x37, 0x07, 0x37 } };
	CF_SYNC_REGISTRATION reg;
	CF_SYNC_POLICIES pol;
	CF_CONNECTION_KEY key;
	wchar_t path[MAX_PATH];
	HRESULT hr;
	int i, seconds;

	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 4 || argc - 3 > MAXF) {
		fprintf(stderr, "usage: cfprobe ROOT SECONDS FILE...\n");
		return 1;
	}
	seconds = _wtoi(argv[2]);
	nfiles = argc - 3;

	for (i = 0; i < nfiles; i++) {
		HANDLE h;
		DWORD n;

		names[i] = argv[3 + i];
		swprintf(path, MAX_PATH, L"%ls\\%ls", argv[1], names[i]);
		h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
				OPEN_EXISTING, 0, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			printf("cannot open %ls: %lu\n", path,
			       (unsigned long)GetLastError());
			return 1;
		}
		GetFileSizeEx(h, (LARGE_INTEGER *)&sizes[i]);
		data[i] = malloc((size_t)sizes[i] + 1);
		ReadFile(h, data[i], (DWORD)sizes[i], &n, NULL);
		CloseHandle(h);
		show(i, path, "start");
	}

	memset(&reg, 0, sizeof(reg));
	reg.StructSize = sizeof(reg);
	reg.ProviderName = L"cfprobe";
	reg.ProviderVersion = L"1.0";
	reg.ProviderId = id;
	memset(&pol, 0, sizeof(pol));
	pol.StructSize = sizeof(pol);
	*(USHORT *)&pol.Hydration.Primary = CF_HYDRATION_POLICY_FULL;
	*(USHORT *)&pol.Population.Primary = CF_POPULATION_POLICY_ALWAYS_FULL;
	pol.InSync = CF_INSYNC_POLICY_TRACK_ALL;
	pol.HardLink = CF_HARDLINK_POLICY_NONE;
	hr = CfRegisterSyncRoot(argv[1], &reg, &pol, CF_REGISTER_FLAG_NONE);
	printf("CfRegisterSyncRoot: 0x%08lx\n", (unsigned long)hr);
	if (FAILED(hr))
		return 1;
	hr = CfConnectSyncRoot(argv[1], table, NULL,
			       CF_CONNECT_FLAG_REQUIRE_PROCESS_INFO |
			       CF_CONNECT_FLAG_REQUIRE_FULL_FILE_PATH, &key);
	printf("CfConnectSyncRoot: 0x%08lx\n", (unsigned long)hr);
	if (FAILED(hr))
		return 1;

	for (i = 0; i < nfiles; i++) {
		HANDLE h;

		swprintf(path, MAX_PATH, L"%ls\\%ls", argv[1], names[i]);
		h = CreateFileW(path, FILE_READ_DATA | FILE_WRITE_DATA |
				FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE |
				FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			printf("cannot open %ls for conversion: %lu\n", path,
			       (unsigned long)GetLastError());
			continue;
		}
		hr = CfConvertToPlaceholder(h, names[i],
					    (DWORD)(wcslen(names[i]) *
						    sizeof(wchar_t)),
					    CF_CONVERT_FLAG_MARK_IN_SYNC, NULL,
					    NULL);
		if (FAILED(hr))
			printf("  %-11ls CfConvertToPlaceholder: 0x%08lx\n",
			       names[i], (unsigned long)hr);
		CloseHandle(h);
		show(i, path, "converted");
	}
	printf("READY1\n");
	{
		wchar_t go[MAX_PATH];
		int t;

		swprintf(go, MAX_PATH, L"%ls\\go", argv[1]);
		for (t = 0; t < 600 && GetFileAttributesW(go) ==
		     INVALID_FILE_ATTRIBUTES; t++)
			Sleep(200);
	}
	for (i = 0; i < nfiles; i++) {
		HANDLE h;
		LARGE_INTEGER zero, all;

		swprintf(path, MAX_PATH, L"%ls\\%ls", argv[1], names[i]);
		h = CreateFileW(path, FILE_READ_DATA | FILE_WRITE_DATA |
				FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE |
				FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			printf("cannot open %ls to free it: %lu\n", path,
			       (unsigned long)GetLastError());
			continue;
		}
		hr = CfSetPinState(h, CF_PIN_STATE_UNPINNED,
				   CF_SET_PIN_FLAG_NONE, NULL);
		if (FAILED(hr))
			printf("  %-11ls CfSetPinState: 0x%08lx\n", names[i],
			       (unsigned long)hr);
		zero.QuadPart = 0;
		all.QuadPart = -1;
		hr = CfDehydratePlaceholder(h, zero, all,
					    CF_DEHYDRATE_FLAG_NONE, NULL);
		if (FAILED(hr))
			printf("  %-11ls CfDehydratePlaceholder: 0x%08lx\n",
			       names[i], (unsigned long)hr);
		CloseHandle(h);
		show(i, path, "freed");
	}
	printf("READY2\n");
	Sleep((DWORD)seconds * 1000);
	CfDisconnectSyncRoot(key);
	printf("DONE\n");
	return 0;
}
