/*
 * Placeholder compatibility mode of this program, as Windows sets it,
 * and the mode of a child started after the parent changed its own:
 *   phcm           print the mode
 *   phcm child M   set the mode to M, then run this program again
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef CHAR (NTAPI *query_fn)(void);
typedef CHAR (NTAPI *set_fn)(CHAR);

int main(int argc, char **argv)
{
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	FARPROC pq = GetProcAddress(ntdll,
			"RtlQueryProcessPlaceholderCompatibilityMode");
	FARPROC ps = GetProcAddress(ntdll,
			"RtlSetProcessPlaceholderCompatibilityMode");
	query_fn q;
	set_fn s;
	char name[MAX_PATH], *base;

	if (!pq || !ps) {
		printf("placeholder mode functions not found\n");
		return 1;
	}
	memcpy(&q, &pq, sizeof(q));
	memcpy(&s, &ps, sizeof(s));
	GetModuleFileNameA(NULL, name, MAX_PATH);
	base = strrchr(name, '\\');
	base = base ? base + 1 : name;
	if (argc == 3 && !strcmp(argv[1], "child")) {
		STARTUPINFOA si;
		PROCESS_INFORMATION pi;
		char cmd[MAX_PATH + 2];
		CHAR before = q();
		CHAR ret = s((CHAR)atoi(argv[2]));

		printf("%-16s mode %d, set to %s (returned %d), now %d;"
		       " its child: ", base, before, argv[2], ret, q());
		fflush(stdout);
		memset(&si, 0, sizeof(si));
		si.cb = sizeof(si);
		snprintf(cmd, sizeof(cmd), "\"%s\"", name);
		if (!CreateProcessA(name, cmd, NULL, NULL, TRUE, 0, NULL, NULL,
				    &si, &pi)) {
			printf("CreateProcess error %lu\n",
			       (unsigned long)GetLastError());
			return 1;
		}
		WaitForSingleObject(pi.hProcess, INFINITE);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return 0;
	}
	printf("%-16s mode %d (0: default, 1: disguise, 2: expose)\n",
	       base, q());
	return 0;
}
