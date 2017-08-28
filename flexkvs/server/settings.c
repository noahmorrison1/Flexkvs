#include "iokvs.h"

struct settings settings;

void settings_init(int argc, char *argv[])
{
    settings.udpport = 11211;
    settings.verbose = 1;
    settings.segsize = 2 * 1024 * 1024;
    settings.segmaxnum = 512;
    settings.segcqsize = 32 * 1024;
}


