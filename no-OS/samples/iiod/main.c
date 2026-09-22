/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <no_os_util.h>

#include <iio/iio.h>
#include <iio/iio-backend.h>

#include "iio_adc.h"
#include "iio_device.h"

#define GAME_W			22
#define GAME_H			10
#define GAME_CELLS		(GAME_W * GAME_H)
#define FRAME_MIN_LEN		((GAME_W + 1) * GAME_H + 1)

#define GLYPH_HEAD		'@'
#define GLYPH_BODY		'o'
#define GLYPH_FOOD		'*'
#define GLYPH_VOID		'.'

enum snake_dir {
	SNAKE_UP,
	SNAKE_DOWN,
	SNAKE_LEFT,
	SNAKE_RIGHT,
};

struct snake_game {
	uint16_t body[GAME_CELLS];
	uint8_t taken[GAME_CELLS];
	uint16_t head;
	uint16_t tail;
	uint16_t len;
	uint16_t food;
	enum snake_dir dir;
	enum snake_dir next_dir;
	unsigned int score;
	bool alive;
};

static const char *const snake_dirs[] = {
	[SNAKE_UP] = "up",
	[SNAKE_DOWN] = "down",
	[SNAKE_LEFT] = "left",
	[SNAKE_RIGHT] = "right",
};

static struct snake_game game;
static unsigned int high_score;
static uint32_t rng_state = 0x2545f491;

static uint32_t rng_next(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;

	return rng_state;
}

static void snake_spawn_food(void)
{
	uint16_t start = (uint16_t)(rng_next() % GAME_CELLS);
	uint16_t i;

	if (game.len >= GAME_CELLS)
		return;

	for (i = 0; i < GAME_CELLS; i++) {
		uint16_t cell = (uint16_t)((start + i) % GAME_CELLS);

		if (game.taken[cell])
			continue;

		game.food = cell;

		return;
	}
}

static void snake_reset(void)
{
	uint16_t start = (uint16_t)((GAME_H / 2) * GAME_W + GAME_W / 4);

	rng_state ^= game.score * 2654435761u + 0x9e3779b9u;
	if (!rng_state)
		rng_state = 0x2545f491;

	memset(game.taken, 0, sizeof(game.taken));
	game.head = 0;
	game.tail = 0;
	game.len = 1;
	game.body[0] = start;
	game.taken[start] = 1;
	game.dir = SNAKE_RIGHT;
	game.next_dir = SNAKE_RIGHT;
	game.score = 0;
	game.alive = true;

	snake_spawn_food();
}

static void snake_die(void)
{
	game.alive = false;

	if (game.score > high_score)
		high_score = game.score;
}

static void snake_turn(enum snake_dir dir)
{
	static const enum snake_dir opposite[] = {
		[SNAKE_UP] = SNAKE_DOWN,
		[SNAKE_DOWN] = SNAKE_UP,
		[SNAKE_LEFT] = SNAKE_RIGHT,
		[SNAKE_RIGHT] = SNAKE_LEFT,
	};

	if (game.len > 1 && dir == opposite[game.dir])
		return;

	game.next_dir = dir;
}

static void snake_step(void)
{
	uint16_t prev = game.body[game.head];
	int x = prev % GAME_W;
	int y = prev / GAME_W;
	bool grew = false;
	uint16_t next;

	if (!game.alive)
		return;

	game.dir = game.next_dir;

	switch (game.dir) {
	case SNAKE_UP:
		y--;
		break;
	case SNAKE_DOWN:
		y++;
		break;
	case SNAKE_LEFT:
		x--;
		break;
	case SNAKE_RIGHT:
		x++;
		break;
	}

	if (x < 0 || x >= GAME_W || y < 0 || y >= GAME_H) {
		snake_die();
		return;
	}

	next = (uint16_t)(y * GAME_W + x);

	if (next == game.food) {
		grew = true;
		game.score++;
	}

	if (!grew) {
		uint16_t gone = game.body[game.tail];

		game.taken[gone] = 0;
		game.tail = (uint16_t)((game.tail + 1) % GAME_CELLS);
		game.len--;
	}

	if (game.taken[next]) {
		snake_die();
		return;
	}

	game.head = (uint16_t)((game.head + 1) % GAME_CELLS);
	game.body[game.head] = next;
	game.taken[next] = 1;
	game.len++;

	if (grew)
		snake_spawn_food();
}

static int snake_render(char *dst, size_t len)
{
	uint16_t head = game.body[game.head];
	size_t pos = 0;
	unsigned int x, y;

	if (len < FRAME_MIN_LEN)
		return -ENOMEM;

	for (y = 0; y < GAME_H; y++) {
		for (x = 0; x < GAME_W; x++) {
			uint16_t cell = (uint16_t)(y * GAME_W + x);

			if (cell == head)
				dst[pos++] = GLYPH_HEAD;
			else if (game.taken[cell])
				dst[pos++] = GLYPH_BODY;
			else if (cell == game.food)
				dst[pos++] = GLYPH_FOOD;
			else
				dst[pos++] = GLYPH_VOID;
		}

		dst[pos++] = '\n';
	}

	dst[pos] = '\0';

	return (int)pos + 1;
}

static int snake_emit_str(char *dst, size_t len, const char *val)
{
	int ret = snprintf(dst, len, "%s", val);

	if (ret < 0 || (size_t)ret >= len)
		return -EINVAL;

	return ret + 1;
}

static int snake_emit_uint(char *dst, size_t len, unsigned int val)
{
	int ret = snprintf(dst, len, "%u", val);

	if (ret < 0 || (size_t)ret >= len)
		return -EINVAL;

	return ret + 1;
}

static int snake_lookup_dir(const char *src, size_t len)
{
	unsigned int i;

	while (len && (src[len - 1] == '\0' || src[len - 1] == '\n' ||
		       src[len - 1] == '\r'))
		len--;

	if (!len)
		return -EINVAL;

	for (i = 0; i < NO_OS_ARRAY_SIZE(snake_dirs); i++) {
		size_t n = strlen(snake_dirs[i]);

		if (len == n && !strncmp(src, snake_dirs[i], n))
			return (int)i;
	}

	if (len != 1)
		return -EINVAL;

	switch (src[0]) {
	case 'w':
	case 'W':
		return SNAKE_UP;
	case 's':
	case 'S':
		return SNAKE_DOWN;
	case 'a':
	case 'A':
		return SNAKE_LEFT;
	case 'd':
	case 'D':
		return SNAKE_RIGHT;
	default:
		return -EINVAL;
	}
}

static int snake_add_attrs(void *dev, struct iio_device *iio_dev)
{
	static const char *const attrs[] = {
		"frame",
		"direction",
		"score",
		"high_score",
		"state",
		"reset",
	};
	unsigned int i;

	for (i = 0; i < NO_OS_ARRAY_SIZE(attrs); i++)
		iio_device_add_attr(iio_dev, attrs[i], IIO_ATTR_TYPE_DEVICE);

	return 0;
}

static int snake_read_attr(void *dev, const struct iio_device *iio_dev,
			   const struct iio_attr *attr, char *dst, size_t len)
{
	const char *name = iio_attr_get_name(attr);

	if (!name || attr->type != IIO_ATTR_TYPE_DEVICE)
		return -EINVAL;

	if (!strcmp(name, "frame")) {
		snake_step();

		return snake_render(dst, len);
	}

	if (!strcmp(name, "direction"))
		return snake_emit_str(dst, len, snake_dirs[game.dir]);

	if (!strcmp(name, "score"))
		return snake_emit_uint(dst, len, game.score);

	if (!strcmp(name, "high_score"))
		return snake_emit_uint(dst, len, high_score);

	if (!strcmp(name, "state"))
		return snake_emit_str(dst, len,
				      game.alive ? "running" : "over");

	if (!strcmp(name, "reset"))
		return snake_emit_uint(dst, len, 0);

	return -EINVAL;
}

static int snake_write_attr(void *dev, const struct iio_device *iio_dev,
			    const struct iio_attr *attr, const char *src,
			    size_t len)
{
	const char *name = iio_attr_get_name(attr);
	int dir;

	if (!name || attr->type != IIO_ATTR_TYPE_DEVICE)
		return -EINVAL;

	if (!strcmp(name, "direction")) {
		dir = snake_lookup_dir(src, len);
		if (dir < 0)
			return dir;

		snake_turn((enum snake_dir)dir);

		return (int)len;
	}

	if (!strcmp(name, "reset")) {
		snake_reset();

		return (int)len;
	}

	return -EPERM;
}

static int snake_get_device_info(struct noos_iio_device_info *info)
{
	if (!info)
		return -EINVAL;

	info->name = "snake";
	info->dev = NULL;
	info->direction = 0;
	info->add_channels = snake_add_attrs;
	info->read_attr = snake_read_attr;
	info->write_attr = snake_write_attr;
	info->read_samples = NULL;
	info->write_samples = NULL;
	info->reg_read = NULL;
	info->reg_write = NULL;

	return 0;
}

static int noos_register_devices(void)
{
	struct noos_iio_device_info info;
	int ret;

	ret = iio_adc_init();
	if (ret)
		return ret;

	ret = iio_adc_get_device_info(&info);
	if (ret)
		return ret;

	ret = noos_iio_register_device(&info);
	if (ret)
		return ret;

	ret = snake_get_device_info(&info);
	if (ret)
		return ret;

	return noos_iio_register_device(&info);
}

int main(void)
{
	int ret;

	snake_reset();

	ret = noos_register_devices();
	if (ret)
		return ret;

	return noos_iiod_run();
}
