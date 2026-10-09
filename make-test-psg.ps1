$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force "$PSScriptRoot\test-psg" | Out-Null
function Add-Reg($Data, [int]$Reg, [int]$Value) { $Data.Add([byte]$Reg); $Data.Add([byte]$Value) }
function Add-Wait($Data, [int]$Frames) { for ($j=0; $j -lt $Frames; $j++) { $Data.Add([byte]255) } }
function Add-Tone($Data, [int]$Channel, [double]$Hz) {
  $period = [int][Math]::Round(1780000 / (16 * $Hz))
  Add-Reg $Data ($Channel*2) ($period -band 255)
  Add-Reg $Data ($Channel*2+1) (($period -shr 8) -band 15)
}
function Initialize-PSG($Data) {
  $Data.AddRange([byte[]](0x50,0x53,0x47,0x1A,0,0,0,0,0,0,0,0,0,0,0,0,0xFF))
  for ($r=0; $r -lt 14; $r++) { Add-Reg $Data $r 0 }
  Add-Reg $Data 7 0x3F
}
function Finish-PSG($Data, $Name) {
  foreach ($r in 8,9,10) { Add-Reg $Data $r 0 }
  Add-Reg $Data 7 0x3F
  Add-Wait $Data 25
  $Data.Add([byte]0xFD)
  [IO.File]::WriteAllBytes("$PSScriptRoot\test-psg\$Name", $Data.ToArray())
}
$data = [Collections.Generic.List[byte]]::new()
Initialize-PSG $data
for ($repeat=0; $repeat -lt 3; $repeat++) {
  for ($channel=0; $channel -lt 3; $channel++) {
    foreach ($r in 8,9,10) { Add-Reg $data $r 0 }
    Add-Tone $data $channel (@(440,660,880)[$channel])
    Add-Reg $data 7 (0x3F -bxor (1 -shl $channel))
    Add-Reg $data (8+$channel) 10
    Add-Wait $data 75
    Add-Reg $data (8+$channel) 0
    Add-Wait $data 25
  }
}
Finish-PSG $data 'TESTABC.PSG'
$data = [Collections.Generic.List[byte]]::new()
Initialize-PSG $data
Add-Reg $data 7 0x3E
for ($repeat=0; $repeat -lt 3; $repeat++) {
  foreach ($frequency in 261.63,293.66,329.63,349.23,392,440,493.88,523.25,493.88,440,392,349.23,329.63,293.66,261.63) {
    Add-Tone $data 0 $frequency
    Add-Reg $data 8 10
    Add-Wait $data 20
    Add-Reg $data 8 0
    Add-Wait $data 5
  }
}
Finish-PSG $data 'TESTMUS.PSG'
Get-ChildItem "$PSScriptRoot\test-psg\*.PSG" | Select-Object Name,Length
