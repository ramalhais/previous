#include <algorithm>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <ftw.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>

#include "config.h"
#include "NFS2Prog.h"
#include "FileTableNFSD.h"
#include "nfsd.h"

using namespace std;

enum {
	NFS_OK = 0,
	NFSERR_PERM = 1,
	NFSERR_NOENT = 2,
	NFSERR_IO = 5,
	NFSERR_NXIO = 6,
	NFSERR_ACCES = 13,
	NFSERR_EXIST = 17,
	NFSERR_NODEV = 19,
	NFSERR_NOTDIR = 20,
	NFSERR_ISDIR = 21,
	NFSERR_FBIG = 27,
	NFSERR_NOSPC = 28,
	NFSERR_ROFS = 30,
	NFSERR_NAMETOOLONG = 63,
	NFSERR_NOTEMPTY = 66,
	NFSERR_DQUOT = 69,
	NFSERR_STALE = 70,
	NFSERR_WFLUSH = 99,
};

enum NFTYPE { NFNON, NFREG, NFDIR, NFBLK, NFCHR, NFLNK, NFSOCK, NFFIFO, NFBAD };

CNFS2Prog::CNFS2Prog() : CRPCProg(PROG_NFS, 2, "nfsd") {
    #define RPC_PROG_CLASS CNFS2Prog
    SET_PROC(1,  GETATTR);
    SET_PROC(2,  SETATTR);
    SET_PROC(4,  LOOKUP);
    SET_PROC(5,  READLINK);
    SET_PROC(6,  READ);
    SET_PROC(7,  WRITECACHE);
    SET_PROC(8,  WRITE);
    SET_PROC(9,  CREATE);
    SET_PROC(10, REMOVE);
    SET_PROC(11, RENAME);
    SET_PROC(12, LINK);
    SET_PROC(13, SYMLINK);
    SET_PROC(14, MKDIR);
    SET_PROC(15, RMDIR);
    SET_PROC(16, READDIR);
    SET_PROC(17, STATFS);
}

CNFS2Prog::~CNFS2Prog() { }

void CNFS2Prog::SetUserID(uint32_t uid, uint32_t gid) {
    nfsd_fts[0]->setDefaultUID_GID(uid, gid);
}

int CNFS2Prog::procedureGETATTR(void) {
    string   path;
    
	GetPath(path);
        
    log("GETATTR %s", path.c_str());
	if (!(CheckFile(path)))
		return PRC_OK;

	m_out->write(NFS_OK);
	WriteFileAttributes(path);
    return PRC_OK;
}

static void set_attrs(const string& path, const FileAttrs& fstat) {
    FileAttrs newAttrs = nfsd_fts[0]->getFileAttrs(path);

    if(FileAttrs::valid16(fstat.mode)) {
        newAttrs.mode &= S_IFMT;
        newAttrs.mode |= fstat.mode & (S_IRWXU | S_IRWXG | S_IRWXO);
        nfsd_fts[0]->vfsChmod(path, newAttrs.mode);
        newAttrs.mode |= fstat.mode;
    }
    if(FileAttrs::valid16(fstat.uid))
        newAttrs.uid = fstat.uid;
    if(FileAttrs::valid16(fstat.gid))
        newAttrs.gid = fstat.gid;

    timeval times[2];
    timeval now;
    gettimeofday(&now, NULL);
    times[0].tv_sec  = FileAttrs::valid32(fstat.atime_sec)  ? fstat.atime_sec  : now.tv_sec;
    times[0].tv_usec = FileAttrs::valid32(fstat.atime_usec) ? fstat.atime_usec : now.tv_usec;
    times[1].tv_sec  = FileAttrs::valid32(fstat.mtime_sec)  ? fstat.mtime_sec  : now.tv_sec;
    times[1].tv_usec = FileAttrs::valid32(fstat.mtime_usec) ? fstat.mtime_usec : now.tv_usec;
    if(FileAttrs::valid32(fstat.atime_sec) || FileAttrs::valid32(fstat.mtime_sec))
        nfsd_fts[0]->vfsUtimes(path, times);
    nfsd_fts[0]->setFileAttrs(path, newAttrs);
}

static struct stat read_stat(XDRInput* xin) {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t atime_sec;
    uint32_t atime_usec;
    uint32_t mtime_sec;
    uint32_t mtime_usec;
    
    xin->read(&mode);
    xin->read(&uid);
    xin->read(&gid);
    xin->read(&size);
    xin->read(&atime_sec);
    xin->read(&atime_usec);
    xin->read(&mtime_sec);
    xin->read(&mtime_usec);
    
    struct stat result;
    result.st_mode              = mode;
    result.st_uid               = uid;
    result.st_gid               = gid;
    result.st_size              = size;
#if HAVE_STRUCT_STAT_ST_ATIMESPEC
    result.st_atimespec.tv_sec  = atime_sec;
    result.st_atimespec.tv_nsec = atime_usec * 1000;
#else
    result.st_atim.tv_sec       = atime_sec;
    result.st_atim.tv_nsec      = atime_usec * 1000;
#endif
#if HAVE_STRUCT_STAT_ST_MTIMESPEC
    result.st_mtimespec.tv_sec  = mtime_sec;
    result.st_mtimespec.tv_nsec = mtime_usec * 1000;
#else
    result.st_mtim.tv_sec       = mtime_sec;
    result.st_mtim.tv_nsec      = mtime_usec * 1000;
#endif
    result.st_rdev              = FATTR_INVALID;
    return result;
}

int CNFS2Prog::procedureSETATTR(void) {
    string   path;
    
	log("SETATTR");
	GetPath(path);
	if (!(CheckFile(path)))
		return PRC_OK;

    set_attrs(path, FileAttrs(read_stat(m_in)));
    
    m_out->write(NFS_OK);
	WriteFileAttributes(path);
    return PRC_OK;
}

static void write_handle(XDROutput* xout, uint64_t handle) {
    uint64_t data[4] = {handle,0,0,0};
    xout->write(data, FHSIZE);
}

int CNFS2Prog::procedureLOOKUP(void) {
    string path;

	GetFullPath(path);
	if (!(CheckFile(path)))
		return PRC_OK;

    uint64_t handle = nfsd_fts[0]->getFileHandle(path);
    if(handle) {
        m_out->write(NFS_OK);
        log("LOOKUP %s=%" PRIu64, path.c_str(), handle);
        write_handle(m_out, handle);
        WriteFileAttributes(path);
    } else {
        m_out->write(NFSERR_NOENT);
    }
    return PRC_OK;
}

int nfs_err(int error) {
    switch (error) {
        case 0:      return NFS_OK;
        case ENOENT: return NFSERR_NOENT;
        case EACCES: return NFSERR_ACCES;
        case EINVAL: return NFSERR_IO;
        default:
            return NFSERR_IO;
    }
}

int CNFS2Prog::procedureREADLINK(void) {
    string   path;
    
    GetPath(path);
    log("READLINK %s", path.c_str());
    if (!(CheckFile(path)))
        return PRC_OK;
    
    VFSPath result;
    if(int err = nfsd_fts[0]->vfsReadlink(path, result)) {
        m_out->write(nfs_err(err));
    } else {
        m_out->write(NFS_OK);
        XDRString data(result.string());
        m_out->write(data);
    }
    
    return PRC_OK;
}

int CNFS2Prog::procedureREAD(void) {
    string   path;
    uint32_t nOffset;
    uint32_t nCount;
    uint32_t nTotalCount;

	GetPath(path);
    log("READ %s", path.c_str());
	if (!(CheckFile(path)))
		return PRC_OK;

	m_in->read(&nOffset);
	m_in->read(&nCount);
	m_in->read(&nTotalCount);
    
    XDROpaque buffer(nCount);
    VFSFile file(*nfsd_fts[0], path, "rb");
    if(file.isOpen()) {
        nCount = file.read(nOffset, buffer.m_data, buffer.m_size);
        buffer.resize(nCount);
        m_out->write(NFS_OK);
    } else {
        buffer.resize(0);
        m_out->write(nfs_err(errno));
    }
	WriteFileAttributes(path);
    m_out->write(buffer);

    return PRC_OK;
}

int CNFS2Prog::procedureWRITECACHE(void) {
    m_out->write(NFS_OK);
    return PRC_OK;
}

int CNFS2Prog::procedureWRITE(void) {
    string   path;
    uint32_t nBeginOffset;
    uint32_t nOffset;
    uint32_t nTotalCount;

	GetPath(path);
    log("WRITE %s", path.c_str());
	if (!(CheckFile(path)))
		return PRC_OK;

	m_in->read(&nBeginOffset);
	m_in->read(&nOffset);
	m_in->read(&nTotalCount);

    XDROpaque buffer;
	m_in->read(buffer);

    FileAttrs attrs = nfsd_fts[0]->getFileAttrs(path);
    if((attrs.mode & S_IFMT) == S_IFREG) {
        VFSFile file(*nfsd_fts[0], path, "r+b");
        if(file.isOpen()) {
            file.write(nOffset, buffer.m_data, buffer.m_size);
            m_out->write(NFS_OK);
        } else {
            m_out->write(nfs_err(errno));
        }
    } else {
        m_out->write(NFSERR_ISDIR);
    }

	WriteFileAttributes(path);

    return PRC_OK;
}

int CNFS2Prog::procedureCREATE(void) {
    string path;
    
	if(!(GetFullPath(path)))
		return PRC_OK;
    log("CREATE %s", path.c_str());
    
    FileAttrs fstat(read_stat(m_in));
    if(nfsd_fts[0]->vfsAccess(path, F_OK)) {
        if(!(FileAttrs::valid16(fstat.uid))) fstat.uid = nfsd_fts[0]->vfsGetUID(path, false);
        if(!(FileAttrs::valid16(fstat.gid))) fstat.gid = nfsd_fts[0]->vfsGetGID(path, true);
    }
     // touch
    VFSFile file(*nfsd_fts[0], path, "wb");
    if(file.isOpen()) {
        set_attrs(path, fstat);
        m_out->write(NFS_OK);
        write_handle(m_out, nfsd_fts[0]->getFileHandle(path));
        WriteFileAttributes(path);
    } else {
        nfs_err(errno);
    }
    return PRC_OK;
}

int CNFS2Prog::procedureREMOVE(void) {
    string path;

	GetFullPath(path);
    log("REMOVE %s", path.c_str());
	if (!(CheckFile(path)))
		return PRC_OK;

    int err = nfs_err(nfsd_fts[0]->vfsRemove(path));
    m_out->write(err);
    if(!(err)) nfsd_fts[0]->remove(path);

    return PRC_OK;
}

int CNFS2Prog::procedureRENAME(void) {
    string pathFrom;
    string pathTo;

	GetFullPath(pathFrom);
	if (!(CheckFile(pathFrom)))
		return PRC_OK;
	GetFullPath(pathTo);
    log("RENAME %s->%s", pathFrom.c_str(), pathTo.c_str());

    int err = nfs_err(nfsd_fts[0]->vfsRename(pathFrom, pathTo));
    m_out->write(err);
    if(!(err)) nfsd_fts[0]->move(pathFrom, pathTo);
    
    return PRC_OK;
}

int CNFS2Prog::procedureLINK(void) {
    string   to;
    string   from;

    GetPath(from);
    GetFullPath(to);
    log("LINK %s->%s", from.c_str(), to.c_str());
    
    m_out->write(nfs_err(nfsd_fts[0]->vfsLink(from, to, false)));
    
    return PRC_OK;
}

int CNFS2Prog::procedureSYMLINK(void) {
    string to;
    
    GetFullPath(to);
    XDRString from;
    m_in->read(from);
    log("SYMLINK %s->%s", from.c_str(), to.c_str());
    
    FileAttrs fstat(read_stat(m_in));
    int err = nfsd_fts[0]->vfsLink(from.c_str(), to, true);
    if(!(err)) set_attrs(to, fstat);
    m_out->write(nfs_err(err));
    
    return PRC_OK;
}

int CNFS2Prog::procedureMKDIR(void) {
    string path;

	log("MKDIR");
	if(!(GetFullPath(path)))
		return PRC_OK;

    FileAttrs fstat(read_stat(m_in));
    if(int err = nfsd_fts[0]->vfsMkdir(path, DEFAULT_PERM)) {
        nfs_err(err);
    } else {
        set_attrs(path, fstat);
        m_out->write(NFS_OK);
        write_handle(m_out, nfsd_fts[0]->getFileHandle(path));
        WriteFileAttributes(path);
    }
    
    return PRC_OK;
}

int CNFS2Prog::procedureRMDIR(void) {
    string path;

	log("RMDIR");
	GetFullPath(path);
	if (!(CheckFile(path)))
		return PRC_OK;
    
    int err = nfs_err(nfsd_fts[0]->vfsNftw(path, VirtualFS::remove, 3, FTW_DEPTH | FTW_PHYS));
    m_out->write(err);
    if(!(err)) nfsd_fts[0]->remove(path);
    
    return PRC_OK;
}

int CNFS2Prog::procedureREADDIR(void) {
    string   path;
    DIR*     handle;
    uint32_t cookie;
    uint32_t count;
    uint64_t fhandle;
    
	GetPath(path, &fhandle);
	if (!(CheckFile(path)))
		return PRC_OK;
    m_in->read(&cookie);
    m_in->read(&count);
    
    log("READDIR %s", path.c_str());
    
    uint32_t eof = 1;
    handle          = nfsd_fts[0]->vfsOpendir(path);
	if (handle) {
        m_out->write(NFS_OK);
        int skip = cookie;
        for(struct dirent* fileinfo = readdir(handle); fileinfo; fileinfo = readdir(handle)) {
#if HAVE_STRUCT_DIRENT_D_NAMELEN
            size_t namelen = fileinfo->d_namlen;
#else
            size_t namelen = strlen(fileinfo->d_name);
#endif
            if(--skip >= 0) continue;
            
            m_out->write(1);  //value follows
            m_out->write(nfsd_fts[0]->fileId(fileinfo->d_ino));
            string dname(fileinfo->d_name, namelen);
            XDRString name(dname);
            log("%d %s %s", cookie, path.c_str(), name.c_str());
            m_out->write(name);
            m_out->write(cookie+1);
            cookie++;
            if(m_out->size() >= count - 128) { // 128: give some space for XDR data
                eof = 0;
                break;
            }
		};
		closedir(handle);
        m_out->write(0);  //no value follows
        m_out->write(eof);
    } else {
        m_out->write(nfs_err(errno));
    }

    return PRC_OK;
}

static const int BLOCK_SIZE = 4096;

static uint32_t nfs_blocks(const struct statvfs* fsstat, uint32_t fsblocks) {
    uint64_t result = fsblocks;
    // take minimum as block size, looks like every filesystem uses these fields somwhat different
    result *= (uint64_t)min(fsstat->f_frsize, fsstat->f_bsize);
    result /= BLOCK_SIZE;
    if(result >= 0x7FFFFFFF) result = 0x7FFFFFFF; // fix size for signed 32bit
    return static_cast<uint32_t>(result);
}

int CNFS2Prog::procedureSTATFS(void) {
    string path;
	struct statvfs fsstat;

	log("STATFS");
	GetPath(path);
	if(!(CheckFile(path)))
		return PRC_OK;

    if(int err = nfsd_fts[0]->vfsStatvfs(path, fsstat)) {
        m_out->write(nfs_err(err));
    } else {
        m_out->write(NFS_OK);
        m_out->write(BLOCK_SIZE*2);  //transfer size
        m_out->write(BLOCK_SIZE);  //block size
        m_out->write(nfs_blocks(&fsstat, fsstat.f_blocks));  //total blocks
        m_out->write(nfs_blocks(&fsstat, fsstat.f_bfree));  //free blocks
        m_out->write(nfs_blocks(&fsstat, fsstat.f_bavail));  //available blocks
    }
    
    return PRC_OK;
}

bool CNFS2Prog::GetPath(string& result, uint64_t* handle) {
    uint64_t data[4];
    m_in->read((void*)data, FHSIZE);
    if(handle) *handle = data[0];
    return nfsd_fts[0]->getCanonicalPath(data[0], result);
}

bool CNFS2Prog::GetFullPath(string& result) {
    if(!(GetPath(result)))
        return false;
    
    XDRString path;
    m_in->read(path);
    if(result[result.length()-1] != '/') result += "/";
    result += path.c_str();
    return true;
}

bool CNFS2Prog::CheckFile(const string& path) {
	if (path.length() == 0) {
		m_out->write(NFSERR_STALE);
		return false;
	}
    
    // links always pass (will be resolved on the client side via readlink)
    struct stat fstat;
    if(nfsd_fts[0]->stat(path, fstat) == 0 && (fstat.st_mode & S_IFMT) == S_IFLNK)
        return true;
    
    if(nfsd_fts[0]->vfsAccess(path, F_OK)) {
		m_out->write(NFSERR_NOENT);
		return false;
	}

    return true;
}

bool CNFS2Prog::WriteFileAttributes(const string& path) {
	struct stat fstat;

	if (nfsd_fts[0]->stat(path, fstat) != 0)
		return false;

    uint32_t type = NFNON;
    if     (S_ISREG (fstat.st_mode)) type = NFREG;
    else if(S_ISDIR (fstat.st_mode)) type = NFDIR;
    else if(S_ISBLK (fstat.st_mode)) type = NFBLK;
    else if(S_ISCHR (fstat.st_mode)) type = NFCHR;
    else if(S_ISLNK (fstat.st_mode)) type = NFLNK;
    else if(S_ISSOCK(fstat.st_mode)) type = NFSOCK;
    else if(S_ISFIFO(fstat.st_mode)) type = NFFIFO;

	m_out->write(type);  //type
	m_out->write(fstat.st_mode);  //mode
	m_out->write(fstat.st_nlink);  //nlink
	m_out->write(fstat.st_uid);  //uid
	m_out->write(fstat.st_gid);  //gid
	m_out->write(static_cast<uint32_t>(fstat.st_size));  //size
	m_out->write(fstat.st_blksize);  //blocksize
	m_out->write(fstat.st_rdev);  //rdev
	m_out->write(static_cast<uint32_t>(fstat.st_blocks));  //blocks
	m_out->write(fstat.st_dev);  //fsid
    m_out->write(nfsd_fts[0]->fileId(fstat.st_ino));
#if HAVE_STRUCT_STAT_ST_ATIMESPEC
	m_out->write(static_cast<uint32_t>(fstat.st_atimespec.tv_sec));  //atime
	m_out->write(static_cast<uint32_t>(fstat.st_atimespec.tv_nsec / 1000));  //atime
#else
	m_out->Write(static_cast<uint32_t>(fstat.st_atim.tv_sec));  //atime
	m_out->Write(static_cast<uint32_t>(fstat.st_atim.tv_nsec / 1000));  //atime
#endif
#if HAVE_STRUCT_STAT_ST_MTIMESPEC
	m_out->write(static_cast<uint32_t>(fstat.st_mtimespec.tv_sec));  //mtime
	m_out->write(static_cast<uint32_t>(fstat.st_mtimespec.tv_nsec / 1000));  //mtime
	m_out->write(static_cast<uint32_t>(fstat.st_mtimespec.tv_sec));  //ctime -- ignored, we use mtime instead
	m_out->write(static_cast<uint32_t>(fstat.st_mtimespec.tv_nsec / 1000));  //ctime
#else
	m_out->write(static_cast<uint32_t>(fstat.st_mtim.tv_sec));  //mtime
	m_out->write(static_cast<uint32_t>(fstat.st_mtim.tv_nsec / 1000));  //mtime
	m_out->write(static_cast<uint32_t>(fstat.st_mtim.tv_sec));  //ctime -- ignored, we use mtime instead
	m_out->write(static_cast<uint32_t>(fstat.st_mtim.tv_nsec / 1000));  //ctime
#endif
	return true;
}
