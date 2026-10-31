# Intended for an ephemeral Windows-2022 GitHub runner; never installs system-wide tools.
#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$ToolsDirectory,
    [string]$ConfigFile = (Join-Path $PSScriptRoot 'windows-toolchain.json')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Check native exits ourselves, including commands whose diagnostic output uses stderr.
$PSNativeCommandUseErrorActionPreference = $false

function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required tool/input is missing: $Path" }
}
function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    $text = @(& $Executable @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed ($LASTEXITCODE): $($text -join [Environment]::NewLine)" }
    return ($text -join [Environment]::NewLine).Trim()
}
function Quote-Batch([string]$Value) {
    if ($Value -match '["%!\r\n]') { throw "Unsupported cmd.exe characters in path: $Value" }
    return '"' + $Value + '"'
}
function Get-PinnedDownload($Pin, [string]$Destination) {
    if ($Pin.url -notmatch '^https://' -or $Pin.sha256 -notmatch '^[0-9a-f]{64}$') {
        throw 'Invalid HTTPS download/checksum pin.'
    }
    if (-not (Test-Path -LiteralPath $Destination -PathType Leaf)) {
        Invoke-WebRequest -Uri $Pin.url -OutFile $Destination
    }
    $actual = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Pin.sha256) { throw "SHA256 mismatch for $Destination; refusing to execute or extract it." }
    return $Destination
}
function Write-GitHubLines([string]$File, [string[]]$Lines) {
    if ($File) {
        foreach ($line in $Lines) {
            if ($line -match '[\r\n]') { throw 'GitHub command-file values must be single-line.' }
        }
        [IO.File]::AppendAllLines($File, $Lines, [Text.UTF8Encoding]::new($false))
    }
}

if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell 7 is required.' }
if (-not [IO.Path]::IsPathFullyQualified($ToolsDirectory)) { throw 'ToolsDirectory must be an absolute path.' }
$tools = [IO.Path]::GetFullPath($ToolsDirectory).TrimEnd('\', '/')
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).TrimEnd('\', '/')
if ($tools -eq $source -or $tools.StartsWith($source + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'ToolsDirectory must be outside the source workspace (including the vcpkg clone).'
}
Require-File $ConfigFile
$config = Get-Content -LiteralPath $ConfigFile -Raw | ConvertFrom-Json
if ($config.vcpkg.ref -notmatch '^[0-9a-f]{40}$') { throw 'vcpkg must be pinned to a full commit.' }
if ($config.msvc.default_toolset_family -notmatch '^14\.\d+$') { throw 'Invalid MSVC toolset family.' }
foreach ($pin in @($config.vulkan_sdk, $config.cmake, $config.ninja)) {
    if ($pin.version -notmatch '^\d+(\.\d+){1,3}$') { throw 'Invalid pinned tool version.' }
}
$null = New-Item -ItemType Directory -Path $tools -Force
$downloads = Join-Path $tools 'downloads'
$null = New-Item -ItemType Directory -Path $downloads -Force

# Select the VS2022 default, not an older side-by-side toolset: the package
# wrapper calls vcvarsall x64 again without -vcvars_ver.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
Require-File $vswhere
$vs = Invoke-Checked $vswhere @('-latest', '-products', '*', '-version', $config.msvc.visual_studio_version_range,
    '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath')
if (-not $vs -or $vs -match '[\r\n]') { throw 'No unambiguous VS2022 x64 C++ installation was found.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
Require-File $vcvars
$defaultVersionFile = Join-Path $vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
Require-File $defaultVersionFile
$msvcVersion = (Get-Content -LiteralPath $defaultVersionFile -Raw).Trim()
if ($msvcVersion -notmatch ('^' + [regex]::Escape($config.msvc.default_toolset_family) + '\.\d+$')) {
    throw "VS2022 default MSVC is $msvcVersion; expected $($config.msvc.default_toolset_family).x. Update the explicit pin deliberately when the runner changes."
}
$compiler = Join-Path $vs "VC/Tools/MSVC/$msvcVersion/bin/Hostx64/x64/cl.exe"
Require-File $compiler
$probe = Join-Path $tools ('vcvars-probe-' + [guid]::NewGuid().ToString('N') + '.cmd')
try {
    [IO.File]::WriteAllLines($probe, @(
        '@echo off',
        ('call ' + (Quote-Batch $vcvars) + ' x64 >nul'),
        'if errorlevel 1 exit /b %errorlevel%',
        'echo CI_SELECTED_VCTOOLS=%VCToolsVersion%',
        'where cl.exe',
        'exit /b %errorlevel%'
    ), [Text.UTF8Encoding]::new($false))
    $probeOutput = Invoke-Checked $env:ComSpec @('/d', '/s', '/c', ('"' + (Quote-Batch $probe) + '"'))
    $probeLines = @($probeOutput -split '\r?\n')
    if ($probeLines.Count -lt 2 -or $probeLines[0].TrimEnd('\', '/') -ne "CI_SELECTED_VCTOOLS=$msvcVersion" -or
        [IO.Path]::GetFullPath($probeLines[1]) -ne [IO.Path]::GetFullPath($compiler)) {
        throw "Default vcvarsall x64 did not select the expected compiler: $probeOutput"
    }
} finally {
    Remove-Item -LiteralPath $probe -Force -ErrorAction Stop
}

$cmakeArchive = Get-PinnedDownload $config.cmake (Join-Path $downloads "cmake-$($config.cmake.version)-windows-x86_64.zip")
$cmakeRoot = Join-Path $tools "cmake-$($config.cmake.version)"
Expand-Archive -LiteralPath $cmakeArchive -DestinationPath $cmakeRoot -Force
$cmakeBin = Join-Path $cmakeRoot "cmake-$($config.cmake.version)-windows-x86_64/bin"
$cmake = Join-Path $cmakeBin 'cmake.exe'
Require-File $cmake
$cmakeActual = Invoke-Checked $cmake @('--version')
if (($cmakeActual -split '\r?\n')[0] -ne "cmake version $($config.cmake.version)") { throw 'Extracted CMake version does not match the pin.' }

$ninjaArchive = Get-PinnedDownload $config.ninja (Join-Path $downloads "ninja-$($config.ninja.version)-win.zip")
$ninjaBin = Join-Path $tools "ninja-$($config.ninja.version)"
Expand-Archive -LiteralPath $ninjaArchive -DestinationPath $ninjaBin -Force
$ninja = Join-Path $ninjaBin 'ninja.exe'
Require-File $ninja
$ninjaActual = Invoke-Checked $ninja @('--version')
if ($ninjaActual -ne $config.ninja.version) { throw 'Extracted Ninja version does not match the pin.' }

$sdkInstaller = Get-PinnedDownload $config.vulkan_sdk (Join-Path $downloads "vulkansdk-windows-X64-$($config.vulkan_sdk.version).exe")
$sdk = Join-Path $tools "VulkanSDK-$($config.vulkan_sdk.version)"
# A fresh copy-only install avoids trusting stale SDK files or changing the
# workstation registry, installed validation layers, or system environment.
if (Test-Path -LiteralPath $sdk) { throw "SDK destination already exists; use a fresh ToolsDirectory: $sdk" }
$installerArguments = @('--root', (Quote-Batch $sdk), '--accept-licenses', '--default-answer', '--confirm-command', 'install', 'copy_only=1')
$job = Start-Process -FilePath $sdkInstaller -ArgumentList $installerArguments -Wait -PassThru
try {
    if ($job.ExitCode -ne 0) { throw "Vulkan SDK copy-only installation failed ($($job.ExitCode))." }
} finally { $job.Dispose() }
$sdkBin = Join-Path $sdk 'Bin'
$slang = Join-Path $sdkBin 'slangc.exe'
foreach ($relative in @('Bin/slangc.exe', 'Include/vulkan/vulkan.h', 'Lib/vulkan-1.lib')) {
    Require-File (Join-Path $sdk $relative)
}
$slangActual = Invoke-Checked $slang @('-version')
if (-not $slangActual) { throw 'Slang compiler did not report a version.' }

$git = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$vcpkgRoot = Join-Path $tools ('vcpkg-' + $config.vcpkg.ref)
if (-not (Test-Path -LiteralPath $vcpkgRoot)) {
    $null = Invoke-Checked $git @('clone', '--no-checkout', $config.vcpkg.url, $vcpkgRoot)
    $null = Invoke-Checked $git @('-C', $vcpkgRoot, 'checkout', '--detach', $config.vcpkg.ref)
}
$vcpkgHead = Invoke-Checked $git @('-C', $vcpkgRoot, 'rev-parse', 'HEAD')
if ($vcpkgHead -ne $config.vcpkg.ref) { throw 'vcpkg checkout HEAD does not match the pinned tooling revision.' }
$vcpkgOrigin = Invoke-Checked $git @('-C', $vcpkgRoot, 'remote', 'get-url', 'origin')
if ($vcpkgOrigin -ne $config.vcpkg.url) { throw 'vcpkg checkout has an unexpected origin.' }
$vcpkgDirty = Invoke-Checked $git @('-C', $vcpkgRoot, 'status', '--porcelain', '--untracked-files=no')
if ($vcpkgDirty) { throw 'vcpkg checkout has modified tracked files.' }
$bootstrap = Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat'
Require-File $bootstrap
$null = Invoke-Checked $env:ComSpec @('/d', '/s', '/c', ('"' + (Quote-Batch $bootstrap) + ' -disableMetrics"'))
$vcpkg = Join-Path $vcpkgRoot 'vcpkg.exe'
Require-File $vcpkg
$vcpkgActual = Invoke-Checked $vcpkg @('version')

$evidence = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    config_file = [IO.Path]::GetFullPath($ConfigFile)
    config_sha256 = (Get-FileHash -LiteralPath $ConfigFile -Algorithm SHA256).Hash.ToLowerInvariant()
    msvc = [ordered]@{
        required_default_family = $config.msvc.default_toolset_family
        directory_version = $msvcVersion
        compiler_file_version = (Get-Item -LiteralPath $compiler).VersionInfo.FileVersion
        compiler = $compiler
        vcvarsall = $vcvars
        visual_studio = $vs
        default_environment_probe = $probeOutput
    }
    vulkan_sdk = [ordered]@{ pin = $config.vulkan_sdk; root = $sdk; slang_version = $slangActual }
    cmake = [ordered]@{ pin = $config.cmake; executable = $cmake; actual_version = $cmakeActual }
    ninja = [ordered]@{ pin = $config.ninja; executable = $ninja; actual_version = $ninjaActual }
    vcpkg = [ordered]@{ pin = $config.vcpkg; root = $vcpkgRoot; actual_head = $vcpkgHead; actual_version = $vcpkgActual }
}
$evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $tools 'toolchain-evidence.json') -Encoding utf8
Write-GitHubLines $env:GITHUB_ENV @("VULKAN_SDK=$sdk", "CI_VCPKG_ROOT=$vcpkgRoot", "CI_VCVARSALL=$vcvars",
    "CI_CMAKE=$cmake", "CI_NINJA=$ninja")
Write-GitHubLines $env:GITHUB_PATH @($cmakeBin, $ninjaBin, $sdkBin)
Write-GitHubLines $env:GITHUB_OUTPUT @("msvc-version=$msvcVersion", "sdk-version=$($config.vulkan_sdk.version)",
    "vcpkg-ref=$vcpkgHead", "cmake-version=$($config.cmake.version)")
Write-Host "Provisioned pinned Windows x64 tools; evidence: $(Join-Path $tools 'toolchain-evidence.json')"
