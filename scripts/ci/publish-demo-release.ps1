# VerifyOnly validates local package identity and integrity only; it never calls GitHub.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ArtifactDirectory,
    [Parameter(Mandatory = $true)]
    [string]$Tag,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('\A[0-9a-fA-F]{40}\z')]
    [string]$Commit,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('\A[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9_.-]+\z')]
    [string]$Repository,
    [switch]$VerifyOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-GitHubApi([string]$Endpoint) {
    $response = & $script:gh api --hostname github.com $Endpoint
    if ($LASTEXITCODE -ne 0) { throw "GitHub API failed: $Endpoint" }
    return (($response -join "`n") | ConvertFrom-Json)
}

# SemVer prerelease numeric identifiers cannot have leading zeroes. Build metadata
# is deliberately excluded from the release-tag interface.
$identifier = '(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)'
$tagPattern = '\Av(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-' + $identifier + '(?:\.' + $identifier + ')*)?\z'
if ($Tag -cnotmatch $tagPattern) { throw 'Tag must be vMAJOR.MINOR.PATCH with an optional SemVer prerelease.' }

$name = 'vk-playground-' + $Tag + '-windows-x64'
$archiveName = $name + '.zip'
$directory = Get-Item -LiteralPath $ArtifactDirectory
if (-not $directory.PSIsContainer -or ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'ArtifactDirectory must be a regular directory.'
}
$files = @(Get-ChildItem -LiteralPath $directory.FullName -Force)
if ($files.Count -ne 2) { throw 'ArtifactDirectory must contain only the ZIP and its .zip.sha256 file.' }
foreach ($file in $files) {
    if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        ($file.Name -cne $archiveName -and $file.Name -cne ($archiveName + '.sha256'))) {
        throw "Unexpected artifact entry: $($file.Name)"
    }
}
$archivePath = Join-Path $directory.FullName $archiveName
$checksumPath = $archivePath + '.sha256'
if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $checksumPath -PathType Leaf)) { throw 'Missing expected archive or checksum.' }
if ((Get-Item -LiteralPath $checksumPath).Length -gt 1024) { throw 'Malformed checksum file.' }
$checksum = [IO.File]::ReadAllText($checksumPath)
$checksumPattern = '\A([0-9a-fA-F]{64})  ' + [regex]::Escape($archiveName) + '(?:\r?\n)?\z'
$checksumMatch = [regex]::Match($checksum, $checksumPattern)
if (-not $checksumMatch.Success) { throw 'Checksum must contain one SHA256 and the exact archive basename.' }

Add-Type -AssemblyName System.IO.Compression
$archiveStream = [IO.File]::Open($archivePath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $actualHash = [BitConverter]::ToString($sha.ComputeHash($archiveStream)).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    if ($actualHash -ne $checksumMatch.Groups[1].Value) { throw 'Archive SHA256 mismatch.' }
    $archiveStream.Position = 0
    $zip = [IO.Compression.ZipArchive]::new($archiveStream, [IO.Compression.ZipArchiveMode]::Read, $true)
    try {
        if ($zip.Entries.Count -eq 0) { throw 'Archive is empty.' }
        $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $infoEntry = $null
        foreach ($entry in $zip.Entries) {
            $path = $entry.FullName
            if ($path.Contains('\') -or $path.Contains(':') -or $path.StartsWith('/') -or
                -not $paths.Add($path.TrimEnd('/'))) { throw "Unsafe or duplicate ZIP path: $path" }
            $segments = $path.TrimEnd('/').Split('/')
            if ($segments[0] -cne $name -or $segments -contains '.' -or $segments -contains '..' -or
                $segments -contains '' -or ($segments.Count -eq 1 -and -not $path.EndsWith('/'))) {
                throw "ZIP entry is outside the expected package root: $path"
            }
            if ($segments[-1] -ieq 'build-info.txt') {
                if ($null -ne $infoEntry -or $path -cne ($name + '/build-info.txt')) {
                    throw 'Archive must contain exactly one build-info.txt at the package root.'
                }
                $infoEntry = $entry
            }
        }
        if ($null -eq $infoEntry -or $infoEntry.Length -eq 0 -or $infoEntry.Length -gt 65536) {
            throw 'Missing or oversized build-info.txt.'
        }
        $reader = [IO.StreamReader]::new($infoEntry.Open(), [Text.UTF8Encoding]::new($false, $true), $true)
        try { $info = $reader.ReadToEnd() | ConvertFrom-Json }
        finally { $reader.Dispose() }
        if ($info -isnot [pscustomobject] -or $info.version -isnot [string] -or $info.version -cne $Tag -or
            $info.commit -isnot [string] -or $info.commit -notmatch '\A[0-9a-fA-F]{40}\z' -or $info.commit -ine $Commit -or
            $info.working_tree_dirty -isnot [bool] -or $info.working_tree_dirty -ne $false -or
            $info.configuration -isnot [string] -or $info.configuration -cne 'RelWithDebInfo' -or
            $info.architecture -isnot [string] -or $info.architecture -cne 'x64') {
            throw 'Package build-info identity does not match the requested clean Windows x64 RelWithDebInfo release.'
        }
    } finally { $zip.Dispose() }

    if ($VerifyOnly) {
        Write-Output "Verified local package: $archivePath"
        Write-Output "SHA256: $actualHash"
        Write-Output 'VerifyOnly did not check the remote tag or create a release.'
        return
    }

    $script:gh = (Get-Command gh -CommandType Application | Select-Object -First 1).Source
    $escapedTag = [Uri]::EscapeDataString($Tag)
    $reference = Invoke-GitHubApi "repos/$Repository/git/ref/tags/$escapedTag"
    if ($reference.ref -cne ('refs/tags/' + $Tag)) { throw 'GitHub returned a different tag reference.' }
    $target = $reference.object
    $seenTags = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    while ($target.type -ceq 'tag') {
        if ($target.sha -notmatch '\A[0-9a-fA-F]{40}\z' -or -not $seenTags.Add($target.sha) -or $seenTags.Count -gt 16) {
            throw 'Malformed or excessively nested annotated tag.'
        }
        $annotation = Invoke-GitHubApi "repos/$Repository/git/tags/$($target.sha)"
        if ($annotation.sha -ine $target.sha) { throw 'Annotated tag identity mismatch.' }
        $target = $annotation.object
    }
    if ($target.type -cne 'commit' -or $target.sha -ine $Commit) { throw 'Published tag does not resolve to the expected commit.' }

    # Listing (including drafts visible to the release token) distinguishes absence
    # from API/auth failures. Never treat a failed lookup as permission to create.
    $releaseTags = @(& $script:gh api --hostname github.com --paginate "repos/$Repository/releases?per_page=100" --jq '.[].tag_name')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot check for an existing release.' }
    if ($releaseTags -ccontains $Tag) { throw "Release already exists for $Tag; it will not be modified." }

    $notes = @"
Windows x64 optimized demo package (RelWithDebInfo).

Source: https://github.com/$Repository/tree/$Commit
Tag: $Tag
Commit: $Commit
Clean source working tree; package identity and SHA256 verified before draft creation.
ZIP SHA256: $actualHash

This is a DRAFT for human review. CI build/package checks are not GPU execution, visual inspection, or Vulkan validation proof. Review the exact candidate on supported hardware before manually publishing.

Local Debug validation on 2026-10-31 (RTX 5070 Ti) reported zero errors after synchronization/presentation-retirement fixes; external layer-name and small-allocation performance warnings remain. This is not validation proof for this release candidate or other GPUs/drivers. Swapchain-maintenance support is now required; see the bundled requirements.

See the bundled README.txt for hardware requirements, controls, licensing, and limitations. The .zip.sha256 sidecar identifies the attached archive.
"@
    $arguments = @('release', 'create', $Tag, '--repo', ('github.com/' + $Repository), '--verify-tag', '--draft',
        '--title', ('vk-playground ' + $Tag), '--notes', $notes)
    if ($Tag.Contains('-')) { $arguments += '--prerelease' }
    $arguments += @('--', $archivePath, $checksumPath)
    $releaseUrl = & $script:gh @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Draft release creation failed; no remote objects will be deleted or replaced.' }
    Write-Output $releaseUrl
} finally { $archiveStream.Dispose() }
