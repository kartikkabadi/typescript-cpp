# conformance_check.ps1 — Windows port of conformance_check.sh.
# Byte-compares `tscpp check` output vs Go `checkdump` oracle for each file.
#   ./conformance_check.ps1 <file.ts> ...   -> per-file PASS/FAIL
#   ./conformance_check.ps1 -List <listfile> -> over a file list
param(
    [string]$List,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Files
)
$ErrorActionPreference = 'Continue'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Checkdump = if ($env:CHECKDUMP) { $env:CHECKDUMP } else { Join-Path $RepoRoot 'tsc\build-oracles\checkdump.exe' }
$Tscpp = if ($env:TSCPP) { $env:TSCPP } else { Join-Path $RepoRoot 'cpp\build\tscpp.exe' }
$Tmp = Join-Path $env:TEMP 'conformance'
New-Item -ItemType Directory -Force $Tmp | Out-Null

if ($List) { $Files = Get-Content $List | Where-Object { $_ -ne '' } }

$pass = 0; $fail = 0; $failures = @()
foreach ($F in $Files) {
    if ($F -notmatch '^[A-Za-z]:[\\/]') { $F = Join-Path $RepoRoot $F }
    $id = [guid]::NewGuid().ToString('N').Substring(0, 12)
    $goOut = Join-Path $Tmp "go_$id.txt"
    $cppOut = Join-Path $Tmp "cpp_$id.txt"
    # cmd redirection preserves raw stdout bytes (no PS re-encoding).
    Push-Location $RepoRoot
    cmd /c "`"$Checkdump`" `"$F`" > `"$goOut`" 2> NUL"
    $goRc = $LASTEXITCODE
    cmd /c "`"$Tscpp`" check `"$F`" > `"$cppOut`" 2> NUL"
    $cppRc = $LASTEXITCODE
    Pop-Location
    $goBytes = [IO.File]::ReadAllBytes($goOut)
    $cppBytes = [IO.File]::ReadAllBytes($cppOut)
    $same = ($goBytes.Length -eq $cppBytes.Length)
    if ($same) {
        for ($i = 0; $i -lt $goBytes.Length; $i++) {
            if ($goBytes[$i] -ne $cppBytes[$i]) { $same = $false; break }
        }
    }
    # rc >= 126: tool never ran (crash/not found) — never PASS two empties.
    if ($same -and $goRc -lt 126 -and $cppRc -lt 126) {
        Write-Output "PASS $F"
        $pass++
    } else {
        Write-Output "FAIL $F (rc go=$goRc cpp=$cppRc bytes go=$($goBytes.Length) cpp=$($cppBytes.Length))"
        $fail++
        $failures += $F
    }
}
Write-Output "TALLY $pass pass / $fail fail / $($Files.Count) total"
if ($failures.Count -gt 0) { exit 1 }
