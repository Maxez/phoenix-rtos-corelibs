/*
 * Phoenix-RTOS
 *
 * VGA library interface
 *
 * Copyright 2021 Phoenix Systems
 * Author: Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _LIBVGA_H_
#define _LIBVGA_H_


/* VGA common defines */
#define VGA_MEMSZ  0x10000          /* VGA memory size */
#define VGA_CMAPSZ 768              /* VGA color map size */
#define VGA_TEXTSZ (VGA_MEMSZ >> 1) /* VGA text size */
#define VGA_FONTSZ VGA_MEMSZ        /* VGA font size */


/* VGA mode flags */
enum {
	VGA_HSYNCP    = (1 << 0),       /* HSync polarity */
	VGA_VSYNCP    = (1 << 1),       /* VSync polarity */
	VGA_CLKDIV    = (1 << 2),       /* Half the clock */
	VGA_DBLSCAN   = (1 << 3),       /* Double scan */
	VGA_INTERLACE = (1 << 4)        /* Interlace mode */
};


typedef struct {
	unsigned int clk;               /* Pixel clock frequency (kHz) */
	/* Horizontal timings */
	unsigned int hres;              /* Horizontal resolution */
	unsigned int hsyncs;            /* Horizontal sync start */
	unsigned int hsynce;            /* Horizontal sync end */
	unsigned int htotal;            /* Horizontal total pixels */
	unsigned int hskew;             /* Horizontal skew */
	/* Vertical timings */
	unsigned int vres;              /* Vertical resolution */
	unsigned int vsyncs;            /* Vertical sync start */
	unsigned int vsynce;            /* Vertical sync end */
	unsigned int vtotal;            /* Vertical total lines */
	unsigned int vscan;             /* Vertical scan multiplier */
	/* Mode configuration */
	unsigned char flags;            /* Mode flags */
} vga_mode_t;


typedef struct {
	unsigned char misc;             /* Miscellaneous register */
	unsigned char crtc[25];         /* CRT controller registers */
	unsigned char seq[5];           /* Sequencer registers */
	unsigned char gfx[9];           /* Graphics controller registers */
	unsigned char attr[21];         /* Attribute controller registers */
	unsigned char *cmap;            /* Color map */
	unsigned char *text;            /* Plane 0 and 1 text */
	unsigned char *font1;           /* Plane 2 font */
	unsigned char *font2;           /* Plane 3 font */
} vga_state_t;


typedef struct {
	void *misc;                     /* Miscellaneous registers base address */
	void *crtc;                     /* CRT controller registers base address */
	void *seq;                      /* Sequencer registers base address */
	void *gfx;                      /* Graphics controller registers base address */
	void *attr;                     /* Attribute controller registers base address */
	void *dac;                      /* Digital to Analog Converter registers base address */
	void *mem;                      /* Mapped VGA memory base address */
	unsigned int memsz;             /* Mapped VGA memory size */
} vga_t;


/*****************************************/
/* Low level interface (hardware access) */
/*****************************************/


/* Reads from input status register */
extern unsigned char vga_status(vga_t *vga);


/* Reads from miscellaneous register */
extern unsigned char vga_readmisc(vga_t *vga);


/* Writes to miscellaneous register */
extern void vga_writemisc(vga_t *vga, unsigned char val);


/* Reads from CRT controller register */
extern unsigned char vga_readcrtc(vga_t *vga, unsigned char reg);


/* Writes to CRT controller register */
extern void vga_writecrtc(vga_t *vga, unsigned char reg, unsigned char val);


/* Reads from sequencer register */
extern unsigned char vga_readseq(vga_t *vga, unsigned char reg);


/* Writes to sequencer register */
extern void vga_writeseq(vga_t *vga, unsigned char reg, unsigned char val);


/* Reads from graphics controller register */
extern unsigned char vga_readgfx(vga_t *vga, unsigned char reg);


/* Writes to graphics controller register */
extern void vga_writegfx(vga_t *vga, unsigned char reg, unsigned char val);


/* Reads from attribute controller register */
extern unsigned char vga_readattr(vga_t *vga, unsigned char reg);


/* Writes to attribute controller register */
extern void vga_writeattr(vga_t *vga, unsigned char reg, unsigned char val);


/* Reads from DAC controller register */
extern unsigned char vga_readdac(vga_t *vga, unsigned char reg);


/* Writes to DAC controller register */
extern void vga_writedac(vga_t *vga, unsigned char reg, unsigned char val);


/* Disables color map */
extern void vga_disablecmap(vga_t *vga);


/* Enables color map */
extern void vga_enablecmap(vga_t *vga);


/* Destroys VGA handle */
extern void vga_done(vga_t *vga);


/* Initializes VGA handle */
extern int vga_init(vga_t *vga);


/************************/
/* High level interface */
/************************/


/* Locks CRTC[0-7] registers */
extern void vga_lock(vga_t *vga);


/* Unlocks CRTC[0-7] registers */
extern void vga_unlock(vga_t *vga);


/* Protects VGA registers and memory during mode switch */
extern void vga_mlock(vga_t *vga);


/* Releases VGA mode switch protection set with vga_mlock() */
extern void vga_munlock(vga_t *vga);


/* Blanks screen */
extern void vga_blank(vga_t *vga);


/* Unblanks screen */
extern void vga_unblank(vga_t *vga);


/* Saves VGA mode */
extern void vga_savemode(vga_t *vga, vga_state_t *state);


/* Restores VGA mode */
extern void vga_restoremode(vga_t *vga, vga_state_t *state);


/* Saves VGA color map */
extern void vga_savecmap(vga_t *vga, vga_state_t *state);


/* Restores VGA color map */
extern void vga_restorecmap(vga_t *vga, vga_state_t *state);


/* Saves VGA fonts and text */
extern void vga_savetext(vga_t *vga, vga_state_t *state);


/* Restores VGA fonts and text */
extern void vga_restoretext(vga_t *vga, vga_state_t *state);


/* Saves VGA settings */
extern void vga_save(vga_t *vga, vga_state_t *state);


/* Restores VGA settings */
extern void vga_restore(vga_t *vga, vga_state_t *state);


/* Initializes VGA state for given mode */
extern void vga_mode(unsigned char clkidx, vga_mode_t *mode, vga_state_t *state);


#endif
