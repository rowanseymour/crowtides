ROOT := ../..
CFLAGS := -Wall -I$(ROOT)/main \
	-I$(ROOT)/components/crowpanel579/include \
	-I$(ROOT)/components/crowpanel579 \
	$(CFLAGS_EXTRA)
SRC := harness.c epd_stub.c $(ROOT)/main/chart.c \
	$(ROOT)/components/crowpanel579/epd_fb.c \
	$(ROOT)/components/crowpanel579/text.c

render: $(SRC) $(ROOT)/main/config.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

.PHONY: clean
clean:
	rm -f render *.pbm
