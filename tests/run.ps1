$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot)
try {
  $avrBin = Join-Path $PWD '.arduino\data\packages\arduino\tools\avr-gcc\7.3.0-atmel3.6.1-arduino7\bin'
  python tests\extract-playback.py
  if ($LASTEXITCODE -ne 0) { throw 'Playback extraction failed' }
  python tests\extract-file-list.py
  if ($LASTEXITCODE -ne 0) { throw 'File-list extraction failed' }
  python tests\extract-transport.py
  if ($LASTEXITCODE -ne 0) { throw 'Transport extraction failed' }
  foreach ($suite in 'buttons','playback','file-list','transport','bookmark') {
  & "$avrBin\avr-g++.exe" -mmcu=atmega328p -std=gnu++11 -Os "tests\$suite.cpp" -o "tests\$suite.elf"
  if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
  & "$avrBin\avr-objcopy.exe" -O binary "tests\$suite.elf" "tests\$suite.bin"
  if ($LASTEXITCODE -ne 0) { throw 'Test conversion failed' }
  & "$avrBin\avr-nm.exe" "tests\$suite.elf" | Set-Content "tests\$suite.symbols"
  node tests\run-avr.cjs $suite
  if ($LASTEXITCODE -ne 0) { throw 'AVR tests failed' }
  }
} finally { Pop-Location }
