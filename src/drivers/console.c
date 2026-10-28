#include <stddef.h>
#include <stdint.h>

#include "console.h"
#include "limine.h"

__attribute__((used, aligned(8), section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
	.id = LIMINE_FRAMEBUFFER_REQUEST_ID,
	.revision = 0
};

/* Original 5x7 bitmap alphabet, enlarged 2x for readability. Lowercase folds
 * to uppercase; unsupported characters use the question-mark glyph. */
static const char characters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789[]:.-/?";
static const uint8_t glyphs[][7] = {
	{14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
	{14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
	{31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
	{14,17,16,23,17,17,14}, {17,17,17,31,17,17,17},
	{14,4,4,4,4,4,14}, {7,2,2,2,2,18,12},
	{17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
	{17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
	{14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
	{14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
	{15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
	{17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
	{17,17,17,21,21,27,17}, {17,17,10,4,10,17,17},
	{17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
	{14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
	{14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
	{2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
	{14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
	{14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
	{14,8,8,8,8,8,14}, {14,2,2,2,2,2,14},
	{0,4,4,0,4,4,0}, {0,0,0,0,0,4,4},
	{0,0,0,31,0,0,0}, {1,2,2,4,8,8,16},
	{14,17,1,2,4,0,4}
};

#define CELL_WIDTH 12
#define CELL_HEIGHT 18

static struct limine_framebuffer *screen;
static uint64_t column, row;
static uint32_t foreground, background;

static uint32_t channel(uint8_t value, uint8_t size, uint8_t shift)
{
	return ((uint32_t)value * ((1u << size) - 1) / 255) << shift;
}

static uint32_t color(uint8_t red, uint8_t green, uint8_t blue)
{
	return channel(red, screen->red_mask_size, screen->red_mask_shift)
		| channel(green, screen->green_mask_size, screen->green_mask_shift)
		| channel(blue, screen->blue_mask_size, screen->blue_mask_shift);
}

static void pixel(uint64_t x, uint64_t y, uint32_t value)
{
	volatile uint8_t *p = (volatile uint8_t *)screen->address
		+ y * screen->pitch + x * (screen->bpp / 8);
	for (unsigned i = 0; i < screen->bpp / 8; ++i)
		p[i] = (uint8_t)(value >> (i * 8));
}

static void clear_rows(uint64_t start)
{
	for (uint64_t y = start; y < screen->height; ++y)
		for (uint64_t x = 0; x < screen->width; ++x)
			pixel(x, y, background);
}

static bool valid_channel(uint8_t size, uint8_t shift, uint16_t bpp)
{
	return size > 0 && size <= 8 && shift + size <= bpp;
}

bool console_init(void)
{
	struct limine_framebuffer_response *response = framebuffer_request.response;
	screen = NULL;
	if (!response || !response->framebuffer_count || !response->framebuffers)
		return false;

	for (uint64_t i = 0; i < response->framebuffer_count; ++i) {
		struct limine_framebuffer *fb = response->framebuffers[i];
		if (!fb || !fb->address || fb->memory_model != LIMINE_FRAMEBUFFER_RGB
			|| (fb->bpp != 24 && fb->bpp != 32)
			|| fb->width < CELL_WIDTH || fb->height < CELL_HEIGHT
			|| fb->width > SIZE_MAX / (fb->bpp / 8)
			|| fb->pitch < fb->width * (fb->bpp / 8)
			|| fb->height > SIZE_MAX / fb->pitch
			|| !valid_channel(fb->red_mask_size, fb->red_mask_shift, fb->bpp)
			|| !valid_channel(fb->green_mask_size, fb->green_mask_shift, fb->bpp)
			|| !valid_channel(fb->blue_mask_size, fb->blue_mask_shift, fb->bpp))
			continue;
		screen = fb;
		column = row = 0;
		foreground = color(220, 230, 240);
		background = color(16, 24, 40);
		clear_rows(0);
		return true;
	}
	return false;
}

static void newline(void)
{
	column = 0;
	if (++row < screen->height / CELL_HEIGHT)
		return;

	/* Copy forwards: the destination is below the source in memory. */
	volatile uint8_t *bytes = screen->address;
	uint64_t offset = CELL_HEIGHT * screen->pitch;
	uint64_t length = (screen->height - CELL_HEIGHT) * screen->pitch;
	for (uint64_t i = 0; i < length; ++i)
		bytes[i] = bytes[i + offset];
	--row;
	clear_rows(row * CELL_HEIGHT);
}

void console_print(const char *text)
{
	if (!screen)
		return;
	while (*text) {
		char c = *text++;
		if (c == '\n') {
			newline();
			continue;
		}
		if (c == '\r') {
			column = 0;
			continue;
		}
		if (column == screen->width / CELL_WIDTH)
			newline();
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		size_t index = 0;
		while (characters[index] && characters[index] != c)
			++index;
		if (!characters[index])
			index = sizeof(glyphs) / sizeof(glyphs[0]) - 1;
		for (unsigned y = 0; y < CELL_HEIGHT; ++y) {
			for (unsigned x = 0; x < CELL_WIDTH; ++x) {
				bool ink = c != ' ' && y < 14 && x < 10
					&& (glyphs[index][y / 2] & (1u << (4 - x / 2)));
				pixel(column * CELL_WIDTH + x, row * CELL_HEIGHT + y,
					ink ? foreground : background);
			}
		}
		++column;
	}
}
