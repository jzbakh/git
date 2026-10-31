/*
 * Which Cloud Files operations change which of CreationTime,
 * LastWriteTime and ChangeTime, each compared to the previous step.
 *
 * usage: cfstages ROOT FILE
 * FILE is relative to ROOT and must exist. ROOT becomes a sync root.
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

static wchar_t *name, path[MAX_PATH];
static char *data;
static LONGLONG size;
static FILE_BASIC_INFO prev;
static int have_prev;

static const char *mv(LONGLONG a, LONGLONG b)
{
	return a == b ? "kept " : "MOVED";
}

static void step(const char *what, HRESULT hr)
{
	HANDLE h;
	FILE_BASIC_INFO b;
	LONGLONG ch, c, w;

	Sleep(100);
	h = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ |
			FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE ||
	    !GetFileInformationByHandleEx(h, FileBasicInfo, &b, sizeof(b))) {
		printf("%-22s ERROR %lu\n", what,
		       (unsigned long)GetLastError());
		if (h != INVALID_HANDLE_VALUE)
			CloseHandle(h);
		return;
	}
	CloseHandle(h);
	ch = b.ChangeTime.QuadPart;
	c = b.CreationTime.QuadPart;
	w = b.LastWriteTime.QuadPart;
	if (!have_prev)
		printf("%-22s hr=0x%08lx attr=0x%08lx\n", what,
		       (unsigned long)hr, (unsigned long)b.FileAttributes);
	else
		printf("%-22s hr=0x%08lx attr=0x%08lx vs previous step:"
		       " C %s W %s Ch %s | Ch>max(C,W): %s\n", what,
		       (unsigned long)hr, (unsigned long)b.FileAttributes,
		       mv(c, prev.CreationTime.QuadPart),
		       mv(w, prev.LastWriteTime.QuadPart),
		       mv(ch, prev.ChangeTime.QuadPart),
		       ch > c && ch > w ? "yes" : "no");
	prev = b;
	have_prev = 1;
}

static void CALLBACK on_fetch(const CF_CALLBACK_INFO *info,
			      const CF_CALLBACK_PARAMETERS *params)
{
	CF_OPERATION_INFO op;
	CF_OPERATION_PARAMETERS par;
	HRESULT hr;

	memset(&op, 0, sizeof(op));
	op.StructSize = sizeof(op);
	op.Type = CF_OPERATION_TYPE_TRANSFER_DATA;
	op.ConnectionKey = info->ConnectionKey;
	op.TransferKey = info->TransferKey;
	memset(&par, 0, sizeof(par));
	par.ParamSize = (ULONG)(FIELD_OFFSET(CF_OPERATION_PARAMETERS,
					     TransferData) +
				sizeof(par.TransferData));
	par.TransferData.CompletionStatus = 0;
	par.TransferData.Buffer = data;
	par.TransferData.Offset.QuadPart = 0;
	par.TransferData.Length.QuadPart = size;
	hr = CfExecute(&op, &par);
	printf("  FETCH_DATA offset=%lld length=%lld -> hr=0x%08lx\n",
	       (long long)params->FetchData.RequiredFileOffset.QuadPart,
	       (long long)params->FetchData.RequiredLength.QuadPart,
	       (unsigned long)hr);
}

static CF_CALLBACK_REGISTRATION table[] = {
	{ CF_CALLBACK_TYPE_FETCH_DATA, on_fetch },
	CF_CALLBACK_REGISTRATION_END
};

static HANDLE open_rw(void)
{
	return CreateFileW(path, FILE_READ_DATA | FILE_WRITE_DATA |
			   FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
			   FILE_SHARE_READ | FILE_SHARE_WRITE |
			   FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
}

static HRESULT pin(CF_PIN_STATE state)
{
	HANDLE h = open_rw();
	HRESULT hr = CfSetPinState(h, state, CF_SET_PIN_FLAG_NONE, NULL);

	CloseHandle(h);
	return hr;
}

static HRESULT in_sync(CF_IN_SYNC_STATE state)
{
	HANDLE h = open_rw();
	HRESULT hr = CfSetInSyncState(h, state, CF_SET_IN_SYNC_FLAG_NONE,
				      NULL);

	CloseHandle(h);
	return hr;
}

static HRESULT dehydrate(void)
{
	HANDLE h = open_rw();
	LARGE_INTEGER zero, all;
	HRESULT hr;

	zero.QuadPart = 0;
	all.QuadPart = -1;
	hr = CfDehydratePlaceholder(h, zero, all, CF_DEHYDRATE_FLAG_NONE,
				    NULL);
	CloseHandle(h);
	return hr;
}

static HRESULT read_all(void)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ |
			       FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			       OPEN_EXISTING, 0, NULL);
	char buf[4096];
	DWORD n = 0;
	BOOL ok;

	if (h == INVALID_HANDLE_VALUE)
		return HRESULT_FROM_WIN32(GetLastError());
	ok = ReadFile(h, buf, sizeof(buf), &n, NULL);
	CloseHandle(h);
	return ok && n == (DWORD)size ? S_OK : E_FAIL;
}

int wmain(int argc, wchar_t **argv)
{
	static const GUID id = { 0x5c1e3708, 0x1d0b, 0x4c5e,
		{ 0x9a, 0x37, 0x07, 0x37, 0x07, 0x37, 0x07, 0x38 } };
	CF_SYNC_REGISTRATION reg;
	CF_SYNC_POLICIES pol;
	CF_CONNECTION_KEY key;
	HANDLE h;
	DWORD n;
	HRESULT hr;

	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc != 3) {
		fprintf(stderr, "usage: cfstages ROOT FILE\n");
		return 1;
	}
	name = argv[2];
	swprintf(path, MAX_PATH, L"%ls\\%ls", argv[1], name);
	h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		printf("cannot open %ls: %lu\n", path,
		       (unsigned long)GetLastError());
		return 1;
	}
	GetFileSizeEx(h, (LARGE_INTEGER *)&size);
	data = malloc((size_t)size + 1);
	ReadFile(h, data, (DWORD)size, &n, NULL);
	CloseHandle(h);
	step("regular file", S_OK);

	memset(&reg, 0, sizeof(reg));
	reg.StructSize = sizeof(reg);
	reg.ProviderName = L"cfstages";
	reg.ProviderVersion = L"1.0";
	reg.ProviderId = id;
	memset(&pol, 0, sizeof(pol));
	pol.StructSize = sizeof(pol);
	*(USHORT *)&pol.Hydration.Primary = CF_HYDRATION_POLICY_FULL;
	*(USHORT *)&pol.Population.Primary = CF_POPULATION_POLICY_ALWAYS_FULL;
	pol.InSync = CF_INSYNC_POLICY_TRACK_ALL;
	pol.HardLink = CF_HARDLINK_POLICY_NONE;
	hr = CfRegisterSyncRoot(argv[1], &reg, &pol, CF_REGISTER_FLAG_NONE);
	step("sync root registered", hr);
	hr = CfConnectSyncRoot(argv[1], table, NULL,
			       CF_CONNECT_FLAG_NONE, &key);
	step("sync root connected", hr);

	h = open_rw();
	hr = CfConvertToPlaceholder(h, name,
				    (DWORD)(wcslen(name) * sizeof(wchar_t)),
				    CF_CONVERT_FLAG_MARK_IN_SYNC, NULL, NULL);
	CloseHandle(h);
	step("converted, in sync", hr);
	step("nothing (control)", S_OK);
	step("pinned", pin(CF_PIN_STATE_PINNED));
	step("unspecified pin", pin(CF_PIN_STATE_UNSPECIFIED));
	step("unpinned", pin(CF_PIN_STATE_UNPINNED));
	step("dehydrated", dehydrate());
	step("read (hydrated)", read_all());
	step("read again", read_all());
	step("not in sync", in_sync(CF_IN_SYNC_STATE_NOT_IN_SYNC));
	step("in sync", in_sync(CF_IN_SYNC_STATE_IN_SYNC));
	step("pinned", pin(CF_PIN_STATE_PINNED));
	step("unpinned", pin(CF_PIN_STATE_UNPINNED));
	step("dehydrated", dehydrate());
	CfDisconnectSyncRoot(key);
	return 0;
}
