"""Apply/restore the guarded Zephyr 4.0 Cortex-M IRQ diagnostic hooks."""
import argparse
import base64
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--framework', required=True, type=Path)
parser.add_argument('--backup', type=Path, default=Path('/tmp/owntech-irq-trace-kernel-backup.json'))
parser.add_argument('action', choices=['apply', 'restore'])
args = parser.parse_args()

def sha(data): return hashlib.sha256(data).hexdigest()

if args.action == 'restore':
    backup = json.loads(args.backup.read_text())
    for item in backup:
        path = args.framework / item['relative']
        if sha(path.read_bytes()) != item['patched_sha256']:
            raise SystemExit('Refusing to overwrite subsequently changed file: ' + str(path))
    for item in backup:
        path = args.framework / item['relative']
        path.write_bytes(base64.b64decode(item['original']))
        print('Restored ' + str(path))
    raise SystemExit(0)

changes = []
def stage(relative, transform):
    path = args.framework / relative
    old = path.read_bytes()
    text = old.decode().replace('\r\n', '\n')
    if 'OWNTECH_TASK_IRQ_LOCK_TRACE' in text:
        raise SystemExit('Already patched; restore first: ' + str(path))
    new = transform(text)
    new_bytes = new.replace('\n', '\r\n').encode() if b'\r\n' in old else new.encode()
    changes.append({'relative': relative, 'original': base64.b64encode(old).decode(),
                    'patched_sha256': sha(new_bytes), 'new': new_bytes})

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit('Unexpected kernel version/context: ' + old)
    return text.replace(old, new)

def inline(text):
    text = replace_once(text, '/* On ARMv7-M and ARMv8-M Mainline CPUs,', '''#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
#define OWNTECH_IRQ_TRACE_PATCH_PRESENT 1
void owntech_irq_trace_begin(uint32_t caller, uint32_t kind);
void owntech_irq_trace_before_restore(uint32_t basepri, uint32_t primask, uint32_t site);
#endif

/* On ARMv7-M and ARMv8-M Mainline CPUs,''')
    text = replace_once(text, '\t__set_BASEPRI_MAX(_EXC_IRQ_DEFAULT_PRIO);\n\t__ISB();', '''	__set_BASEPRI_MAX(_EXC_IRQ_DEFAULT_PRIO);
	__ISB();
#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
    uint32_t trace_site;
    __asm__ volatile("mov %0, pc" : "=r" (trace_site));
    owntech_irq_trace_begin(trace_site, 1U);
#endif''')
    return replace_once(text, '\t__set_BASEPRI(key);\n\t__ISB();', '''#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
    uint32_t trace_site;
    __asm__ volatile("mov %0, pc" : "=r" (trace_site));
    owntech_irq_trace_before_restore(key, __get_PRIMASK(), trace_site);
#endif
	__set_BASEPRI(key);
	__ISB();''')

def swap(text):
    text = replace_once(text, '    isb /* Make the effect of disabling interrupts be realized immediately */', '''    isb /* Make the effect of disabling interrupts be realized immediately */
#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
    /* Preserve the caller-saved registers and exception-return LR, and keep
     * the stack 8-byte aligned around the integer-only trace hook. */
    push {r0-r3, r12, lr}
    mov r0, pc
    movs r1, #2
    bl owntech_irq_trace_begin
    pop {r0-r3, r12, lr}
#endif''')
    return replace_once(text, '    /* restore BASEPRI for the incoming thread */\n    msr BASEPRI, r0', '''    /* restore BASEPRI for the incoming thread */
#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
    push {r0-r3, r12, lr}
    movs r1, #0
    mov r2, pc
    bl owntech_irq_trace_before_restore
    pop {r0-r3, r12, lr}
#endif
    msr BASEPRI, r0''')

def idle(text):
    text = replace_once(text, '#include <cmsis_core.h>', '''#include <cmsis_core.h>
#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
#include "owntech_irq_trace.h"
#endif''')
    first, second = text.split('void arch_cpu_atomic_idle(unsigned int key)', 1)
    for name, fragment in [('arch_cpu_idle', first), ('arch_cpu_atomic_idle', second)]:
        fragment = replace_once(fragment, '\t__disable_irq();', '''	__disable_irq();
#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
    owntech_irq_trace_begin((uint32_t)(uintptr_t)''' + name + ''', 4U);
#endif''')
        instr = '__WFI' if name == 'arch_cpu_idle' else '__WFE'
        fragment = replace_once(fragment, '\tSLEEP_IF_ALLOWED(' + instr + ');', '''#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
    owntech_irq_trace_sleep_begin();
#endif
	SLEEP_IF_ALLOWED(''' + instr + ''');
#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
    owntech_irq_trace_sleep_end();
#endif''')
        fragment = replace_once(fragment, '\t__enable_irq();', '''#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
    owntech_irq_trace_before_restore(__get_BASEPRI(), 0U,
                                     (uint32_t)(uintptr_t)''' + name + ''');
#endif
	__enable_irq();''')
        if name == 'arch_cpu_idle': first = fragment
        else: second = fragment
    return first + 'void arch_cpu_atomic_idle(unsigned int key)' + second

stage('include/zephyr/arch/arm/asm_inline_gcc.h', inline)
stage('arch/arm/core/cortex_m/swap_helper.S', swap)
stage('arch/arm/core/cortex_m/cpu_idle.c', idle)
if args.backup.exists():
    previous = json.loads(args.backup.read_text())
    if any(sha(base64.b64decode(x['original'])) != sha(base64.b64decode(y['original']))
           for x, y in zip(previous, changes)):
        raise SystemExit('Existing backup differs; choose another --backup path')
args.backup.write_text(json.dumps([{k: v for k, v in item.items() if k != 'new'} for item in changes]))
for item in changes:
    path = args.framework / item['relative']
    path.write_bytes(item['new'])
    print('Patched ' + str(path))
