const fs = require('fs');
const { CPU, avrInstruction } = require('../.tools/avr8js/package');
const suite = process.argv[2] || 'buttons';
const bytes = fs.readFileSync('tests/' + suite + '.bin');
const program = new Uint16Array(16384);
for (let i = 0; i < bytes.length; i += 2) program[i >> 1] = bytes[i] | ((bytes[i + 1] || 0) << 8);
const symbols = fs.readFileSync('tests/' + suite + '.symbols', 'utf8');
const address = name => {
  const match = symbols.match(new RegExp('^([0-9a-f]+) \\w ' + name + '$', 'm'));
  if (!match) throw new Error('Missing symbol ' + name);
  return parseInt(match[1], 16) - 0x800000;
};
const cpu = new CPU(program);
const done = address('test_done');
for (let n = 0; n < 5000000 && cpu.data[done] !== 0xa5; n++) avrInstruction(cpu);
if (cpu.data[done] !== 0xa5) throw new Error('AVR tests timed out');
const failed = cpu.data[address('test_failure')];
if (failed) throw new Error('AVR assertion failed: ' + failed);
console.log('PASS: ' + cpu.data[address('test_count')] + ' assertions against compiled ' + suite + ' on simulated AVR');
