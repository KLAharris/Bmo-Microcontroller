# Builds a ready-to-import CubeIDE test project: P2's project + P4's games and home screen.
# Nothing in the repo is changed. Run it again any time P2 or P4 push something new.
#
#   powershell -ExecutionPolicy Bypass -File team\p4\Tools\make_board_test.ps1
#   (optional)  -Dest "D:\somewhere\p4_board_test"      default: <folder next to the repo>\bmo_p4_test
#   (optional)  -Force                                  replace an existing test folder
#
# Then in CubeIDE: File > Switch Workspace > Other... (a NEW workspace, because the
# project is called "boum" like P2's), File > Open Projects from File System...,
# Directory = the folder printed at the end, Finish. Select the project, Build, Run.
param([string]$Dest = "", [switch]$Force)

$p4   = Split-Path -Parent $PSScriptRoot            # ...\team\p4
$repo = Split-Path -Parent (Split-Path -Parent $p4) # repo root
$p2   = Join-Path $repo "team\p2\boum"
if (-not (Test-Path "$p2\Core\Inc\main.h")) { Write-Error "P2's project not found at $p2"; exit 1 }
if ($Dest -eq "") { $Dest = Join-Path (Split-Path -Parent $repo) "bmo_p4_test" }

if (Test-Path $Dest) {
    if (-not $Force) { Write-Error "$Dest already exists. Use -Force to replace it, or pick another -Dest."; exit 1 }
    Remove-Item $Dest -Recurse -Force
}
New-Item -ItemType Directory -Force $Dest | Out-Null

# 1. P2's project, without build output
robocopy $p2 $Dest /E /XD Debug Release .git /NFL /NDL /NJH /NJS /NP | Out-Null

# 2. P4's files
& "$PSScriptRoot\sync_to_test.ps1" -Proj $Dest | Out-Null

# 3. the three lines in main.c (only if not there yet)
$main = Join-Path $Dest "Core\Src\main.c"
$text = [System.IO.File]::ReadAllText($main)
$nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
if (-not $text.Contains('p4_demo.h')) {
    $text = $text.Replace('#include "bmo_face.h"', '#include "bmo_face.h"' + $nl + '#include "p4_demo.h"')
    $text = $text.Replace('say("Face ready\r\n");', 'say("Face ready\r\n");' + $nl + '  p4_demo_init();')
    $text = $text.Replace('face_tick(HAL_GetTick());', 'face_tick(HAL_GetTick());' + $nl + '    p4_demo_tick(HAL_GetTick());')
    [System.IO.File]::WriteAllText($main, $text, (New-Object System.Text.UTF8Encoding($false)))
}
$ok = (Select-String -Path $main -Pattern 'p4_demo.h','p4_demo_init','p4_demo_tick').Count
if ($ok -ne 3) { Write-Error "main.c was not patched correctly ($ok of 3 lines found). Add the 3 lines by hand (see README)."; exit 1 }

Write-Host ""
Write-Host "Test project ready: $Dest"
Write-Host "CubeIDE: new workspace > File > Open Projects from File System > Directory = $Dest"
Write-Host "Then: select the project, Project > Build Project, Run > Run As > STM32 C/C++ Application."
Write-Host "Serial terminal 115200: m = home screen, 1-4 = apps, h = help."
