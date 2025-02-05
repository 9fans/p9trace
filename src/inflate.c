#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trace.h"

typedef unsigned long uint32;
typedef unsigned char uint8;

enum {
	HistorySize=	32*1024,
	BufSize=	4*1024,
	MaxHuffBits=	17,	/* maximum bits in a encoded code */
	Nlitlen=	288,	/* number of litlen codes */
	Noff=		32,	/* number of offset codes */
	Nclen=		19,	/* number of codelen codes */
	LenShift=	10,	/* code = len<<LenShift|code */
	LitlenBits=	7,	/* number of bits in litlen decode table */
	OffBits=	6,	/* number of bits in offset decode table */
	ClenBits=	6,	/* number of bits in code len decode table */
	MaxFlatBits=	LitlenBits,
	MaxLeaf=	Nlitlen
};

enum
{
	FlateOk			= 0,
	FlateNoMem		= -1,
	FlateInputFail		= -2,
	FlateOutputFail		= -3,
	FlateCorrupted		= -4,
	FlateInternal		= -5,
};

typedef struct Input	Input;
typedef struct History	History;
typedef struct Huff	Huff;
typedef struct Block	Block;

struct Block
{
	uint8	*pos;
	uint8	*limit;
};

struct Input
{
	int	error;		/* first error encountered, or FlateOk */
	void	*wr;
	int	(*w)(void*, void*, int);
	void	*getr;
	int	(*get)(void*);
	uint32	sreg;
	int	nbits;
};

struct History
{
	uint8	his[HistorySize];
	uint8	*cp;		/* current pointer in history */
	int	full;		/* his has been filled up at least once */
};

struct Huff
{
	int	maxbits;	/* max bits for any code */
	int	minbits;	/* min bits to get before looking in flat */
	int	flatmask;	/* bits used in "flat" fast decoding table */
	uint32	flat[1<<MaxFlatBits];
	uint32	maxcode[MaxHuffBits];
	uint32	last[MaxHuffBits];
	uint32	decode[MaxLeaf];
};

/* litlen code words 257-285 extra bits */
static int litlenextra[Nlitlen-257] =
{
/* 257 */	0, 0, 0,
/* 260 */	0, 0, 0, 0, 0, 1, 1, 1, 1, 2,
/* 270 */	2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
/* 280 */	4, 5, 5, 5, 5, 0, 0, 0
};

static int litlenbase[Nlitlen-257];

/* offset code word extra bits */
static int offextra[Noff] =
{
	0,  0,  0,  0,  1,  1,  2,  2,  3,  3,
	4,  4,  5,  5,  6,  6,  7,  7,  8,  8,
	9,  9,  10, 10, 11, 11, 12, 12, 13, 13,
	0,  0,
};
static int offbase[Noff];

/* order code lengths */
static int clenorder[Nclen] =
{
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* for static huffman tables */
static	Huff	litlentab;
static	Huff	offtab;
static	uint8	revtab[256];

static int	uncblock(Input *in, History*);
static int	fixedblock(Input *in, History*);
static int	dynamicblock(Input *in, History*);
static int	sregfill(Input *in, int n);
static int	sregunget(Input *in);
static int	decode(Input*, History*, Huff*, Huff*);
static int	hufftab(Huff*, char*, int, int);
static int	hdecsym(Input *in, Huff *h, int b);

static int
inflateinit(void)
{
	char *len;
	int i, j, base;

	/* byte reverse table */
	for(i=0; i<256; i++)
		for(j=0; j<8; j++)
			if(i & (1<<j))
				revtab[i] |= 0x80 >> j;

	for(i=257,base=3; i<Nlitlen; i++) {
		litlenbase[i-257] = base;
		base += 1<<litlenextra[i-257];
	}
	/* strange table entry in spec... */
	litlenbase[285-257]--;

	for(i=0,base=1; i<Noff; i++) {
		offbase[i] = base;
		base += 1<<offextra[i];
	}

	len = malloc(MaxLeaf);
	if(len == 0)
		return FlateNoMem;

	/* static Litlen bit lengths */
	for(i=0; i<144; i++)
		len[i] = 8;
	for(i=144; i<256; i++)
		len[i] = 9;
	for(i=256; i<280; i++)
		len[i] = 7;
	for(i=280; i<Nlitlen; i++)
		len[i] = 8;

	if(!hufftab(&litlentab, len, Nlitlen, MaxFlatBits)) {
		free(len);
		return FlateInternal;
	}

	/* static Offset bit lengths */
	for(i=0; i<Noff; i++)
		len[i] = 5;

	if(!hufftab(&offtab, len, Noff, MaxFlatBits)) {
		free(len);
		return FlateInternal;
	}

	free(len);

	return FlateOk;
}

static int
inflate(void *wr, int (*w)(void*, void*, int), void *getr, int (*get)(void*))
{
	History *his;
	Input in;
	int final, type, e;
	static int init;

	if(!init) {
		e = inflateinit();
		if(e != FlateOk)
			return e;
		init = 1;
	}

	his = malloc(sizeof(History));
	if(his == 0)
		return FlateNoMem;
	his->cp = his->his;
	his->full = 0;
	in.getr = getr;
	in.get = get;
	in.wr = wr;
	in.w = w;
	in.nbits = 0;
	in.sreg = 0;
	in.error = FlateOk;

	do {
		if(!sregfill(&in, 3))
			goto bad;
		final = in.sreg & 0x1;
		type = (in.sreg>>1) & 0x3;
		in.sreg >>= 3;
		in.nbits -= 3;
		switch(type) {
		default:
			in.error = FlateCorrupted;
			goto bad;
		case 0:
			/* uncompressed */
			if(!uncblock(&in, his))
				goto bad;
			break;
		case 1:
			/* fixed huffman */
			if(!fixedblock(&in, his))
				goto bad;
			break;
		case 2:
			/* dynamic huffman */
			if(!dynamicblock(&in, his))
				goto bad;
			break;
		}
	} while(!final);

	if(his->cp != his->his && (*w)(wr, his->his, his->cp - his->his) != his->cp - his->his) {
		in.error = FlateOutputFail;
		goto bad;
	}

	if(!sregunget(&in))
		goto bad;

	free(his);
	if(in.error != FlateOk)
		return FlateInternal;
	return FlateOk;

bad:
	free(his);
	if(in.error == FlateOk)
		return FlateInternal;
	return in.error;
}

static int
uncblock(Input *in, History *his)
{
	int len, nlen, c;
	uint8 *hs, *hp, *he;

	if(!sregunget(in))
		return 0;
	len = (*in->get)(in->getr);
	len |= (*in->get)(in->getr)<<8;
	nlen = (*in->get)(in->getr);
	nlen |= (*in->get)(in->getr)<<8;
	if(len != (~nlen&0xffff)) {
		in->error = FlateCorrupted;
		return 0;
	}

	hp = his->cp;
	hs = his->his;
	he = hs + HistorySize;

	while(len > 0) {
		c = (*in->get)(in->getr);
		if(c < 0)
			return 0;
		*hp++ = c;
		if(hp == he) {
			his->full = 1;
			if((*in->w)(in->wr, hs, HistorySize) != HistorySize) {
				in->error = FlateOutputFail;
				return 0;
			}
			hp = hs;
		}
		len--;
	}

	his->cp = hp;

	return 1;
}

static int
fixedblock(Input *in, History *his)
{
	return decode(in, his, &litlentab, &offtab);
}

static int
dynamicblock(Input *in, History *his)
{
	Huff *lentab, *offtab;
	char *len;
	int i, j, n, c, nlit, ndist, nclen, res, nb;

	if(!sregfill(in, 14))
		return 0;
	nlit = (in->sreg&0x1f) + 257;
	ndist = ((in->sreg>>5) & 0x1f) + 1;
	nclen = ((in->sreg>>10) & 0xf) + 4;
	in->sreg >>= 14;
	in->nbits -= 14;

	if(nlit > Nlitlen || ndist > Noff || nlit < 257) {
		in->error = FlateCorrupted;
		return 0;
	}

	/* huff table header */
	len = malloc(Nlitlen+Noff);
	lentab = malloc(sizeof(Huff));
	offtab = malloc(sizeof(Huff));
	if(len == 0 || lentab == 0 || offtab == 0){
		in->error = FlateNoMem;
		goto bad;
	}
	for(i=0; i < Nclen; i++)
		len[i] = 0;
	for(i=0; i<nclen; i++) {
		if(!sregfill(in, 3))
			goto bad;
		len[clenorder[i]] = in->sreg & 0x7;
		in->sreg >>= 3;
		in->nbits -= 3;
	}

	if(!hufftab(lentab, len, Nclen, ClenBits)){
		in->error = FlateCorrupted;
		goto bad;
	}

	n = nlit+ndist;
	for(i=0; i<n;) {
		nb = lentab->minbits;
		for(;;){
			if(in->nbits<nb && !sregfill(in, nb))
				goto bad;
			c = lentab->flat[in->sreg & lentab->flatmask];
			nb = c & 0xff;
			if(nb > in->nbits){
				if(nb != 0xff)
					continue;
				c = hdecsym(in, lentab, c);
				if(c < 0)
					goto bad;
			}else{
				c >>= 8;
				in->sreg >>= nb;
				in->nbits -= nb;
			}
			break;
		}

		if(c < 16) {
			j = 1;
		} else if(c == 16) {
			if(in->nbits<2 && !sregfill(in, 2))
				goto bad;
			j = (in->sreg&0x3)+3;
			in->sreg >>= 2;
			in->nbits -= 2;
			if(i == 0) {
				in->error = FlateCorrupted;
				goto bad;
			}
			c = len[i-1];
		} else if(c == 17) {
			if(in->nbits<3 && !sregfill(in, 3))
				goto bad;
			j = (in->sreg&0x7)+3;
			in->sreg >>= 3;
			in->nbits -= 3;
			c = 0;
		} else if(c == 18) {
			if(in->nbits<7 && !sregfill(in, 7))
				goto bad;
			j = (in->sreg&0x7f)+11;
			in->sreg >>= 7;
			in->nbits -= 7;
			c = 0;
		} else {
			in->error = FlateCorrupted;
			goto bad;
		}

		if(i+j > n) {
			in->error = FlateCorrupted;
			goto bad;
		}

		while(j) {
			len[i] = c;
			i++;
			j--;
		}
	}

	if(!hufftab(lentab, len, nlit, LitlenBits)
	|| !hufftab(offtab, &len[nlit], ndist, OffBits)){
		in->error = FlateCorrupted;
		goto bad;
	}

	res = decode(in, his, lentab, offtab);

	free(len);
	free(lentab);
	free(offtab);

	return res;

bad:
	free(len);
	free(lentab);
	free(offtab);
	return 0;
}

static int
decode(Input *in, History *his, Huff *litlentab, Huff *offtab)
{
	int len, off;
	uint8 *hs, *hp, *hq, *he;
	int c;
	int nb;

	hs = his->his;
	he = hs + HistorySize;
	hp = his->cp;

	for(;;) {
		nb = litlentab->minbits;
		for(;;){
			if(in->nbits<nb && !sregfill(in, nb))
				return 0;
			c = litlentab->flat[in->sreg & litlentab->flatmask];
			nb = c & 0xff;
			if(nb > in->nbits){
				if(nb != 0xff)
					continue;
				c = hdecsym(in, litlentab, c);
				if(c < 0)
					return 0;
			}else{
				c >>= 8;
				in->sreg >>= nb;
				in->nbits -= nb;
			}
			break;
		}

		if(c < 256) {
			/* literal */
			*hp++ = c;
			if(hp == he) {
				his->full = 1;
				if((*in->w)(in->wr, hs, HistorySize) != HistorySize) {
					in->error = FlateOutputFail;
					return 0;
				}
				hp = hs;
			}
			continue;
		}

		if(c == 256)
			break;

		if(c > 285) {
			in->error = FlateCorrupted;
			return 0;
		}

		c -= 257;
		nb = litlenextra[c];
		if(in->nbits < nb && !sregfill(in, nb))
			return 0;
		len = litlenbase[c] + (in->sreg & ((1<<nb)-1));
		in->sreg >>= nb;
		in->nbits -= nb;

		/* get offset */
		nb = offtab->minbits;
		for(;;){
			if(in->nbits<nb && !sregfill(in, nb))
				return 0;
			c = offtab->flat[in->sreg & offtab->flatmask];
			nb = c & 0xff;
			if(nb > in->nbits){
				if(nb != 0xff)
					continue;
				c = hdecsym(in, offtab, c);
				if(c < 0)
					return 0;
			}else{
				c >>= 8;
				in->sreg >>= nb;
				in->nbits -= nb;
			}
			break;
		}

		if(c > 29) {
			in->error = FlateCorrupted;
			return 0;
		}

		nb = offextra[c];
		if(in->nbits < nb && !sregfill(in, nb))
			return 0;

		off = offbase[c] + (in->sreg & ((1<<nb)-1));
		in->sreg >>= nb;
		in->nbits -= nb;

		hq = hp - off;
		if(hq < hs) {
			if(!his->full) {
				in->error = FlateCorrupted;
				return 0;
			}
			hq += HistorySize;
		}

		/* slow but correct */
		while(len) {
			*hp = *hq;
			hq++;
			hp++;
			if(hq >= he)
				hq = hs;
			if(hp == he) {
				his->full = 1;
				if((*in->w)(in->wr, hs, HistorySize) != HistorySize) {
					in->error = FlateOutputFail;
					return 0;
				}
				hp = hs;
			}
			len--;
		}

	}

	his->cp = hp;

	return 1;
}

static int
revcode(int c, int b)
{
	/* shift encode up so it starts on bit 15 then reverse */
	c <<= (16-b);
	c = revtab[c>>8] | (revtab[c&0xff]<<8);
	return c;
}

/*
 * construct the huffman decoding arrays and a fast lookup table.
 * the fast lookup is a table indexed by the next flatbits bits,
 * which returns the symbol matched and the number of bits consumed,
 * or the minimum number of bits needed and 0xff if more than flatbits
 * bits are needed.
 *
 * flatbits can be longer than the smallest huffman code,
 * because shorter codes are assigned smaller lexical prefixes.
 * this means assuming zeros for the next few bits will give a
 * conservative answer, in the sense that it will either give the
 * correct answer, or return the minimum number of bits which
 * are needed for an answer.
 */
static int
hufftab(Huff *h, char *hb, int maxleaf, int flatbits)
{
	uint32 bitcount[MaxHuffBits];
	uint32 c, fc, ec, mincode, code, nc[MaxHuffBits];
	int i, b, minbits, maxbits;

	for(i = 0; i < MaxHuffBits; i++)
		bitcount[i] = 0;
	maxbits = -1;
	minbits = MaxHuffBits + 1;
	for(i=0; i < maxleaf; i++){
		b = hb[i];
		if(b){
			bitcount[b]++;
			if(b < minbits)
				minbits = b;
			if(b > maxbits)
				maxbits = b;
		}
	}

	h->maxbits = maxbits;
	if(maxbits <= 0){
		h->maxbits = 0;
		h->minbits = 0;
		h->flatmask = 0;
		return 1;
	}
	code = 0;
	c = 0;
	for(b = 0; b <= maxbits; b++){
		h->last[b] = c;
		c += bitcount[b];
		mincode = code << 1;
		nc[b] = mincode;
		code = mincode + bitcount[b];
		if(code > (1 << b))
			return 0;
		h->maxcode[b] = code - 1;
		h->last[b] += code - 1;
	}

	if(flatbits > maxbits)
		flatbits = maxbits;
	h->flatmask = (1 << flatbits) - 1;
	if(minbits > flatbits)
		minbits = flatbits;
	h->minbits = minbits;

	b = 1 << flatbits;
	for(i = 0; i < b; i++)
		h->flat[i] = ~0;

	/*
	 * initialize the flat table to include the minimum possible
	 * bit length for each code prefix
	 */
	for(b = maxbits; b > flatbits; b--){
		code = h->maxcode[b];
		if(code == -1)
			break;
		mincode = code + 1 - bitcount[b];
		mincode >>= b - flatbits;
		code >>= b - flatbits;
		for(; mincode <= code; mincode++)
			h->flat[revcode(mincode, flatbits)] = (b << 8) | 0xff;
	}

	for(i = 0; i < maxleaf; i++){
		b = hb[i];
		if(b <= 0)
			continue;
		c = nc[b]++;
		if(b <= flatbits){
			code = (i << 8) | b;
			ec = (c + 1) << (flatbits - b);
			if(ec > (1<<flatbits))
				return 0;	/* this is actually an internal error */
			for(fc = c << (flatbits - b); fc < ec; fc++)
				h->flat[revcode(fc, flatbits)] = code;
		}
		if(b > minbits){
			c = h->last[b] - c;
			if(c >= maxleaf)
				return 0;
			h->decode[c] = i;
		}
	}
	return 1;
}

static int
hdecsym(Input *in, Huff *h, int nb)
{
	long c;

	if((nb & 0xff) == 0xff)
		nb = nb >> 8;
	else
		nb = nb & 0xff;
	for(; nb <= h->maxbits; nb++){
		if(in->nbits<nb && !sregfill(in, nb))
			return -1;
		c = revtab[in->sreg&0xff]<<8;
		c |= revtab[(in->sreg>>8)&0xff];
		c >>= (16-nb);
		if(c <= h->maxcode[nb]){
			in->sreg >>= nb;
			in->nbits -= nb;
			return h->decode[h->last[nb] - c];
		}
	}
	in->error = FlateCorrupted;
	return -1;
}

static int
sregfill(Input *in, int n)
{
	int c;

	while(n > in->nbits) {
		c = (*in->get)(in->getr);
		if(c < 0){
			in->error = FlateInputFail;
			return 0;
		}
		in->sreg |= c<<in->nbits;
		in->nbits += 8;
	}
	return 1;
}

static int
sregunget(Input *in)
{
	if(in->nbits >= 8) {
		in->error = FlateInternal;
		return 0;
	}

	/* throw other bits on the floor */
	in->nbits = 0;
	in->sreg = 0;
	return 1;
}

static int
blgetc(void *vb)
{
	Block *b;

	b = vb;
	if(b->pos >= b->limit)
		return -1;
	return *b->pos++;
}

static int
blwrite(void *vb, void *buf, int n)
{
	Block *b;

	b = vb;

	if(n > b->limit - b->pos)
		n = b->limit - b->pos;
	memmove(b->pos, buf, n);
	b->pos += n;
	return n;
}

int
inflateBlock(char *dst, int dsize, char *src, int ssize)
{
	Block bd, bs;
	int ok;

	bs.pos = (uint8*)src;
	bs.limit = (uint8*)src + ssize;

	bd.pos = (uint8*)dst;
	bd.limit = (uint8*)dst + dsize;

	ok = inflate(&bd, blwrite, &bs, blgetc);
	if(ok != FlateOk)
		return ok;
	return bd.pos - (uint8*)dst;
}
