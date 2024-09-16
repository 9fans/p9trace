[This page was originally http://cm.bell-labs.com/who/seanq/p9fs.html.
Another archived copy can be found at <https://9p.io/who/seanq/p9fs.html>.]

# Plan 9 File System

The following is an extract from the paper “Plan 9 from Bell Labs”, by Rob Pike, Dave Presotto, Sean Dorward, Bob Flandrena, Ken Thompson, Howard Trickey, and Phil Winterbottom. [[html](https://9p.io/sys/doc/9.html) | [ps](https://9p.io/sys/doc/9.ps) | [pdf](https://9p.io/sys/doc/9.pdf)]

## The File Server
A central file server stores permanent files and presents them to the
network as a file hierarchy exported using 9P.  The server is a
stand-alone system, accessible only over the network, designed to do
its one job well.  It runs no user processes, only a fixed set of
routines compiled into the boot image.  Rather than a set of disks or
separate file systems, the main hierarchy exported by the server is a
single tree, representing files on many disks.  That hierarchy is
shared by many users over a wide area on a variety of networks.  Other
file trees exported by the server include special-purpose systems such
as temporary storage and, as explained below, a backup service.

The file server has three levels of storage.  The central server in
our installation has about 100 megabytes of memory buffers, 27
gigabytes of magnetic disks, and 350 gigabytes of bulk storage in a
write-once-read-many (WORM) jukebox.  The disk is a cache for the WORM
and the memory is a cache for the disk; each is much faster, and sees
about an order of magnitude more traffic, than the level it caches.
The addressable data in the file system can be larger than the size of
the magnetic disks, because they are only a cache; our main file
server has about 40 gigabytes of active storage.

The most unusual feature of the file server comes from its use of a
WORM device for stable storage.  Every morning at 5 o'clock, a dump of
the file system occurs automatically.  The file system is frozen and
all blocks modified since the last dump are queued to be written to
the WORM.  Once the blocks are queued, service is restored and the
read-only root of the dumped file system appears in a hierarchy of all
dumps ever taken, named by its date.  For example, the directory
/n/dump/1995/0315 is the root directory of an image of the file system
as it appeared in the early morning of March 15, 1995.  It takes a few
minutes to queue the blocks, but the process to copy blocks to the
WORM, which runs in the background, may take hours.

There are two ways the dump file system is used.  The first is by the
users themselves, who can browse the dump file system directly or
attach pieces of it to their name space.  For example, to track down a
bug, it is straightforward to try the compiler from three months ago
or to link a program with yesterday's library.  With daily snapshots
of all files, it is easy to find when a particular change was made or
what changes were made on a particular date.  People feel free to make
large speculative changes to files in the knowledge that they can be
backed out with a single copy command.  There is no backup system as
such; instead, because the dump is in the file name space, backup
problems can be solved with standard tools such as cp, ls, grep, and
diff.

The other (very rare) use is complete system backup.  In the event of
disaster, the active file system can be initialized from any dump by
clearing the disk cache and setting the root of the active file system
to be a copy of the dumped root.  Although easy to do, this is not to
be taken lightly: besides losing any change made after the date of the
dump, this recovery method results in a very slow system.  The cache
must be reloaded from WORM, which is much slower than magnetic disks.
The file system takes a few days to reload the working set and regain
its full performance.

Access permissions of files in the dump are the same as they were when
the dump was made.  Normal utilities have normal permissions in the
dump without any special arrangement.  The dump file system is
read-only, though, which means that files in the dump cannot be
written regardless of their permission bits; in fact, since
directories are part of the read-only structure, even the permissions
cannot be changed.

Once a file is written to WORM, it cannot be removed, so our users
never see ``please clean up your files'' messages and there is no df
command.  We regard the WORM jukebox as an unlimited resource.  The
only issue is how long it will take to fill.  Our WORM has served a
community of about 50 users for five years and has absorbed daily
dumps, consuming a total of 65% of the storage in the jukebox.  In
that time, the manufacturer has improved the technology, doubling the
capacity of the individual disks.  If we were to upgrade to the new
media, we would have more free space than in the original empty
jukebox.  Technology has created storage faster than we can use it.

## Other References

A slightly out of date description of the implementation of the file server:

 - Sean Quinlan. A cache worm file system. _Software-Practice and Experience_, Vol 21, 12, pp 1289-1299, December 1991. [[pdf](cw.pdf)]

A more recent description of the structure and the operation of the file server:

 - Ken Thompson. The Plan 9 File Server. [[html](https://9p.io/sys/doc/fs/fs.html) | [ps](https://9p.io/sys/doc/fs/fs.ps) | [pdf](https://9p.io/sys/doc/fs/fs.pdf)].

Original last modified: 10/2/2001 \
Updated for GitHub repository by rsc@swtch.com: 9/12/2024

<td valign="middle" align="right">
<img src="power36.gif" alt="Powered by Plan 9">
</td>
</tr></table>
