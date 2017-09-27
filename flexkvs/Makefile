FLEXKVS_FILES=hashtable.o ialloc.o jenkins_hash.o main.o settings.o global.o cache_tester.o database.o database_tester.o ram_ht.o ssd_ht.o ssd_tester.o tester.o
BENCH_FILES=main.o rng.o commandline.o

CFLAGS = -std=gnu99 -g -w -Icommon/include -O3 -march=native
LDFLAGS = -pthread -g
LDLIBS = -lrt

ifeq ($(RTE_SDK),)
$(error "Please set RTE_SDK to point to your dpdk installation")
endif
ifeq ($(RTE_ARCH),)
$(error "Please set RTE_ARCH to point to your dpdk installation")
endif

###############################################################################
# DPDK things
#
CFLAGS+= -I$(RTE_SDK)/$(RTE_ARCH)/include -I$(RTE_SDK)/$(RTE_ARCH)/include/dpdk
CFLAGS+= -include $(RTE_SDK)/$(RTE_ARCH)/include/rte_config.h
LDFLAGS+= -L$(RTE_SDK)/$(RTE_ARCH)/lib/
LIBS_DPDK+= -Wl,--whole-archive -lrte_pmd_ixgbe \
            -Wl,--no-whole-archive                                 # IXGBE driver
LIBS_DPDK+= -lrte_eal -lrte_mempool \
	    -lrte_ring -lrte_hash                                  # DPDK Basics
LIBS_DPDK+= -lrte_ethdev -lrte_mbuf -lrte_eal -lrte_pmd_ring           # DPDK net
###############################################################################

LDLIBS += $(LIBS_DPDK) -lm -lpthread -lrt -ldl


all: flexkvs kvsbench

flexkvs: $(addprefix server/,$(FLEXKVS_FILES))
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

kvsbench: $(addprefix bench/,$(BENCH_FILES))
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f */*.o flexkvs kvsbench
