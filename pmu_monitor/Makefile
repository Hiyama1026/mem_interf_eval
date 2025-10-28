CC = gcc
CFLAGS = -O2 -Wall -pthread -lrt -D_GNU_SOURCE
TARGETS = pmu-monitor
SRCS = $(TARGET).c

all: $(TARGETS)

#
# No object files, to keep the file count low.
#
TARGET = pmu-monitor
$(TARGET): $(SRCS)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(TARGETS)
