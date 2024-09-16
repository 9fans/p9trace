#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "trace.h"

long cnt[TrTagMax];
long next = 0;
long last = 0;
long offset = -1;
long ndir = 0;
long nind = 0;
long num;

void
super(FILE *file)
{
	TrBlock *b;
	int i;

	for(i=0;;i++) {
		b = trRead(file);
		if(b == 0)
			break;
		if(offset == -1)
			offset = b->addr;
		if(b->addr != offset)
			fprintf(stderr, "bad address %ld %ld\n", offset, b->addr);
		offset++;
		assert(b->tag >= 0 && b->tag < TrTagMax);
		cnt[b->tag]++;
		switch(b->tag) {
		case TrTagDir:
			ndir += b->u.dir.length;
			break;
		case TrTagInd1:
		case TrTagInd2:
			nind += b->u.ind.length;
			break;
		case TrTagSuper:
			if(next != 0 && next != b->addr)
				fprintf(stderr, "missing super block %ld\n", next);
			if(last != 0 && last != b->u.super.last)
				fprintf(stderr, "bad last block %ld\n", b->addr);
			fprintf(stdout, "super %ld: cwraddr %ld roraddr %ld last %ld next %ld\n", b->addr, b->u.super.cwraddr,
				b->u.super.roraddr, b->u.super.last, b->u.super.next);
			next = b->u.super.next;
			last = b->addr;
			break;
		}
		free(b);
	}
	num += i;
}


int
main(int argc, char *argv[])
{
	FILE *file;
	int i;

	for(i=1; i<argc; i++) {
		file = fopen(argv[i], "r");
		if(file == 0) {
			fprintf(stderr, "could not open file: %s\n", argv[0]);
			exit(-1);
		}
		super(file);
		fclose(file);
	}
	
	for(i=0; i<TrTagMax; i++)
		fprintf(stderr, "%d: %ld\n", i, cnt[i]);
	fprintf(stderr, "num = %ld ndir = %ld nind = %ld\n", num, ndir, nind);
	return 0;
}
