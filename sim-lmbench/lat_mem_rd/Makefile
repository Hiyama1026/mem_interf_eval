CC = gcc
CFLAGS = -O2 -Wall -pthread -lrt -D_GNU_SOURCE
TARGETS = sim-lat_mem_rd inf-sim-lat_mem_rd pmu-sim-lat_mem_rd
SRCS = $(TARGET).c

all: $(TARGETS)

#
# No object files, to keep the file count low.
#
TARGET = sim-lat_mem_rd
$(TARGET): $(SRCS)
	$(CC) -o $@ $^ $(CFLAGS)

TARGET = inf-sim-lat_mem_rd
$(TARGET): $(SRCS)
	$(CC) -o $@ $^ $(CFLAGS)

TARGET = pmu-sim-lat_mem_rd
# Path to pmu_counter.c
PMU_COUNTER_PATH ?= ../../pmu_counter/pmu_counter.c
SRCS += $(PMU_COUNTER_PATH)
$(TARGET): $(SRCS)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(TARGETS)
