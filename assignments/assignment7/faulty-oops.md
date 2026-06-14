# Faulty Driver Kernel Oops Analysis

## Command Executed

```
echo "hello_world" > /dev/faulty
```

## Kernel Oops Output

```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b68000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 141 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008dabd20
x29: ffffffc008dabd80 x28: ffffff8001b31a80 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 000000000000000c x22: 000000000000000c x21: ffffffc008dabdc0
x20: 0000005578db1990 x19: ffffff8001b67d00 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000787000 x3 : ffffffc008dabdc0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f)
---[ end trace 0000000000000000 ]---
```

## Analysis

### Root Cause

The oops is a **NULL pointer dereference** in the `faulty` kernel module's write handler. The fault occurs at virtual address `0x0000000000000000` — the driver intentionally dereferences a NULL pointer to demonstrate kernel fault behavior.

The responsible line in `misc-modules/faulty.c` is:

```c
ssize_t faulty_write(struct file *filp, const char __user *buf, size_t count,
        loff_t *pos)
{
    /* make a simple fault by dereferencing a null pointer */
    *(int *)0 = 0;
    return 0;
}
```

### Key Fields Explained

| Field | Value | Meaning |
|-------|-------|---------|
| `ESR = 0x96000045` | Exception Syndrome Register | Encodes a Data Abort at current EL (kernel mode) |
| `EC = 0x25` | DABT (current EL) | Exception Class: Data Abort from EL1 (kernel) |
| `FSC = 0x05` | Level 1 translation fault | Page table walk failed at level 1 — address 0x0 is not mapped |
| `WnR = 1` | Write fault | The fault was caused by a **write** operation |
| `pc : faulty_write+0x10/0x20 [faulty]` | Program counter | Crash is 16 bytes into `faulty_write` (function is 32 bytes total) |
| `Tainted: G O` | Kernel taint flags | G = tainted by out-of-tree module; O = out-of-tree module loaded |

### Call Trace Walkthrough

```
faulty_write+0x10/0x20 [faulty]     <- NULL dereference happens here
ksys_write+0x74/0x110               <- Kernel implementation of write() syscall
__arm64_sys_write+0x1c/0x30        <- ARM64 syscall entry wrapper
invoke_syscall+0x54/0x130           <- Syscall dispatch layer
el0_svc_common.constprop.0+0x44    <- EL0 (userspace) service common path
do_el0_svc+0x2c/0xc0               <- EL0 service handler
el0_svc+0x2c/0x90                  <- Entry from userspace svc instruction
el0t_64_sync_handler+0xf4/0x120    <- Synchronous exception handler
el0t_64_sync+0x18c/0x190           <- Low-level ARM64 exception vector
```

The user ran `echo "hello_world" > /dev/faulty`, which issued a `write()` syscall. The kernel routed it through the VFS layer to `faulty_write`, which immediately attempted to write to address `0x0`. The MMU raised a Data Abort exception, and the kernel printed the oops.

### Register State at Crash

| Register | Value | Meaning |
|----------|-------|---------|
| `x0` | `0x0` | First argument (struct file pointer) |
| `x1` | `0x0` | Second argument (buf pointer) — the NULL being dereferenced |
| `x2` | `0xc` (12) | Third argument (count) — length of `"hello_world\n"` (12 bytes) |
| `pc` | `faulty_write+0x10` | Points to the faulting store instruction `b900003f` = `str wzr, [x1]` |

### Code Dump

```
Code: d2800001 d2800000 d503233f d50323bf (b900003f)
```

The instruction in parentheses `(b900003f)` is the faulting instruction. On ARM64:
- `b900003f` = `str wzr, [x1]` — store zero word to the address in register `x1`
- `x1 = 0x0` at the time of the fault, confirming the NULL pointer write

### Why the System Survives

The `[#1]` in the oops header means this is the first oops. Since the fault occurred in a kernel module's file operation handler (not in core kernel code), the kernel killed only the offending process (`sh`, PID 141) and continued running normally.
