# Places the pinned GLB in front of the existing demo camera. Geometry, textures,
# accessors and binary chunks are unchanged; only a parent node is added.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination,
    [Parameter(Mandatory = $true)][double[]]$Translation
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($Translation.Count -ne 3) { throw 'Scene translation requires three coordinates.' }
$inputStream = [IO.File]::OpenRead($Source)
$reader = [IO.BinaryReader]::new($inputStream)
$outputStream = $null
$writer = $null
try {
    if ($reader.ReadUInt32() -ne 0x46546c67 -or $reader.ReadUInt32() -ne 2) { throw 'Expected a GLB version 2 scene.' }
    if ($reader.ReadUInt32() -ne $inputStream.Length) { throw 'Invalid GLB total length.' }
    $jsonLength = $reader.ReadUInt32()
    if ($reader.ReadUInt32() -ne 0x4e4f534a) { throw 'Expected GLB JSON chunk.' }
    $document = [Text.Encoding]::UTF8.GetString($reader.ReadBytes($jsonLength)) | ConvertFrom-Json
    if ($document.scenes.Count -ne 1) { throw 'The pinned demo scene must contain one scene.' }
    $parent = [pscustomobject]@{
        name = 'vk-playground demo placement'
        children = @($document.scenes[0].nodes)
        translation = @($Translation)
    }
    $parentIndex = $document.nodes.Count
    $document.nodes = @($document.nodes) + @($parent)
    $document.scenes[0].nodes = @($parentIndex)
    $json = $document | ConvertTo-Json -Depth 100 -Compress
    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
    $padding = (4 - ($bytes.Length % 4)) % 4
    $remaining = $inputStream.Length - $inputStream.Position
    $outputStream = [IO.File]::Create($Destination)
    $writer = [IO.BinaryWriter]::new($outputStream)
    $writer.Write([uint32]0x46546c67)
    $writer.Write([uint32]2)
    $writer.Write([uint32](20 + $bytes.Length + $padding + $remaining))
    $writer.Write([uint32]($bytes.Length + $padding))
    $writer.Write([uint32]0x4e4f534a)
    $writer.Write($bytes)
    for ($i = 0; $i -lt $padding; $i++) { $writer.Write([byte]32) }
    $inputStream.CopyTo($outputStream)
} finally {
    if ($writer) { $writer.Dispose() }
    elseif ($outputStream) { $outputStream.Dispose() }
    $reader.Dispose()
}
