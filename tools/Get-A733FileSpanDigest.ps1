# SPDX-License-Identifier: Apache-2.0
# Read-only bounded SHA256 of an exact byte interval; no file writes.
param([Parameter(Mandatory=$true)][string]$Path,
      [long]$Begin=0,[long]$End=-1)
$ErrorActionPreference='Stop'
$stream=[System.IO.File]::OpenRead((Resolve-Path -LiteralPath $Path).Path)
$hash=[System.Security.Cryptography.SHA256]::Create()
try {
    if($End -eq -1){$End=$stream.Length}
    if($Begin -lt 0 -or $End -lt $Begin -or $End -gt $stream.Length){throw 'Invalid byte interval'}
    $stream.Position=$Begin
    $remaining=$End-$Begin
    $buffer=New-Object byte[] 1048576
    while($remaining -gt 0){
        $count=$stream.Read($buffer,0,[int][Math]::Min($remaining,$buffer.Length))
        if($count -eq 0){throw 'Unexpected EOF'}
        [void]$hash.TransformBlock($buffer,0,$count,$buffer,0)
        $remaining-=$count
    }
    [void]$hash.TransformFinalBlock([byte[]]@(),0,0)
    [BitConverter]::ToString($hash.Hash).Replace('-','').ToLowerInvariant()
} finally {$hash.Dispose();$stream.Dispose()}
