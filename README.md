# Mac OS X Ext2 Filesystem 

* It doesn't need Fuse.
* It doesn't need any particular version of xcode, it's relatively xcode independent.

Original at https://sourceforge.net/projects/ext2fsx/

Why another ext2fs module? Why not use fusefs? Rationale behind: 
* I hate fuse. It's slow, relatively unstable, I hate it, period.
* I needed decent extX support in OSX without Fuse, xcode, any other dependencies.


# TODO:
* Port ext2fs code from FreeBSD 11, see: https://wiki.freebsd.org/Ext2fs
* Drop e2fs progs, integrate the code used. Macports does better.

