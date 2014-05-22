// path:
// 4 bits - generic file type (Qctl, Qevent)
// 3 bits - parent type
// 3 bits - chosen scheme type (Qgeneric, Qbcm, Qboard, Qwpi)
// 6 bits - input number

#define PIN_TABLE_SIZE	32

#define PIN_OFFSET		SCHEME_OFFSET + SCHEME_BITS
#define PIN_BITS		6
#define PIN_MASK		((1 << PIN_BITS) - 1)
#define PIN_NUMBER(q)	(((q).path >> PIN_OFFSET) & PIN_MASK)

#define SCHEME_OFFSET	PARENT_OFFSET + PARENT_BITS
#define SCHEME_BITS	3
#define SCHEME_MASK	((1 << SCHEME_BITS) - 1)
#define SCHEME_TYPE(q)	(((q).path >> SCHEME_OFFSET) & SCHEME_MASK)

#define PARENT_OFFSET	FILE_OFFSET + FILE_BITS
#define PARENT_BITS	3
#define PARENT_MASK	((1 << PARENT_BITS) - 1)
#define PARENT_TYPE(q)	(((q).path >> PARENT_OFFSET) & PARENT_MASK)

#define FILE_OFFSET	0
#define FILE_BITS		4
#define FILE_MASK		((1 << FILE_BITS) - 1)
#define FILE_TYPE(q)	(((q).path >> FILE_OFFSET) & FILE_MASK)

// pin is valid only when file is Qdata otherwise 0 is used
#define PATH(pin, scheme, parent, file) \
((pin & PIN_MASK) << PIN_OFFSET) \
| ((scheme & SCHEME_MASK) << SCHEME_OFFSET) \
| ((parent & PARENT_MASK) << PARENT_OFFSET) \
| ((file & FILE_MASK) << FILE_OFFSET)

#define SET_BIT(f, offset, value) \
(*f = ((*f & ~(1 << (offset % 32))) | (value << (offset % 32))))

static int dflag = 0;

#define D(...)	if(dflag) print(__VA_ARGS__)

#define SPI_READ(offset) (((u32int*)SPIREGS)[offset])
#define SPI_WRITE(offset, value) (((u32int*)SPIREGS)[offset] = value)

enum {
	// parent types
	Qtopdir = 0,
	Qgpiodir,
	Qspidir,
	// file types
	Qdir,
	Qdata,
	Qctl,
	Qevent,
	Qspi0,
	Qspi1,
};
enum {
	// naming schemes
	Qbcm,
	Qboard,
	Qwpi,
	Qgeneric
};

enum {
	// commands
	CMzero,
	CMone,
	CMscheme,
	CMfunc,
	CMpull,
	CMevent,
	CMclock,
	CMmode,
};

// dev entries
Dirtab topdir = { "#G", {PATH(0, Qgeneric, Qtopdir, Qdir), 0, QTDIR}, 0, 0555 };
Dirtab bcmdir[] = {
	"gpio", { PATH(0, Qgeneric, Qgpiodir, Qdir), 0, QTDIR }, 0, 0555,
	"spi", { PATH(0, Qgeneric, Qspidir, Qdir), 0, QTDIR }, 0, 0555,
	"ctl",	{ PATH(0, Qgeneric, Qtopdir, Qctl), 0, QTFILE }, 0, 0666,
};

Dirtab gpiodir[] = {
	"OK", { PATH(16, Qgeneric, Qgpiodir, Qdata), 0, QTFILE }, 0, 0666,
	"event", { PATH(0, Qgeneric, Qgpiodir, Qevent), 0, QTFILE }, 0, 0444,
};

Dirtab spidir[] = {
	"spi0",	{ PATH(0, Qgeneric, Qspidir, Qspi0), 0, QTFILE }, 0, 0666,
	"spi1",	{ PATH(0, Qgeneric, Qspidir, Qspi1), 0, QTFILE }, 0, 0666,
};

// commands definition
static
Cmdtab ctlcmd[] = {
	CMscheme,	"scheme",	2,
	CMfunc, 	"function",	3,
	CMpull,		"pull",		3,
	CMevent,	"event",	4,
	CMclock,	"clock",	3,
	CMmode, 	"mode",		2,
};

static
Cmdtab gpiocmd[] = {
	CMzero,		"0",		1,
	CMone,		"1",		1,
};

static int pinscheme;
static int boardrev;

static Rendez gpiorend, spirend[2];
static u32int gpioeventvalue;
static long gpioeventinuse;
static Lock gpioeventlock;

#define SPI_BUF_LEN 256
static char spimode;
static u32int spibuflen = SPI_BUF_LEN;
static u32int spitxoff[2];
static u32int spirxoff[2];
static u32int spitxlen[2];
static u32int spirxlen[2];
static u8int spitxbuf0[SPI_BUF_LEN];
static u8int spitxbuf1[SPI_BUF_LEN];
static u8int spirxbuf0[SPI_BUF_LEN];
static u8int spirxbuf1[SPI_BUF_LEN];
static u8int *spitxbuf[] = {spitxbuf0, spitxbuf1};
static u8int *spirxbuf[] = {spirxbuf0, spirxbuf1};

//
// BCM
//
enum {
	Fin = 0,
	Fout,
	Ffunc5,
	Ffunc4,
	Ffunc0,
	Ffunc1,
	Ffunc2,
	Ffunc3,
};

static char *funcname[] = {
	"in", "out", "5", "4", "0", "1", "2", "3",
};

enum {
	Poff = 0,
	Pdown,
	Pup,
};

static char *pudname[] = {
	"off", "down", "up",
};

static char *evstatename[] = {
	"disable", "enable",
};

enum {
	Erising,
	Efalling,
};

static char *evtypename[] = {
	"edge-rising", "edge-falling",
};

static char *bcmtableR1[PIN_TABLE_SIZE] = {
	"1", "2", 0, 0,			// 0-3
	"4", 0, 0, "7",			// 4-7
	"8", "9", "10", "11",	// 8-11
	0, 0, "14", "15",		// 12-15
	0, "17", "18", 0,		// 16-19
	0, "21", "22", "23",	// 20-23
	"24", "25", 0, 0,		// 24-27
	0, 0, 0, 0,				// 28-31
};

static char *bcmtableR2[PIN_TABLE_SIZE] = {
	0, 0, "2", "3",			// 0-3
	"4", 0, 0, "7",			// 4-7
	"8", "9", "10", "11",	// 8-11
	0, 0, "14", "15",		// 12-15
	0, "17", "18", 0,		// 16-19
	0, 0, "22", "23",		// 20-23
	"24", "25", 0, "27",	// 24-27
	"28", "29", "30", "31",	// 28-31
};

static char *boardtableR1[PIN_TABLE_SIZE] = {
	"SDA", "SCL", 0, 0,				// 0-3
	"GPIO7", 0, 0, "CE1",			// 4-7
	"CE0", "MISO", "MOSI", "SCLK",	// 8-11
	0, 0, "TxD", "RxD",				// 12-15
	0, "GPIO0", "GPIO1", 0,			// 16-19
	0, "GPIO2", "GPIO3", "GPIO4",	// 20-23
	"GPIO5", "GPIO6", 0, 0,			// 24-27
	0, 0, 0, 0,						// 28-31
};

static char *boardtableR2[PIN_TABLE_SIZE] = {
	0, 0, "SDA", "SCL",						// 0-3
	"GPIO7", 0, 0, "CE1",					// 4-7
	"CE0", "MISO", "MOSI", "SCLK",			// 8-11
	0, 0, "TxD", "RxD",						// 12-15
	0, "GPIO0", "GPIO1", 0,					// 16-19
	0, 0, "GPIO3", "GPIO4",					// 20-23
	"GPIO5", "GPIO6", 0, "GPIO2",			// 24-27
	"GPIO8", "GPIO9", "GPIO10", "GPIO11",	// 28-31
};

static char *wpitableR1[PIN_TABLE_SIZE] = {
	"8", "9", 0, 0,			// 0-3
	"7", 0, 0, "11",		// 4-7
	"10", "13", "12", "14",	// 8-11
	0, 0, "15", "16",		// 12-15
	0, "0", "1", 0,			// 16-19
	0, "2", "3", "4",		// 20-23
	"5", "6", 0, 0,			// 24-27
	0, 0, 0, 0,				// 28-31
};

static char *wpitableR2[PIN_TABLE_SIZE] = {
	0, 0, "8", "9",			// 0-3
	"7", 0, 0, "11",		// 4-7
	"10", "13", "12", "14",	// 8-11
	0, 0, "15", "16",		// 12-15
	0, "0", "1", 0,			// 16-19
	0, 0, "3", "4",			// 20-23
	"5", "6", 0, "2",		// 24-27
	"17", "18", "19", "20",	// 28-31
};

static char *schemename[] = {
	"bcm", "board", "wpi",
};

static char**
getpintable(void);

// stolen from uartmini.c
#define GPIOREGS	(VIRTIO+0x200000)
#define SPIREGS	(VIRTIO+0x204000)

/* GPIO regs */
enum {
	Fsel0	= 0x00>>2,
	FuncMask= 0x7,
	Set0	= 0x1c>>2,
	Clr0	= 0x28>>2,
	Lev0	= 0x34>>2,
	Evds0	= 0x40>>2,
	Redge0	= 0x4C>>2,
	Fedge0	= 0x58>>2,
	Hpin0	= 0x64>>2,
	Lpin0	= 0x70>>2,
	ARedge0	= 0x7C>>2,
	AFedge0	= 0x88>2,
	PUD	= 0x94>>2,
	PUDclk0	= 0x98>>2,
	PUDclk1	= 0x9c>>2,
};

/* SPI regs */
enum {
	CSreg	= 0x00>>2,
	FIFOreg	= 0x04>>2,
	CLKreg	= 0x08>>2,
	DLENreg	= 0x0C>>2,
	LTOHreg	= 0x10>>2,
	DCreg	= 0x14>>2,
};

enum {
	CS_01field	= 0x00000001,
	CS_10field	= 0x00000002,
	CPHAfield	= 0x00000004,
	CPOLfield	= 0x00000008,
	TAfield		= 0x00000080,
	INTDfield	= 0x00000200,
	INTRfield   = 0x00000400,
	DONEfield	= 0x00010000,
	RXDfield	= 0x00020000,
	RXRfield	= 0x00080000,
	
	CSPOL1field	= 0x00400000,
	CSPOL0field = 0x00200000,
	CSPOLfield	= 0x00000040,
	RENfield	= 0x00001000,
	CLRRXfield	= 0x00000020,
	CLRTXfield	= 0x00000010,
};

//
// device functions
//

static void
bcminit(void);

static void
bcmshutdown(void);

static Chan*
bcmattach(char *spec);

static Walkqid*
bcmwalk(Chan *c, Chan *nc, char** name, int nname);

static int
bcmstat(Chan *c, uchar *db, int n);

static Chan*
bcmopen(Chan *c, int omode);

static void
bcmclose(Chan *c);

static long
bcmread(Chan *c, void *va, long n, vlong off);

static long
bcmwrite(Chan *c, void *va, long n, vlong off);

//
// GPIO functions
//

static void
gpiofuncset(uint pin, int func);

static int
gpiofuncget(uint pin);

static void
gpiopullset(uint pin, int state);

static void
gpioout(uint pin, int set);

static int
gpioin(uint pin);

static void
gpioevent(uint pin, int event, int enable);