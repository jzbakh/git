# Reproduit git-for-windows/git#3707 : Git ne voit pas une modification
# quand un fichier est supprime puis recree avec la meme taille et la meme
# date de modification.
#
# Usage :
#   powershell -ExecutionPolicy Bypass -File repro-3707.ps1
#   powershell -ExecutionPolicy Bypass -File repro-3707.ps1 -Git C:\chemin\vers\git.exe
#
# Bug present  : les deux "git status" sont vides.
# Bug corrige  : les deux "git status" affichent " M a.txt".
param([string]$Git = 'git')

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NtQ {
  [StructLayout(LayoutKind.Sequential)] public struct UNICODE_STRING { public ushort Length; public ushort MaximumLength; public IntPtr Buffer; }
  [StructLayout(LayoutKind.Sequential)] public struct OBJECT_ATTRIBUTES { public int Length; public IntPtr RootDirectory; public IntPtr ObjectName; public uint Attributes; public IntPtr SecurityDescriptor; public IntPtr SecurityQualityOfService; }
  [StructLayout(LayoutKind.Sequential)] public struct FILE_NETWORK_OPEN_INFORMATION { public long CreationTime, LastAccessTime, LastWriteTime, ChangeTime, AllocationSize, EndOfFile; public uint FileAttributes; }
  [DllImport("ntdll.dll")] public static extern int NtQueryFullAttributesFile(ref OBJECT_ATTRIBUTES oa, out FILE_NETWORK_OPEN_INFORMATION info);
  public static long[] Query(string path) {
    string nt = @"\??\" + path;
    IntPtr buf = Marshal.StringToHGlobalUni(nt);
    UNICODE_STRING us = new UNICODE_STRING();
    us.Length = (ushort)(nt.Length * 2); us.MaximumLength = (ushort)(nt.Length * 2 + 2); us.Buffer = buf;
    IntPtr pus = Marshal.AllocHGlobal(Marshal.SizeOf(us));
    Marshal.StructureToPtr(us, pus, false);
    OBJECT_ATTRIBUTES oa = new OBJECT_ATTRIBUTES();
    oa.Length = Marshal.SizeOf(oa); oa.ObjectName = pus; oa.Attributes = 0x40;
    FILE_NETWORK_OPEN_INFORMATION fi;
    int st = NtQueryFullAttributesFile(ref oa, out fi);
    Marshal.FreeHGlobal(pus); Marshal.FreeHGlobal(buf);
    if (st != 0) throw new Exception("NTSTATUS 0x" + st.ToString("X8"));
    return new long[] { fi.CreationTime, fi.LastWriteTime, fi.ChangeTime };
  }
}
'@

function Show($label, $path) {
  $t = [NtQ]::Query($path)
  "{0,-6} Creation={1}  LastWrite={2}  Change={3}" -f $label,
    [DateTime]::FromFileTime($t[0]).ToString('HH:mm:ss.fffffff'),
    [DateTime]::FromFileTime($t[1]).ToString('HH:mm:ss.fffffff'),
    [DateTime]::FromFileTime($t[2]).ToString('HH:mm:ss.fffffff')
}

$d = Join-Path $env:TEMP 'repro3707'
if (Test-Path $d) { Remove-Item -Recurse -Force $d }
New-Item -ItemType Directory $d | Out-Null
Push-Location $d
try {
  & $Git --version
  & $Git init -q
  & $Git config user.email 'repro@example.invalid'
  & $Git config user.name 'repro'

  $f = Join-Path $d 'a.txt'
  Set-Content $f 'AAAA' -NoNewline
  $old = (Get-Date).AddHours(-1)
  (Get-Item $f).LastWriteTime = $old
  & $Git add a.txt
  & $Git commit -qm init
  Show 'avant' $f

  # Supprimer puis recreer : meme nom, meme taille, contenu different,
  # et on remet l'ancienne date de modification.
  Remove-Item $f
  Set-Content $f 'BBBB' -NoNewline
  (Get-Item $f).LastWriteTime = $old
  Show 'apres' $f

  "disque : $(Get-Content $f -Raw)   commit : $(& $Git show HEAD:a.txt)"
  "git status (core.fscache=false) :"; & $Git -c core.fscache=false status --short
  "git status (core.fscache=true)  :"; & $Git -c core.fscache=true status --short
} finally {
  Pop-Location
}
