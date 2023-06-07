# Arduino-AY-player
<<<<<<< HEAD
Arduino Pro Micro project: PSG music player on AY-3-8910 chip from SD card with OLED display and control buttons

Based on (and great thanks to the author):
https://www.youtube.com/watch?v=m23hZ-1JwqI

Added:
- SRAM memory (23lc1024) allowing to read file names from SD card and randomizing them without repeats
- 8 buttons: switching between full/demo song playback, random/sequencial playback, previous/next song, +/- 5 songs, +/- 10 songs
- shift registers: two 74hc595 for controlling AY-3-8910 via 3 pins and 74hc165 for connecting 8 buttons via 3 pins on Arduino
- switch between line audio output and TDA7050 analog amplifier with volume potentiometer for connecting head phones
=======
Arduino Pro Micro project: music player with AY-3-8910 chip with OLED display and control buttons playing PSG files from SD card
>>>>>>> f57014e0d72136e66d8733e3db64c961dae49802
