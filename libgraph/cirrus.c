/*
 * Phoenix-RTOS
 *
 * Cirrus Logic GD5446 VGA driver
 *
 * Copyright 2021 Phoenix Systems
 * Author: Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/platform.h>

#include <phoenix/arch/ia32.h>

#include <libvga.h>

#include "libgraph.h"


/* Default graphics mode index */
#define DEFMODE  31                  /* 1024x768x32 @ 60Hz */

/* Stable VCLK range (kHz) */
#define MIN_VCLK 28636               /* Double the oscillator = 14.31818 MHz */
#define MAX_VCLK 111000              /* Below max pixel clock = 135 MHz */


typedef struct {
	unsigned int freq;               /* VCLK frequency (kHz) */
	unsigned char num;               /* VCLK numerator */
	unsigned char den;               /* VCLK denominator */
} cirrus_vclk_t;


typedef struct {
	graph_mode_t mode;               /* Graphics mode */
	unsigned char depth;             /* Color depth */
	union {
		/* Power management mode */
		struct {
			unsigned char sr01;      /* DPMS sr01 register configuration */
			unsigned char gr0e;      /* DPMS gr0e register configuration */
		} pwm;
		/* Graphics mode */
		struct {
			graph_freq_t freq;       /* Screen refresh rate */
			const vga_mode_t *cfg;   /* Mode configuration */
		} gfx;
	};
} cirrus_mode_t;


typedef struct {
	vga_state_t state;               /* Base VGA state */
	/* Extended CRT controller registers */
	unsigned char cr1a;              /* CRT controller 0x1a register */
	unsigned char cr1b;              /* CRT controller 0x1b register */
	unsigned char cr1d;              /* CRT controller 0x1d register */
	/* Extended sequencer registers */
	unsigned char sr07;              /* Sequencer 0x07 register */
	unsigned char sr0e;              /* Sequencer 0x0e register */
	unsigned char sr12;              /* Sequencer 0x12 register */
	unsigned char sr13;              /* Sequencer 0x13 register */
	unsigned char sr17;              /* Sequencer 0x17 register */
	unsigned char sr1e;              /* Sequencer 0x1e register */
	unsigned char sr21;              /* Sequencer 0x21 register */
	unsigned char sr2d;              /* Sequencer 0x2d register */
	/* Extended graphics controller registers */
	unsigned char gr17;              /* Graphics controler 0x17 register */
	unsigned char gr18;              /* Graphics controler 0x18 register */
	/* Extended DAC registers */
	unsigned char hdr;               /* Hidden DAC Register */
} cirrus_state_t;


typedef struct {
	vga_t vga;                       /* VGA data */
	void *vmem;                      /* Mapped video memory base address */
	unsigned int vmemsz;             /* Mapped video memory size */
	cirrus_state_t state;            /* Saved video state */
	unsigned char cmap[VGA_CMAPSZ];  /* Saved color map */
	unsigned char text[VGA_TEXTSZ];  /* Saved text */
	unsigned char font1[VGA_FONTSZ]; /* Saved font1 */
	unsigned char font2[VGA_FONTSZ]; /* Saved font2 */
} cirrus_dev_t;


/* Graphics modes configuration table */
static const vga_mode_t cfgs[] = {
	{ 25175,   640,  656,  752,  800, 0,  400,  412,  414,  449, 0, VGA_VSYNCP },                              /* 640x400   @ 70Hz */
	{ 25175,   640,  656,  752,  800, 0,  480,  490,  492,  525, 0, 0 },                                       /* 640x480   @ 60Hz */
	{ 31500,   640,  664,  704,  832, 0,  480,  489,  491,  520, 0, 0 },                                       /* 640x480   @ 72Hz */
	{ 31500,   640,  656,  720,  840, 0,  480,  481,  484,  500, 0, 0 },                                       /* 640x480   @ 75Hz */
	{ 36000,   640,  696,  752,  832, 0,  480,  481,  484,  509, 0, 0 },                                       /* 640x480   @ 85Hz */
	{ 40000,   800,  840,  968, 1056, 0,  600,  601,  605,  628, 0, VGA_HSYNCP | VGA_VSYNCP },                 /* 800x600   @ 60Hz */
	{ 36000,   800,  824,  896, 1024, 0,  600,  601,  603,  625, 0, VGA_HSYNCP | VGA_VSYNCP },                 /* 800x600   @ 56Hz */
	{ 50000,   800,  856,  976, 1040, 0,  600,  637,  643,  666, 0, VGA_HSYNCP | VGA_VSYNCP },                 /* 800x600   @ 72Hz */
	{ 49500,   800,  816,  896, 1056, 0,  600,  601,  604,  625, 0, VGA_HSYNCP | VGA_VSYNCP },                 /* 800x600   @ 75Hz */
	{ 56250,   800,  832,  896, 1048, 0,  600,  601,  604,  631, 0, VGA_HSYNCP | VGA_VSYNCP },                 /* 800x600   @ 85Hz */
	{ 65000,  1024, 1048, 1184, 1344, 0,  768,  771,  777,  806, 0, 0 },                                       /* 1024x768  @ 60Hz */
	{ 44900,  1024, 1032, 1208, 1264, 0,  768,  768,  776,  817, 0, VGA_HSYNCP | VGA_VSYNCP | VGA_INTERLACE }, /* 1024x768  @ 43Hz interlaced */
	{ 75000,  1024, 1048, 1184, 1328, 0,  768,  771,  777,  806, 0, 0 },                                       /* 1024x768  @ 70Hz */
	{ 78800,  1024, 1040, 1136, 1312, 0,  768,  769,  772,  800, 0, VGA_HSYNCP | VGA_VSYNCP },                 /* 1024x768  @ 75Hz */
	{ 94500,  1024, 1072, 1168, 1376, 0,  768,  769,  772,  808, 0, VGA_VSYNCP | VGA_VSYNCP },                 /* 1024x768  @ 85Hz */
	{ 108000, 1152, 1216, 1344, 1600, 0,  864,  865,  868,  900, 0, VGA_HSYNCP | VGA_VSYNCP },                 /* 1152x864  @ 75Hz */
	{ 108000, 1280, 1328, 1440, 1688, 0, 1024, 1025, 1028, 1066, 0, VGA_HSYNCP | VGA_VSYNCP },                 /* 1280x1024 @ 60Hz */
	{ 135000, 1280, 1296, 1440, 1688, 0, 1024, 1025, 1028, 1066, 0, VGA_HSYNCP | VGA_VSYNCP },                 /* 1280x1024 @ 75Hz */
	/* No configuration */
	{ 0 }
};


/* Graphics modes table */
static const cirrus_mode_t modes[] = {
	/* Power management modes */
	{ GRAPH_ON,          0, .pwm = { 0x00, 0x00 } },             /*  0, Screen: on,  HSync: on,  VSync: on  */
	{ GRAPH_OFF,         0, .pwm = { 0x20, 0x06 } },             /*  1, Screen: off, HSync: off, VSync: off */
	{ GRAPH_STANDBY,     0, .pwm = { 0x20, 0x02 } },             /*  2, Screen: off, HSync: off, VSync: on  */
	{ GRAPH_SUSPEND,     0, .pwm = { 0x20, 0x04 } },             /*  3, Screen: off, HSync: on,  VSync: off */
	/* 8-bit color palette */
	{ GRAPH_640x400x8,   1, .gfx = { GRAPH_70Hz,  cfgs } },      /*  4 */
	{ GRAPH_640x480x8,   1, .gfx = { GRAPH_60Hz,  cfgs + 1 } },  /*  5 */
	{ GRAPH_640x480x8,   1, .gfx = { GRAPH_72Hz,  cfgs + 2 } },  /*  6 */
	{ GRAPH_640x480x8,   1, .gfx = { GRAPH_75Hz,  cfgs + 3 } },  /*  7 */
	{ GRAPH_640x480x8,   1, .gfx = { GRAPH_85Hz,  cfgs + 4 } },  /*  8 */
	{ GRAPH_800x600x8,   1, .gfx = { GRAPH_60Hz,  cfgs + 5 } },  /*  9 */
	{ GRAPH_800x600x8,   1, .gfx = { GRAPH_56Hz,  cfgs + 6 } },  /* 10 */
	{ GRAPH_800x600x8,   1, .gfx = { GRAPH_72Hz,  cfgs + 7 } },  /* 11 */
	{ GRAPH_800x600x8,   1, .gfx = { GRAPH_75Hz,  cfgs + 8 } },  /* 12 */
	{ GRAPH_800x600x8,   1, .gfx = { GRAPH_85Hz,  cfgs + 9 } },  /* 13 */
	{ GRAPH_1024x768x8,  1, .gfx = { GRAPH_60Hz,  cfgs + 10 } }, /* 14 */
	{ GRAPH_1024x768x8,  1, .gfx = { GRAPH_43Hzi, cfgs + 11 } }, /* 15 */
	{ GRAPH_1024x768x8,  1, .gfx = { GRAPH_70Hz,  cfgs + 12 } }, /* 16 */
	{ GRAPH_1024x768x8,  1, .gfx = { GRAPH_75Hz,  cfgs + 13 } }, /* 17 */
	{ GRAPH_1024x768x8,  1, .gfx = { GRAPH_85Hz,  cfgs + 14 } }, /* 18 */
	{ GRAPH_1152x864x8,  1, .gfx = { GRAPH_75Hz,  cfgs + 15 } }, /* 19 */
	{ GRAPH_1280x1024x8, 1, .gfx = { GRAPH_60Hz,  cfgs + 16 } }, /* 20 */
	{ GRAPH_1280x1024x8, 1, .gfx = { GRAPH_75Hz,  cfgs + 17 } }, /* 21 */
	/* 16-bit color (5:6:5) */
	{ GRAPH_640x480x16,  2, .gfx = { GRAPH_60Hz,  cfgs + 1 } },  /* 22 */
	{ GRAPH_640x480x16,  2, .gfx = { GRAPH_72Hz,  cfgs + 2 } },  /* 23 */
	{ GRAPH_640x480x16,  2, .gfx = { GRAPH_75Hz,  cfgs + 3 } },  /* 24 */
	{ GRAPH_640x480x16,  2, .gfx = { GRAPH_85Hz,  cfgs + 4 } },  /* 25 */
	{ GRAPH_800x600x16,  2, .gfx = { GRAPH_60Hz,  cfgs + 5 } },  /* 26 */
	{ GRAPH_800x600x16,  2, .gfx = { GRAPH_56Hz,  cfgs + 6 } },  /* 27 */
	{ GRAPH_800x600x16,  2, .gfx = { GRAPH_72Hz,  cfgs + 7 } },  /* 28 */
	{ GRAPH_800x600x16,  2, .gfx = { GRAPH_75Hz,  cfgs + 8 } },  /* 29 */
	{ GRAPH_800x600x16,  2, .gfx = { GRAPH_85Hz,  cfgs + 9 } },  /* 30 */
	{ GRAPH_1024x768x16, 2, .gfx = { GRAPH_60Hz,  cfgs + 10 } }, /* 31 */
	{ GRAPH_1024x768x16, 2, .gfx = { GRAPH_43Hzi, cfgs + 11 } }, /* 32 */
	{ GRAPH_1024x768x16, 2, .gfx = { GRAPH_70Hz,  cfgs + 12 } }, /* 33 */
	{ GRAPH_1024x768x16, 2, .gfx = { GRAPH_75Hz,  cfgs + 13 } }, /* 34 */
	{ GRAPH_1024x768x16, 2, .gfx = { GRAPH_85Hz,  cfgs + 14 } }, /* 35 */
	/* 24-bit color (8:8:8) */
	{ GRAPH_640x480x24,  3, .gfx = { GRAPH_60Hz,  cfgs + 1 } },  /* 36 */
	{ GRAPH_640x480x24,  3, .gfx = { GRAPH_72Hz,  cfgs + 2 } },  /* 37 */
	{ GRAPH_640x480x24,  3, .gfx = { GRAPH_75Hz,  cfgs + 3 } },  /* 38 */
	{ GRAPH_640x480x24,  3, .gfx = { GRAPH_85Hz,  cfgs + 4 } },  /* 39 */
	{ GRAPH_800x600x24,  3, .gfx = { GRAPH_60Hz,  cfgs + 5 } },  /* 40 */
	{ GRAPH_800x600x24,  3, .gfx = { GRAPH_56Hz,  cfgs + 6 } },  /* 41 */
	{ GRAPH_800x600x24,  3, .gfx = { GRAPH_72Hz,  cfgs + 7 } },  /* 42 */
	{ GRAPH_800x600x24,  3, .gfx = { GRAPH_75Hz,  cfgs + 8 } },  /* 43 */
	{ GRAPH_800x600x24,  3, .gfx = { GRAPH_85Hz,  cfgs + 9 } },  /* 44 */
	{ GRAPH_1024x768x24, 3, .gfx = { GRAPH_60Hz,  cfgs + 10 } }, /* 45 */
	{ GRAPH_1024x768x24, 3, .gfx = { GRAPH_43Hzi, cfgs + 11 } }, /* 46 */
	{ GRAPH_1024x768x24, 3, .gfx = { GRAPH_70Hz,  cfgs + 12 } }, /* 47 */
	{ GRAPH_1024x768x24, 3, .gfx = { GRAPH_75Hz,  cfgs + 13 } }, /* 48 */
	{ GRAPH_1024x768x24, 3, .gfx = { GRAPH_85Hz,  cfgs + 14 } }, /* 49 */
	/* 32-bit color (8:8:8:8) */
	{ GRAPH_640x480x32,  4, .gfx = { GRAPH_60Hz,  cfgs + 1 } },  /* 50 */
	{ GRAPH_640x480x32,  4, .gfx = { GRAPH_72Hz,  cfgs + 2 } },  /* 51 */
	{ GRAPH_640x480x32,  4, .gfx = { GRAPH_75Hz,  cfgs + 3 } },  /* 52 */
	{ GRAPH_640x480x32,  4, .gfx = { GRAPH_85Hz,  cfgs + 4 } },  /* 53 */
	{ GRAPH_800x600x32,  4, .gfx = { GRAPH_60Hz,  cfgs + 5 } },  /* 54 */
	{ GRAPH_800x600x32,  4, .gfx = { GRAPH_56Hz,  cfgs + 6 } },  /* 55 */
	{ GRAPH_800x600x32,  4, .gfx = { GRAPH_72Hz,  cfgs + 7 } },  /* 56 */
	{ GRAPH_800x600x32,  4, .gfx = { GRAPH_75Hz,  cfgs + 8 } },  /* 57 */
	{ GRAPH_800x600x32,  4, .gfx = { GRAPH_85Hz,  cfgs + 9 } },  /* 58 */
	{ GRAPH_1024x768x32, 4, .gfx = { GRAPH_60Hz,  cfgs + 10 } }, /* 59 */
	{ GRAPH_1024x768x32, 4, .gfx = { GRAPH_43Hzi, cfgs + 11 } }, /* 60 */
	{ GRAPH_1024x768x32, 4, .gfx = { GRAPH_70Hz,  cfgs + 12 } }, /* 61 */
	{ GRAPH_1024x768x32, 4, .gfx = { GRAPH_75Hz,  cfgs + 13 } }, /* 62 */
	{ GRAPH_1024x768x32, 4, .gfx = { GRAPH_85Hz,  cfgs + 14 } }, /* 63 */
	/* No mode */
	{ 0 }
};


/* Max VCLK for given color depth */
static const unsigned int maxvclks[] = { 0, 135100, 85500, 85500, 0 };


/* VCLK table */
static const cirrus_vclk_t vclks[] = {
	/* Known stable VCLK */
	{ 12599,  0x2c, 0x33 },
	{ 25226,  0x4a, 0x2b },
	{ 28324,  0x5b, 0x2f },
	{ 31499,  0x42, 0x1f },
	{ 36081,  0x7e, 0x33 },
	{ 39991,  0x51, 0x3a },
	{ 41164,  0x45, 0x30 },
	{ 45075,  0x55, 0x36 },
	{ 49866,  0x65, 0x3a },
	{ 64981,  0x76, 0x34 },
	{ 72162,  0x7e, 0x32 },
	{ 74999,  0x6e, 0x2a },
	{ 80012,  0x5f, 0x22 },
	{ 85226,  0x7d, 0x2a },
	{ 89998,  0x58, 0x1c },
	{ 95019,  0x49, 0x16 },
	{ 100226, 0x46, 0x14 },
	{ 108035, 0x53, 0x16 },
	{ 109771, 0x5c, 0x18 },
	{ 120050, 0x6d, 0x1a },
	{ 125998, 0x58, 0x14 },
	{ 130055, 0x6d, 0x18 },
	{ 134998, 0x42, 0x0e },
	{ 150339, 0x69, 0x14 },
	{ 168236, 0x5e, 0x10 },
	{ 188179, 0x5c, 0x0e },
	{ 210679, 0x67, 0x0e },
	{ 229088, 0x60, 0x0c },
	/* No clock */
	{ 0 }
};


struct {
	/* Cirrus graphics card detection context */
	unsigned char bus;  /* PCI bus index */
	unsigned char dev;  /* PCI device index */
	unsigned char func; /* PCI function index */
} cirrus_common;


/* Schedules and executes new task */
extern int graph_schedule(graph_t *graph);


/* Returns internal VCO (kHz) */
static inline unsigned int cirrus_vco(unsigned int n, unsigned int d)
{
	return (n & 0x7f) * MIN_VCLK / (d & 0x3e);
}


/* Finds best numerator and denominator values for given VCLK frequency */
static int cirrus_vclk(unsigned int maxvclk, cirrus_vclk_t *vclk)
{
	unsigned int n, d, f, diff, mindiff, freq = vclk->freq;

	/* Prefer tested clock if it matches within 0.1% */
	for (n = 0; vclks[n].freq; n++) {
		if (abs((int)vclks[n].freq - (int)freq) < freq / 1000) {
			*vclk = vclks[n];
			return EOK;
		}
	}

	if (maxvclk < MAX_VCLK)
		maxvclk = MAX_VCLK;

	/* Find VCLK */
	vclk->freq = 0;
	mindiff = freq;
	for (n = 0x10; n < 0x7f; n++) {
		for (d = 0x14; d < 0x3f; d++) {
			/* Skip unstable combinations */
			f = cirrus_vco(n, d);
			if ((f < MIN_VCLK) || (f > maxvclk))
				continue;
			f >>= (d & 0x1);

			if ((diff = abs((int)f - (int)freq)) < mindiff) {
				vclk->freq = f;
				vclk->num = n;
				vclk->den = d;
				mindiff = diff;
			}
		}
	}

	return (vclk->freq) ? EOK : -EINVAL; 
}


int cirrus_cursorpos(graph_t *graph, unsigned int x, unsigned int y)
{
	cirrus_dev_t * cdev = (cirrus_dev_t *)graph->adapter;
	vga_t *vga = &cdev->vga;

	vga_writeseq(vga, (x << 5) | 0x10, x >> 3);
	vga_writeseq(vga, (y << 5) | 0x11, y >> 3);

	return EOK;
}


int cirrus_cursorset(graph_t *graph, const unsigned char *and, const unsigned char *xor, unsigned int bg, unsigned int fg)
{
	cirrus_dev_t *cdev = (cirrus_dev_t *)graph->adapter;
	vga_t *vga = &cdev->vga;
	unsigned char *cur, sr12;
	unsigned int i, j;

	if (cdev->vmemsz < graph->height * graph->width * graph->depth + 0x1000)
		return -ENOSPC;

	cur = (unsigned char *)cdev->vmem + cdev->vmemsz - 0x1000;
	for (i = 0; i < 64; i++) {
		for (j = 0; j < 8; j++)
			*cur++ = *xor++;
		for (j = 0; j < 8; j++)
			*cur++ = ~(*and++);
	}
	vga_writeseq(vga, 0x13, 0x30);

	sr12 = vga_readseq(vga, 0x12);
	vga_writeseq(vga, 0x12, sr12 | 0x82);
	vga_writedac(vga, 0x02, 0x00);
	vga_writedac(vga, 0x03, bg);
	vga_writedac(vga, 0x03, bg >> 8);
	vga_writedac(vga, 0x03, bg >> 16);
	vga_writedac(vga, 0x02, 0x0f);
	vga_writedac(vga, 0x03, fg);
	vga_writedac(vga, 0x03, fg >> 8);
	vga_writedac(vga, 0x03, fg >> 16);
	vga_writeseq(vga, 0x12, sr12 & ~0x02);

	return EOK;
}


int cirrus_cursorhide(graph_t *graph)
{
	cirrus_dev_t * cdev = (cirrus_dev_t *)graph->adapter;
	vga_t *vga = &cdev->vga;

	vga_writeseq(vga, 0x12, vga_readseq(vga, 0x12) & ~0x01);

	return EOK;
}


int cirrus_cursorshow(graph_t *graph)
{
	cirrus_dev_t * cdev = (cirrus_dev_t *)graph->adapter;
	vga_t *vga = &cdev->vga;

	vga_writeseq(vga, 0x12, vga_readseq(vga, 0x12) | 0x01);

	return EOK;
}


int cirrus_colorset(graph_t *graph, const unsigned char *colors, unsigned int first, unsigned int last)
{
	return EOK;
}


int cirrus_colorget(graph_t *graph, unsigned char *colors, unsigned int first, unsigned int last)
{
	return EOK;
}


int cirrus_isbusy(graph_t *graph)
{
	return 0;
}


int cirrus_commit(graph_t *graph)
{
	return EOK;
}


int cirrus_trigger(graph_t *graph)
{
	if (cirrus_isbusy(graph))
		return -EBUSY;

	return graph_schedule(graph);
}


int cirrus_vsync(graph_t *graph)
{
	return 1;
}


static void cirrus_save(cirrus_dev_t *cdev, cirrus_state_t *state)
{
	vga_t *vga = &cdev->vga;

	/* Save base VGA state */
	vga_save(vga, &state->state);

	/* Save extended VGA state */
	state->cr1a = vga_readcrtc(vga, 0x1a);
	state->cr1b = vga_readcrtc(vga, 0x1b);
	state->cr1d = vga_readcrtc(vga, 0x1d);
	state->sr07 = vga_readseq(vga, 0x07);
	state->sr0e = vga_readseq(vga, 0x0e);
	state->sr12 = vga_readseq(vga, 0x12);
	state->sr13 = vga_readseq(vga, 0x13);
	state->sr17 = vga_readseq(vga, 0x17);
	state->sr1e = vga_readseq(vga, 0x1e);
	state->sr21 = vga_readseq(vga, 0x21);
	state->sr2d = vga_readseq(vga, 0x2d);
	state->gr17 = vga_readgfx(vga, 0x17);
	state->gr18 = vga_readgfx(vga, 0x18);
	/* Read DAC pixel mask before HDR access */
	vga_readdac(vga, 0x00);
	vga_readdac(vga, 0x00);
	vga_readdac(vga, 0x00);
	vga_readdac(vga, 0x00);
	state->hdr = vga_readdac(vga, 0x00);
}


static void cirrus_restore(cirrus_dev_t *cdev, cirrus_state_t *state)
{
	vga_t *vga = &cdev->vga;

	/* Restore extended VGA state */
	vga_writecrtc(vga, 0x1a, state->cr1a);
	vga_writecrtc(vga, 0x1b, state->cr1b);
	vga_writecrtc(vga, 0x1d, state->cr1d);
	vga_writeseq(vga, 0x07, state->sr07);
	vga_writeseq(vga, 0x0e, state->sr0e);
	vga_writeseq(vga, 0x12, state->sr12);
	vga_writeseq(vga, 0x13, state->sr13);
	vga_writeseq(vga, 0x17, state->sr17);
	vga_writeseq(vga, 0x1e, state->sr1e);
	vga_writeseq(vga, 0x21, state->sr21);
	vga_writeseq(vga, 0x2d, state->sr2d);
	vga_writegfx(vga, 0x17, state->gr17);
	vga_writegfx(vga, 0x18, state->gr18);
	/* Read DAC pixel mask before HDR access */
	vga_readdac(vga, 0x00);
	vga_readdac(vga, 0x00);
	vga_readdac(vga, 0x00);
	vga_readdac(vga, 0x00);
	vga_writedac(vga, 0x00, state->hdr);

	/* Restore base VGA state */
	vga_restore(vga, &state->state);
}


int cirrus_mode(graph_t *graph, graph_mode_t mode, graph_freq_t freq)
{
	unsigned int pitch, hdiv = 0, vdiv = 0, i = DEFMODE;
	cirrus_dev_t *cdev = (cirrus_dev_t *)graph->adapter;
	vga_t *vga = &cdev->vga;
	cirrus_state_t state;
	cirrus_vclk_t vclk;
	vga_mode_t cfg;

	if (mode != GRAPH_DEFMODE) {
		for (i = 0; (modes[i].mode != mode) || (modes[i].depth && (freq != GRAPH_DEFFREQ) && (modes[i].gfx.freq != freq)); i++)
			if (!modes[i].mode)
				return -ENOTSUP;
	}

	/* Power management mode (DPMS) */
	if (!modes[i].depth) {
		vga_writeseq(vga, 0x01, (vga_readseq(vga, 0x01) & ~0x20) | modes[i].pwm.sr01);
		vga_writegfx(vga, 0x0e, (vga_readgfx(vga, 0x0e) & ~0x06) | modes[i].pwm.gr0e);
		return EOK;
	}
	pitch = modes[i].gfx.cfg->hres * modes[i].depth;
	cfg = *modes[i].gfx.cfg;

	/* Adjust horizontal timings */
	if (cfg.clk > 85500) {
		cfg.hres >>= 1;
		cfg.hsyncs >>= 1;
		cfg.hsynce >>= 1;
		cfg.htotal >>= 1;
		cfg.clk >>= 1;
		hdiv = 1;
	}

	/* Adjust vertical timings */
	if (cfg.vtotal >= 1024 && !(cfg.flags & VGA_INTERLACE)) {
		cfg.vres >>= 1;
		cfg.vsyncs >>= 1;
		cfg.vsynce >>= 1;
		cfg.vtotal >>= 1;
		vdiv = 1;
	}

	/* Find pixel clock */
	vclk.freq = cfg.clk;
	if (cirrus_vclk(maxvclks[modes[i].depth], &vclk) < 0)
		return -EFAULT;

	/* Initialize VGA state */
	vga_mode(3, &cfg, &state.state);
	state.state.cmap = NULL;
	state.state.text = NULL;
	state.state.font1 = NULL;
	state.state.font2 = NULL;
	state.state.crtc[0x13] = pitch >> 3;
	state.state.crtc[0x17] |= vdiv * 0x04;

	/* Initialize extended VGA registers */
	state.cr1a = 0x00;
	state.cr1b = ((pitch >> 7) & 0x10) | ((pitch >> 6) & 0x40) | 0x22;
	state.cr1d = 0x00;
	state.sr07 = 0xe0;
	state.sr0e = 0x00;
	state.sr12 = 0x04;
	state.sr13 = 0x00;
	state.sr17 = 0x00;
	state.sr1e = 0x00;
	state.sr21 = 0x00;
	state.sr2d = 0x00;

	switch (modes[i].depth) {
	case 1:
		state.sr07 |= (hdiv) ? 0x17 : 0x11;
		state.hdr = (hdiv) ? 0x4a : 0x00;
		break;

	case 2:
		state.sr07 |= (hdiv) ? 0x19 : 0x17;
		state.hdr = 0xc1;
		break;

	case 3:
		state.sr07 |= 0x15;
		state.hdr = 0xc5;
		break;

	case 4:
		state.sr07 |= 0x19;
		state.hdr = 0xc5;
		break;

	default:
		return -EFAULT;
	}

	state.gr17 = 0x08;
	state.gr18 = (hdiv) ? 0x20 : 0x00;

	/* Program mode */
	vga_mlock(vga);
	cirrus_restore(cdev, &state);
	vga_writeseq(vga, 0x0e, (vga_readseq(vga, 0x0e) & 0x80) | vclk.num);
	vga_writeseq(vga, 0x1e, vclk.den);
	vga_munlock(vga);

	/* Update graph data and clear screen */
	graph->depth = modes[i].depth;
	graph->width = modes[i].gfx.cfg->hres;
	graph->height = modes[i].gfx.cfg->vres;
	memset(cdev->vmem, 0, graph->width * graph->height * graph->depth);

	return EOK;
}


void cirrus_close(graph_t *graph)
{
	cirrus_dev_t *cdev = (cirrus_dev_t *)graph->adapter;
	vga_t *vga = &cdev->vga;

	/* Restore original video state */
	vga_mlock(vga);
	cirrus_restore(cdev, &cdev->state);
	vga_munlock(vga);

	/* Lock VGA registers and destroy device */
	vga_lock(vga);
	vga_done(vga);
	munmap(cdev->vmem, (cdev->vmemsz + _PAGE_SIZE - 1) & ~(_PAGE_SIZE - 1));
	free(cdev);
}


/* Returns video memory size */
static unsigned int cirrus_vmemsz(cirrus_dev_t *cdev)
{
	vga_t *vga = &cdev->vga;
	unsigned char sr0f, sr17;

	sr0f = vga_readseq(vga, 0x0f);
	sr17 = vga_readseq(vga, 0x17);

	if ((sr0f & 0x18) == 0x18) {
		if (sr0f & 0x80) {
			if (sr17 & 0x80)
				return 0x200000;

			if (sr17 & 0x02)
				return 0x300000;

			return 0x400000;
		}

		if (!(sr17 & 0x80))
			return 0x200000;
	}

	return 0x100000;
}


int cirrus_open(graph_t *graph)
{
	platformctl_t pctl = { .action = pctl_get, .type = pctl_pci };
	cirrus_dev_t *cdev;
	vga_t *vga;
	int ret;

	pctl.pci.id.vendor = 0x1013;
	pctl.pci.id.device = 0x00b8;
	pctl.pci.id.subvendor = PCI_ANY;
	pctl.pci.id.subdevice = PCI_ANY;
	pctl.pci.id.cl = PCI_ANY;
	pctl.pci.dev.bus = cirrus_common.bus;
	pctl.pci.dev.dev = cirrus_common.dev;
	pctl.pci.dev.func = cirrus_common.func;
	pctl.pci.caps = NULL;

	do {
		if ((ret = platformctl(&pctl)) < 0)
			break;

		/* Check PCI BAR0 for video memory space */
		if (!pctl.pci.dev.resources[0].base || !pctl.pci.dev.resources[0].limit || (pctl.pci.dev.resources[0].flags & 0x01)) {
			ret = -EFAULT;
			break;
		}

		/* Allocate device memory */
		if ((cdev = malloc(sizeof(cirrus_dev_t))) == NULL) {
			ret = -ENOMEM;
			break;
		}
		vga = &cdev->vga;

		/* Initialize VGA chip */
		if ((ret = vga_init(vga)) < 0) {
			free(cdev);
			break;
		}

		/* Check color support */
		if (!(vga_readmisc(vga) & 0x01)) {
			vga_done(vga);
			free(cdev);
			ret = -ENOTSUP;
			break;
		}

		/* Map video memory */
		cdev->vmemsz = cirrus_vmemsz(cdev);
		if ((cdev->vmem = mmap(NULL, (cdev->vmemsz + _PAGE_SIZE - 1) & ~(_PAGE_SIZE - 1), PROT_READ | PROT_WRITE, MAP_DEVICE | MAP_ANONYMOUS | MAP_UNCACHED, OID_PHYSMEM, pctl.pci.dev.resources[0].base)) == MAP_FAILED) {
			vga_done(vga);
			free(cdev);
			ret = -ENOMEM;
			break;
		}

		/* Unlock VGA registers and save current video state */
		vga_unlock(vga);
		cdev->state.state.cmap = cdev->cmap;
		cdev->state.state.font1 = cdev->font1;
		cdev->state.state.font2 = cdev->font2;
		cdev->state.state.text = cdev->text;
		cirrus_save(cdev, &cdev->state);

		/* Initialize graph info */
		graph->adapter = cdev;
		graph->data = cdev->vmem;
		graph->width = 0;
		graph->height = 0;
		graph->depth = 0;

		/* Set graph functions */
		graph->close = cirrus_close;
		graph->mode = cirrus_mode;
		graph->vsync = cirrus_vsync;
		graph->isbusy = cirrus_isbusy;
		graph->trigger = cirrus_trigger;
		graph->commit = cirrus_commit;
		graph->colorset = cirrus_colorset;
		graph->colorget = cirrus_colorget;
		graph->cursorset = cirrus_cursorset;
		graph->cursorpos = cirrus_cursorpos;
		graph->cursorshow = cirrus_cursorshow;
		graph->cursorhide = cirrus_cursorhide;

		ret = EOK;
	} while (0);

	cirrus_common.bus = pctl.pci.dev.bus;
	cirrus_common.dev = pctl.pci.dev.dev;
	cirrus_common.func = pctl.pci.dev.func + 1;

	return ret;
}


void cirrus_done(void)
{
	return;
}


int cirrus_init(void)
{
	return EOK;
}
