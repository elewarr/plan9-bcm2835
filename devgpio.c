#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

// path:
// 4 bits - generic file type (Qinctl, Qindata)
// 4 bits - path (Qbcmpindir, Qboardpindir, , ...)
// 8 bits - input number

#define INPUT_NUMBER(q)	(((q).path >> 8) & 0x00ff)
#define PARENT_TYPE(q)		(((q).path >> 4) & 0x000f)
#define FILE_TYPE(q)		((q).path & 0x000f)
#define PATH(s, p, f)			(((s & 0xff) << 8) | ((p & 0x0f) << 4) | (f & 0x0f))

enum {
	Qtopdir = 0,
	Qgpiodir,
	Qbcmdir,
	Qboarddir,
	Qwpidir,
	Qdata,
};

enum {
	CMzero,
	CMone,
	CMfunc,
	CMpull,
};

Dirtab topdir = { "#G", {PATH(0, Qtopdir, 0), 0, QTDIR}, 0, 0555 }; 
Dirtab gpiodir = { "gpio", {PATH(0, Qgpiodir, 0), 0, QTDIR}, 0, 0555 };

Dirtab typedir[] = {
	"bcm",	{ PATH(0, Qbcmdir, 0), 0, QTDIR }, 0, 0555,
	"board",	{ PATH(0, Qboarddir, 0), 0, QTDIR }, 0, 0555,
	"wpi",	{ PATH(0, Qwpidir, 0), 0, QTDIR }, 0, 0555,
	"OK",	{ PATH(16, Qgpiodir, Qdata), 0, QTFILE }, 0, 0666,
};

static
Cmdtab gpiocmd[] = {
	CMzero,		"0",			1,
	CMone,		"1",			1,
	CMfunc, 		"func",		2,
	CMpull,		"pull",		2,
};

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

static
char *funcname[] = {
	"in", "out", "f5", "f4", "f0", "f1", "f2", "f3",
};

enum {
	Poff = 0,
	Pdown,
	Pup,
	Prsrvd,
	Punk,
};

static
char *pudname[] = {
	"off", "down", "up",
};

static int *bcmtable;

static
int bcmtableR1[] = { 
	1, 1, 0, 0,	// 0-3
	1, 0, 0, 1,	// 4-7
	1, 1, 1, 1,	// 8-11
	0, 0, 1, 1,	// 12-15
	1, 1, 1, 0,	// 16-19
	0, 1, 1, 1,	// 20-23
	1, 1, 0, 0,	// 24-27
	0, 0, 0, 0,	// 28-31
};

static
int bcmtableR2[] = { 
	0, 0, 1, 1,	// 0-3
	1, 0, 0, 1,	// 4-7
	1, 1, 1, 1,	// 8-11
	0, 0, 1, 1,	// 12-15
	1, 1, 1, 0,	// 16-19
	0, 0, 1, 1,	// 20-23
	1, 1, 0, 1,	// 24-27
	1, 1, 1, 1,	// 28-31
};

static char **nametable;

static
char *nametableR1[] = {
	"SDA", "SCL", 0, 0,					// 0-3
	"GPIO7", 0, 0, "CE1",					// 4-7
	"CE0", "MISO", "MOSI", "SCLK",			// 8-11
	0, 0, "TxD", "RxD",					// 12-15
	0, "GPIO0", "GPIO1", 0,				// 16-19
	0, "GPIO2", "GPIO3", "GPIO4",			// 20-23
	"GPIO5", "GPIO6", 0, 0,				// 24-27
	0, 0, 0, 0,							// 28-31
};

static
char *nametableR2[] = {
	0, 0, "SDA", "SCL",					// 0-3
	"GPIO7", 0, 0, "CE1",					// 4-7
	"CE0", "MISO", "MOSI", "SCLK",			// 8-11
	0, 0, "TxD", "RxD",					// 12-15
	0, "GPIO0", "GPIO1", 0,				// 16-19
	0, 0, "GPIO3", "GPIO4",				// 20-23
	"GPIO5", "GPIO6", 0, "GPIO2",			// 24-27
	"GPIO8", "GPIO9", "GPIO10", "GPIO11",	// 28-31
};

static int *wpitable;

static
int wpitableR1[] = { 
	8, 9, -1, -1,	// 0-3
	7, -1, -1, 11,	// 4-7
	10, 13, 12, 14,	// 8-11
	-1, -1, 15, 16,	// 12-15
	-1, 0, 1, -1,	// 16-19
	-1, 2, 3, 4,	// 20-23
	5, 6, -1, -1,	// 24-27
	-1, -1, -1, -1,	// 28-31
};

static
int wpitableR2[] = {
	-1, -1, 8, 9,	// 0-3
	7, -1, -1, 11,	// 4-7
	10, 13, 12, 14,	// 8-11
	-1, -1, 15, 16,	// 12-15
	-1, 0, 1, -1,	// 16-19
	-1, -1, 3, 4,	// 20-23
	5, 6, -1, 2,	// 24-27
	17, 18, 19, 20,	// 28-31
};

// stolen from uartmini.c
#define GPIOREGS	(VIRTIO+0x200000)
/* GPIO regs */
enum {
	Fsel0	= 0x00>>2,
		FuncMask= 0x7,
	Set0	= 0x1c>>2,
	Clr0	= 0x28>>2,
	Lev0	= 0x34>>2,
	PUD	= 0x94>>2,
	PUDclk0	= 0x98>>2,
	PUDclk1	= 0x9c>>2,
};

static void
gpiofuncset(uint pin, int func)
{	
	u32int *gp, *fsel;
	int off;

	gp = (u32int*)GPIOREGS;
	fsel = &gp[Fsel0 + pin/10];
	off = (pin % 10) * 3;
	*fsel = (*fsel & ~(FuncMask<<off)) | func<<off;
}

static int
gpiofuncget(uint pin)
{	
	u32int *gp, *fsel;
	int off;

	gp = (u32int*)GPIOREGS;
	fsel = &gp[Fsel0 + pin/10];
	off = (pin % 10) * 3;
	return ((*fsel >> off) & FuncMask);
}

static void
gpiopullset(uint pin, int state)
{
	u32int *gp, *reg;
	u32int mask;

	gp = (u32int*)GPIOREGS;
	reg = &gp[PUDclk0 + pin/32];
	mask = 1 << (pin % 32);
	gp[PUD] = state;
	microdelay(1);
	*reg = mask;
	microdelay(1);
	*reg = 0;
}

static void
gpioout(uint pin, int set)
{
	u32int *gp;
	int v;

	gp = (u32int*)GPIOREGS;
	v = set? Set0 : Clr0;
	gp[v + pin/32] = 1 << (pin % 32);
}

static int
gpioin(uint pin)
{
	u32int *gp;

	gp = (u32int*)GPIOREGS;
	return (gp[Lev0 + pin/32] & (1 << (pin % 32))) != 0;
}

static void
mkdeventry(Chan *c, Qid qid, Dirtab *tab, Dir *db)
{
	mkqid(&qid, tab->qid.path, tab->qid.vers, tab->qid.type);
	devdir(c, qid, tab->name, tab->length, eve, tab->perm, db);
}

static int
gpiogen(Chan *c, char *, Dirtab *, int , int s, Dir *db)
{
	Qid qid;
	int t;

	qid.vers = 0;
	t = PARENT_TYPE(c->qid);

	if(s == DEVDOTDOT)
	{
		switch(t)
		{
		case Qtopdir:
		case Qgpiodir:
			mkdeventry(c, qid, &topdir, db);
			break;
		case Qbcmdir:
		case Qboarddir:
		case Qwpidir:
			mkdeventry(c, qid, &gpiodir, db);
			break;
		default:
			return -1;
		}
		return 1;
	}

	if(t == Qtopdir)
	{
		switch(s)
		{
		case 0:
			mkdeventry(c, qid, &gpiodir, db);
			break;
		default:
			return -1;
		}
		return 1;
	} else
	if(t == Qgpiodir)
	{
		if(s < nelem(typedir))
		{
			mkdeventry(c, qid, &typedir[s], db);
		}
		else
		{
			return -1;
		}
		return 1;
	} else
	if(t == Qbcmdir)
	{
		if(s < nelem(bcmtableR1))
		{
			if(bcmtable[s] == 0)
			{
				return 0;
			}
			mkqid(&qid, PATH(s, t, Qdata), 0, QTFILE);
			snprint(up->genbuf, sizeof up->genbuf, "%d", s);
			devdir(c, qid, up->genbuf, 0, eve, 0666, db);
		}
		else
		{
			return -1;
		}
		return 1;
	} else
	if(t == Qboarddir)
	{
		if(s < nelem(nametableR1))
		{
			if(nametable[s] == 0)
			{
				return 0;
			}
			mkqid(&qid, PATH(s, t, Qdata), 0, QTFILE);
			snprint(up->genbuf, sizeof up->genbuf, "%s", nametable[s]);
			devdir(c, qid, up->genbuf, 0, eve, 0666, db);
		}
		else
		{
			return -1;
		}
		return 1;
	} else
	if(t == Qwpidir)
	{
		if(s < nelem(wpitableR1))
		{
			if(wpitable[s] == -1)
			{
				return 0;
			}
			mkqid(&qid, PATH(s, t, Qdata), 0, QTFILE);
			snprint(up->genbuf, sizeof up->genbuf, "%d", wpitable[s]);
			devdir(c, qid, up->genbuf, 0, eve, 0666, db);
		}
		else
		{
			return -1;
		}
		return 1;
	}

	return 1;
}

static void
gpioinit(void)
{
	if(nelem(bcmtableR2) != nelem(bcmtableR1))
	{
		panic("gpio: different BCM table sizes: %d vs %d", nelem(bcmtableR1), nelem(bcmtableR2));
	}

	if(nelem(nametableR2) != nelem(nametableR1))
	{
		panic("gpio: different name table sizes: %d vs %d", nelem(nametableR1), nelem(nametableR2));
	}

	if(nelem(wpitableR2) != nelem(wpitableR1))
	{
		panic("gpio: different Witing Pi table sizes: %d vs %d", nelem(wpitableR1), nelem(wpitableR2));
	}

	if((nelem(bcmtableR1) != nelem(nametableR1)) || (nelem(bcmtableR1) != nelem(wpitableR1)))
	{
		panic("gpio: different table sizes: %d vs %d vs %d", nelem(bcmtableR1), nelem(nametableR1), nelem(wpitableR1));
	}

	if(getrevision() & 0xff > 3)
	{
		bcmtable = bcmtableR2;
		nametable = nametableR2;
		wpitable = wpitableR2;
	}
	else
	{
		bcmtable = bcmtableR1;
		nametable = nametableR1;
		wpitable = wpitableR1;
	}
}

static void
gpioshutdown(void)
{
}

static Chan*
gpioattach(char *spec)
{
	return devattach('G', spec);
}

static Walkqid*
gpiowalk(Chan *c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, gpiogen);
}

static int
gpiostat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, 0, 0, gpiogen);
}

static Chan*
gpioopen(Chan *c, int omode)
{
	return devopen(c, omode, 0, 0, gpiogen);
}

static void
gpioclose(Chan *)
{ }

static long
readdata(uint pin, char *buf, long n)
{
	snprint(
		buf, n,
		"%d", gpioin(pin));
	return strlen(buf);
}

static long
gpioread(Chan *c, void *va, long n, vlong off)
{
	int t, j;
	uint pin;
	ulong offset;
	char *a;

	a = va;
	offset = off;
	j = 0;

	if(c->qid.type & QTDIR)
	{
		return devdirread(c, va, n, 0, 0, gpiogen);
	}

	t = FILE_TYPE(c->qid);

	switch(t)
	{
	case Qdata:
		pin = INPUT_NUMBER(c->qid);
		j = readdata(pin, up->genbuf, sizeof up->genbuf);
		break;
	}

	if(off >= j)
	{
		return 0;
	} else
	if(offset + n > j)
	{
		n = j - offset;
	}
	memmove(a, &up->genbuf[offset], n);
	return n;
}

static long
gpiowrite(Chan *c, void *va, long n, vlong)
{
	int t, i;
	uint pin;
	char *arg;

	Cmdbuf *cb;
	Cmdtab *ct;

	if(c->qid.type & QTDIR)
	{
		error(Eisdir);
	}

	t = FILE_TYPE(c->qid);
	switch(t)
	{
	case Qdata:
		pin = INPUT_NUMBER(c->qid);
		cb = parsecmd(va, n);
		if(waserror())
		{
			free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, gpiocmd,  nelem(gpiocmd));

		switch(ct->index)
		{
		case CMzero:
			gpioout(pin, 0);
			break;
		case CMone:
			gpioout(pin, 1);
			break;
		case CMfunc:
			arg = cb->f[1];
			for(i = 0; i < nelem(funcname); i++)
			{
				if(strncmp(funcname[i], arg, strlen(funcname[i])) == 0)
				{
					gpiofuncset(pin, i);
					break;
				}
			}
			break;
		case CMpull:
			arg = cb->f[1];
			for(i = 0; i < nelem(pudname); i++)
			{
				if(strncmp(pudname[i], arg, strlen(pudname[i])) == 0)
				{
					gpiopullset(pin, i);
					break;
				}
			}
			break;
		}
		poperror();
		free(cb);
	}
	return n;
}

Dev gpiodevtab = {
	'G',
	"gpio",

	devreset,
	gpioinit,
	gpioshutdown,
	gpioattach,
	gpiowalk,
	gpiostat,
	gpioopen,
	devcreate,
	gpioclose,
	gpioread,
	devbread,
	gpiowrite,
	devbwrite,
	devremove,
	devwstat,
};