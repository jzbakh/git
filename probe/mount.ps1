# F: FAT32, G: exFAT, H: ReFS, N: NTFS (VHDs); M: WinFsp memfs; R: rclone
$ErrorActionPreference = 'Continue'
foreach ($v in @(@('fat32', 'F', 512), @('exfat', 'G', 512),
                 @('refs', 'H', 4096), @('ntfs', 'N', 512))) {
  $fs, $letter, $mb = $v
  $vhd = "$env:RUNNER_TEMP\$fs.vhdx"
  @("create vdisk file=`"$vhd`" maximum=$mb type=expandable",
    "select vdisk file=`"$vhd`"",
    "attach vdisk",
    "create partition primary",
    "format fs=$fs quick label=P3707",
    "assign letter=$letter") |
    Set-Content -Encoding ascii "$env:RUNNER_TEMP\dp-$fs.txt"
  diskpart /s "$env:RUNNER_TEMP\dp-$fs.txt" | Select-Object -Last 1
}
New-Item -ItemType Directory -Force "$env:RUNNER_TEMP\smb" | Out-Null
choco install winfsp rclone -y --no-progress | Select-Object -Last 2
$memfs = Get-ChildItem -Recurse -Filter memfs-x64.exe `
  "${env:ProgramFiles(x86)}\WinFsp", "$env:ProgramFiles\WinFsp" `
  -ErrorAction SilentlyContinue | Select-Object -First 1
if ($memfs) {
  Start-Process $memfs.FullName -WindowStyle Hidden `
    -ArgumentList '-i', '-F', 'NTFS', '-n', '65536', '-s', '268435456', '-m', 'M:'
} else { 'memfs-x64.exe not found' }
New-Item -ItemType Directory -Force C:\rclone-src | Out-Null
$rclone = (Get-Command rclone -ErrorAction SilentlyContinue).Source
if ($rclone) {
  Start-Process $rclone -WindowStyle Hidden `
    -ArgumentList 'mount', 'C:\rclone-src', 'R:', '--vfs-cache-mode', 'full'
} else { 'rclone not found' }
foreach ($d in 'M:\', 'R:\') {
  for ($i = 0; $i -lt 30 -and !(Test-Path $d); $i++) { Start-Sleep 1 }
  "$d present: $(Test-Path $d)"
}
Get-Volume | Sort-Object DriveLetter |
  Format-Table -AutoSize DriveLetter, FileSystem, FileSystemLabel, Size
'Cloud Files minifilter:'
fltmc filters | Select-String -Pattern 'cld' -SimpleMatch
sc.exe query cldflt | Select-String -Pattern 'STATE'
