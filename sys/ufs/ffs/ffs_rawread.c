/*-
 * Copyright (c) 2000-2003 Tor Egge
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/limits.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/conf.h>
#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>
#include <ufs/ffs/fs.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

static int ffs_rawread_readahead(struct vnode *vp,
				 caddr_t udata,
				 off_t offset,
				 size_t len,
				 struct thread *td,
				 struct buf *bp,
				 caddr_t sa);
static int ffs_rawread_main(struct vnode *vp,
			    struct uio *uio);

static int ffs_rawread_sync(struct vnode *vp, struct thread *td);

int ffs_rawread(struct vnode *vp, struct uio *uio, int *workdone);

void ffs_rawread_setup(void);

static void ffs_rawreadwakeup(struct buf *bp);


SYSCTL_DECL(_vfs_ffs);

static int ffsrawbufcnt = 4;
SYSCTL_INT(_vfs_ffs, OID_AUTO, ffsrawbufcnt, CTLFLAG_RD, &ffsrawbufcnt, 0,
	   "Buffers available for raw reads");

static int allowrawread = 1;
SYSCTL_INT(_vfs_ffs, OID_AUTO, allowrawread, CTLFLAG_RW, &allowrawread, 0,
	   "Flag to enable raw reads");

static int rawreadahead = 1;
SYSCTL_INT(_vfs_ffs, OID_AUTO, rawreadahead, CTLFLAG_RW, &rawreadahead, 0,
	   "Flag to enable readahead for long raw reads");


void
ffs_rawread_setup(void)
{
	ffsrawbufcnt = (nswbuf > 100 ) ? (nswbuf - (nswbuf >> 4)) : nswbuf - 8;
}


static int
ffs_rawread_sync(struct vnode *vp, struct thread *td)
{
	int spl;
	int error;
	int upgraded;

	GIANT_REQUIRED;
	/* Check for dirty mmap, pending writes and dirty buffers */
	spl = splbio();
	VI_LOCK(vp);
	if (vp->v_numoutput > 0 ||
	    !TAILQ_EMPTY(&vp->v_dirtyblkhd) ||
	    (vp->v_iflag & VI_OBJDIRTY) != 0) {
		splx(spl);
		VI_UNLOCK(vp);
		
		if (VOP_ISLOCKED(vp, td) != LK_EXCLUSIVE) {
			upgraded = 1;
			/* Upgrade to exclusive lock, this might block */
			VOP_LOCK(vp, LK_UPGRADE | LK_NOPAUSE, td);
		} else
			upgraded = 0;
			
		
		/* Attempt to msync mmap() regions to clean dirty mmap */ 
		VI_LOCK(vp);
		if ((vp->v_iflag & VI_OBJDIRTY) != 0) {
			struct vm_object *obj;
			VI_UNLOCK(vp);
			if (VOP_GETVOBJECT(vp, &obj) == 0) {
				VM_OBJECT_LOCK(obj);
				vm_object_page_clean(obj, 0, 0, OBJPC_SYNC);
				VM_OBJECT_UNLOCK(obj);
			}
			VI_LOCK(vp);
		}

		/* Wait for pending writes to complete */
		spl = splbio();
		while (vp->v_numoutput) {
			vp->v_iflag |= VI_BWAIT;
			error = msleep((caddr_t)&vp->v_numoutput,
				       VI_MTX(vp),
				       PRIBIO + 1,
				       "rawrdfls", 0);
			if (error != 0) {
				splx(spl);
				VI_UNLOCK(vp);
				if (upgraded != 0)
					VOP_LOCK(vp, LK_DOWNGRADE, td);
				return (error);
			}
		}
		/* Flush dirty buffers */
		if (!TAILQ_EMPTY(&vp->v_dirtyblkhd)) {
			splx(spl);
			VI_UNLOCK(vp);
			if ((error = VOP_FSYNC(vp, NOCRED, MNT_WAIT, td)) != 0) {
				if (upgraded != 0)
					VOP_LOCK(vp, LK_DOWNGRADE, td);
				return (error);
			}
			VI_LOCK(vp);
			spl = splbio();
			if (vp->v_numoutput > 0 ||
			    !TAILQ_EMPTY(&vp->v_dirtyblkhd))
				panic("ffs_rawread_sync: dirty bufs");
		}
		splx(spl);
		VI_UNLOCK(vp);
		if (upgraded != 0)
			VOP_LOCK(vp, LK_DOWNGRADE, td);
	} else {
		splx(spl);
		VI_UNLOCK(vp);
	}
	return 0;
}


static int
ffs_rawread_readahead(struct vnode *vp,
		      caddr_t udata,
		      off_t offset,
		      size_t len,
		      struct thread *td,
		      struct buf *bp,
		      caddr_t sa)
{
	int error;
	u_int iolen;
	off_t blockno;
	int blockoff;
	int bsize;
	struct vnode *dp;
	int bforwards;
	struct inode *ip;
	ufs2_daddr_t blkno;
	
	GIANT_REQUIRED;
	bsize = vp->v_mount->mnt_stat.f_iosize;
	
	ip = VTOI(vp);
	dp = ip->i_devvp;

	iolen = ((vm_offset_t) udata) & PAGE_MASK;
	bp->b_bcount = len;
	if (bp->b_bcount + iolen > bp->b_kvasize) {
		bp->b_bcount = bp->b_kvasize;
		if (iolen != 0)
			bp->b_bcount -= PAGE_SIZE;
	}
	bp->b_flags = B_PHYS;
	bp->b_iocmd = BIO_READ;
	bp->b_iodone = ffs_rawreadwakeup;
	bp->b_data = udata;
	bp->b_saveaddr = sa;
	bp->b_offset = offset;
	blockno = bp->b_offset / bsize;
	blockoff = (bp->b_offset % bsize) / DEV_BSIZE;
	if ((daddr_t) blockno != blockno) {
		return EINVAL; /* blockno overflow */
	}
	
	bp->b_lblkno = bp->b_blkno = blockno;
	
	error = ufs_bmaparray(vp, bp->b_lblkno, &blkno, NULL, &bforwards, NULL);
	if (error != 0)
		return error;
	bp->b_blkno = blkno;
	if (bp->b_blkno == -1) {

		/* Fill holes with NULs to preserve semantics */
		
		if (bp->b_bcount + blockoff * DEV_BSIZE > bsize)
			bp->b_bcount = bsize - blockoff * DEV_BSIZE;
		bp->b_bufsize = bp->b_bcount;
		
		if (vmapbuf(bp) < 0)
			return EFAULT;
		
		if (ticks - PCPU_GET(switchticks) >= hogticks)
			uio_yield();
		bzero(bp->b_data, bp->b_bufsize);

		/* Mark operation completed (similar to bufdone()) */

		bp->b_resid = 0;
		bp->b_flags |= B_DONE;
		return 0;
	}
	
	if (bp->b_bcount + blockoff * DEV_BSIZE > bsize * (1 + bforwards))
		bp->b_bcount = bsize * (1 + bforwards) - blockoff * DEV_BSIZE;
	bp->b_bufsize = bp->b_bcount;
	bp->b_blkno += blockoff;
	bp->b_dev = dp->v_rdev;
	
	if (vmapbuf(bp) < 0)
		return EFAULT;
	
	if (dp->v_type == VCHR)
		(void) VOP_SPECSTRATEGY(dp, bp);
	else
		(void) VOP_STRATEGY(dp, bp);
	return 0;
}


static int
ffs_rawread_main(struct vnode *vp,
		 struct uio *uio)
{
	int error, nerror;
	struct buf *bp, *nbp, *tbp;
	caddr_t sa, nsa, tsa;
	u_int iolen;
	int spl;
	caddr_t udata;
	long resid;
	off_t offset;
	struct thread *td;
	
	GIANT_REQUIRED;
	td = uio->uio_td ? uio->uio_td : curthread;
	udata = uio->uio_iov->iov_base;
	resid = uio->uio_resid;
	offset = uio->uio_offset;

	/*
	 * keep the process from being swapped
	 */
	PHOLD(td->td_proc);
	
	error = 0;
	nerror = 0;
	
	bp = NULL;
	nbp = NULL;
	sa = NULL;
	nsa = NULL;
	
	while (resid > 0) {
		
		if (bp == NULL) { /* Setup first read */
			/* XXX: Leave some bufs for swap */
			bp = getpbuf(&ffsrawbufcnt);
			sa = bp->b_data;
			bp->b_vp = vp; 
			error = ffs_rawread_readahead(vp, udata, offset,
						     resid, td, bp, sa);
			if (error != 0)
				break;
			
			if (resid > bp->b_bufsize) { /* Setup fist readahead */
				/* XXX: Leave bufs for swap */
				if (rawreadahead != 0) 
					nbp = trypbuf(&ffsrawbufcnt);
				else
					nbp = NULL;
				if (nbp != NULL) {
					nsa = nbp->b_data;
					nbp->b_vp = vp;
					
					nerror = ffs_rawread_readahead(vp, 
								       udata +
								       bp->b_bufsize,
								       offset +
								       bp->b_bufsize,
								       resid -
								       bp->b_bufsize,
								       td,
								       nbp,
								       nsa);
					if (nerror) {
						relpbuf(nbp, &ffsrawbufcnt);
						nbp = NULL;
					}
				}
			}
		}
		
		spl = splbio();
		bwait(bp, PRIBIO, "rawrd");
		splx(spl);
		
		vunmapbuf(bp);
		
		iolen = bp->b_bcount - bp->b_resid;
		if (iolen == 0 && (bp->b_ioflags & BIO_ERROR) == 0) {
			nerror = 0;	/* Ignore possible beyond EOF error */
			break; /* EOF */
		}
		
		if ((bp->b_ioflags & BIO_ERROR) != 0) {
			error = bp->b_error;
			break;
		}
		resid -= iolen;
		udata += iolen;
		offset += iolen;
		if (iolen < bp->b_bufsize) {
			/* Incomplete read.  Try to read remaining part */
			error = ffs_rawread_readahead(vp,
						      udata,
						      offset,
						      bp->b_bufsize - iolen,
						      td,
						      bp,
						      sa);
			if (error != 0)
				break;
		} else if (nbp != NULL) { /* Complete read with readahead */
			
			tbp = bp;
			bp = nbp;
			nbp = tbp;
			
			tsa = sa;
			sa = nsa;
			nsa = tsa;
			
			if (resid <= bp->b_bufsize) { /* No more readaheads */
				relpbuf(nbp, &ffsrawbufcnt);
				nbp = NULL;
			} else { /* Setup next readahead */
				nerror = ffs_rawread_readahead(vp,
							       udata +
							       bp->b_bufsize,
							       offset +
							       bp->b_bufsize,
							       resid -
							       bp->b_bufsize,
							       td,
							       nbp,
							       nsa);
				if (nerror != 0) {
					relpbuf(nbp, &ffsrawbufcnt);
					nbp = NULL;
				}
			}
		} else if (nerror != 0) {/* Deferred Readahead error */
			break;		
		}  else if (resid > 0) { /* More to read, no readahead */
			error = ffs_rawread_readahead(vp, udata, offset,
						      resid, td, bp, sa);
			if (error != 0)
				break;
		}
	}
	
	if (bp != NULL)
		relpbuf(bp, &ffsrawbufcnt);
	if (nbp != NULL) {			/* Run down readahead buffer */
		spl = splbio();
		bwait(nbp, PRIBIO, "rawrd");
		splx(spl);
		vunmapbuf(nbp);
		relpbuf(nbp, &ffsrawbufcnt);
	}
	
	if (error == 0)
		error = nerror;
	PRELE(td->td_proc);
	uio->uio_iov->iov_base = udata;
	uio->uio_resid = resid;
	uio->uio_offset = offset;
	return error;
}


int
ffs_rawread(struct vnode *vp,
	    struct uio *uio,
	    int *workdone)
{
	if (allowrawread != 0 &&
	    uio->uio_iovcnt == 1 && 
	    uio->uio_segflg == UIO_USERSPACE &&
	    uio->uio_resid == uio->uio_iov->iov_len &&
	    (((uio->uio_td != NULL) ? uio->uio_td : curthread)->td_flags &
	     TDF_DEADLKTREAT) == 0) {
		int secsize;		/* Media sector size */
		off_t filebytes;	/* Bytes left of file */
		int blockbytes;		/* Bytes left of file in full blocks */
		int partialbytes;	/* Bytes in last partial block */
		int skipbytes;		/* Bytes not to read in ffs_rawread */
		struct inode *ip;
		int error;
		

		/* Only handle sector aligned reads */
		ip = VTOI(vp);
		secsize = ip->i_devvp->v_rdev->si_bsize_phys;
		if ((uio->uio_offset & (secsize - 1)) == 0 &&
		    (uio->uio_resid & (secsize - 1)) == 0) {
			
			/* Sync dirty pages and buffers if needed */
			error = ffs_rawread_sync(vp,
						 (uio->uio_td != NULL) ?
						 uio->uio_td : curthread);
			if (error != 0)
				return error;
			
			/* Check for end of file */
			if (ip->i_size > uio->uio_offset) {
				filebytes = ip->i_size - uio->uio_offset;

				/* No special eof handling needed ? */
				if (uio->uio_resid <= filebytes) {
					*workdone = 1;
					return ffs_rawread_main(vp, uio);
				}
				
				partialbytes = ((unsigned int) ip->i_size) %
					ip->i_fs->fs_bsize;
				blockbytes = (int) filebytes - partialbytes;
				if (blockbytes > 0) {
					skipbytes = uio->uio_resid -
						blockbytes;
					uio->uio_resid = blockbytes;
					error = ffs_rawread_main(vp, uio);
					uio->uio_resid += skipbytes;
					if (error != 0)
						return error;
					/* Read remaining part using buffer */
				}
			}
		}
	}
	*workdone = 0;
	return 0;
}


static void
ffs_rawreadwakeup(struct buf *bp)
{
	bdone(bp);
}
