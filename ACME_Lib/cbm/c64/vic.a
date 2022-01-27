;ACME 0.96.4

!ifdef lib_cbm_c64_vic_a !eof
lib_cbm_c64_vic_a = 1

; This is from Christian Bauer's VIC text: {
;	6566: designed for static RAM, never used in C64/128
;	6567/8562: C64, NTSC
;	6569/8565: C64, PAL
;	6572: Drean C64, PAL-N
;	8564: C128, NTSC
;	8566: C128, PAL
;	PAL chips generate 312 lines with 63 cycles per line.
;	PAL-N chips generate 312 lines with 65 cycles per line.
;	NTSC chips generate 263 lines with 65 cycles per line.
;	Very early NTSC chips (6567R56A) have an off-by-one
;	error, they generate 262 lines with 64 cycles per line.
; }

; The c128 version is officially called "VIC-IIe".
; It has two additional registers and more pins.

; color codes
viccolor_BLACK	= $0
viccolor_WHITE	= $1
viccolor_RED	= $2
viccolor_CYAN	= $3
viccolor_PURPLE	= $4
viccolor_GREEN	= $5
viccolor_BLUE	= $6
viccolor_YELLOW	= $7
viccolor_ORANGE	= $8
viccolor_BROWN	= $9
viccolor_LRED	= $a
viccolor_GRAY1	= $b
viccolor_GRAY2	= $c
viccolor_LGREEN	= $d
viccolor_LBLUE	= $e
viccolor_GRAY3	= $f

!addr	vic_base	= $d000
	; sprite coordinates:
	vic_xs0		= vic_base + $00
	vic_ys0		= vic_base + $01
	vic_xs1		= vic_base + $02
	vic_ys1		= vic_base + $03
	vic_xs2		= vic_base + $04
	vic_ys2		= vic_base + $05
	vic_xs3		= vic_base + $06
	vic_ys3		= vic_base + $07
	vic_xs4		= vic_base + $08
	vic_ys4		= vic_base + $09
	vic_xs5		= vic_base + $0a
	vic_ys5		= vic_base + $0b
	vic_xs6		= vic_base + $0c
	vic_ys6		= vic_base + $0d
	vic_xs7		= vic_base + $0e
	vic_ys7		= vic_base + $0f
	vic_msb_xs	= vic_base + $10	; bit 8 of x position	(one bit per sprite)
	vic_controlv	= vic_base + $11	; vertical control and other stuff:
	; %7....... "bit 8" of $d012
	; %.6...... extended background color mode (1=on)
	; %..5..... 0 = text mode, 1 = bitmap mode
	; %...4.... display enable: 0 = disable (border only), 1 = enable	VIC only checks this once per frame!
	; %....3... lines: 0 = 24, 1 = 25	; if set, upper and lower border gain 4 pixels each
	; %.....210 vertical smooth scroll (default %011), screen contents are moved down by this amount
	vic_line	= vic_base + $12	; raster line (also see bit 7 of $d011)	reading returns current value, writing sets interrupt value
	vic_xlp		= vic_base + $13	; light pen coordinates, x (only half the resolution)
	vic_ylp		= vic_base + $14	; light pen coordinates, y
	vic_sactive	= vic_base + $15	; sprite enable	(one bit per sprite)
	vic_controlh	= vic_base + $16	; horizontal control and other stuff:
	; %76...... always set
	; %..5..... "test", should be 0
	; %...4.... 0 = hires, 1 = multicolor
	; %....3... columns: 0 = 38, 1 = 40	; if set, left border gains 7 pixels and right border gains 9 pixels
	; %.....210 horizontal smooth scroll (default %000), screen contents are moved to the right by this amount
	vic_sdy		= vic_base + $17	; sprites, double height	(one bit per sprite)
	vic_ram		= vic_base + $18	; RAM pointer
	; %7654.... which K of VIC bank is video ram (default %0001)
	; %....321. text: which 2K of VIC bank is character set (default %010)
	; %.......0 text: unused
	; %....3... bitmap: which 8K of VIC bank is bitmap
	; %.....210 bitmap: unused
	vic_irq		= vic_base + $19	; writing back acknowledges!
	; %7....... 1: VIC requested interrupt
	; %.654.... always set
	; %....3... 1: light pen active
	; %.....2.. 1: sprite-sprite collision
	; %......1. 1: sprite-data collision
	; %.......0 1: raster interrupt
	vic_irqmask	= vic_base + $1a
	; %7654.... always set
	; %....3... 1: enable lightpen interrupt
	; %.....2.. 1: enable sprite-sprite collision interrupt
	; %......1. 1: enable sprite-data collision interrupt
	; %.......0 1: enable raster interrupt
	vic_sback	= vic_base + $1b	; sprites, background	(one bit per sprite)
	vic_smc		= vic_base + $1c	; sprites, multicolor	(one bit per sprite)
	vic_sdx		= vic_base + $1d	; sprites, double width	(one bit per sprite)
	vic_ss_collided	= vic_base + $1e	; sprites, sprite collision	(one bit per sprite)	reading clears register!
	vic_sd_collided	= vic_base + $1f	; sprites, data collision	(one bit per sprite)	reading clears register!
	; color registers (high nibbles are always %1111):
	vic_cborder	= vic_base + $20	; border color
	vic_cbg		= vic_base + $21	; general background color
	vic_cbg0	= vic_base + $21
	vic_cbg1	= vic_base + $22	; background color 1 (for EBC and MC text mode)
	vic_cbg2	= vic_base + $23	; background color 2 (for EBC and MC text mode)
	vic_cbg3	= vic_base + $24	; background color 3 (for EBC mode)
	vic_sc01	= vic_base + $25	; sprite color for MC-bitpattern %01
	vic_sc11	= vic_base + $26	; sprite color for MC-bitpattern %11
	; individual sprite colors (in MC mode, these are used for bit pattern %10):
	vic_cs0		= vic_base + $27	; sprite 0 color
	vic_cs1		= vic_base + $28	; sprite 1 color
	vic_cs2		= vic_base + $29	; sprite 2 color
	vic_cs3		= vic_base + $2a	; sprite 3 color
	vic_cs4		= vic_base + $2b	; sprite 4 color
	vic_cs5		= vic_base + $2c	; sprite 5 color
	vic_cs6		= vic_base + $2d	; sprite 6 color
	vic_cs7		= vic_base + $2e	; sprite 7 color
; See <cbm/c128/vic.a> for the C128's two additional registers at $d02f/$d030.
; They are accessible even in C64 mode and $d030 can garble the video output,
; so be careful not to write to it accidentally in a C64 program!
