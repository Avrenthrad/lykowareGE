# Local evidence only. No upload, account, paid service or telemetry.
[CmdletBinding()]
param(
    [string]$Executable,
    [string]$OutputDirectory,
    [int]$Frames = 600,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $Executable) { $Executable = Join-Path $repository 'build/windows/tools/cli/Release/lyko.exe' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repository 'build/measurements' }
if (-not $SkipBuild) {
    Push-Location $repository
    try {
        & cmake --preset windows
        if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
        & cmake --build --preset windows-release --parallel 4
        if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
        & ctest --preset windows-release --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    } finally { Pop-Location }
}
if (-not (Test-Path -LiteralPath $Executable)) { throw "Executable missing: $Executable" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$system = Get-CimInstance Win32_ComputerSystem
$os = Get-CimInstance Win32_OperatingSystem
$cpu = Get-CimInstance Win32_Processor
$metadata = [ordered]@{
    format = 'lykoware.hardware-evidence'; schema_version = 1
    date_utc = [DateTime]::UtcNow.ToString('o')
    commit = (& git -C $repository rev-parse HEAD)
    os = $os.Caption; os_version = $os.Version
    cpu = @($cpu | ForEach-Object { $_.Name })
    ram_bytes = [uint64]$system.TotalPhysicalMemory
    gpu_adapters = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion)
    scope = 'Small offscreen fixture; excludes swapchain frame pacing, GPU timestamps, VRAM residency, large-world/AI load.'
    recommended_profile_validated = $false
}
$metadata | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'hardware.json')
$scene = Join-Path $repository 'samples/scenes/falling-crates.scene.json'
$commands = Join-Path $repository 'samples/scenes/falling-crates.commands.json'
for ($run = 1; $run -le 3; $run++) {
    $report = Join-Path $OutputDirectory "scene-$run.json"
    $capture = Join-Path $OutputDirectory "scene-$run.ppm"
    & $Executable run --scene $scene --commands $commands --ticks 120 --width 2560 --height 1440 `
        --warmup 120 --measure-frames $Frames --capture $capture --report $report
    if ($LASTEXITCODE -eq 77) { throw 'GPU unavailable; hardware validation is pending' }
    if ($LASTEXITCODE -ne 0) { throw "Scene run $run failed" }
    $data = Get-Content -Raw -LiteralPath $report | ConvertFrom-Json
    if (-not $data.hardware_gpu) { throw 'Software renderer detected; this is not hardware GPU evidence' }
    Write-Host ("Run {0}: {1}, p99 {2:N3} ms, process peak {3:N1} MiB" -f `
        $run, $data.device.name, $data.simulation_and_draw.p99_ms, ($data.peak_process_resident_bytes / 1MB))
}
Write-Host "Local evidence saved in $OutputDirectory. Recommended-profile and frame-pacing review remain pending."
