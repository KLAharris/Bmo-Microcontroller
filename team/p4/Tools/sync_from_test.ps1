# Copies P4 files you changed in the CubeIDE test project back into team\p4 (test copy -> repo).
# Only the p4 files move: Games, Apps, Common, Board, and the headers that already exist in
# team\p4\Interfaces. P2's files and main.c are never copied.
#
#   preview only:  powershell -ExecutionPolicy Bypass -File team\p4\Tools\sync_from_test.ps1 -Proj "D:\in\microcontroller\bmo_test\boumm" -Check
#   copy back:     powershell -ExecutionPolicy Bypass -File team\p4\Tools\sync_from_test.ps1 -Proj "D:\in\microcontroller\bmo_test\boumm"
#
# Afterwards: cd to the repo, git status, git add team/p4, git commit, git push.
param([Parameter(Mandatory = $true)][string]$Proj, [switch]$Check)

$p4 = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path "$Proj\Core\Src\p4")) { Write-Error "$Proj\Core\Src\p4 not found. Is -Proj the CubeIDE project folder?"; exit 1 }

# pairs of (test copy file, repo file)
$pairs = @()
foreach ($d in "Games", "Apps", "Common", "Board") {
    Get-ChildItem "$Proj\Core\Src\p4\$d" -File -ErrorAction SilentlyContinue | ForEach-Object {
        $pairs += ,@($_.FullName, "$p4\$d\$($_.Name)")
    }
}
Get-ChildItem "$p4\Interfaces" -File -Filter *.h | ForEach-Object {
    $src = "$Proj\Core\Inc\$($_.Name)"
    if (Test-Path $src) { $pairs += ,@($src, $_.FullName) }
}

$changed = 0
foreach ($p in $pairs) {
    $src = $p[0]; $dst = $p[1]
    $isNew  = -not (Test-Path $dst)
    $differ = $isNew -or ((Get-FileHash $src).Hash -ne (Get-FileHash $dst).Hash)
    if (-not $differ) { continue }
    $changed++
    $rel = $dst.Substring($p4.Length + 1)
    if ($Check) { Write-Host ("  would copy  " + $rel + $(if ($isNew) { "  (new)" } else { "" })) }
    else        { New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
                  Copy-Item $src $dst -Force; Write-Host ("  copied      " + $rel) }
}
if ($changed -eq 0) { Write-Host "No differences: team\p4 already matches the test project." }
elseif ($Check)     { Write-Host "$changed file(s) differ. Run again without -Check to copy them into team\p4." }
else                { Write-Host "$changed file(s) copied into $p4. Next: git status, git add team/p4, git commit, git push." }
