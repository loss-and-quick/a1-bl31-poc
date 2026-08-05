# A1 BL31 Storage Parser Exploit PoC

This repository contains a proof-of-concept for research into the `storage_parse` path in A1 / A113L BL31 firmware.

> [!WARNING]
> **This PoC has not been tested end-to-end on real hardware.**
> The code, addresses, and workflow are based on reverse engineering and should be treated as research material, not a validated exploit chain.

## Overview

The current hypothesis is that **SMC `0x82000069`** (`storage_parse`) contains a bounds-check issue while parsing key entries:

- the destination buffer holds 64 entries (`0x2400` bytes);
- the parser appears to accept more entries than the buffer can safely store; and
- overflowing entries may overwrite adjacent BL31 state, including data used by other secure monitor handlers.

The repository currently provides:

- `a1_storage_exploit.py` — payload generator for overflow, arbitrary-read, and arbitrary-write experiments;
- `a1_linux_smc.c` — a minimal Linux user-space helper for invoking relevant SMC calls and inspecting memory-backed state; and
- supporting build and usage notes for research environments.

## Status and limitations

This repository is intentionally conservative about its claims.

- **Not tested on real hardware.** No successful end-to-end validation is claimed here.
- **Locked production devices are not currently confirmed test platforms.** The main blocker is obtaining shell or equivalent low-level execution on production devices.
- **Addresses may be firmware-specific.** Constants in the PoC were derived from reverse engineering and may differ across builds.
- **The Linux helper assumes privileged access.** In practice this means root access, `/dev/mem`, and a kernel/runtime environment that permits the intended SMC path.

## Candidate test environments

If you want to validate the research, start with a Linux-capable A1 / A113L platform where you already control the software environment.

| Platform | Shell access | Notes |
| --- | --- | --- |
| Khadas VIM3L | Yes | Most practical known candidate for initial validation |
| Amlogic development board | Yes | Reference-style environment if available |
| Consumer device (A1 SoC) | No confirmed path | Production device remains locked down |

## Building

### Option 1: Nix

```bash
nix-shell -p pkgsCross.aarch64-multiplatform.buildPackages.gcc
make
```

### Option 2: Debian / Ubuntu

```bash
sudo apt install gcc-aarch64-linux-gnu python3 python3-pip
pip install pycryptodome
make
```

### Available targets

```bash
make help
```

The main targets are:

- `make` — build the Linux helper and generate payloads;
- `make a1_smc` — build only the helper binary;
- `make payloads` — generate the PoC payload blobs; and
- `make test` — run lightweight local checks for the Python tooling.

## Repository layout

```text
poc/
├── README.md
├── Makefile
├── a1_linux_smc.c
└── a1_storage_exploit.py
```

Generated files such as `a1_smc`, `overflow_payload.bin`, and `arb_read_payload.bin` are not part of the source tree.

## Typical workflow

On a suitable Linux-based test target with root access:

```bash
# Build locally
make

# Copy artifacts to the target
scp a1_smc overflow_payload.bin user@device:~

# Run on the target
ssh user@device
sudo ./a1_smc test
sudo ./a1_smc parse overflow_payload.bin
sudo ./a1_smc dump
```

Expected output is still **hypothetical** at this point. A successful run would likely show a key entry count greater than 64 after parsing, which would be a strong indicator that the overflow condition is real.

## Next research steps

1. Validate the parser behavior on a controllable A1 / A113L Linux target.
2. Confirm whether post-overflow entries can be shaped into a stable read/write primitive.
3. Verify firmware-version differences in addresses and structure layout.
4. Determine whether the same path is reachable on other locked production devices based on the same SoC.

## Legal and safety notice

This repository is for defensive security research and reverse-engineering analysis.

Do not use it against devices, systems, or data you do not own or do not have explicit permission to test.
