/* print the placeholder compatibility mode Windows gives this program */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef CHAR (NTAPI *query_fn)(void);

int main(void)
{
	FARPROC p = GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
				   "RtlQueryProcessPlaceholderCompatibilityMode");
	query_fn q;
	char name[MAX_PATH], *base;

	memcpy(&q, &p, sizeof(q));
	GetModuleFileNameA(NULL, name, MAX_PATH);
	base = strrchr(name, '\\');
	printf("%-16s mode %d (0: default, 1: disguise, 2: expose)\n",
	       base ? base + 1 : name, q ? (int)q() : -100);
	return 0;
}
