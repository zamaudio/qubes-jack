/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (C) 2017  Damien Zammit <damien@zamaudio.com>
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


#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/select.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h> // ceilf()

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "qubes-vchan-jack.h"
#include <libvchan.h>

#include <jack/jack.h>
#include <jack/statistics.h>

#define MAX_CH 2
#define MAX_JACK_BUFFER 8192

struct userdata {
	unsigned int jack_sample_rate;
	unsigned int jack_buffer_size;
	unsigned int jack_xruns;
	unsigned int jack_latency;
	unsigned int bytes_per_frame;

	jack_client_t *jack_client;
	jack_port_t *input_ports[MAX_CH];
	jack_port_t *output_ports[MAX_CH];

	libvchan_t *control;
	libvchan_t *play;
	libvchan_t *rec;

	char *tmpbuffer;
	unsigned int play_count;
	unsigned int record_count;
	bool ports_ready;
	bool pause;
};

static void get_jack_rec_port_count(struct userdata *u)
{
	u->record_count = 2;
}

static void get_jack_play_port_count(struct userdata *u)
{
	u->play_count = 2;
}

static void close_jack_ports(struct userdata *u)
{
	unsigned int c;

	for (c = 0; c < u->record_count; c++) {
		if (u->output_ports[c]) {
			jack_port_unregister(u->jack_client, u->output_ports[c]);
			u->output_ports[c] = NULL;
		}
	}

	for (c = 0; c < u->play_count; c++) {
		if (u->input_ports[c]) {
			jack_port_unregister(u->jack_client, u->input_ports[c]);
			u->input_ports[c] = NULL;
		}
	}
}

static void open_jack_ports(struct userdata *u)
{
	unsigned int c;

	for (c = 0; c < u->record_count; c++) {
		char portname[20];
		snprintf(portname, 20, "record_%d", c + 1);
		u->output_ports[c] = jack_port_register(u->jack_client,
					portname,
					JACK_DEFAULT_AUDIO_TYPE,
					JackPortIsOutput, 0);
	}

	for (c = 0; c < u->play_count; c++) {
		char portname[20];
		snprintf(portname, 20, "playback_%d", c + 1);
		u->input_ports[c] = jack_port_register(u->jack_client,
					portname,
					JACK_DEFAULT_AUDIO_TYPE,
					JackPortIsInput, 0);
	}
}

static int qubes_jack_xrun_callback(void *arg)
{
	struct userdata *u = (struct userdata *)arg;

	float delay = jack_get_xrun_delayed_usecs(u->jack_client);
	int fragments = (int)ceilf( ((delay / 1000000.0) * u->jack_sample_rate )
				   / (float)(u->jack_buffer_size) );
	u->jack_xruns += fragments;
	return 0;
}

static int qubes_jack_graph_order_callback(void *arg)
{
	struct userdata *u = (struct userdata *)arg;
	unsigned int i;

	jack_latency_range_t latency_range;
	jack_nframes_t port_latency, max_latency = 0;

	if (!u->jack_client)
		return 0;
	if (!u->ports_ready)
		return 0;

	for (i = 0; i < u->play_count; ++i) {
		jack_port_get_latency_range(u->output_ports[i], JackPlaybackLatency, &latency_range);
		port_latency = latency_range.max;
		if (port_latency > max_latency) {
			max_latency = port_latency;
		}
		/* Cap minimum latency to 16 frames */
		if (max_latency < 16)
			max_latency = 16;
	}

	u->jack_latency = (max_latency * 1000) / jack_get_sample_rate(u->jack_client);
	return 0;
}

static void reconfigure_jack_client(struct userdata *u, int play, int rec)
{
	u->ports_ready = false;

	close_jack_ports(u);

	u->play_count = play;
	u->record_count = rec;

	open_jack_ports(u);

	u->ports_ready = true;
}

static void process_vchan_server_response(struct userdata *u)
{
	uint8_t buf[QUBES_JACK_CONFIG_QUERY_SIZE];
	uint8_t new_play_count = u->play_count;
	uint8_t new_record_count = u->record_count;
	uint32_t new_buffer_size = u->jack_buffer_size;
	uint32_t new_sample_rate = u->jack_sample_rate;
	uint32_t new_xrun_count = u->jack_xruns;

        if (libvchan_data_ready(u->control) >= QUBES_JACK_CONFIG_QUERY_SIZE) {
                // Read config packet if it's waiting
                libvchan_read(u->control, buf, QUBES_JACK_CONFIG_QUERY_SIZE);

                // Parse config packet
                if ((buf[0] == QUBES_JACK_CONFIG_QUERY_START) &&
                                buf[12] == QUBES_JACK_CONFIG_QUERY_END) {
                        new_play_count = buf[1];
                        new_record_count = buf[2];
                        new_buffer_size = (uint32_t)(1 << buf[3]);
                        new_sample_rate = read_nth_u32(buf, 1);
			new_xrun_count = read_nth_u32(buf, 2); 
		}

		// Check if jack config changed
		if ((new_play_count != u->play_count) ||
				(new_record_count != u->record_count) ||
				(new_buffer_size != u->jack_buffer_size)) {
			reconfigure_jack_client(u, new_play_count, new_record_count);
		}
		// FIXME: Handle jack server changing its sample rate
		u->jack_xruns = new_xrun_count;
		new_sample_rate = new_sample_rate;
	}
}

static int qubes_jack_process(jack_nframes_t nframes, void *arg)
{
	struct userdata *u = (struct userdata *)arg;
	int t_jack_xruns = u->jack_xruns;
	int k;
	unsigned int i;
	unsigned int c;
	long f;
	long j;

        int rec_ready = libvchan_is_open(u->rec);
        int play_ready = libvchan_is_open(u->play);

	//fprintf(stderr, "Process...");
        if (rec_ready == 1 && play_ready == 1) {
                u->pause = false;
        } else if (rec_ready != 1 || play_ready != 1) {
                u->pause = true;
        }
	float *bufs_out[u->record_count];
	float *bufs_in[u->play_count];

	// handle xruns by skipping audio that should have been played
	for (k = 0; k < t_jack_xruns; k++) {
	    //u->position += u->fragment_size;
	}
	u->jack_xruns -= t_jack_xruns;

	if (!u->ports_ready)
		return 0;

	process_vchan_server_response(u);

	// get jack output buffers
	for (i = 0; i < u->play_count; i++)
		bufs_in[i] = (float*)jack_port_get_buffer(u->input_ports[i], nframes);

	// get jack input buffers
	for (i = 0; i < u->record_count; i++)
		bufs_out[i] = (float*)jack_port_get_buffer(u->output_ports[i], nframes);

	if (u->pause) {
		// paused, play silence on output
		for (c = 0; c < u->record_count; c++) {
			float *buffer_out = bufs_out[c];
			for (f = 0; f < nframes; f++) {
				buffer_out[f] = 0.f;
			}
		}
		// paused, capture silence
		for (c = 0; c < u->play_count; c++) {
			float *buffer_in = bufs_in[c];
			for (f = 0; f < nframes; f++) {
				buffer_in[f] = 0.f;
			}
		}
	} else {
		// unpaused, record audio

		j = u->record_count * nframes * sizeof(float);
		if (libvchan_data_ready(u->rec) >= j) {
			//fprintf(stderr, "Rec...");
			// read a jack sized block from vchan record buffer
			libvchan_read(u->rec, u->tmpbuffer, j);

			// write jack sized block to jack (rec)
			for (c = 0; c < u->record_count; c++) {
				float *buffer_out = bufs_out[c];
				for (f = 0; f < nframes; f++) {
					// read interleaved buffer
					buffer_out[f] = read_nth_float(u->tmpbuffer, c + f * u->record_count);
				}
			}
			//fprintf(stderr, "Rec done...");
		} else {
			// capture silence
			//fprintf(stderr, "Silence...");
			for (c = 0; c < u->record_count; c++) {
				float *buffer_out = bufs_out[c];
				for (f = 0; f < nframes; f++) {
					// read interleaved buffer
					buffer_out[f] = 0.f;
				}
			}
		}
		// unpaused, play audio

		// capture jack sized buffer and write interleaved floats to tmpbuffer
		for (c = 0; c < u->play_count; c++) {
			float *buffer_in = bufs_in[c];
			for (f = 0; f < nframes; f++) {
				// write interleaved buffer
				write_nth_float(u->tmpbuffer, c + f * u->play_count, buffer_in[f]);
			}
		}
		// commit tmpbuffer to vchan
		//fprintf(stderr, "Play0...");
		j = u->play_count * nframes * sizeof(float);
		if (libvchan_buffer_space(u->play) >= j) {
			//fprintf(stderr, "Play1...");
			libvchan_write(u->play, u->tmpbuffer, j);
		//} else {
			//fprintf(stderr, "Silence1...");
		}
	}
	return 0;
}

static void qubes_jack_destroy(struct userdata *u)
{
	if (u->jack_client != NULL)
  		jack_client_close(u->jack_client);

	if (u->tmpbuffer)
		free(u->tmpbuffer);
}

static int qubes_jack_init(struct userdata *u)
{
	u->tmpbuffer = (char *)malloc(sizeof(float) * MAX_CH * MAX_JACK_BUFFER);
	if (!u->tmpbuffer) {
		qubes_jack_destroy(u);
		return -1;
	}

	const char *jack_client_name = "qubes-vchan-client";
	u->jack_client = jack_client_open(jack_client_name, JackNoStartServer, NULL);

	if (!u->jack_client) {
		qubes_jack_destroy(u);
		return -1;
	}

	u->jack_xruns = 0;

	jack_set_process_callback (u->jack_client, qubes_jack_process, u);
	jack_set_xrun_callback (u->jack_client, qubes_jack_xrun_callback, u);
	jack_set_graph_order_callback (u->jack_client, qubes_jack_graph_order_callback, u);

	if (jack_activate (u->jack_client)) {
		qubes_jack_destroy(u);
		return -1; 
	}

	u->jack_sample_rate = jack_get_sample_rate(u->jack_client);
	u->jack_latency = 16 * 1000 / u->jack_sample_rate;

	return 0;
}

static int vchan_conn(struct userdata *u, int domid)
{
	u->play = libvchan_client_init(domid, QUBES_JACK_PLAYBACK_VCHAN_PORT);
	if (!u->play) {
		fprintf(stderr, "libvchan_client_init play failed\n");
		return -1;
	}
	u->rec = libvchan_client_init(domid, QUBES_JACK_RECORD_VCHAN_PORT);
	if (!u->rec) {
		fprintf(stderr, "libvchan_client_init rec failed\n");
		return -1;
	}
	u->control = libvchan_client_init(domid, QUBES_JACK_CONFIG_VCHAN_PORT);
	if (!u->control) {
		fprintf(stderr, "libvchan_client_init control failed\n");
		return -1;
	}
	return 0;
}

void vchan_done(struct userdata *u)
{
	if (u->play)
		libvchan_close(u->play);

	if (u->rec)
		libvchan_close(u->rec);
}

int main(int argc, char **argv)
{
	struct userdata u;

	u.pause = true;
	u.ports_ready = false;

	if (argc < 2) {
		fprintf(stderr, "Error: need domid, exiting\n");
		return 1;
	}

	fprintf(stderr, "Open Vchan...");
	if (vchan_conn(&u, atoi(argv[1])))
		return 1;
	fprintf(stderr, "done\n");

	fprintf(stderr, "Open JACK...");
	if (qubes_jack_init(&u))
		return 1;
	fprintf(stderr, "done\n");

	fprintf(stderr, "Get config...");
	get_jack_play_port_count(&u);
	get_jack_rec_port_count(&u);
	fprintf(stderr, "done\n");

	fprintf(stderr, "Open JACK ports...");
	open_jack_ports(&u);
	fprintf(stderr, "done\n");

	u.ports_ready = true;
	u.pause = false;

	// Wait until killed
	sleep(-1);

	// shutdown
	u.pause = true;
	u.ports_ready = false;

	close_jack_ports(&u);

	qubes_jack_destroy(&u);
	vchan_done(&u);
	return 0;
}
