/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (C) 2017  Damien Zammit <damien@zamaudio.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#define QUBES_JACK_CONFIG_VCHAN_PORT 4715
#define QUBES_JACK_PLAYBACK_VCHAN_PORT 4716
#define QUBES_JACK_RECORD_VCHAN_PORT 4717

// Query byte:
#define QUBES_JACK_CONFIG_QUERY_CMD 0xEE
// Response packet size:
#define QUBES_JACK_CONFIG_QUERY_SIZE 13
// Response packet:
#define QUBES_JACK_CONFIG_QUERY_START 0xFF
// uint8_t play_count (channel count)
// uint8_t record_count (channel count)
// uint8_t server_buffer_size (power of 2)
// uint32_t server_sample_rate (44100 etc)
// uint32_t server_xruns (xrun count)
#define QUBES_JACK_CONFIG_QUERY_END 0xFE
// End packet

#define MAX_CH 8
#define MAX_JACK_BUFFER 8192

static int __attribute__((unused)) read_nth_u32(void *buf, long n)
{
	uint8_t *base = (uint8_t *)buf;
	base += sizeof(uint32_t) * n;

	return 	(uint32_t)(base[0] << 24) |
		(uint32_t)(base[1] << 16) |
	 	(uint32_t)(base[2] <<  8) |
		(uint32_t)(base[3] <<  0);
}

static float __attribute__((unused)) read_nth_float(void *buf, long n)
{
	uint8_t *base = (uint8_t *)buf;
	union dual {
		uint32_t u;
		float f;
	};
	union dual v;
	base += sizeof(float) * n;

	v.u =	(uint32_t)(base[0] << 24) |
		(uint32_t)(base[1] << 16) |
	 	(uint32_t)(base[2] <<  8) |
		(uint32_t)(base[3] <<  0);
	return v.f;
}

static void __attribute__((unused)) write_nth_u32(void *buf, long n, uint32_t value)
{
	uint8_t *base = (uint8_t *)buf;
	base += sizeof(uint32_t) * n;

	base[0] = (value >> 24) & 0xff;
	base[1] = (value >> 16) & 0xff;
	base[2] = (value >>  8) & 0xff;
	base[3] = (value >>  0) & 0xff;
}

static void __attribute__((unused)) write_nth_float(void *buf, long n, float value)
{
	uint8_t *base = (uint8_t *)buf;
	union dual {
		uint32_t u;
		float f;
	};
	union dual v;
	v.f = value;
	base += sizeof(float) * n;

	base[0] = (v.u >> 24) & 0xff;
	base[1] = (v.u >> 16) & 0xff;
	base[2] = (v.u >>  8) & 0xff;
	base[3] = (v.u >>  0) & 0xff;
}

static uint8_t __attribute__((unused)) log2_(uint32_t value)
{
	uint32_t power = sizeof(value) * 8 - 1;
	uint32_t i = 1 << power;

	if (!value)
		return 0;

	for (; i > value; i >>= 1, power--);

	return power;
}
