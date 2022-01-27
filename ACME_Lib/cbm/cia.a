;ACME 0.96.4

!ifdef lib_cbm_cia_a !eof
lib_cbm_cia_a = 1

; CBM's "complex interface adapter" chip, known as 6526.
; A newer version of this chip was initially called 8521, but later the name was
; changed back to 6526 again.

; There are two of these in a C64/128 computer, and one in 1570/1571 drives.

; pinout:
;               ____ ____
;              |    V    |
;          GND |  1   40 | cnt  clock for shift register
;        / pa0 |  2   39 | sp   data for shift register
;       |  pa1 |  3   38 | a0 \
;       |  pa2 |  4   37 | a1  |_  address
; Port _|  pa3 |  5   36 | a2  |   bus
;   A   |  pa4 |  6   35 | a3 /
;       |  pa5 |  7   34 | /reset
;       |  pa6 |  8   33 | d0 \
;        \ pa7 |  9   32 | d1  |
;        / pb0 | 10   31 | d2  |
;       |  pb1 | 11   30 | d3  |_  data
;       |  pb2 | 12   29 | d4  |   bus
; Port _|  pb3 | 13   28 | d5  |
;   B   |  pb4 | 14   27 | d6  |
;       |  pb5 | 15   26 | d7 /
;       |  pb6 | 16   25 | 0in  clock
;        \ pb7 | 17   24 | /flag
;          /pc | 18   23 | /cs  chip select
;      tod_clk | 19   22 | r/w
;          +5V | 20   21 | /irq
;              |_________|


; register offsets:
	; two 8-bit ports:
	cia_port_a		= $0
	cia_port_b		= $1
	cia_data_direction_a	= $2	; clear means input,
	cia_data_direction_b	= $3	; set means output

	; two 16-bit timers, can be combined to form a single 32-bit timer:
	cia_timer_a_low		= $4
	cia_timer_a_high	= $5
	cia_timer_b_low		= $6
	cia_timer_b_high	= $7
	; reading returns current counter value,
	; writing sets start value.
	; in 32-bit mode, timer A is "low word" and timer B is "high word".

	; TOD (time of day) clock, clocked with 50 or 60 Hz:
	; (CAUTION, registers use binary coded decimal format)
	cia_timeofday_10ths	= $8	; %....3210	0..9, counts 10ths of seconds
	cia_timeofday_seconds	= $9	; %.6543210	0..59, as two BCD digits:
		; %.654....	0..5	"tens"
		; %....3210	0..9	"ones"
	cia_timeofday_minutes	= $a	; %.6543210	0..59, as two BCD digits:
		; %.654....	0..5	"tens"
		; %....3210	0..9	"ones"
	cia_timeofday_hours	= $b	; %7..43210	AM/PM and 1..12 as two BCD digits:
		; %7.......	0 means AM, 1 means PM
		; %...4....	0..1	"tens"
		; %....3210	0..9	"ones"
	; when reading or writing time, start with hours and end with 10ths:
	; accessing hours uncouples registers from clock, accessing 10ths re-couples them.
	; if your read access does not start with the hours register,
	; you have a race condition!

	; shift register:
	; msb is sent/received first. send clock is half of timer A's underflow rate.
	cia_serial_data		= $c

	; control registers:
	cia_interrupt_control	= $d
	; %7.......	read: 1 = interrupt requested (reading clears register)
	;		write:	0 = disable interrupts indicated by set bits
	;			1 = enable interrupts indicated by set bits
	; %.65.....	unused
	; %...4....	negative edge on /flag detected
	; %....3...	shift register has finished sending or receiving
	; %.....2..	time of day alarm
	; %......1.	timer B underflow
	; %.......0	timer A underflow

	cia_control_a		= $e
	; %7.......	TOD clock: 0 means 60 Hz, 1 means 50 Hz
	; %.6......	shift register direction: 0 = input, 1 = output
	; %..5.....	timer A clock: 0 = system clock, 1 = CNT
	; %...4....	1 = force load timer start value
	; %....3...	timer mode: 0 = continuous, 1 = one-shot (needs restart via bit 0)
	; %.....2..	0 = pulse on PB6, 1 = invert PB6
	; %......1.	1 = timer underflow shows on PB6 (this forces PB6 to output)
	; %.......0	0 stops timer, 1 starts timer

	cia_control_b		= $f
	; %7.......	TOD write mode: 0 = actual time, 1 = alarm time (it is not possible to _read_ the alarm time!)
	; %.65.....	timer B clock:
	;		%00 = system clock
	;		%01 = CNT
	;		%10 = underflow of timer A
	;		%11 = combination of %01 and %10
	; %...4....	1 = force load timer start value
	; %....3...	timer mode: 0 = continuous, 1 = one-shot (needs restart via bit 0)
	; %.....2..	0 = pulse on PB7, 1 = invert PB7
	; %......1.	1 = timer underflow shows on PB7 (this forces PB7 to output)
	; %.......0	0 stops timer, 1 starts timer
