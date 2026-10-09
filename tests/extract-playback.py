from pathlib import Path

source = Path('AY_Player_OLED_SRAM.ino').read_text()
names = ['bool loadNextByte()', 'bool isNextByteAvailable()', 'byte getNextByte()', 'void playNotes()']
functions = []
for name in names:
    start = source.index(name + ' {')
    opening = source.index('{', start)
    depth = 1
    pos = opening + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    functions.append(source[start:pos])
Path('tests/playback-under-test.h').write_text('\n\n'.join(functions))
