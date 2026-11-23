#!/usr/bin/env python3
"""Save states against an uninterrupted run (runtime/src/state.c, gng/state.h).

gng_headless runs a script from power-on, saving the board's state after frame K's VBLANK; a second gng_headless
starts from that state. Everything the second one gives must be what the first gave after the state's frame:
the bus at each VBLANK, each IRQ taken (its state, PC, cycle), the sound board at each VBLANK, every YM2203
write, the mix's samples (the WAV) and the pictures.

    python tools/check_states.py [--accuracy mame|board] [--inputs FILE] [--frames N] [--at K ...] [--program EXE]

--program checks another build of the runner: build/bin/gng_headless_c, the readable C (its states are taken where
the main loop waits for work, the first time after the frame's VBLANK: runtime/src/m6809_program.c).

--load-with EXE: the state saved by --program, loaded by EXE (the readable C's states in the translation,
build/bin/gng_headless: runtime/src/state.c), against EXE's own uninterrupted run. The stack's bytes below S are
each program's own (the readable C does not write the state the IRQ's entry pushes), so the bus is compared at each
IRQ but the stack (--irq-state, as tools/check_translation.py --decompiled), not at each VBLANK.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADLESS = os.path.join(ROOT, 'build', 'bin', 'gng_headless.exe' if os.name == 'nt' else 'gng_headless')


def run(args, folder, name, program=None, stackless=False):
    outputs = {key: os.path.join(folder, f'{name}.{key}') for key in ('bus', 'irq', 'sound', 'ym', 'wav', 'pictures')}
    command = [program or HEADLESS, *args, '--bus-hashes', outputs['bus'],
               '--irq-state' if stackless else '--irq-hashes', outputs['irq'],
               '--sound', outputs['sound'], '--ym-writes', outputs['ym'], '--wav', outputs['wav'],
               '--picture-hashes', outputs['pictures']]
    done = subprocess.run(command, capture_output=True, text=True)
    if done.returncode:
        sys.exit(f'gng_headless failed: {" ".join(command)}\n{done.stdout}{done.stderr}')
    return outputs, done.stdout


def lines(path):
    with open(path) as file:
        return file.read().splitlines()


def by_frame(path, first):
    """The lines of frames >= first (the first column the frame)"""
    return [line for line in lines(path) if int(line.split()[0]) >= first]


def wav_data(path):
    with open(path, 'rb') as file:
        return file.read()[44:]


def compare(what, whole, resumed):
    if whole == resumed:
        print(f'  {what}: the same ({len(resumed)})')
        return True
    for i, (a, b) in enumerate(zip(whole, resumed)):
        if a != b:
            print(f'  {what}: DIFFERENT at {i}: {a!r} uninterrupted, {b!r} from the state')
            return False
    print(f'  {what}: DIFFERENT lengths: {len(whole)} uninterrupted, {len(resumed)} from the state')
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--accuracy', default='mame', choices=('mame', 'board'))
    parser.add_argument('--inputs', default=os.path.join(ROOT, 'tests', 'controls', 'walk_right.txt'))
    parser.add_argument('--frames', type=int, default=2400)
    parser.add_argument('--at', type=int, nargs='+', default=[700, 1500])
    parser.add_argument('--program', help='the runner (default: build/bin/gng_headless)')
    parser.add_argument('--load-with', help='the runner that loads the state (default: the same)')
    options = parser.parse_args()
    cross = options.load_with is not None
    global HEADLESS
    if options.program:
        HEADLESS = options.program
    common = ['--accuracy', options.accuracy, '--frames', str(options.frames)]
    if options.inputs:
        common += ['--inputs', options.inputs]
    good = True
    with tempfile.TemporaryDirectory() as folder:
        for at in options.at:
            state = os.path.join(folder, f'{at}.state')
            whole, said = run([*common, '--save-state', str(at), state], folder, f'whole{at}')
            found = re.search(r'state saved in frame (\d+) \(pc ([0-9A-F]+), cycle (\d+), sound sample (\d+)\)', said)
            if not found:
                sys.exit(f'no state saved at frame {at}:\n{said}')
            first, sample = int(found.group(1)), int(found.group(4))
            print(f'{options.accuracy} mode, {os.path.basename(options.inputs)}: the state after frame {at}\'s '
                  f'VBLANK (frame {first}, pc {found.group(2)}, cycle {found.group(3)}), to frame {options.frames}')
            if cross:
                # (the loader's own run from the power on is what the state's run must give from there)
                print(f'  loaded by {os.path.basename(options.load_with)}, against its own uninterrupted run')
                whole, _ = run(common, folder, f'loader{at}', options.load_with, stackless=True)
                resumed, _ = run([*common, '--load-state', state], folder, f'resumed{at}', options.load_with,
                                 stackless=True)
            else:
                resumed, _ = run([*common, '--load-state', state], folder, f'resumed{at}')
                good &= compare('bus at each VBLANK', by_frame(whole['bus'], first), lines(resumed['bus']))
            good &= compare('sound board at each VBLANK', by_frame(whole['sound'], first), lines(resumed['sound']))
            # (the IRQs and the YM2203 writes after the state's: the uninterrupted run's last ones; an IRQ's line
            # starts with its count from power-on)
            irqs = [line.split(' ', 1)[1] for line in lines(resumed['irq'])]
            whole_irqs = [line.split(' ', 1)[1] for line in lines(whole['irq'])]
            good &= compare('IRQs taken', whole_irqs[len(whole_irqs) - len(irqs):] if irqs else [], irqs)
            writes = lines(resumed['ym'])
            whole_writes = lines(whole['ym'])
            good &= compare('YM2203 writes', whole_writes[len(whole_writes) - len(writes):] if writes else [],
                            writes)
            if not writes or not irqs:
                print('  (no IRQs or YM2203 writes after the state: not a check)')
                good = False
            data = wav_data(resumed['wav'])
            whole_data = wav_data(whole['wav'])[2 * sample:]
            same = data == whole_data
            print(f'  sound samples: {"the same" if same else "DIFFERENT"} ({len(data) // 2} from sample {sample})')
            good &= same
            # (MAME mode's check pictures are a frame's indexes in the next frame's colours: from the state's
            # second frame on)
            good &= compare('pictures', by_frame(whole['pictures'], first + 1),
                            by_frame(resumed['pictures'], first + 1))
    print('ALL THE SAME' if good else 'FAILED')
    return 0 if good else 1


if __name__ == '__main__':
    sys.exit(main())
