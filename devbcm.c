#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "devbcm.h"

//
// generic functions
//

static void
mkdeventry(Chan *c, Qid qid, Dirtab *tab, Dir *db)
{
	mkqid(&qid, tab->qid.path, tab->qid.vers, tab->qid.type);
	devdir(c, qid, tab->name, tab->length, eve, tab->perm, db);
}

static char**
getpintable(void)
{
	switch(pinscheme)
	{
		case Qbcm:
			return (boardrev>3)?bcmtableR2:bcmtableR1;
		case Qboard:
			return (boardrev>3)?boardtableR2:boardtableR1;
		case Qwpi:
			return (boardrev>3)?wpitableR2:wpitableR1;
		default:
			return nil;
	}
}

static int
getpin(char *pinname)
{
	int i;
	char **pintable = getpintable();
	for(i = 0; i < PIN_TABLE_SIZE; i++)
	{
		if(!pintable[i])
		{
			continue;
		}
		if(strncmp(pintable[i], pinname, strlen(pintable[i])) == 0)
		{
			return i;
		}
	}
	return -1;
}

//
// top dir functions
//

static long
bcmtopread(Chan *c, void *va, long n, vlong)
{
	USED(n, va);
	int type = FILE_TYPE(c->qid);
	switch(type)
	{
		case Qctl:
			break;
	}
	return 0;
}

static long
bcmtopwrite(Chan *c, void *va, long n, vlong)
{
	int type, i, pin, mode;
	const char *arg;
	
	type = FILE_TYPE(c->qid);
	
	Cmdbuf *cb;
	Cmdtab *ct;
	
	cb = parsecmd(va, n);
	if(waserror())
	{
		free(cb);
		nexterror();
	}
	
	ct = lookupcmd(cb, ctlcmd,  nelem(ctlcmd));
	if(ct == nil)
	{
		error(Ebadctl);
	}
	
	switch(type)
	{
		case Qctl:
			switch(ct->index)
		{
			case CMscheme:
				arg = cb->f[1];
				for(i = 0; i < nelem(schemename); i++)
				{
					if(strncmp(schemename[i], arg, strlen(schemename[i])) == 0)
					{
						pinscheme = i;
						break;
					}
				}
				break;
			case CMfunc:
				pin = getpin(cb->f[2]);
				arg = cb->f[1];
				if(pin == -1) {
					error(Ebadctl);
				}
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
				pin = getpin(cb->f[2]);
				if(pin == -1) {
					error(Ebadctl);
				}
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
			case CMevent:
				pin = getpin(cb->f[3]);
				if(pin == -1) {
					error(Ebadctl);
				}
				
				arg = cb->f[1];
				for(i = 0; i < nelem(evtypename); i++)
				{
					if(strncmp(evtypename[i], arg, strlen(evtypename[i])) == 0)
					{
						gpioevent(pin, i, (cb->f[2][0] == 'e'));
						break;
					}
				}
				break;
			case CMmode:
				arg = cb->f[1];
				mode = atoi(arg);
				
				switch(mode)
			{
				case 0:
					spimode = 0;
					break;
				case 1:
					spimode = 0 | CPHAfield;
					break;
				case 2:
					spimode = CPOLfield | 0;
					break;
				case 3:
					spimode = CPOLfield | CPHAfield;
					break;
				default:
					error(Ebadctl);
					break;
			}
				break;
			default:
				error(Ebadctl);
		}
			break;
	}
	free(cb);
	poperror();
	
	return n;
}
//
// GPIO functions
//

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
gpioevent(uint pin, int event, int enable)
{
	u32int *gp, *field;
	int reg = 0;
	
	switch(event)
	{
		case Erising:
			reg = Redge0;
			break;
		case Efalling:
			reg = Fedge0;
			break;
		default:
			panic("gpio: unknown event type");
	}
	gp = (u32int*)GPIOREGS;
	field = &gp[reg + pin/32];
	SET_BIT(field, pin, enable);
}

static void
gpioint(Ureg*, void *)
{
	
	u32int *gp, *field;
	char pin;
	
	gp = (u32int*)GPIOREGS;
	
	int set;
	
	coherence();
	
	gpioeventvalue = 0;
	
	for(pin = 0; pin < PIN_TABLE_SIZE; pin++)
	{
		set = (gp[Evds0 + pin/32] & (1 << (pin % 32))) != 0;
		
		if(set)
		{
			field = &gp[Evds0 + pin/32];
			SET_BIT(field, pin, 1);
			SET_BIT(&gpioeventvalue, pin, 1);
		}
	}
	coherence();
	
	wakeup(&gpiorend);
}

static int
gpioiseventset(void *)
{
	return gpioeventvalue;
}

static long
bcmgpioread(Chan *c, void *va, long n, vlong off)
{
	int type = FILE_TYPE(c->qid);
	uint pin;
	ulong offset = off;
	char *a = va;
	
	switch(type)
	{
		case Qdata:
			pin = PIN_NUMBER(c->qid);
			a[0] = (gpioin(pin))?'1':'0';
			n = 1;
			break;
		case Qevent:
			if(offset >= 4)
			{
				offset %= 4;
				gpioeventvalue = 0;
			}
			sleep(&gpiorend, gpioiseventset, 0);
			
			if(offset + n > 4)
			{
				n = 4 - off;
			}
			memmove(a, &gpioeventvalue + offset, n);
	}
	return n;
}

static long
bcmgpiowrite(Chan *c, void *va, long n, vlong)
{
	uint pin;
	int type = FILE_TYPE(c->qid);
	
	Cmdbuf *cb;
	Cmdtab *ct;
	
	cb = parsecmd(va, n);
	if(waserror())
	{
		free(cb);
		nexterror();
	}
	
	ct = lookupcmd(cb, gpiocmd, nelem(gpiocmd));
	if(ct == nil)
	{
		error(Ebadctl);
	}
	
	switch(type)
	{
		case Qdata:
			pin = PIN_NUMBER(c->qid);
			
			switch(ct->index)
		{
			case CMzero:
				gpioout(pin, 0);
				break;
			case CMone:
				gpioout(pin, 1);
				break;
			default:
				error(Ebadctl);
		}
			break;
			
	}
	free(cb);
	poperror();
	
	return n;
}

//
// SPI functions
//

static void
spiwrite(int deviceno, ulong n)
{
	u32int i;
	u8int data;
	for(i = 0; i < n; i++)
	{
		if(spitxoff[deviceno] >= spitxlen[deviceno])
		{
			//			data = 0;
			//			D("DUMMY WR dev=%d %d\n", deviceno, data);
			//			SPI_WRITE(FIFOreg, data);
			break;
		}
		else
		{
			data = spitxbuf[deviceno][spitxoff[deviceno]++];
			SPI_WRITE(FIFOreg, data);
			//			D("WR[%ud]: dev=%d %x\n", spitxoff[deviceno] - 1, deviceno, data);
		}
	}
}

static void
spiread(int deviceno, ulong n)
{
	u32int i;
	u8int data;
	for(i = 0; i < n; i++)
	{
		if(spirxlen[deviceno] >= spitxlen[deviceno])
		{
			//			data = SPI_READ(FIFOreg);
			//			D("DUMMY RD dev=%d %d\n", deviceno, data);
			break;
		}
		else
		{
			data = SPI_READ(FIFOreg);
			spirxbuf[deviceno][spirxlen[deviceno]++] = data;
			//			D("RD[%ud]: dev=%d %x\n", spirxlen[deviceno] - 1, deviceno, data);
		}
	}
}

static void
spiint(Ureg*, void *)
{
	coherence();
	
	u32int cs = SPI_READ(CSreg);
	int deviceno = (cs & 0x03);
	// If RXR is set read 12 bytes data from SPI_FIFO and if more data to write, write up to
	// 12 bytes to SPIFIFO.
	if(cs & RXRfield)
	{
		//		D("RXR set: %d\n", deviceno);
		spiread(deviceno, 12);
		spiwrite(deviceno, 12);
		return;
	}
	
	// If DONE is set and data to write (this means it is the first interrupt), write up to 16
	// bytes to SPI_FIFO. If DONE is set and no more data, set TA = 0. Read trailing data
	// from SPI_FIFO until RXD is 0.
	if(cs & DONEfield)
	{
		//		D("DONE set: %d\n", deviceno);
		if(spitxoff[deviceno] < spitxlen[deviceno])
		{
			//			D("DONE first\n");
			spiwrite(deviceno, 16);
		}
		else
		{
			//			D("DONE next\n");
			cs = SPI_READ(CSreg);
			cs &= ~(INTRfield | INTDfield | TAfield);
			SPI_WRITE(CSreg, cs);
			//			D("intr, disable INTR|INTD: %x\n", SPI_READ(CSreg));
			
			while(cs & RXDfield)
			{
				spiread(deviceno, 1);
				cs = SPI_READ(CSreg);
			}
			wakeup(&spirend[deviceno]);
		}
	}
}

static int
spirxbufferfilled0(void *)
{
	return spirxlen[0] == spitxlen[0];
}

static int
spirxbufferfilled1(void *)
{
	return spirxlen[1] == spitxlen[1];
}

static long
bcmspiread(Chan *c, void *va, long n, vlong)
{
	int type = FILE_TYPE(c->qid);
	int deviceno = 0;
	
	switch(type)
	{
		case Qspi0:
			deviceno = 0;
			break;
		case Qspi1:
			deviceno = 1;
			break;
		default:
			error(Ebadctl);
			break;
	}
	
	n = MIN(spirxlen[deviceno] - spirxoff[deviceno], n);
	if(n > 0)
	{
		memmove(va, (spirxbuf[deviceno] + spirxoff[deviceno]), n);
		spirxoff[deviceno] += n;
	}
	return n;
}

static long
bcmspiwrite(Chan *c, void *va, long n, vlong)
{
	int (*f)(void*) = 0;
	int deviceno = -1;
	
	int type = FILE_TYPE(c->qid);
		
	switch(type)
	{
		case Qspi0:
			deviceno = 0;
			f = spirxbufferfilled0;
			break;
		case Qspi1:
			deviceno = 1;
			f = spirxbufferfilled1;
			break;
	}
	
	if(deviceno != -1)
	{
		spitxoff[deviceno] = 0;
		spirxoff[deviceno] = 0;
		spirxlen[deviceno] = 0;
		
		SPI_WRITE(CLKreg, (u32int)6);
		
		spitxlen[deviceno] = MIN(SPI_BUF_LEN, n);
		//				D("WRite dev=%d n=%lud, off=%lud, len=%ud\n", deviceno, n, offset, spitxlen[deviceno]);
		memmove(spitxbuf[deviceno], va, spitxlen[deviceno]);
		//				spitxbuf[deviceno] = va;
		n = spitxlen[deviceno];
		
		u32int cs = 0;
		cs |= CLRRXfield | CLRTXfield;
		SPI_WRITE(CSreg, cs);
		
		//				coherence();
		//				cs = 0;
		//				SPI_WRITE(CSreg, cs);
		
		//				D("cleared CS: %x\n", SPI_READ(CSreg));
		
		//				cs = SPI_READ(CSreg);
		cs = 0;
		cs |= spimode;
		//				SPI_WRITE(CSreg, cs);
		//				D("data mode: %x\n", SPI_READ(CSreg));
		//
		//				cs = SPI_READ(CSreg);
		cs |= deviceno;
		//				SPI_WRITE(CSreg, cs);
		//				D("select device: %x\n", SPI_READ(CSreg));
		//
		//				cs = SPI_READ(CSreg);
		cs |= INTRfield | INTDfield;
		//				SPI_WRITE(CSreg, cs);
		//				D("enable interrupts: %x\n", SPI_READ(CSreg));
		//
		//				cs = SPI_READ(CSreg);
		cs |= TAfield;
		//				cs |= CSPOL0field << deviceno;
		SPI_WRITE(CSreg, cs);
		//				D("enable TA: %x\n", SPI_READ(CSreg));
		
		//				D("WRite SLEEP dev=%d\n", deviceno);
		sleep(&spirend[deviceno], f, 0);
		//				D("WRite DONE dev=%d\n", deviceno);
		
		//				cs = SPI_READ(CSreg);
		//				cs &= ~TAfield;
		//				cs |= CSPOL0field << deviceno;
		//				SPI_WRITE(CSreg, cs);
		//				D("disable TA: %x\n", SPI_READ(CSreg));
		
	}
	return n;
}

//
// device functions
//

static int
bcmgen(Chan *c, char *, Dirtab *, int , int s, Dir *db)
{
	Qid qid;
	int parent, scheme, l;
	char **pintable = getpintable();
	
	qid.vers = 0;
	parent = PARENT_TYPE(c->qid);
	scheme = SCHEME_TYPE(c->qid);
	
	if(s == DEVDOTDOT)
	{
		switch(parent)
		{
			case Qtopdir:
			case Qgpiodir:
			case Qspidir:
				mkdeventry(c, qid, &topdir, db);
				break;
			default:
				return -1;
		}
		return 1;
	}
	
	if(parent == Qtopdir)
	{
		if(s < nelem(bcmdir))
		{
			mkdeventry(c, qid, &bcmdir[s], db);
		}
		else
		{
			return -1;
		}
		return 1;
	}
	
	if(scheme != Qgeneric && scheme != pinscheme)
	{
		error(Enotconf);
	}
	
	if(parent == Qgpiodir)
	{
		l = nelem(gpiodir);
		if(s < l)
		{
			mkdeventry(c, qid, &gpiodir[s], db);
		} else if (s < l + PIN_TABLE_SIZE)
		{
			s -= l;
			
			if(pintable[s] == 0)
			{
				return 0;
			}
			mkqid(&qid, PATH(s, pinscheme, Qgpiodir, Qdata), 0, QTFILE);
			snprint(up->genbuf, sizeof up->genbuf, "%s", pintable[s]);
			devdir(c, qid, up->genbuf, 0, eve, 0666, db);
		}
		else
		{
			return -1;
		}
		return 1;
	}
	else if(parent == Qspidir)
	{
		l = nelem(spidir);
		if(s < l)
		{
			mkdeventry(c, qid, &spidir[s], db);
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
bcminit(void)
{
	boardrev = getrevision() & 0xff;
	pinscheme = Qboard;
	intrenable(IRQgpio1, gpioint, nil, 0, "gpio1");
	intrenable(IRQspi, spiint, nil, 0, "spi");
}

static void
bcmshutdown(void)
{ }

static Chan*
bcmattach(char *spec)
{
	return devattach('G', spec);
}

static Walkqid*
bcmwalk(Chan *c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, bcmgen);
}

static int
bcmstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, 0, 0, bcmgen);
}

static Chan*
bcmopen(Chan *c, int omode)
{
	int type, parent;
	
	c = devopen(c, omode, 0, 0, bcmgen);
	
	parent = PARENT_TYPE(c->qid);
	type = FILE_TYPE(c->qid);
	
	if(parent == Qtopdir)
	{
		switch(type)
		{
			case Qctl:
				break;
		}
	}
	else if(parent == Qgpiodir)
	{
		switch(type)
		{
			case Qdata:
				c->iounit = 1;
				break;
			case Qevent:
				lock(&gpioeventlock);
				if(gpioeventinuse != 0){
					c->flag &= ~COPEN;
					unlock(&gpioeventlock);
					error(Einuse);
				}
				gpioeventinuse = 1;
				unlock(&gpioeventlock);
				gpioeventvalue = 0;
				c->iounit = 4;
		}
	}
	else if(parent == Qspidir)
	{
		switch(type)
		{
			case Qspi0:
			case Qspi1:
				gpiofuncset(7, 4);
				gpiofuncset(8, 4);
				gpiofuncset(9, 4);
				gpiofuncset(10, 4);
				gpiofuncset(11, 4);
				
				break;
		}
	}
	return c;
}

static void
bcmclose(Chan *c)
{
	int type, parent;
	
	parent = PARENT_TYPE(c->qid);
	type = FILE_TYPE(c->qid);
	
	if(parent == Qtopdir)
	{
		switch(type)
		{
			case Qctl:
				break;
		}
		
	}
	else if(parent == Qgpiodir)
	{
		switch(type)
		{
			case Qevent:
				if(c->flag & COPEN)
				{
					if(c->flag & COPEN){
						gpioeventinuse = 0;
					}
				}
				break;
		}
	}
	else if(parent == Qspidir)
	{
		switch(type)
		{
			case Qspi0:
				break;
			case Qspi1:
				break;
		}
	}
	
}

static long
bcmread(Chan *c, void *va, long n, vlong off)
{
	if(c->qid.type & QTDIR)
	{
		return devdirread(c, va, n, 0, 0, bcmgen);
	}
	
	int parent = PARENT_TYPE(c->qid);
	
	switch(parent)
	{
		case Qtopdir:
			n = bcmtopread(c, va, n, off);
			break;
		case Qgpiodir:
			n = bcmgpioread(c, va, n, off);
			break;
		case Qspidir:
			n = bcmspiread(c, va, n, off);
			break;
		default:
			error(Ebadctl);
	}
	
	return n;
}

static long
bcmwrite(Chan *c, void *va, long n, vlong off)
{
	if(c->qid.type & QTDIR)
	{
		error(Eisdir);
	}
	
	int parent = PARENT_TYPE(c->qid);
	
	switch(parent)
	{
		case Qtopdir:
			n = bcmtopwrite(c, va, n, off);
			break;
		case Qgpiodir:
			n = bcmgpiowrite(c, va, n, off);
			break;
		case Qspidir:
			n = bcmspiwrite(c, va, n, off);
			break;
		default:
			error(Ebadctl);
	}
	return n;
}

Dev bcmdevtab = {
	'G',
	"bcm",
	
	devreset,
	bcminit,
	bcmshutdown,
	bcmattach,
	bcmwalk,
	bcmstat,
	bcmopen,
	devcreate,
	bcmclose,
	bcmread,
	devbread,
	bcmwrite,
	devbwrite,
	devremove,
	devwstat,
};
