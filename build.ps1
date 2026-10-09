param([switch]$Upload, [string]$Port = "COM3")
$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot
try {
  Copy-Item AY_Player_OLED_SRAM.ino,aymeter.h,ButtonControls.h,TrackBookmark.h AY_Player_OLED_SRAM
  & .\.tools\arduino-cli.exe --config-file .tools\arduino-cli.yaml compile -b SparkFun:avr:promicro:cpu=16MHzatmega32U4 --build-property compiler.c.extra_flags=-mcall-prologues --build-property compiler.cpp.extra_flags=-mcall-prologues --build-property compiler.c.elf.extra_flags=-mcall-prologues --output-dir build AY_Player_OLED_SRAM
  if ($LASTEXITCODE -ne 0) { throw "Compilation failed" }
  if ($Upload) {
    & .\.tools\arduino-cli.exe --config-file .tools\arduino-cli.yaml upload -b SparkFun:avr:promicro:cpu=16MHzatmega32U4 -p $Port --input-dir build --verify
    if ($LASTEXITCODE -ne 0) { throw "Upload failed" }
  }
} finally { Pop-Location }
