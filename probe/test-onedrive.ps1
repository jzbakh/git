<#
Git for Windows issue 3707: what "git status" does to OneDrive files.

Compares the installed Git (if any), the pull request, and the variant
it rejects (ChangeTime for all files, no exemption for reparse points),
with and without the FSCache, on in-sync and on freed OneDrive files
("Free up space"), with OneDrive running and then closed.

It only works in a new folder inside OneDrive, closes OneDrive for about
a minute, then starts it again. It writes everything to result-*.txt.

  powershell -ExecutionPolicy Bypass -File .\test-onedrive.ps1

-Root <folder>  test folder (default: <OneDrive>\git-3707-test-<time>)
-SelfTest       run without OneDrive, to check the script and the builds
#>
param(
	[string]$Root,
	[switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $here "result-$stamp.txt"
Start-Transcript -Path $log | Out-Null

$N = 20                                 # files per repository
$RECALL_ON_DATA_ACCESS = 0x00400000     # set on dehydrated placeholders
$REPARSE_POINT = 0x00000400             # set on all placeholders
$cfattr = Join-Path $here 'cfattr.exe'

function Invoke-Native([string]$exe, [string[]]$arguments) {
	$psi = New-Object System.Diagnostics.ProcessStartInfo
	$psi.FileName = $exe
	$quoted = foreach ($a in $arguments) {
		if ($a -match '[\s"]') { '"' + ($a -replace '"', '\"') + '"' }
		else { $a }
	}
	$psi.Arguments = $quoted -join ' '
	$psi.UseShellExecute = $false
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.CreateNoWindow = $true
	$p = [System.Diagnostics.Process]::Start($psi)
	$out = $p.StandardOutput.ReadToEndAsync()
	$err = $p.StandardError.ReadToEndAsync()
	$p.WaitForExit()
	[pscustomobject]@{ Exit = $p.ExitCode; Out = $out.Result.Trim();
			   Err = $err.Result.Trim() }
}

# attributes and times of files, placeholders exposed, data not read
function Get-Attr([string[]]$files) {
	$r = Invoke-Native $cfattr $files
	$h = @{}
	foreach ($line in ($r.Out -split "`r?`n")) {
		if ($line -match '^(.*) attr=0x([0-9a-f]+) C=(\d+) W=(\d+) Ch=(\d+)$') {
			$h[$Matches[1]] = [pscustomobject]@{
				Attr = [Convert]::ToInt64($Matches[2], 16)
				C = $Matches[3]; W = $Matches[4]; Ch = $Matches[5] }
		} elseif ($line) { Write-Host "cfattr: $line" }
	}
	$h
}

function Get-Files($case) {
	0..($N - 1) | ForEach-Object { Join-Path $case.Dir ('f{0:D2}' -f $_) }
}

# how many of the case's files have all/none of the given attribute bits
function Count-With($case, [long]$bit) {
	$a = Get-Attr (Get-Files $case)
	@($a.Values | Where-Object { ($_.Attr -band $bit) -ne 0 }).Count
}

function Wait-For([string]$what, [int]$seconds, [scriptblock]$done) {
	$t = 0
	while (-not (& $done)) {
		if ($t -ge $seconds) { throw "timed out waiting for $what" }
		Start-Sleep -Seconds 5
		$t += 5
		Write-Host "  waiting for $what ($t s)"
	}
}

function G($case, [string[]]$arguments) {
	Invoke-Native $case.Exe (@('-C', $case.Dir, '-c', "core.fscache=$($case.FSCache)",
		'-c', 'core.autocrlf=false') + $arguments)
}

function Show-Result($r) {
	$text = (@($r.Out, $r.Err) | Where-Object { $_ }) -join ' | '
	if (-not $text) { $text = '(nothing)' }
	"exit $($r.Exit): $($text -replace "`r?`n", ' ')"
}

# --- setup -----------------------------------------------------------------

Write-Host "Windows: $((Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' |
	ForEach-Object { "$($_.ProductName) $($_.DisplayVersion) build $($_.CurrentBuild).$($_.UBR)" }))"

$od = $null
if (-not $SelfTest) {
	if (-not $env:OneDrive -or -not (Test-Path $env:OneDrive)) {
		throw 'OneDrive folder not found ($env:OneDrive is not set)'
	}
	$proc = Get-Process OneDrive -ErrorAction SilentlyContinue | Select-Object -First 1
	if (-not $proc) { throw 'OneDrive is not running; start it and try again' }
	$od = $proc.Path
	Write-Host "OneDrive: $od, version $((Get-Item $od).VersionInfo.FileVersion)"
	if (-not $Root) { $Root = Join-Path $env:OneDrive "git-3707-test-$stamp" }
} elseif (-not $Root) {
	$Root = Join-Path $env:TEMP "git-3707-selftest-$stamp"
}
if (Test-Path $Root) { throw "$Root already exists" }
New-Item -ItemType Directory -Path $Root | Out-Null
Write-Host "Test folder: $Root"

$empty = Join-Path $Root 'empty.gitconfig'
[IO.File]::WriteAllText($empty, '')
$env:GIT_CONFIG_NOSYSTEM = '1'
$env:GIT_CONFIG_GLOBAL = $empty

$gits = [ordered]@{}
$installed = Get-Command git.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ($installed) {
	$gits['installed Git'] = $installed.Source
} else {
	$gd = Get-ChildItem "$env:LOCALAPPDATA\GitHubDesktop\app-*\resources\app\git\cmd\git.exe" `
		-ErrorAction SilentlyContinue | Select-Object -Last 1
	if ($gd) { $gits['GitHub Desktop Git'] = $gd.FullName }
}
$gits['without exemption'] = Join-Path $here 'rejected\git.exe'
$gits['pull request'] = Join-Path $here 'pr\git.exe'

$cases = foreach ($name in $gits.Keys) {
	$v = Invoke-Native $gits[$name] @('version')
	if ($v.Exit -ne 0) { throw "$($gits[$name]) does not run: $($v.Err)" }
	Write-Host "$name`: $($gits[$name]) ($($v.Out))"
	foreach ($fs in 'true', 'false') {
		[pscustomobject]@{
			Name = "$name, core.fscache=$fs"; Exe = $gits[$name]; FSCache = $fs
			Dir = Join-Path $Root (($name -replace '[^A-Za-z0-9]+', '-') + "-fscache-$fs")
			Result = [ordered]@{} }
	}
}

foreach ($c in $cases) {
	New-Item -ItemType Directory -Path $c.Dir | Out-Null
	foreach ($f in Get-Files $c) {
		$i = [int]($f.Substring($f.Length - 2))
		[IO.File]::WriteAllText($f, ("file $i`n" * (1 + 20 * $i)))
	}
	foreach ($cmd in @(@('-c', 'init.defaultBranch=main', 'init', '-q'), @('add', '.'),
			   @('-c', 'user.name=t', '-c', 'user.email=t@example.com',
			     'commit', '-q', '-m', 'one'))) {
		$r = G $c $cmd
		if ($r.Exit -ne 0) { throw "$($c.Name): git $($cmd -join ' '): $($r.Err)" }
	}
}
Start-Sleep -Seconds 2
foreach ($c in $cases) { [void](G $c @('update-index', '-q', '--refresh')) }

if ($SelfTest) {
	# new files have FILE_ATTRIBUTE_ARCHIVE: check that cfattr reads them all
	foreach ($c in $cases) {
		$n = Count-With $c 0x20
		if ($n -ne $N) { throw "cfattr: $n of $N files read in $($c.Dir)" }
	}
	Write-Host "cfattr read the attributes of all $($N * @($cases).Count) files"
} else {
	Wait-For 'OneDrive to upload the files' 600 {
		foreach ($c in $cases) {
			if ((Count-With $c $REPARSE_POINT) -lt $N) { return $false }
		}
		$true
	}
}

# --- 1. in-sync files --------------------------------------------------------

Write-Host "`n1. In-sync files, OneDrive running"
foreach ($c in $cases) {
	$c.Result['1 in-sync'] = Show-Result (G $c @('status', '--porcelain'))
	Write-Host "  $($c.Name): $($c.Result['1 in-sync'])"
}

function Free-All {
	foreach ($c in $cases) {
		$r = Invoke-Native 'attrib.exe' @('+U', '-P', (Join-Path $c.Dir 'f*'))
		if ($r.Exit -ne 0) { throw "attrib: $($r.Out) $($r.Err)" }
	}
	Wait-For 'OneDrive to free up the files' 300 {
		foreach ($c in $cases) {
			if ((Count-With $c $RECALL_ON_DATA_ACCESS) -lt $N) { return $false }
		}
		$true
	}
}

if (-not $SelfTest) {
	$probe = (Get-Files $cases[0])[0]
	$before = (Get-Attr @($probe))[$probe]
	Free-All
	$after = (Get-Attr @($probe))[$probe]
	Write-Host ("`nFree up space, {0}: CreationTime {1}, LastWriteTime {2}, ChangeTime {3}" -f $probe,
		$(if ($before.C -eq $after.C) { 'kept' } else { 'MOVED' }),
		$(if ($before.W -eq $after.W) { 'kept' } else { 'MOVED' }),
		$(if ($before.Ch -eq $after.Ch) { 'kept' } else { 'MOVED' }))

	# --- 2. freed files, OneDrive running ------------------------------
	Write-Host "`n2. Freed files, OneDrive running"
	foreach ($c in $cases) {
		$r = Show-Result (G $c @('status', '--porcelain'))
		$down = $N - (Count-With $c $RECALL_ON_DATA_ACCESS)
		$c.Result['2 freed'] = "downloaded $down of $N; $r"
		Write-Host "  $($c.Name): $($c.Result['2 freed'])"
	}

	# --- 3. freed files, OneDrive closed -------------------------------
	Write-Host "`n3. Freed files, OneDrive closed"
	Free-All
	& $od /shutdown
	Wait-For 'OneDrive to close' 120 {
		-not (Get-Process OneDrive -ErrorAction SilentlyContinue)
	}
	try {
		foreach ($c in $cases) {
			$r = Show-Result (G $c @('status', '--porcelain'))
			$c.Result['3 OneDrive closed'] = $r
			Write-Host "  $($c.Name): $r"
		}
	} finally {
		Start-Process $od -ArgumentList '/background'
		Write-Host 'OneDrive started again'
	}
}

# --- summary -----------------------------------------------------------------

Write-Host "`nSummary"
foreach ($c in $cases) {
	Write-Host $c.Name
	foreach ($k in $c.Result.Keys) { Write-Host "  $k`: $($c.Result[$k])" }
}
Write-Host "`nDone. Please send $log"
Write-Host "You can delete $Root afterwards."
Stop-Transcript | Out-Null
