# Local evidence only. No upload, account, paid service or telemetry.
[CmdletBinding()]
param(
    [string]$Executable,
    [string]$OutputDirectory,
    [ValidateRange(1, 1000000)][int]$Frames = 600,
    [string]$BudgetFile,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
# Optional inventory tools may fail; required native commands are checked below.
$PSNativeCommandUseErrorActionPreference = $false
$repository = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $Executable) { $Executable = Join-Path $repository 'build/windows/tools/cli/Release/lyko.exe' }
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repository ('build/measurements/' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmss') + '-' + [Guid]::NewGuid().ToString('N'))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory; existing evidence will not be overwritten' }
if (-not $SkipBuild) {
    Push-Location $repository
    try {
        & cmake --preset windows -DLYKOWARE_HOST_TOOL_JOBS=2
        if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
        & cmake --build --preset windows-release --parallel 2
        if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
        & ctest --preset windows-release --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    } finally { Pop-Location }
}
if (-not (Test-Path -LiteralPath $Executable)) { throw "Executable missing: $Executable" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$scene = Join-Path $repository 'samples/scenes/falling-crates.scene.json'
$commands = Join-Path $repository 'samples/scenes/falling-crates.commands.json'
$sourceCommit = & git -C $repository rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source commit' }
$sourceStatus = & git -C $repository status --porcelain
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source dirtiness' }
# Total framebuffer memory is an inventory value, not process residency. WMI's
# 32-bit AdapterRAM field cannot represent modern cards reliably.
$inventory = @()
$inventoryStatus = 'unavailable'
$nvidia = Get-Command nvidia-smi -ErrorAction SilentlyContinue
if ($nvidia) {
    $rows = & $nvidia.Source --query-gpu=name,memory.total,driver_version --format=csv,noheader,nounits 2>$null
    if ($LASTEXITCODE -eq 0) {
        try {
            $parsed = @($rows | ConvertFrom-Csv -Header Name, MemoryMiB, Driver)
            foreach ($gpu in $parsed) {
                $memory = [double]::Parse($gpu.MemoryMiB.Trim(), [Globalization.CultureInfo]::InvariantCulture)
                if (-not ($memory -gt 0) -or [double]::IsInfinity($memory)) { throw 'Invalid GPU memory' }
                $inventory += [ordered]@{ name = $gpu.Name.Trim(); total_memory_mib = $memory; driver_version = $gpu.Driver.Trim() }
            }
            if ($inventory.Count -gt 0) { $inventoryStatus = 'collected' }
        } catch { $inventory = @(); $inventoryStatus = 'invalid' }
    }
}
$system = Get-CimInstance Win32_ComputerSystem
$os = Get-CimInstance Win32_OperatingSystem
$cpu = Get-CimInstance Win32_Processor
$metadata = [ordered]@{
    format = 'lykoware.hardware-evidence'; schema_version = 2
    date_utc = [DateTime]::UtcNow.ToString('o')
    commit = $sourceCommit; source_dirty = [bool]$sourceStatus
    executable_sha256 = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
    nvidia_inventory_status = $inventoryStatus; nvidia_gpus = @($inventory)
    fixture = [ordered]@{
        width = 2560; height = 1440; ticks = 120; warmup = 120; frames = $Frames
        scene_sha256 = (Get-FileHash -LiteralPath $scene -Algorithm SHA256).Hash
        commands_sha256 = (Get-FileHash -LiteralPath $commands -Algorithm SHA256).Hash
    }
    os = $os.Caption; os_version = $os.Version
    cpu = @($cpu | ForEach-Object { $_.Name })
    ram_bytes = [uint64]$system.TotalPhysicalMemory
    gpu_adapters = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion)
    scope = 'Small offscreen fixture; excludes swapchain frame pacing, GPU timestamps, VRAM residency, large-world/AI load.'
    recommended_profile_validated = $false
}
$metadata | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'hardware.json')
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
$assessmentArguments = @((Join-Path $PSScriptRoot 'assess_scene.py'), $OutputDirectory)
if ($BudgetFile) { $assessmentArguments += @('--budget', $BudgetFile) }
& python @assessmentArguments
if ($LASTEXITCODE -ne 0) { throw "Evidence incomplete or assessment failed; inspect $OutputDirectory/assessment.json" }
Write-Host "Local evidence saved in $OutputDirectory. Recommended-profile and frame-pacing review remain pending."
