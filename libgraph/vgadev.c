/*
 * Phoenix-RTOS
 *
 * Generic VGA device driver based on XFree86 implementation
 *
 * Copyright 2021 Phoenix Systems
 * Author: Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 *
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * Copyright 1991-1999 by The XFree86 Project, Inc.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions, and the following disclaimer.
 *
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution, and in the same place and form as other copyright,
 *     license and disclaimer information.
 *
 * 3.  The end-user documentation included with the redistribution,
 *     if any, must include the following acknowledgment: "This product
 *     includes software developed by The XFree86 Project, Inc
 *     (http://www.xfree86.org/) and its contributors", in the same
 *     place and form as other third-party acknowledgments.  Alternately,
 *     this acknowledgment may appear in the software itself, in the
 *     same form and location as other such third-party acknowledgments.
 *
 * 4.  Except as contained in this notice, the name of The XFree86
 *     Project, Inc shall not be used in advertising or otherwise to
 *     promote the sale, use or other dealings in this Software without
 *     prior written authorization from The XFree86 Project, Inc.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE XFREE86 PROJECT, INC OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libvga.h>

#include "libgraph.h"


/* Default graphics mode index */
#define DEFMODE 5                    /* 320x200x8 @ 70Hz */


typedef struct {
	graph_mode_t mode;               /* Graphics mode */
	unsigned char depth;             /* Color depth */
	union {
		/* Power management mode */
		struct {
			unsigned char sr01;      /* DPMS sr01 register configuration */
			unsigned char cr17;      /* DPMS gr0e register configuration */
		} pwm;
		/* Graphics mode */
		struct {
			graph_freq_t freq;       /* Screen refresh rate */
			vga_mode_t cfg;          /* Mode configuration */
		} gfx;
	};
} vgadev_mode_t;


typedef struct {
	vga_t vga;                       /* VGA data */
	vga_state_t state;               /* Saved video state */
	unsigned char cmap[VGA_CMAPSZ];  /* Saved color map */
	unsigned char text[VGA_TEXTSZ];  /* Saved text */
	unsigned char font1[VGA_FONTSZ]; /* Saved font1 */
	unsigned char font2[VGA_FONTSZ]; /* Saved font2 */
} vgadev_t;


/* Graphics modes table */
static const vgadev_mode_t modes[] = {
	/* Control modes */
	{ GRAPH_ON,        0, .pwm = { 0x00, 0x80 } }, /* 0, Screen: on,  HSync: on,  VSync: on */
	{ GRAPH_OFF,       0, .pwm = { 0x20, 0x00 } }, /* 1, Screen: off, HSync: off, VSync: off */
	{ GRAPH_STANDBY,   0, .pwm = { 0x20, 0x80 } }, /* 2, Screen: off, HSync: off, VSync: on */
	{ GRAPH_SUSPEND,   0, .pwm = { 0x20, 0x80 } }, /* 4, Screen: off, HSync: on,  VSync: off */
	/* 8-bit color palette */
	{ GRAPH_320x200x8, 1, .gfx = { GRAPH_70Hz, .cfg = { 25175, 320, 336, 384, 400, 0, 200, 206, 207, 224, 2, VGA_VSYNCP | VGA_CLKDIV } } }, /* 5 */
	/* No mode */
	{ 0 }
};


/* Schedules and executes new task */
extern int graph_schedule(graph_t *graph);


int vgadev_cursorpos(graph_t *graph, unsigned int x, unsigned int y)
{
	return -ENOTSUP;
}


int vgadev_cursorset(graph_t *graph, const unsigned char *and, const unsigned char *xor, unsigned int bg, unsigned int fg)
{
	return -ENOTSUP;
}


int vgadev_cursorhide(graph_t *graph)
{
	return -ENOTSUP;
}


int vgadev_cursorshow(graph_t *graph)
{
	return -ENOTSUP;
}


int vgadev_colorset(graph_t *graph, const unsigned char *colors, unsigned int first, unsigned int last)
{
	return EOK;
}


int vgadev_colorget(graph_t *graph, unsigned char *colors, unsigned int first, unsigned int last)
{
	return EOK;
}


int vgadev_isbusy(graph_t *graph)
{
	return 0;
}


int vgadev_commit(graph_t *graph)
{
	return EOK;
}


int vgadev_trigger(graph_t *graph)
{
	if (vgadev_isbusy(graph))
		return -EBUSY;

	return graph_schedule(graph);
}


int vgadev_vsync(graph_t *graph)
{
	return 1;
}


int vgadev_mode(graph_t *graph, graph_mode_t mode, graph_freq_t freq)
{
	unsigned int i = DEFMODE;
	vgadev_t *vgadev = (vgadev_t *)graph->adapter;
	vga_t *vga = &vgadev->vga;
	vga_state_t state;
	vga_mode_t cfg;

	if (mode != GRAPH_DEFMODE) {
		for (i = 0; (modes[i].mode != mode) || (modes[i].depth && (freq != GRAPH_DEFFREQ) && (modes[i].gfx.freq != freq)); i++)
			if (!modes[i].mode)
				return -ENOTSUP;
	}

	/* Power management mode (DPMS) */
	if (!modes[i].depth) {
		vga_writeseq(vga, 0x00, 0x01);
		vga_writeseq(vga, 0x01, (vga_readseq(vga, 0x01) & ~0x20) | modes[i].pwm.sr01);
		vga_writecrtc(vga, 0x17, (vga_readcrtc(vga, 0x17) & ~0x80) | modes[i].pwm.cr17);
		vga_writeseq(vga, 0x00, 0x03);
		return EOK;
	}
	cfg = modes[i].gfx.cfg;

	/* Initialize VGA state */
	vga_mode(0, &cfg, &state);
	state.cmap = NULL;
	state.text = NULL;
	state.font1 = NULL;
	state.font2 = NULL;

	/* Program mode */
	vga_mlock(vga);
	vga_restoremode(vga, &state);
	vga_munlock(vga);

	/* Update graph data and clear screen */
	graph->depth = modes[i].depth;
	graph->width = modes[i].gfx.cfg.hres;
	graph->height = modes[i].gfx.cfg.vres;
	memset(vga->mem, 0, graph->width * graph->height * graph->depth);

	return EOK;
}


void vgadev_close(graph_t *graph)
{
	vgadev_t *vgadev = (vgadev_t *)graph->adapter;
	vga_t *vga = &vgadev->vga;

	/* Restore original video state */
	vga_mlock(vga);
	vga_restore(vga, &vgadev->state);
	vga_munlock(vga);

	/* Lock VGA registers and destroy device */
	vga_lock(&vgadev->vga);
	vga_done(&vgadev->vga);
	free(vgadev);
}


int vgadev_open(graph_t *graph)
{
	vgadev_t *vgadev;
	vga_t *vga;
	int err;

	if ((vgadev = malloc(sizeof(vgadev_t))) == NULL)
		return -ENOMEM;
	vga = &vgadev->vga;

	if ((err = vga_init(&vgadev->vga)) < 0) {
		free(vgadev);
		return err;
	}

	/* Check color support */
	if (!(vga_readmisc(vga) & 0x01)) {
		vga_done(&vgadev->vga);
		free(vgadev);
		return -ENOTSUP;
	}

	/* Unlock VGA registers and save current video state */
	vga_unlock(&vgadev->vga);
	vgadev->state.cmap = vgadev->cmap;
	vgadev->state.font1 = vgadev->font1;
	vgadev->state.font2 = vgadev->font2;
	vgadev->state.text = vgadev->text;
	vga_save(vga, &vgadev->state);

	/* Initialize graph info */
	graph->adapter = vgadev;
	graph->data = vga->mem;
	graph->width = 0;
	graph->height = 0;
	graph->depth = 0;

	/* Set graph functions */
	graph->close = vgadev_close;
	graph->mode = vgadev_mode;
	graph->vsync = vgadev_vsync;
	graph->isbusy = vgadev_isbusy;
	graph->trigger = vgadev_trigger;
	graph->commit = vgadev_commit;
	graph->colorset = vgadev_colorset;
	graph->colorget = vgadev_colorget;
	graph->cursorset = vgadev_cursorset;
	graph->cursorpos = vgadev_cursorpos;
	graph->cursorshow = vgadev_cursorshow;
	graph->cursorhide = vgadev_cursorhide;

	return EOK;
}


void vgadev_done(void)
{
	return;
}


int vgadev_init(void)
{
	return EOK;
}
