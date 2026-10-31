# Requires an existing Windows x64 MSVC CMake build tree (Release or RelWithDebInfo).
[CmdletBinding()]
param(
    [string]$BuildDirectory = 'cmake-build-relwithdebinfo-visual-studio',
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$')]
    [string]$Version,
    [string]$OutputDirectory = 'out/packages',
    [string]$VcVarsAll
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
function Resolve-RootPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $root $Path))
}
function Get-Sha256([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
}
function Get-CacheValue([string]$Name) {
    $entry = @($cache | Where-Object { $_ -match ('^' + [regex]::Escape($Name) + ':[^=]+=') })
    if ($entry.Count -ne 1) { throw "Missing or ambiguous CMake cache entry: $Name" }
    return ($entry[0] -split '=', 2)[1]
}
function Quote-Batch([string]$Value) {
    # cmd.exe expands percent/environment expressions even within quoted paths.
    if ($Value -match '["%!\r\n]') { throw "Unsupported cmd.exe characters in path: $Value" }
    return '"' + $Value + '"'
}

$mutex = $null
$ownsMutex = $false
$work = $null
try {
    if ($env:OS -ne 'Windows_NT') { throw 'Windows is required.' }
    $mutex = [Threading.Mutex]::new($false, 'Local\vk-playground-renderer-smoke')
    try { $ownsMutex = $mutex.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
    if (-not $ownsMutex) { throw 'Another renderer build/smoke/package operation is running.' }
    if (Get-Process -Name demo -ErrorAction SilentlyContinue) { throw 'Close demo.exe before packaging; no running renderer will be overwritten.' }

    $build = Resolve-RootPath $BuildDirectory
    $output = Resolve-RootPath $OutputDirectory
    $cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
    if ((Resolve-RootPath (Get-CacheValue 'CMAKE_HOME_DIRECTORY')) -ne $root) { throw 'Build tree belongs to another source checkout.' }
    $configurationTypes = @($cache | Where-Object { $_ -match '^CMAKE_CONFIGURATION_TYPES:[^=]+=' })
    if ($configurationTypes.Count -gt 0) {
        if ($Configuration -notin ((Get-CacheValue 'CMAKE_CONFIGURATION_TYPES') -split ';')) { throw 'Requested configuration is not available in this multi-config build tree.' }
    } elseif ((Get-CacheValue 'CMAKE_BUILD_TYPE') -ne $Configuration) {
        throw 'CMake build type does not match Configuration.'
    }
    if ((Get-CacheValue 'VCPKG_TARGET_TRIPLET') -ne 'x64-windows') { throw 'Demo packaging currently requires the x64-windows vcpkg triplet.' }
    $compiler = (Get-CacheValue 'CMAKE_CXX_COMPILER').Replace('\', '/')
    if ($compiler -notmatch '/bin/Hostx64/x64/cl.exe$') { throw 'An x64 MSVC compiler is required.' }
    if (-not $VcVarsAll) {
        $index = $compiler.IndexOf('/VC/Tools/MSVC/', [StringComparison]::OrdinalIgnoreCase)
        if ($index -lt 0) { throw 'Cannot derive MSVC environment; pass -VcVarsAll.' }
        $VcVarsAll = $compiler.Substring(0, $index) + '/VC/Auxiliary/Build/vcvarsall.bat'
    }
    if (-not (Test-Path -LiteralPath $VcVarsAll -PathType Leaf)) { throw "Missing MSVC environment: $VcVarsAll" }
    $cmake = (Get-Command cmake -CommandType Application | Select-Object -First 1).Source
    $commit = (& git -C $root rev-parse HEAD | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') { throw 'Cannot identify the Git source revision.' }
    $dirty = [bool](& git -C $root status --porcelain --untracked-files=normal)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify working-tree state.' }
    if (-not $Version) { $Version = 'dev-' + $commit.Substring(0, 12); if ($dirty) { $Version += '-dirty' } }
    $name = 'vk-playground-' + $Version + '-windows-x64'
    $archive = Join-Path $output ($name + '.zip')
    $checksum = $archive + '.sha256'
    if ((Test-Path -LiteralPath $archive) -or (Test-Path -LiteralPath $checksum)) { throw "Package already exists: $archive" }
    $null = New-Item -ItemType Directory -Path $output -Force
    $work = Join-Path $output ('.work-' + [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $work

    $manifest = Get-Content -LiteralPath (Join-Path $root 'packaging/demo-scene.json') -Raw | ConvertFrom-Json
    if ($manifest.sha256 -notmatch '^[0-9a-f]{64}$' -or $manifest.url -notmatch '^https://') { throw 'Invalid pinned scene manifest.' }
    $sceneDirectory = Join-Path $build 'demo-assets'
    $sceneSource = Join-Path $sceneDirectory 'source.glb'
    $scene = Join-Path $sceneDirectory 'structure.glb'
    $null = New-Item -ItemType Directory -Path $sceneDirectory -Force
    if (-not (Test-Path -LiteralPath $sceneSource -PathType Leaf) -or (Get-Sha256 $sceneSource) -ne $manifest.sha256) {
        $download = Join-Path $work 'scene.download'
        $localScene = Join-Path $root 'assets/structure.glb'
        if ((Test-Path -LiteralPath $localScene -PathType Leaf) -and (Get-Sha256 $localScene) -eq $manifest.sha256) {
            Copy-Item -LiteralPath $localScene -Destination $download
        } else {
            [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -Uri $manifest.url -OutFile $download -UseBasicParsing
        }
        if ((Get-Sha256 $download) -ne $manifest.sha256) { throw 'Downloaded scene SHA256 does not match the pinned manifest.' }
        Move-Item -LiteralPath $download -Destination $sceneSource -Force
    }
    & (Join-Path $PSScriptRoot 'prepare-demo-scene.ps1') -Source $sceneSource -Destination $scene -Translation $manifest.translation
    $preparedSceneHash = Get-Sha256 $scene

    $stage = Join-Path $work $name
    $batchPath = Join-Path $work 'build-and-install.cmd'
    $lines = @(
        '@echo off',
        'chcp 65001 >nul',
        ('call ' + (Quote-Batch $VcVarsAll) + ' x64'),
        'if errorlevel 1 exit /b %errorlevel%',
        ((Quote-Batch $cmake) + ' -S ' + (Quote-Batch $root) + ' -B ' + (Quote-Batch $build) + ' ' +
            (Quote-Batch ('-DDEMO_SCENE_FILE:FILEPATH=' + $scene)) + ' ' +
            (Quote-Batch ('-DDEMO_SCENE_SOURCE_FILE:FILEPATH=' + $sceneSource)) + ' ' +
            (Quote-Batch ('-DDEMO_SCENE_SHA256:STRING=' + $preparedSceneHash))),
        'if errorlevel 1 exit /b %errorlevel%',
        # Profiles share outputs. Force the selected target's actual executable
        # to relink, including generators that add a configuration subdirectory.
        ('set /p DEMO_EXE=<' + (Quote-Batch (Join-Path $build ('demo-executable-' + $Configuration + '.txt')))),
        'if not defined DEMO_EXE exit /b 1',
        ((Quote-Batch $cmake) + ' -E rm -f "%DEMO_EXE%"'),
        'if errorlevel 1 exit /b %errorlevel%',
        ((Quote-Batch $cmake) + ' --build ' + (Quote-Batch $build) + ' --config ' + $Configuration + ' --target demo'),
        'if errorlevel 1 exit /b %errorlevel%',
        ((Quote-Batch $cmake) + ' --install ' + (Quote-Batch $build) + ' --config ' + $Configuration + ' --prefix ' + (Quote-Batch $stage) + ' --component Demo'),
        'exit /b %errorlevel%'
    )
    [IO.File]::WriteAllLines($batchPath, $lines, [Text.UTF8Encoding]::new($false))
    $job = Start-Process -FilePath $env:ComSpec -ArgumentList @('/d', '/s', '/c', ('"' + (Quote-Batch $batchPath) + '"')) -WorkingDirectory $root -RedirectStandardOutput (Join-Path $work 'build.stdout.log') -RedirectStandardError (Join-Path $work 'build.stderr.log') -PassThru
    $null = $job.Handle
    try { $job.WaitForExit(); if ($job.ExitCode -ne 0) { throw "Build/install failed ($($job.ExitCode)); inspect $work" } }
    finally { $job.Dispose() }

    $required = @('demo.exe', 'assets/structure.glb', 'README.txt', 'LICENSE.txt', 'licenses/demo-scene.json')
    foreach ($shader in (Get-Content -LiteralPath (Join-Path $build 'demo-shaders.txt'))) {
        if ($shader) { $required += 'spirv/' + [IO.Path]::GetFileName($shader) }
    }
    foreach ($relative in $required) {
        $file = Join-Path $stage $relative
        if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -eq 0) { throw "Missing package input: $relative" }
    }
    if ((Get-Sha256 (Join-Path $stage 'assets/structure.glb')) -ne $preparedSceneHash) { throw 'Installed scene differs from the prepared licensed asset.' }
    $info = [ordered]@{
        version = $Version; commit = $commit; working_tree_dirty = $dirty
        configuration = $Configuration; architecture = 'x64'; compiler = $compiler
        slang_compiler = Get-CacheValue 'SLANGC'; scene_source_sha256 = $manifest.sha256
        scene_sha256 = $preparedSceneHash
        built_at_utc = [DateTime]::UtcNow.ToString('o')
    }
    $info | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'build-info.txt') -Encoding UTF8
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $temporaryArchive = Join-Path $work ($name + '.zip')
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $temporaryArchive, [IO.Compression.CompressionLevel]::Optimal, $true)
    $hash = Get-Sha256 $temporaryArchive
    Move-Item -LiteralPath $temporaryArchive -Destination $archive
    [IO.File]::WriteAllText($checksum, $hash + '  ' + [IO.Path]::GetFileName($archive) + "`n", [Text.Encoding]::ASCII)
    Remove-Item -LiteralPath $stage -Recurse -Force
    Remove-Item -LiteralPath $batchPath
    Write-Host "Package: $archive"
    Write-Host "SHA256: $hash"
    Write-Host "Build logs: $work"
    Write-Host 'Build/package success is not GPU or Vulkan validation proof.'
} catch {
    Write-Error $_
    exit 1
} finally {
    if ($ownsMutex) { $mutex.ReleaseMutex() }
    if ($mutex) { $mutex.Dispose() }
}
