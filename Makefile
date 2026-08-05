# A1 BL31 exploit PoC build helpers
#
# Cross-compile for aarch64 Linux.

CC = aarch64-linux-gnu-gcc
CFLAGS = -Wall -Wextra -O2 -static
PYTHON = python3

.PHONY: all clean payloads test deps help

all: a1_smc payloads

# Build the Linux SMC helper.
a1_smc: a1_linux_smc.c
	$(CC) $(CFLAGS) -o $@ $<
	@echo "[+] Built a1_smc (static aarch64 binary)"

# Generate research payload blobs.
payloads:
	$(PYTHON) a1_storage_exploit.py
	@echo "[+] Generated payload files"

# Run lightweight local checks.
test:
	$(PYTHON) -c "from Crypto.Cipher import AES; print('[+] pycryptodome OK')"
	$(PYTHON) a1_storage_exploit.py

clean:
	rm -f a1_smc *.bin

# Install Python dependency used by the payload generator.
deps:
	pip install pycryptodome

help:
	@echo "A1 BL31 Storage Parser Exploit PoC"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build the helper and generate payloads"
	@echo "  a1_smc   - Build the Linux SMC helper"
	@echo "  payloads - Generate PoC payload blobs"
	@echo "  test     - Run lightweight local checks"
	@echo "  clean    - Remove generated binaries and payloads"
	@echo "  deps     - Install Python dependencies"
	@echo ""
	@echo "Example target-side workflow (requires root and a compatible device):"
	@echo "  1. scp a1_smc overflow_payload.bin user@device:"
	@echo "  2. ssh user@device"
	@echo "  3. sudo ./a1_smc test"
	@echo "  4. sudo ./a1_smc parse overflow_payload.bin"
	@echo "  5. sudo ./a1_smc dump"
