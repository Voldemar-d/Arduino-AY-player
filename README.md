# Arduino-AY-player

Arduino Pro Micro project: music player on AY-3-8910 chip with OLED display and control buttons for playing PSG files from SD card

Based on (and great thanks to the author):
https://www.youtube.com/watch?v=m23hZ-1JwqI

Added:
- SRAM memory (23lc1024) allowing to read file names from SD card and randomize them without repeats
- 8 buttons: switching between full/demo song playback, random/sequencial playback, previous/next song, +/- 5 songs, +/- 10 songs
- shift registers: two 74hc595 for controlling AY-3-8910 via 3 pins and 74hc165 for connecting 8 buttons via 3 pins on Arduino
- switch between audio line output and TDA7050 analog amplifier with volume potentiometer for connecting headphones

## Needed libraries

SSD1306Ascii:
https://github.com/greiman/SSD1306Ascii

Frequency generator:
https://github.com/Rick-G1/FrequencyGenerator

SRAM_23LC:
https://github.com/mattairtech/SRAM_23LC
