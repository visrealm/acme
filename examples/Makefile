ASSEMBLER6502	= acme
AS_FLAGS	= -v8 -Wtype-mismatch --strict
RM		= rm

PROGS		= c64doubledabble.prg c64misc.prg ddrv128.prg ddrv64.prg macedit.prg reu-detect.prg trigono.o

all: $(PROGS)

c64doubledabble.prg: c64doubledabble.a
	$(ASSEMBLER6502) $(AS_FLAGS) --outfile c64doubledabble.prg --format cbm c64doubledabble.a

c64misc.prg: c64misc.a
	$(ASSEMBLER6502) $(AS_FLAGS) --outfile c64misc.prg --format cbm c64misc.a

ddrv128.prg: ddrv.a
	$(ASSEMBLER6502) $(AS_FLAGS) --outfile ddrv128.prg --format cbm -DSYSTEM=128 ddrv.a

ddrv64.prg: ddrv.a
	$(ASSEMBLER6502) $(AS_FLAGS) --outfile ddrv64.prg --format cbm -DSYSTEM=64 ddrv.a

macedit.prg: macedit.a
	$(ASSEMBLER6502) $(AS_FLAGS) --outfile macedit.prg --format cbm macedit.a

reu-detect.prg: reu-detect.a
	$(ASSEMBLER6502) $(AS_FLAGS) --outfile reu-detect.prg --format cbm reu-detect.a

trigono.o: trigono.a
	$(ASSEMBLER6502) $(AS_FLAGS) --outfile trigono.o --format plain trigono.a

clean:
	-$(RM) -f *.o *.tmp $(PROGS) *~ core
