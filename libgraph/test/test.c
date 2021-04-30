/*
 * Phoenix-RTOS
 *
 * Graph library test
 *
 * Copyright 2021 Phoenix Systems
 * Copyright 2002-2007 IMMOS
 * Author: Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/time.h>

#include <libgraph.h>

#include "cursor.h"
#include "font.h"
#include "logo16.h"
#include "logo32.h"


/* Triggers scheduled tasks execution */
static int test_trigger(graph_t *graph)
{
	int err;

	while ((err = graph_trigger(graph)) && (err != -EAGAIN));
	return graph_commit(graph);
}


/* Tiggers scheduled tasks execution until VSYNC event */
static int test_vtrigger(graph_t *graph)
{
	while (graph_trigger(graph), !graph_vsync(graph));
	return graph_commit(graph);
}


int test_lines1(graph_t *graph, unsigned int dx, unsigned int dy, int step)
{
	unsigned int i;
	int err;

	/* Slow lines */
	for (i = 0; i < 500; i++) {
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_line(graph, rand() % (graph->width - dx - 2 * step) + step, rand() % (graph->height - dx - 2 * step) + step, rand() % dx, rand() % dy, 1, rand() % (1ULL << 8 * graph->depth), GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	/* Fast lines */
	for (i = 0; i < 100000; i++) {
		if ((err = test_trigger(graph)) < 0)
			return err;
		if ((err = graph_line(graph, rand() % (graph->width - 2 * dx - 2 * step) + step + dx, rand() % (graph->height - 2 * dy - 2 * step) + step + dy, rand() % (2 * dx) - dx, rand() % (2 * dy) - dy, 1, rand() % (1ULL << 8 * graph->depth), GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	/* Move up */
	for (i = 0; i < graph->height; i += step) {
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_move(graph, 0, step, graph->width, graph->height - step, 0, -step, GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	return EOK;
}


int test_lines2(graph_t *graph, unsigned int dx, unsigned int dy, int step)
{
	unsigned int i;
	int err;

	/* Background rect */
	if ((err = graph_rect(graph, 100, 100, graph->width - 199, graph->height - 199, rand() % (1ULL << 8 * graph->depth), GRAPH_QUEUE_HIGH)) < 0)
		return err;

	/* Slow lines */
	for (i = 0; i < graph->height - 199; i += step) {
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_line(graph, 100, 100 + i, graph->width - 200, graph->height - 200 - i * step, 1, rand() % (1ULL << 8 * graph->depth), GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	for (i = 0; i < graph->width - 199; i += step) {
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_line(graph, 100 + i, graph->height - 100, graph->width - 200 - i * step, 200 - graph->height, 1, rand() % (1ULL << 8 * graph->depth), GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	/* Move up */
	for (i = 0; i < graph->height; i += step) {
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_move(graph, 0, step, graph->width, graph->height - step, 0, -step, GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	return EOK;
}


int test_rectangles(graph_t *graph, unsigned int dx, unsigned int dy, int step)
{
	unsigned int i;
	int err;

	/* Slow rectangles */
	for (i = 0; i < 300; i++) {
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_rect(graph, rand() % (graph->width - dx - 2 * step) + step, rand() % (graph->height - dy - 2 * step) + step, dx, dy, rand() % (1ULL << 8 * graph->depth), GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	/* Fast rectangles */
	for (i = 0; i < 10000; i++) {
		if ((err = test_trigger(graph)) < 0)
			return err;
		if ((err = graph_rect(graph, rand() % (graph->width - dx - 2 * step) + step, rand() % (graph->height - dy - 2 * step) + step, dx, dy, rand() % (1ULL << 8 * graph->depth), GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	/* Move right */
	for (i = 0; i < graph->width; i += step) {
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_move(graph, 0, 0, graph->width - step, graph->height, step, 0, GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	return EOK;
}


int test_logo(graph_t *graph, int step)
{
	static const char text[] = "Phoenix-RTOS";                      /* Text under logo */
	static const unsigned int fx = (sizeof(text) - 1) * font.width; /* Text width */
	static const unsigned int fy = font.height;                     /* Text height */
	static const unsigned int lx = 200;                             /* Logo width */
	static const unsigned int ly = 150;                             /* Logo height */
	static const unsigned int dy = ly + (3 * fy) / 2;               /* Total height */
	const unsigned char *logo;
	unsigned int i, x, y, bg;
	int err, sy, ay;

	switch (graph->depth) {
	case 2:
		logo = logo16[0];
		bg = *(uint16_t *)logo;
		break;

	case 4:
		logo = logo32[0];
		bg = *(uint32_t *)logo;
		break;

	default:
		printf("test_libgraph: logo test not supported for selected graphics mode. Skipping...\n");
		return EOK;
	}

	x = graph->width - lx - 2 * step;
	y = graph->height - dy - 2 * step;

	/* Compose logo at bottom left corner */
	if ((err = graph_rect(graph, 0, 0, graph->width, graph->height, bg, GRAPH_QUEUE_HIGH)) < 0)
		return err;
	if ((err = graph_copy(graph, logo, (void *)((uintptr_t)graph->data + graph->depth * ((graph->height - dy - step) * graph->width + step)), lx, ly, graph->depth * lx, graph->depth * graph->width, GRAPH_QUEUE_HIGH)) < 0)
		return err;
	if ((err = graph_print(graph, &font, text, step + (lx - fx) / 2 + 1, graph->height - fy - step, font.height, font.height, 0xffffffff, GRAPH_QUEUE_HIGH)) < 0)
		return err;

	/* Move right */
	for (i = 0; i < x; i += step) {
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_move(graph, 0, graph->height - dy - step, graph->width - step, dy, step, 0, GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	/* Move diagonal */
	for (i = 0, ay = 0; i < x; i += step, ay += sy) {
		sy = i * y / x;
		sy = (ay < sy) ? sy - ay : 0;
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_move(graph, step, step, graph->width - step, graph->height - step, -step, -sy, GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	/* Move right */
	for (i = 0; i < x; i += step) {
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_move(graph, 0, 0, graph->width - step, dy, step, 0, GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	/* Move diagonal to center */
	for (i = 0, ay = 0, x >>= 1, y >>= 1; i < x; i += step, ay += sy) {
		sy = i * y / x;
		sy = (ay < sy) ? sy - ay : 0;
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_move(graph, step, 0, graph->width - step, graph->height - step, -step, sy, GRAPH_QUEUE_HIGH)) < 0)
			return err;
	}

	return EOK;
}


int test_cursor(graph_t *graph)
{
	unsigned int i;
	int err;

	if ((err = graph_cursorset(graph, cand[0], cxor[0], 0xff000000, 0xffffffff)) < 0)
		return err;

	if ((err = graph_cursorshow(graph)) < 0)
		return err;

	for (i = 0; i < graph->height; i++) {
		if ((err = test_vtrigger(graph)) < 0)
			return err;
		if ((err = graph_cursorpos(graph, i * graph->width / graph->height, i)) < 0)
			return err;
	}

	if ((err = graph_cursorhide(graph)) < 0)
		return err;

	return EOK;
}


int main(void)
{
	graph_t graph;
	int ret;

	if ((ret = graph_init()) < 0) {
		fprintf(stderr, "test_libgraph: failed to initialize library\n");
		return ret;
	}

	if ((ret = graph_open(&graph, 0x2000, GRAPH_ANY)) < 0) {
		fprintf(stderr, "test_libgraph: failed to initialize graphics adapter\n");
		graph_done();
		return ret;
	}

	do {
		if ((ret = graph_mode(&graph, GRAPH_DEFMODE, GRAPH_DEFFREQ)) < 0) {
			fprintf(stderr, "test_libgraph: failed to set default graphics mode\n");
			break;
		}

		printf("test_libgraph: starting test in %ux%ux%u graphics mode\n", graph.width, graph.height, graph.depth << 3);
		srand(time(NULL));

		printf("test_libgraph: starting lines1 test...\n");
		if ((ret = test_lines1(&graph, 100, 100, 2)) < 0) {
			fprintf(stderr, "test_libgraph: lines1 test failed\n");
			break;
		}

		printf("test_libgraph: starting lines2 test...\n");
		if ((ret = test_lines2(&graph, 100, 100, 2)) < 0) {
			fprintf(stderr, "test_libgraph: lines2 test failed\n");
			break;
		}

		printf("test_libgraph: starting rectangles test...\n");
		if ((ret = test_rectangles(&graph, 100, 100, 2)) < 0) {
			fprintf(stderr, "test_libgraph: rectangles test failed\n");
			break;
		}

		printf("test_libgraph: starting logo test...\n");
		if ((ret = test_logo(&graph, 2)) < 0) {
			fprintf(stderr, "test_libgraph: logo test failed\n");
			break;
		}

		printf("test_libgraph: starting cursor test...\n");
		if ((ret = test_cursor(&graph)) < 0) {
			fprintf(stderr, "test_libgraph: cursor test failed\n");
			break;
		}
	} while (0);

	graph_close(&graph);
	graph_done();

	if (!ret)
		printf("test_libgraph: test finished successfully\n");

	return ret;
}
