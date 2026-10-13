#!/usr/bin/env python3
"""Measure bounded DWT profiles through ST-Link using the matching STLink ELF.

Run with PlatformIO's Python (pyelftools dependency), after flashing both boards:
  ~/.platformio/penv/bin/python owntech/scripts/measure_rs485_cpu.py --mode 1 --label outer
For the second command, first add -DRS485_PROFILE_DETAIL, rebuild and flash both:
  ~/.platformio/penv/bin/python owntech/scripts/measure_rs485_cpu.py --mode 2 --label detailed
Use --kind adc for ADC dispatch and enable -DADC_DISPATCH_PROFILE in the build.
ADC internal timings additionally require -DADC_PROFILE_DETAIL.
This writes only profiler RAM, never flash, and never halts or resets the CPU.
"""
import argparse
import concurrent.futures
import json
import hashlib
from pathlib import Path
import struct
import subprocess
from elftools.elf.elffile import ELFFile

STAGES = ['dispatch', 'poll_before', 'extra_tx_poll', 'dma_position', 'copy',
          'poll_after', 'frame_parse', 'tx_prepare', 'clock_floor']
TARGETS = [('master', '000D00304741500520383733', 0x002B001A, 0),
           ('slave', '001E003A3331511234333834', 0x002B004E, 1)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', type=int, choices=[1, 2], required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--kind', choices=['rs485', 'adc'], default='rs485')
    args = parser.parse_args()
    if not args.label.replace('-', '').replace('_', '').isalnum():
        parser.error('label must contain only letters, digits, hyphens or underscores')
    build = Path('.pio/build/STLink')
    with (build / 'firmware.elf').open('rb') as file:
        elf = ELFFile(file)
        symbols = {s.name: int(s['st_value']) for s in
                   elf.get_section_by_name('.symtab').iter_symbols()}
    stages = STAGES if args.kind == 'rs485' else ['dispatch', 'adc1', 'adc2', 'adc3', 'adc4', 'adc5', 'dma_cursor', 'copy', 'clock_floor']
    profile_prefix = 'rs485' if args.kind == 'rs485' else 'adc'
    stat_address = symbols[profile_prefix + '_profile_stats']
    mode_address = symbols[profile_prefix + '_profile_mode']
    counter_address = symbols['counter_receive']
    output = Path('docs/rs485-dispatch-check/cpu' if args.kind == 'rs485' else 'docs/adc-dispatch-check/cpu')
    output.mkdir(parents=True, exist_ok=True)
    zeros = output / 'zero-stats.bin'
    zeros.write_bytes(bytes(32 * len(stages)))
    package = Path.home() / '.platformio/packages/tool-openocd'
    base = [str(package / 'bin/openocd'), '-s', str(package / 'openocd/scripts'),
            '-f', 'interface/stlink.cfg', '-f', 'target/stm32g4x.cfg']

    def measure(target):
        role, serial, uid, module_id = target
        prefix = output / (args.label + '-' + role)
        active = prefix.with_suffix('.active.bin')
        # Verify exact code before using ELF addresses or writing profiler RAM.
        checks = [f'adapter serial {serial}', 'gdb port disabled',
                  'tcl port disabled', 'telnet port disabled', 'init',
                  f'if {{[read_memory 0x1fff7590 32 1] != {uid}}} {{error "Wrong target UID"}}',
                  f'dump_image {active} 0x08010000 {(build / "firmware.bin").stat().st_size}',
                  'shutdown']
        command = base + [arg for c in checks for arg in ['-c', c]]
        check = subprocess.run(command, capture_output=True, text=True, timeout=15)
        if check.returncode or active.read_bytes()[512:] != (build / 'firmware.bin').read_bytes()[512:]:
            raise RuntimeError(f'{role}: running firmware does not match STLink ELF\n{check.stdout}{check.stderr}')
        active.unlink()
        commands = [f'adapter serial {serial}', 'gdb port disabled',
                    'tcl port disabled', 'telnet port disabled', 'init',
                    f'if {{[read_memory {symbols["module_ID"]} 8 1] != {module_id}}} {{error "Wrong module role"}}',
                    f'mww {mode_address} 0', 'sleep 2',
                    f'load_image {zeros} {stat_address} bin',
                    f'dump_image {prefix}.before.bin {counter_address} 40',
                    f'mww {mode_address} {args.mode}', 'sleep 21000',
                    f'if {{[read_memory {mode_address} 32 1] != 0}} {{mww {mode_address} 0; error "Profile did not finish"}}',
                    f'dump_image {prefix}.stats.bin {stat_address} {32 * len(stages)}',
                    f'dump_image {prefix}.after.bin {counter_address} 40', 'shutdown']
        run = subprocess.run(base + [arg for c in commands for arg in ['-c', c]],
                             capture_output=True, text=True, timeout=30)
        prefix.with_suffix('.log').write_text(check.stdout + check.stderr + run.stdout + run.stderr)
        if run.returncode:
            raise RuntimeError(f'{role}: {run.stdout}{run.stderr}')
        raw = Path(str(prefix) + '.stats.bin').read_bytes()
        stats = {}
        for i, stage in enumerate(stages):
            count, minimum, maximum, last, cycles, nbytes = struct.unpack_from('<IIIIQQ', raw, i * 32)
            stats[stage] = dict(count=count, minimum_cycles=minimum, maximum_cycles=maximum,
                                last_cycles=last, sum_cycles=cycles, bytes=nbytes,
                                mean_us=cycles / count / 170 if count else None,
                                minimum_us=minimum / 170, maximum_us=maximum / 170)
            if args.kind == 'adc':
                stats[stage]['samples'] = stats[stage].pop('bytes')
        counters = {}
        counter_names = ['counter_receive', 'counter_timer', 'rs485_rx_discarded',
                         'rs485_tx_errors', 'rs485_tx_skipped', 'rs485_errors']
        counter_labels = ['received', 'tasks', 'discarded', 'tx_errors', 'tx_skipped', 'errors']
        for suffix in ['before', 'after']:
            data = Path(str(prefix) + '.' + suffix + '.bin').read_bytes()
            counters[suffix] = {}
            for name, label in zip(counter_names, counter_labels):
                address = next(value for key, value in symbols.items() if key.endswith(name))
                counters[suffix][label] = struct.unpack_from('<I', data, address - counter_address)[0]
        if stats['dispatch']['count'] != 100000:
            raise RuntimeError(f'{role}: incomplete dispatch profile')
        if args.mode == 2 and stats['copy']['count'] == 0:
            define = 'RS485_PROFILE_DETAIL' if args.kind == 'rs485' else 'ADC_PROFILE_DETAIL'
            raise RuntimeError(f'{role}: detailed build requires -D{define}')
        result = dict(role=role, kind=args.kind, mode=args.mode, environment='STLink', cpu_hz=170000000,
                      elf_sha256=hashlib.sha256((build / 'firmware.elf').read_bytes()).hexdigest(),
                      stats=stats, counters=counters)
        prefix.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
        return result

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        for result in pool.map(measure, TARGETS):
            print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
