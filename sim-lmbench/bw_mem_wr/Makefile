CC = gcc
CFLAGS = -O2 -Wall -pthread -lrt -D_GNU_SOURCE
TARGETS = sim-bw_mem_wr inf-sim-bw_mem_wr
SRCS = $(TARGET).c

all: $(TARGETS)

#
# No object files, to keep the file count low.
#
TARGET = sim-bw_mem_wr
$(TARGET): $(SRCS)
	$(CC) -o $@ $^ $(CFLAGS)

TARGET = inf-sim-bw_mem_wr
$(TARGET): $(SRCS)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(TARGETS)
