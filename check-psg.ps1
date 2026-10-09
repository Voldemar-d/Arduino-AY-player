param([string]$Directory = "$PSScriptRoot\test-psg")
$ErrorActionPreference = 'Stop'
Get-ChildItem -LiteralPath $Directory -Filter *.PSG | ForEach-Object {
  $bytes = [IO.File]::ReadAllBytes($_.FullName)
  if ($bytes.Length -lt 16 -or [BitConverter]::ToString($bytes[0..3]) -ne '50-53-47-1A') { throw "Bad header: $($_.Name)" }
  $position=16; $frames=0; $writes=0; $ended=$false
  while ($position -lt $bytes.Length) {
    $command = $bytes[$position++]
    if ($command -eq 255) { $frames++; continue }
    if ($command -eq 253) { $ended=$true; break }
    if ($command -eq 254) {
      if ($position -ge $bytes.Length) { throw "Truncated delay: $($_.Name)" }
      $frames += 4*$bytes[$position++]; continue
    }
    if ($command -gt 15 -or $position -ge $bytes.Length) { throw "Invalid/truncated register at $($position-1): $($_.Name)" }
    $position++; $writes++
  }
  [pscustomobject]@{Name=$_.Name;Bytes=$bytes.Length;Seconds=$frames/50;Writes=$writes;EndMarker=$ended;UnparsedBytes=$bytes.Length-$position}
}
