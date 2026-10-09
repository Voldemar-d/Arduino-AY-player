from pathlib import Path
source = Path('AY_Player_OLED_SRAM.ino').read_text()
functions = []
for name in ['void pollButtons()', 'void muteAY()', 'void resumePlayback()', 'void loop()']:
    start = source.index(name + ' {')
    pos = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    functions.append(source[start:pos])
Path('tests/transport-under-test.h').write_text('\n\n'.join(functions))
