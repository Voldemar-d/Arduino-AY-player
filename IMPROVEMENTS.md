# Playback controls and reliability improvements

Buttons 1–4 select the previous/next track or move by five tracks. Button 5 toggles pause/resume. A short press and release of button 6 stops playback or restarts the selected track; while paused it resumes playback. Holding button 6 for five seconds saves the selected filename to EEPROM and stops playback. The saved track starts from the beginning at power-on, or the first track is used if the saved file is missing.

Buttons 7/8 retain random/sequential and demo mode selection; pressing both changes the meter view. Buttons 5/6 replace the original minus/plus ten-track shortcuts. The 74HC165 wiring used here is D8 to CP (pin 2), D9 to PL (pin 1), and A0 to QH (pin 9).

The firmware moves button, OLED, and EEPROM work out of the timer interrupt, handles partial PSG commands safely, validates the 16-byte PSG header, and checks file access and filename bounds. SRAM is tested at startup; when unavailable, filenames are read directly from the SD root directory. The fallback shuffle visits every track once per cycle but does not produce a uniform distribution over all permutations.

## Build and tests

Use the libraries from the original README, with SdFat 1.1.4 (the 1.x API), SSD1306Ascii 1.3.5, Arduino AVR 1.8.8, and SparkFun AVR 1.1.13. The tested board is `SparkFun:avr:promicro:cpu=16MHzatmega32U4`.

`build.ps1` expects Arduino CLI and its configuration in `.tools/arduino-cli.exe` and `.tools/arduino-cli.yaml`. It copies the four root source files into the Arduino sketch directory and compiles with `-mcall-prologues`. Run `./build.ps1`; upload is optional through `-Upload -Port <port>`.

`./tests/run.ps1` requires Python, Node.js, AVR GCC at the path specified in that script, and avr8js unpacked at `.tools/avr8js/package`. These local dependencies are excluded from Git. The runner extracts the current firmware functions before compiling and simulating five suites. `./check-psg.ps1` validates the included synthetic PSG fixtures; `make-test-psg.ps1` regenerates them.

Validation on 2026-10-10: all 271 AVR assertions passed (57 buttons, 19 playback, 154 file list, 20 transport, 21 bookmark). Both PSG fixtures passed validation. The firmware compiled using 28,258/28,672 bytes Flash and 1,738/2,560 bytes static RAM.

Hardware audio, button behavior, power-cycle restoration, and stack headroom still need physical validation. Pause preserves the PSG position and output register values; it does not preserve the internal phase of the AY envelope or oscillators. Flash headroom is limited, and the SD fallback scans the directory when selecting tracks. The tests simulate ATmega328P instruction execution with peripheral stubs; the complete sketch is compiled for ATmega32U4 separately.
