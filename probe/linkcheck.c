/*
 * Does the toolchain's ntdll import library export what a static
 * (non-lazy) use in compat/mingw.c would need? Not part of Git.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef LONG NTSTATUS;

__declspec(dllimport) NTSTATUS NTAPI NtQueryFullAttributesFile(void *, void *);
__declspec(dllimport) NTSTATUS NTAPI
RtlDosPathNameToNtPathName_U_WithStatus(const WCHAR *, void *, WCHAR **,
					void *);
__declspec(dllimport) NTSTATUS NTAPI
RtlDosPathNameToRelativeNtPathName_U_WithStatus(const WCHAR *, void *,
						WCHAR **, void *);
__declspec(dllimport) VOID NTAPI RtlReleaseRelativeName(void *);
__declspec(dllimport) VOID NTAPI RtlFreeUnicodeString(void *);
__declspec(dllimport) ULONG NTAPI RtlNtStatusToDosError(NTSTATUS);
__declspec(dllimport) NTSTATUS NTAPI NtQueryInformationFile(HANDLE, void *,
							     void *, ULONG,
							     ULONG);

int main(int argc, char **argv)
{
	(void)argv;
	if (argc > 99) { /* never true; only forces the references */
		NtQueryFullAttributesFile(NULL, NULL);
		RtlDosPathNameToNtPathName_U_WithStatus(NULL, NULL, NULL, NULL);
		RtlDosPathNameToRelativeNtPathName_U_WithStatus(NULL, NULL,
								NULL, NULL);
		RtlReleaseRelativeName(NULL);
		RtlFreeUnicodeString(NULL);
		NtQueryInformationFile(NULL, NULL, NULL, 0, 0);
	}
	printf("RtlNtStatusToDosError(0xC0000034) = %lu\n",
	       (unsigned long)RtlNtStatusToDosError((NTSTATUS)0xC0000034L));
	return 0;
}
