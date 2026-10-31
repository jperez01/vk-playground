# Requires Windows PowerShell 5.1 or PowerShell 7 on an interactive Windows desktop.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo')]
    [string]$Profile = 'Debug',
    [string]$OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) ('vk-playground-smoke-' + [guid]::NewGuid().ToString('N'))),
    [string]$VcVarsAll,
    [ValidateRange(1, 30)]
    [int]$SettleSeconds = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$buildDirectory = Join-Path $root ('cmake-build-' + $Profile.ToLowerInvariant() + '-visual-studio')
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) {
    throw 'OutputDirectory must be a new directory; existing evidence is never overwritten.'
}
$null = New-Item -ItemType Directory -Path $output
$report = [ordered]@{
    profile = $Profile
    buildDirectory = $buildDirectory
    build = [ordered]@{ status = 'not_run'; exitCode = $null }
    runtime = [ordered]@{ status = 'not_run'; exitCode = $null; normalShutdown = $false; forcedTermination = $false }
    validation = [ordered]@{ status = 'not_checked'; errors = 0; warnings = 0; identifiers = @(); duplicateLimitReached = $false }
    visualReview = 'required: inspect screenshots; capture success does not prove rendering correctness'
    stages = @()
    failure = $null
}
$renderer = $null
$mutex = $null
$ownsMutex = $false
$iniCaptured = $false
$iniPath = Join-Path $root 'bin/imgui.ini'
$phase = 'prerequisites'
$exitCode = 1

try {
    if ($env:OS -ne 'Windows_NT' -or -not [Environment]::UserInteractive) {
        throw 'An interactive Windows desktop is required.'
    }
    # All profiles share bin/. Serialize this harness and refuse pre-existing renderers.
    $mutex = [Threading.Mutex]::new($false, 'Local\vk-playground-renderer-smoke')
    try { $ownsMutex = $mutex.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
    if (-not $ownsMutex) { throw 'Another renderer smoke harness is running.' }
    if (Get-Process -Name demo -ErrorAction SilentlyContinue) {
        throw 'A demo.exe is already running. Leave user instances untouched and close them before building.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $root 'assets/structure.glb') -PathType Leaf)) {
        throw 'Missing assets/structure.glb; provide the real scene before running.'
    }
    $cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
    $cache = Get-Content -LiteralPath $cachePath
    function Get-CacheValue([string]$Name) {
        $entry = @($cache | Where-Object { $_ -match ('^' + [regex]::Escape($Name) + ':[^=]+=') })
        if ($entry.Count -ne 1) { throw "Missing or ambiguous CMake cache entry: $Name" }
        return ($entry[0] -split '=', 2)[1]
    }
    if ((Get-CacheValue 'CMAKE_BUILD_TYPE') -ne $Profile) { throw 'CMake cache does not match the selected profile.' }
    $slang = Get-CacheValue 'SLANGC'
    if (-not (Test-Path -LiteralPath $slang -PathType Leaf)) { throw "Slang compiler not found: $slang" }
    $compiler = (Get-CacheValue 'CMAKE_CXX_COMPILER').Replace('\', '/')
    if (-not $VcVarsAll) {
        $vcIndex = $compiler.IndexOf('/VC/Tools/MSVC/', [StringComparison]::OrdinalIgnoreCase)
        if ($vcIndex -lt 0) { throw 'Cannot derive MSVC setup from the cache; pass -VcVarsAll.' }
        $VcVarsAll = $compiler.Substring(0, $vcIndex) + '/VC/Auxiliary/Build/vcvarsall.bat'
    }
    if (-not (Test-Path -LiteralPath $VcVarsAll -PathType Leaf)) { throw "MSVC setup not found: $VcVarsAll" }
    $cmake = (Get-Command cmake -CommandType Application).Source
    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.Windows.Forms
    if (-not ('RendererSmoke.Window' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace RendererSmoke {
    public static class Window {
        [StructLayout(LayoutKind.Sequential)]
        public struct Rect { public int Left, Top, Right, Bottom; }
        private delegate bool EnumCallback(IntPtr window, IntPtr data);
        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumCallback callback, IntPtr data);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
        [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
        [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
        [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
        [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
        [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
        [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
        [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
        public static IntPtr Find(int processId) {
            IntPtr result = IntPtr.Zero;
            EnumWindows((window, data) => {
                uint pid;
                GetWindowThreadProcessId(window, out pid);
                var title = new StringBuilder(256);
                GetWindowText(window, title, title.Capacity);
                if (pid == processId && IsWindowVisible(window) && title.ToString() == "Vulkan Engine") {
                    result = window;
                    return false;
                }
                return true;
            }, IntPtr.Zero);
            return result;
        }
    }
}
'@
    }
    $null = [RendererSmoke.Window]::SetProcessDPIAware()
    $screen = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    if ($screen.Width -lt 1000 -or $screen.Height -lt 740) { throw 'Desktop too small for the smoke window sizes (minimum working area 1000x740).' }

    $phase = 'build'
    $report.build.status = 'running'
    # Force the selected profile's link and post-build DLL deployment, even if another
    # profile has left a newer executable in the shared output directory.
    $command = 'call "{0}" x64 && "{1}" -E rm -f "{2}" && "{1}" --build "{3}" --target demo' -f $VcVarsAll, $cmake, (Join-Path $root 'bin/demo.exe'), $buildDirectory
    $build = Start-Process -FilePath $env:ComSpec -ArgumentList @('/d', '/s', '/c', ('"' + $command + '"')) -WorkingDirectory $root -RedirectStandardOutput (Join-Path $output 'build.stdout.log') -RedirectStandardError (Join-Path $output 'build.stderr.log') -PassThru
    # Cache the native handle: Windows PowerShell's Start-Process can otherwise
    # return a null ExitCode after WaitForExit.
    $null = $build.Handle
    $build.WaitForExit()
    $report.build.exitCode = $build.ExitCode
    if ($build.ExitCode -ne 0) { throw "Build failed; inspect $output\build.*.log" }
    $report.build.status = 'passed'
    $build.Dispose()

    $phase = 'runtime'
    $exitCode = 2
    $report.runtime.status = 'running'
    $iniExisted = Test-Path -LiteralPath $iniPath
    $iniBytes = if ($iniExisted) { [IO.File]::ReadAllBytes($iniPath) } else { $null }
    $iniCaptured = $true
    $renderer = Start-Process -FilePath (Join-Path $root 'bin/demo.exe') -WorkingDirectory (Join-Path $root 'bin') -RedirectStandardOutput (Join-Path $output 'renderer.stdout.log') -RedirectStandardError (Join-Path $output 'renderer.stderr.log') -PassThru
    $null = $renderer.Handle
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    $window = [IntPtr]::Zero
    while ([DateTime]::UtcNow -lt $deadline -and -not $renderer.HasExited) {
        $window = [RendererSmoke.Window]::Find($renderer.Id)
        if ($window -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($window -eq [IntPtr]::Zero) { throw 'Renderer exited or did not create a visible Vulkan Engine window within 30 seconds.' }

    function Save-Stage([string]$Name, [int]$Width, [int]$Height) {
        if ($renderer.HasExited) { throw "Renderer exited before $Name." }
        $widthLimit = [Math]::Min($Width, $screen.Width - 40)
        $heightLimit = [Math]::Min($Height, $screen.Height - 40)
        if (-not [RendererSmoke.Window]::SetWindowPos($window, [IntPtr](-1), ($screen.Left + 20), ($screen.Top + 20), $widthLimit, $heightLimit, 0x0010)) {
            throw "Cannot position renderer for $Name."
        }
        $bitmap = $null
        $graphics = $null
        try {
            Start-Sleep -Seconds $SettleSeconds
            if ($renderer.HasExited) { throw "Renderer exited during $Name." }
            $rect = New-Object RendererSmoke.Window+Rect
            $client = New-Object RendererSmoke.Window+Rect
            if (-not [RendererSmoke.Window]::GetWindowRect($window, [ref]$rect) -or -not [RendererSmoke.Window]::GetClientRect($window, [ref]$client)) {
                throw "Cannot inspect renderer dimensions for $Name."
            }
            $bounds = [Drawing.Rectangle]::FromLTRB($rect.Left, $rect.Top, $rect.Right, $rect.Bottom)
            if (-not [Windows.Forms.SystemInformation]::VirtualScreen.Contains($bounds)) { throw 'Renderer screenshot would extend outside the desktop.' }
            $bitmap = New-Object Drawing.Bitmap($bounds.Width, $bounds.Height)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $graphics.CopyFromScreen($bounds.Location, [Drawing.Point]::Empty, $bounds.Size)
            $imagePath = Join-Path $output ($Name + '.png')
            $bitmap.Save($imagePath, [Drawing.Imaging.ImageFormat]::Png)
            $report.stages += [ordered]@{ name = $Name; clientWidth = $client.Right; clientHeight = $client.Bottom; screenshot = $imagePath }
            Write-Host "$Name captured: $($client.Right)x$($client.Bottom)"
        }
        finally {
            if ($graphics) { $graphics.Dispose() }
            if ($bitmap) { $bitmap.Dispose() }
            $null = [RendererSmoke.Window]::SetWindowPos($window, [IntPtr](-2), 0, 0, 0, 0, 0x0013)
        }
    }

    Save-Stage '01-initial' 1000 720
    Save-Stage '02-shrunk' 660 520
    Save-Stage '03-expanded' 1200 800
    $null = [RendererSmoke.Window]::ShowWindow($window, 6)
    Start-Sleep -Seconds $SettleSeconds
    if ($renderer.HasExited -or -not [RendererSmoke.Window]::IsIconic($window)) { throw 'Renderer did not remain running and minimized.' }
    $null = [RendererSmoke.Window]::ShowWindow($window, 9)
    Save-Stage '04-restored' 1200 800
    if ([RendererSmoke.Window]::IsIconic($window)) { throw 'Renderer did not restore.' }
    if (-not [RendererSmoke.Window]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)) { throw 'Cannot send normal window close.' }
    if (-not $renderer.WaitForExit(20000)) { throw 'Normal window close timed out after 20 seconds.' }
    $report.runtime.exitCode = $renderer.ExitCode
    if ($renderer.ExitCode -ne 0) { throw "Renderer exited with code $($renderer.ExitCode)." }
    $report.runtime.normalShutdown = $true
    $report.runtime.status = 'passed'
    $exitCode = 0
}
catch {
    $report.failure = $_.Exception.Message
    if ($phase -eq 'build') { $report.build.status = 'failed' }
    if ($phase -eq 'runtime') { $report.runtime.status = 'failed' }
    Write-Warning $report.failure
}
finally {
    try {
        if ($renderer) {
            if (-not $renderer.HasExited) {
                # Failure cleanup affects only the process created by this harness.
                $report.runtime.forcedTermination = $true
                $renderer.Kill()
                $renderer.WaitForExit()
            }
            $report.runtime.exitCode = $renderer.ExitCode
            $renderer.Dispose()
        }
        if ($iniCaptured) {
            if ($iniExisted) { [IO.File]::WriteAllBytes($iniPath, $iniBytes) }
            elseif (Test-Path -LiteralPath $iniPath) { Remove-Item -LiteralPath $iniPath }
        }
        $stdoutPath = Join-Path $output 'renderer.stdout.log'
        $stderrPath = Join-Path $output 'renderer.stderr.log'
        if (Test-Path -LiteralPath $stdoutPath) {
            $log = [IO.File]::ReadAllText($stdoutPath) + "`n" + [IO.File]::ReadAllText($stderrPath)
            $report.validation.errors = [regex]::Matches($log, '(?m)^\[ERROR(?::[^\]]*)?\]').Count
            $report.validation.warnings = [regex]::Matches($log, '\[WARNING(?::[^\]]*)?\]').Count
            $report.validation.identifiers = @([regex]::Matches($log, 'VUID-[A-Za-z0-9_-]+|SYNC-HAZARD-[A-Z_-]+|\b[A-Z]+(?:_[A-Z]+)+(?= hazard)') | ForEach-Object { $_.Value } | Sort-Object -Unique)
            $report.validation.duplicateLimitReached = $log.Contains('duplicate_message_limit')
            if ($report.validation.errors -gt 0) {
                $report.validation.status = 'failed'
                if ($exitCode -eq 0) { $exitCode = 3 }
            }
            elseif ($Profile -eq 'Debug' -and $report.runtime.status -eq 'passed') {
                $report.validation.status = if ($report.validation.warnings -gt 0) { 'passed_with_warnings' } else { 'passed' }
            }
        }
    }
    catch {
        $report.failure = 'Cleanup/reporting failure: ' + $_.Exception.Message
        if ($phase -eq 'runtime') { $report.runtime.status = 'failed' }
        $exitCode = 2
        Write-Warning $report.failure
    }
    finally {
        if ($ownsMutex) { $mutex.ReleaseMutex() }
        if ($mutex) { $mutex.Dispose() }
        $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'report.json') -Encoding UTF8
        Write-Host "Evidence: $output"
        Write-Host "Build: $($report.build.status); runtime: $($report.runtime.status); validation: $($report.validation.status)"
        Write-Host 'Visual review is still required. No feature-specific UI interactions were automated.'
    }
}
exit $exitCode
