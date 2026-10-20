# Copies team\p4 into a private CubeIDE copy of P2's project (one way: repo -> test copy).
# Never edit the p4 files inside the test project; edit them here in the repo, then run this.
#
#   powershell -ExecutionPolicy Bypass -File team\p4\Tools\sync_to_test.ps1 -Proj "D:\in\microcontroller\bmo_test\boumm"
#
# Afterwards in CubeIDE: select the project, F5, Project > Clean, Build, Run.
param([Parameter(Mandatory = $true)][string]$Proj)

$p4 = Split-Path -Parent $PSScriptRoot          # ...\team\p4
if (-not (Test-Path "$Proj\Core\Inc\main.h")) { Write-Error "$Proj is not a CubeIDE project folder (no Core\Inc\main.h)"; exit 1 }

# headers go straight into Core\Inc (already on the include path)
Copy-Item "$p4\Interfaces\*.h" "$Proj\Core\Inc\" -Force

# source folders are mirrored, so a file deleted from the repo is deleted from the copy too
foreach ($d in "Games", "Apps", "Common", "Board") {
    robocopy "$p4\$d" "$Proj\Core\Src\p4\$d" /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
}
Write-Host "Synced $p4 -> $Proj"
Write-Host "main.c needs these 3 lines (see README): include p4_demo.h, p4_demo_init(), p4_demo_tick(HAL_GetTick())"
