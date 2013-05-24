noinst_LIBRARIES	= libx8632.a

SUFFIXES = .asm

libx8632_a_SOURCES	= sha256_xmm.asm

.asm.o:
	$(YASM) -f $(YASM_FMT) $<
