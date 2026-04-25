# Run the RECVX PC port and tee output to runtime.log so the full session
# is captured even if the console scrollback buffer truncates it.
#
# Usage from the port/ directory:
#   .\run.ps1                         # default args
#   .\run.ps1 -Iso "C:\path\to.iso"   # override ISO path
#   .\run.ps1 -ExtraArgs "--msa-rate 32000"

param(
    [string]$Iso       = "C:\Claude\codeveronica\recvx\recvx.iso",
    [string]$Config    = "Debug",
    [string]$ExtraArgs = "",
    [string]$Log       = "runtime.log"
)

$exe = ".\build\$Config\recvx_pc.exe"
if (-not (Test-Path $exe)) {
    Write-Host "ERROR: $exe not found. Run .\rebuild.ps1 first." -ForegroundColor Red
    exit 1
}

$argList = @("--iso", $Iso, "--game")
if ($ExtraArgs) { $argList += $ExtraArgs.Split(' ') }

Write-Host "-- Running $exe $($argList -join ' ') --"
Write-Host "-- Mirroring output to $Log --"

# 2>&1 merges stderr into stdout so the log captures both streams.
& $exe @argList 2>&1 | Tee-Object -FilePath $Log

Write-Host ""
Write-Host "-- Session ended. Full log: $Log --"
