from pathlib import Path
source = Path('AY_Player_OLED_SRAM.ino').read_text()
start = source.index('class CFileList {')
end = source.index('\nCFileList files;', start)
Path('tests/file-list-under-test.h').write_text(source[start:end])
