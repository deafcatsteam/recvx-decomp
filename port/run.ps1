# Run the RECVX PC port and tee output to runtime.log so the full session
# is captured even if the console scrollback buffer truncates it.
#
# Usage from the port/ directory:
#   .\run.ps1                         # interactive menu picks the boot mode
#   .\run.ps1 -Iso "C:\path\to.iso"   # override ISO path (menu still shown)
#   .\run.ps1 -ExtraArgs "--msa-rate 32000"   # extra flags appended to chosen mode

param(
    [string]$Iso       = "C:\Claude\codeveronica\recvx\recvx.iso",
    [string]$Config    = "Debug",
    [string]$ExtraArgs = "",
    [string]$Log       = "runtime.log",
    [switch]$DumpTex,
    [switch]$Inventory,   # -Inventory: skip menu, force --inventory boot mode
    [switch]$Battle       # -Battle:    skip menu, force --battle boot mode
)

# RECVX_DUMP_TEX=1 makes recvx_dump_rgba_tga write each decoded TIM2 to
# port/debug/tex/tex_NN_WxH.tga so we can inspect what's actually being
# rendered.
if ($DumpTex) {
    $env:RECVX_DUMP_TEX = "1"
    Write-Host "-- Texture dump enabled: TGAs will land in port/debug/tex/ --" -ForegroundColor Yellow
}

$exe = ".\build\$Config\recvx_pc.exe"
if (-not (Test-Path $exe)) {
    Write-Host "ERROR: $exe not found. Run .\rebuild.ps1 first." -ForegroundColor Red
    exit 1
}

# Boot-mode menu - shown every time so there's zero ambiguity about which
# code path is being exercised. Each entry maps to the literal CLI flag(s)
# that main_pc.c parses.
$bootArgs = $null

# Skip the menu entirely when an explicit -Inventory or -Battle switch is
# passed -- useful for scripted testing.
if ($Inventory) {
    $bootArgs = @("--inventory")
    Write-Host "-- -Inventory switch: forcing --inventory boot mode --" -ForegroundColor Yellow
} elseif ($Battle) {
    $bootArgs = @("--battle")
    Write-Host "-- -Battle switch: forcing --battle boot mode --" -ForegroundColor Yellow
}

while ($null -eq $bootArgs) {
    Write-Host ""
    Write-Host "== RECVX PC port -- boot mode ==" -ForegroundColor Cyan
    Write-Host "  1) (default)      real game flow (logos -> FMV -> title menu -> game)"
    Write-Host "  2) --inventory    skip to inventory STATUS screen post-MV_000"
    Write-Host "  3) --battle       skip to Battle Mode (gm_mode=3, normally unlockable)"
    Write-Host "  4) --gallery      TIM2 texture viewer, scans every AFS partition"
    Write-Host "  5) --list-afs     print AFS partition TOCs to stdout, then exit"
    Write-Host "  6) --play-movie   pick a movie index (0=opening, 16=Capcom presents)"
    Write-Host "  7) custom         type your own flag string"
    Write-Host ""
    $sel = Read-Host "Choose [1]"
    if ([string]::IsNullOrWhiteSpace($sel)) { $sel = "1" }

    switch ($sel) {
        "1" { $bootArgs = @() }                 # no flag -> default = --game flow
        "2" { $bootArgs = @("--inventory") }
        "3" { $bootArgs = @("--battle") }
        "4" { $bootArgs = @("--gallery") }
        "5" { $bootArgs = @("--list-afs") }
        "6" {
            $idx = Read-Host "Movie index (0-99)"
            if ($idx -match '^\d+$') {
                $bootArgs = @("--play-movie", $idx)
            } else {
                Write-Host "Not a number - try again." -ForegroundColor Yellow
            }
        }
        "7" {
            $custom = Read-Host "Enter raw flags (e.g. '--game --msa-rate 32000')"
            if (-not [string]::IsNullOrWhiteSpace($custom)) {
                $bootArgs = $custom.Split(' ') | Where-Object { $_ -ne "" }
            }
        }
        default { Write-Host "Invalid selection - try again." -ForegroundColor Yellow }
    }
}

$argList = @("--iso", $Iso) + $bootArgs
if ($ExtraArgs) { $argList += $ExtraArgs.Split(' ') | Where-Object { $_ -ne "" } }

Write-Host ""
Write-Host "-- Running $exe $($argList -join ' ') --" -ForegroundColor Green
Write-Host "-- Mirroring output to $Log --"

# 2>&1 merges stderr into stdout so the log captures both streams.
& $exe @argList 2>&1 | Tee-Object -FilePath $Log

Write-Host ""
Write-Host "-- Session ended. Full log: $Log --"
