/*
 * Attributes and times of each FILE, with cloud file placeholders exposed,
 * without reading the files' data:
 *   cfattr FILE...
 * prints, per FILE: FILE attr=0x... C=... W=... Ch=...
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef CHAR (NTAPI *set_fn)(CHAR);

int wmain(int argc, wchar_t **argv)
{
	FARPROC p = GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
				   "RtlSetProcessPlaceholderCompatibilityMode");
	set_fn set;
	int i, ret = 0;

	memcpy(&set, &p, sizeof(set));
	if (set)
		set(2); /* PHCM_EXPOSE_PLACEHOLDERS */
	for (i = 1; i < argc; i++) {
		HANDLE h = CreateFileW(argv[i], 0, FILE_SHARE_READ |
				       FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				       NULL, OPEN_EXISTING,
				       FILE_FLAG_BACKUP_SEMANTICS |
				       FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		FILE_BASIC_INFO b;

		if (h == INVALID_HANDLE_VALUE ||
		    !GetFileInformationByHandleEx(h, FileBasicInfo, &b,
						  sizeof(b))) {
			printf("%ls error=%lu\n", argv[i],
			       (unsigned long)GetLastError());
			ret = 1;
		} else
			printf("%ls attr=0x%08lx C=%lld W=%lld Ch=%lld\n",
			       argv[i], (unsigned long)b.FileAttributes,
			       (long long)b.CreationTime.QuadPart,
			       (long long)b.LastWriteTime.QuadPart,
			       (long long)b.ChangeTime.QuadPart);
		if (h != INVALID_HANDLE_VALUE)
			CloseHandle(h);
	}
	return ret;
}
