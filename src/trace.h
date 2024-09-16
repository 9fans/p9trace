typedef struct TrBlock TrBlock;
typedef struct TrDir TrDir;

enum {
	TrScoreSize = 20,
	TrNumDirect = 6,
	TrDirSize = 62,
	TrHeaderSize = 35,
};

enum {
	TrModeDir =	0x4000,
	TrModeAppend = 	0x2000,
	TrModeLock =	0x1000,
	TrModeRead =	0x4,
	TrModeWrite =	0x2,
	TrModeExec =	0x1,
};

enum {
	TrTagNull = 0,		/* blank block */
	TrTagSuper,		/* super block */
	TrTagDir,		/* directory contents */
	TrTagInd1,		/* points to blocks */
	TrTagInd2,		/* points to TrTagInd1 */
	TrTagFile,		/* file contents */
	TrTagMax
};

struct TrDir {
	short slot;
	long path;
	long version;
	short mode;
	long size;
	long dblock[TrNumDirect];
	long iblock;
	long diblock;
	long mtime;
	long atime;
	short uid;
	short gid;
	short wid;
};

struct TrBlock {
	int tag;
	long path;		/* file it is part of */
	long addr;		/* address */
	short zsize;		/* zero truncated size */
	short wsize;		/* wack compressed size */
	short dsize;		/* deflate compressed size */
	char score[TrScoreSize];	/* block identity */
	union {
		struct {
			long	cwraddr;	/* cfs root addr */
			long	roraddr;	/* dump root addr */
			long	last;		/* last super block addr */
			long	next;		/* next super block addr */
		} super;
		struct {
			int length;	/* in TrDirs */
			TrDir dir[1];	/* variable length */
		} dir;
		struct {
			int length;	/* in longs */
			long addr[1];	/* variable length */
		} ind;
	} u;
};

extern TrBlock *trRead(FILE*);
