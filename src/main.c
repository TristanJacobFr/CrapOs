#include <stdint.h>

#include "serial.h"
#include "console.h"
#include "limine.h"

__attribute__((used, aligned(8), section(".limine_requests")))
/* The bundled Limine 10.8.5 supports base revisions up to 5. */
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(5);

__attribute__((used, aligned(8), section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, aligned(8), section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

__attribute__((noreturn))
void kmain(void)
{
	serial_init();

	if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
		serial_print("[ERROR] Unsupported Limine base revision\n");
		goto halt;
	}

	serial_print("[OK] Serial initialized\n");
	if (!console_init()) {
		serial_print("[ERROR] No supported framebuffer available\n");
		goto halt;
	}

	console_print("CrapOS\n\n");
	console_print("[OK] Screen initialized\n");
	console_print("[OK] CrapOS kernel started\n");
	serial_print("[OK] Screen initialized\n");
	serial_print("[OK] CrapOs kernel started\n");

	halt:
		for (;;)
			__asm__ volatile ("cli; hlt");
}
