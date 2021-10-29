
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

/*Debug log control*/
#define DEBUG_ENABLED 1

#if DEBUG_ENABLED
#define mtdbg(fmt, ...)  \
	do {    \
		printf("[mtdbg] " fmt "\n", ##__VA_ARGS__);  \
	} while (0);
#else
#define mtdbg(...)
#endif

/*TODO*/
#define ARCH_IS_64BIT 1

#if ARCH_IS_64BIT
/*If md is without b|w|l|q, default to 8 bytes on 64-bit system*/
#define DEFAULT_WIDTH 8
#else
/*If md is without b|w|l, default to 4 bytes on 32-bit system*/
#define DEFAULT_WIDTH 4
#endif

/* Argument count validation for md or mw
 *
 * min args for md = 2 (md.l, addr)
 * max args for md = 3 (md.l, addr, count)
 * min args for mw = 3 (mw.l, addr, val)
 * max args for mw = 4 (mw.l, addr, val, count)
 *
 * OpIsWrite variable adds 1 to argument count limits as below 
 * */
#define MIN_ARG_CNT(OpIsWrite) (2 + OpIsWrite)
#define MAX_ARG_CNT(OpIsWrite) (3 + OpIsWrite)
#define ARG_CNT_IS_VALID(cnt, OpIsWrite) \
	((cnt >= MIN_ARG_CNT (OpIsWrite)) && (cnt <= MAX_ARG_CNT (OpIsWrite)))

void PrintUsage (int OpIsWrite)
{
	char *UsageString[] = {
		"md.[b|w|l|q] <addr> [count]      ", 
		"mw.[b|w|l|q] <addr> <val> [count]"
	};

	printf ("Usage: %s\n", UsageString[OpIsWrite]);
}

void SetupDataWidth (char *cmd, uint32_t *DataWidth)
{
	char WidthType;

	WidthType = ((cmd[2] == '\0') ? 0 : cmd[3]);

	switch (WidthType) {
		case 'b': *DataWidth = 1; break;
		case 'w': *DataWidth = 2; break;
		case 'l': *DataWidth = 4; break;
		case 'q':
#if ARCH_IS_64BIT
				  *DataWidth = 8;
#else
				  printf ("'q' is unsupported on 32-bit system!\n");
#endif
				  break;
		case 0: *DataWidth = DEFAULT_WIDTH; break;
		default: /*Added just to avoid warning*/ break;
	}
	/*mtdbg ("Width selected: %u", *DataWidth);*/
}

int SetupMemoryMap (uint32_t paddr, int *fd_devmem, void **vaddr)
{
#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)
	int fd;
	void *vbase;

	/*Check if the paddr is aligned to 32-bit*/
	if (paddr & 0x3) {
		printf ("Address %08x is not 32-bit aligned\n", paddr);
		return -1;
	}

	fd = open ("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror ("/dev/mem");
		return -errno;
	}
	/*fflush (stdout);*/

	/*TODO - allow to map more than 4K size*/
	vbase = mmap (0, MAP_SIZE, 
			PROT_READ|PROT_WRITE, 
			MAP_SHARED, fd, paddr & ~MAP_MASK);
	if (vbase == (void *) -1) {
		perror ("mmap on given addr");
		close (fd);
		return -errno;
	}

	*fd_devmem = fd;
	*vaddr = vbase + (paddr & MAP_MASK);

	return 0;
}

#define MAX_DATA_OBJS_PER_LINE   (16)
#define MAX_BYTES_PER_LINE       (64)
#define DEFAULT_NUM_DATA_OBJECTS (64)

int do_memread (uint32_t paddr, const void *vaddr, uint32_t DataWidth, uint32_t numDataObjects)
{
    /* linebuf as a union causes proper alignment */
    union linebuf {
#ifdef ARCH_IS_64BIT
        uint64_t uq[MAX_BYTES_PER_LINE/sizeof(uint64_t) + 1];
#endif
        uint32_t ui[MAX_BYTES_PER_LINE/sizeof(uint32_t) + 1];
        uint16_t us[MAX_BYTES_PER_LINE/sizeof(uint16_t) + 1];
        uint8_t  uc[MAX_BYTES_PER_LINE/sizeof(uint8_t) + 1];
    } lb;
    int i;
#ifdef ARCH_IS_64BIT
    uint64_t x;
#else
    uint32_t x;
#endif
	uint32_t DataObjsPerLine = MAX_DATA_OBJS_PER_LINE/DataWidth;

	mtdbg ("paddr           : %08x", paddr);
	mtdbg ("vaddr           : %p", vaddr);
	mtdbg ("DataWidth       : %u", DataWidth);
	mtdbg ("numDataObjects  : %u", numDataObjects);

    if (DataObjsPerLine*DataWidth > MAX_BYTES_PER_LINE)
        DataObjsPerLine = MAX_BYTES_PER_LINE / DataWidth;
    if (DataObjsPerLine < 1)
        DataObjsPerLine = DEFAULT_NUM_DATA_OBJECTS / DataWidth;
	mtdbg ("DataObjsPerLine : %u", DataObjsPerLine);

	printf ("----------------------------\n\n");

    while (numDataObjects > 0) {
        uint32_t thisDataObjsPerLine = DataObjsPerLine;
        printf("%08x:", paddr);

        /* check for overflow condition */
        if (numDataObjects < thisDataObjsPerLine)
            thisDataObjsPerLine = numDataObjects;

		/*mtdbg ("thisopl: %u", thisDataObjsPerLine);*/
        /* Copy from memory into linebuf and print hex values */
        for (i = 0; i < thisDataObjsPerLine; i++) {
			switch (DataWidth) {
#ifdef ARCH_IS_64BIT
				case 8: x = lb.uq[i] = *(volatile uint64_t *)vaddr; break;
#endif
				case 4: x = lb.ui[i] = *(volatile uint32_t *)vaddr; break;
				case 2: x = lb.us[i] = *(volatile uint16_t *)vaddr; break;
				case 1: x = lb.uc[i] = *(volatile uint8_t *)vaddr; break;
			}
#ifdef ARCH_IS_64BIT
            printf(" %0*llx", DataWidth * 2, (long long)x);
#else
            printf(" %0*x", DataWidth * 2, x);
#endif
            vaddr += DataWidth;
        }

        while (thisDataObjsPerLine < DataObjsPerLine) {
            /* fill line with whitespace for nice ASCII print */
            for (i=0; i<DataWidth*2+1; i++)
                printf (" ");
            DataObjsPerLine--;
        }

        /* Print data in ASCII characters */
        for (i = 0; i < thisDataObjsPerLine * DataWidth; i++) {
            if (!isprint(lb.uc[i]) || lb.uc[i] >= 0x80)
                lb.uc[i] = '.';
        }
        lb.uc[i] = '\0';
        printf("    %s\n", lb.uc);

        /* update references */
        paddr += thisDataObjsPerLine * DataWidth;
        numDataObjects -= thisDataObjsPerLine;
    }

    return 0;
}

int do_memwrite (const void *vaddr, uint32_t DataWidth, uint32_t numDataObjects, uint64_t writeval)
{
	mtdbg ("vaddr           : %p", vaddr);
	mtdbg ("DataWidth       : %u", DataWidth);
	mtdbg ("numDataObjects  : %u", numDataObjects);
	mtdbg ("writeval        : %lu (%016lx)", writeval, writeval);

    while (numDataObjects-- > 0) {
		switch (DataWidth) {
#if ARCH_IS_64BIT
			case 8: *(volatile uint64_t *)(vaddr++) = (uint64_t)writeval; break;
#endif
			case 4: *(volatile uint32_t *)(vaddr++) = (uint32_t)writeval; break;
			case 2: *(volatile uint16_t *)(vaddr++) = (uint16_t)writeval; break;
			case 1: *(volatile uint8_t *)(vaddr++)  = (uint8_t)writeval; break;
		}
		mtdbg ("next vaddr  : %p", vaddr);
    }

    return 0;
}

/* LIMITATIONS: If not kept in mind, this may cause crash and memory corruption!
 * 1. Cannot map more than 4K page in a single run
 * 2. Cannot allow address-dataObjectCount combincation which requires cross page boundaries
 * */
int main (int argc, char **argv)
{
/* Check for 'count' field in md/mw command
 *  - args for md = 3 (md.l, addr, count)
 *  - args for mw = 4 (mw.l, addr, val, count)
 * OpIsWrite variable adds 1 to argument count limits as below 
 * */
#define COUNT_ARG_AVAIL(OpIsWrite) (argc == (3 + OpIsWrite))

	/*variables used commonly for md/mw handler functions*/
	uint32_t DataWidth; /*refer SetupDataWidth*/
	char *cmd; /*md.X or mw.X (X is nothing or b or w or l or q)*/
	int OpIsWrite; /*0 for read; 1 for write*/
	uint32_t paddr; /*physical address given as cmd-line argument*/
	void * vaddr; /*memory mapped address for given paddr*/
	uint32_t numDataObjects; /*number of data objects read-from/written-to memory*/
	uint64_t writeval = 0xc0ffee00deadbeef;
	int fd_devmem;
	int ret;

	cmd = argv[0];

	/* The binary is allowed to be invoked in either of following ways only:
	 * 1. ./md.l addr count
	 *    Go to container directory and run the application
	 * 2. md.l addr count
	 *    Setup symbolic link in $PATH and invoke like linux command
	 * We skip ./ from case 1 and make sure the cmd is finally like md.l */
	if (0 == strncmp ("./", cmd, 2)) {
		cmd += 2;
	}

	/*Store the IO type - read or write - in OpIsWrite flag*/
	OpIsWrite = ((cmd[1] == 'd') ? 0 /*read*/ : 1/*write*/ );

	/*Validate number of arguments now*/
	if (!ARG_CNT_IS_VALID (argc, OpIsWrite)) {
		PrintUsage (OpIsWrite);
		return 0;
	}

	/*Validate and setup DataWidth as per binary/command invoked*/
	SetupDataWidth (cmd, &DataWidth);

	/*Setup data object count as per argument and md/mw operation*/
	if (COUNT_ARG_AVAIL (OpIsWrite)) {
		/*Count argument is available, we have to do IO based on count*/
		numDataObjects = strtoul (argv[2 + OpIsWrite], NULL, 10);
		if (errno) {
			perror ("strtoul expects integer for count argument");
			return -errno;
		}
	} else {
		/*Assign default data-object count for IO*/
		if (OpIsWrite) {
			/*For write operation, set default count as 1*/
			numDataObjects = 1;
		} else {
			/*For read operation, set default count as DEFAULT_NUM_DATA_OBJECTS*/
			numDataObjects = DEFAULT_NUM_DATA_OBJECTS;
		}
	}

	/*Get the 32-bit physical address from argv[2] string*/
	paddr = strtoul (argv[1], NULL, 16);
	/*Try memory map with physical address given*/
	ret = SetupMemoryMap (paddr, &fd_devmem, &vaddr);
	if (ret != 0) {
		return ret;
	}

	if (!OpIsWrite) {
		/*This is memory display request*/
		do_memread (paddr, vaddr, DataWidth, numDataObjects);
	} else {
		/*This is memory write request*/
		writeval = strtoull (argv[2], NULL, 16);
		do_memwrite (vaddr, DataWidth, numDataObjects, writeval);
	}

	munmap ((void *)((uint32_t)vaddr & ~MAP_MASK), MAP_SIZE);
	close (fd_devmem);
	return 0;
}
