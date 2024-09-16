#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "trace.h"

extern int inflateBlock(char *dst, int dsize, char *src, int ssize);

static int
getShort(char *p)
{
	unsigned char *q = (unsigned char *)p;
	return (q[0]<<8) | q[1];
}

static long
getLong(char *p)
{
	unsigned char *q = (unsigned char *)p;
	return ((long)q[0]<<24) | ((long)q[1]<<16) | (q[2]<<8) | q[3];
}

static void
getDir(char *p, TrDir *dir)
{
	int i;

	dir->slot = getShort(p);
	dir->path = getLong(p+2);
	dir->version = getLong(p+6);
	dir->mode = getShort(p+10);
	dir->size = getLong(p+12);
	p += 16;
	for(i=0; i<TrNumDirect; i++)
		dir->dblock[i] = getLong(p+i*4);
	p += TrNumDirect*4;
	dir->iblock = getLong(p);
	dir->diblock = getLong(p+4);
	dir->mtime = getLong(p+8);
	dir->atime = getLong(p+12);
	dir->uid = getShort(p+16);
	dir->gid = getShort(p+18);
	dir->wid = getShort(p+20);
}

static int
readN(FILE *file, char *buf, int n)
{	
	int nn;

	while(n > 0) {
		nn = fread(buf, 1, n, file);
		if(nn <= 0)
			return 0;
		buf += nn;
		n -= nn;
	}
	return 1;
}

TrBlock *
trRead(FILE *file)
{
	int n, nn, extra, length = 0;
	char cbuf[32*1024], buf[32*1024], *p;
	int tag, i;
	TrBlock *b;

	n = (fgetc(file)<<8);
	n |= fgetc(file);
	if(n & (1<<15)) {
		/* compressed */
		n &= ~(1<<15);
		if(n > sizeof(cbuf))
			return 0;
		if(!readN(file, cbuf, n))
			return 0;
		n = inflateBlock(buf, sizeof(buf), cbuf, n);
	} else {
		if(!readN(file, buf, n))
			return 0;
	}
	if(n < TrHeaderSize)
		return 0;
	tag = buf[0];
	extra = 0;

	/* determine expected size */
	switch(tag) {
	default:	
		return 0;
	case TrTagNull:
	case TrTagFile:
		nn = TrHeaderSize;
		break;
	case TrTagSuper:
		nn = TrHeaderSize + 16;
		break;
	case TrTagInd1:
	case TrTagInd2:
		if(n < TrHeaderSize+2)
			return 0;
		length = getShort(buf+TrHeaderSize);
		nn = TrHeaderSize + 2 + 4*length;
		extra = length*sizeof(long);
		break;
	case TrTagDir:
		if(n < TrHeaderSize+2)
			return 0;
		length = getShort(buf+TrHeaderSize);
		nn = TrHeaderSize + 2 + TrDirSize*length;
		extra = length*sizeof(TrDir);
		break;
	}
if(n != nn) fprintf(stderr, "bad size: %d %d %d\n", tag, n, nn);
	if(n != nn)
		return 0;
	b = malloc(sizeof(TrBlock) + extra);
	if(b == 0)
		return 0;
	b->tag = tag;
	b->path = getLong(buf+1);
	b->addr = getLong(buf+5);
	b->zsize = getShort(buf+9);
	b->wsize = getShort(buf+11);
	b->dsize = getShort(buf+13);
	memmove(b->score, buf+15, 20);
	p = buf + TrHeaderSize;
	switch(tag) {
	case TrTagSuper:
		b->u.super.cwraddr = getLong(p);
		b->u.super.roraddr = getLong(p+4);
		b->u.super.last = getLong(p+8);
		b->u.super.next = getLong(p+12);
		break;
	case TrTagInd1:
	case TrTagInd2:
		b->u.ind.length = length;
		for(i=0; i<length; i++)
			b->u.ind.addr[i] = getLong(p+2+i*4);
		break;
	case TrTagDir:
		b->u.ind.length = length;
		for(i=0; i<length; i++)
			getDir(p+2+i*TrDirSize, b->u.dir.dir+i);
		break;
	}
	return b;
}

