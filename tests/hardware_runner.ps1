# Windows-only integration check. Synthetic inventory; never target-PC evidence.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$repository = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('lyko-hardware-test-' + [Guid]::NewGuid().ToString('N'))
$previousPath = $env:PATH
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    # Exercise native argument handling and >4 GiB VRAM without requiring a GPU.
    Set-Content -Encoding ASCII -LiteralPath (Join-Path $temporary 'nvidia-smi.cmd') -Value @('@echo off', 'echo NVIDIA Test GPU, 12288, 123.4')
    $env:PATH = $temporary + [IO.Path]::PathSeparator + $previousPath
    $output = Join-Path $temporary 'evidence'
    $expectedFailure = $false
    try {
        & (Join-Path $repository 'tools/validation/measure-scene.ps1') -Executable $Executable -OutputDirectory $output -SkipBuild -Frames 1
    } catch {
        if ($_.Exception.Message -notlike '*GPU unavailable; hardware validation is pending*') { throw }
        $expectedFailure = $true
    }
    if (-not $expectedFailure) { throw 'Hosted Windows must leave unavailable Vulkan evidence pending' }
    $data = Get-Content -Raw -LiteralPath (Join-Path $output 'hardware.json') | ConvertFrom-Json
    if ($data.schema_version -ne 2 -or $data.nvidia_gpus.Count -ne 1 -or
        $data.nvidia_gpus[0].total_memory_mib -ne 12288 -or
        $data.nvidia_gpus[0].name -ne 'NVIDIA Test GPU' -or
        $data.nvidia_gpus[0].driver_version -ne '123.4' -or
        $data.recommended_profile_validated -ne $false -or
        $data.executable_sha256.Length -ne 64 -or $data.fixture.scene_sha256.Length -ne 64) {
        throw 'Hardware metadata does not preserve inventory, provenance or pending acceptance'
    }
    if (Test-Path -LiteralPath (Join-Path $output 'assessment.json')) { throw 'Unavailable GPU must not produce a successful assessment' }
    $refused = $false
    try {
        & (Join-Path $repository 'tools/validation/measure-scene.ps1') -Executable $Executable -OutputDirectory $output -SkipBuild -Frames 1
    } catch {
        if ($_.Exception.Message -notlike '*existing evidence will not be overwritten*') { throw }
        $refused = $true
    }
    if (-not $refused) { throw 'Existing evidence directory was overwritten' }
    Write-Host 'Windows runner integration passed (synthetic inventory; Vulkan unavailable as expected).'
} finally {
    $env:PATH = $previousPath
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
exit 0
