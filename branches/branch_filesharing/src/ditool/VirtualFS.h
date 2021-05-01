#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <dirent.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define DEFAULT_PERM 0755
#define FATTR_INVALID ~0

class PathCommon  : public std::vector<std::string> {
public:
    PathCommon(const std::string& sep) : sep(sep) {}
    PathCommon(const std::string& sep, const char* path);
    PathCommon(const std::string& sep, const std::string& path);
    
    const char*  c_str(void)  const {return path.c_str();}
    size_t       length(void) const {return path.length();};
    std::string  string(void) const {return path;}
    void         append(const PathCommon& path);
    bool         exists(void);
    
    virtual bool is_absolute(void) const = 0;

    static std::vector<std::string> split(const std::string& sep, const std::string& path);
protected:
    std::string sep;
    std::string path;
    
    std::string to_string(void) const;
    
    friend std::ostream& operator<<(std::ostream& os, const PathCommon& path);
};

class VFSPath : public PathCommon {
public:
    VFSPath(void)                    : PathCommon("/")       {}
    VFSPath(const char* path)        : PathCommon("/", path) {};
    VFSPath(const std::string& path) : PathCommon("/", path) {};
    
    VFSPath            canonicalize(void) const;
    std::string        filename(void) const;
    VFSPath            parent_path(void) const;
    virtual bool       is_absolute(void) const;
    
    VFSPath&  operator /= (const VFSPath& path);
    VFSPath   operator / (const VFSPath& path) const;

    static VFSPath relative(const VFSPath& path, const VFSPath& basePath);
};

#ifdef _WIN32
    #define HOST_SEPARATOR "\\"
#else
    #define HOST_SEPARATOR "/"
#endif

class HostPath : public PathCommon {
public:
    HostPath(void)                    : PathCommon(HOST_SEPARATOR)       {}
    HostPath(const char* path)        : PathCommon(HOST_SEPARATOR, path) {};
    HostPath(const std::string& path) : PathCommon(HOST_SEPARATOR, path) {};
        
    virtual bool       is_absolute(void) const;
    
    HostPath&  operator /= (const HostPath& path);
    HostPath   operator / (const HostPath& path) const;
};

class FileAttrs {
public:
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t atime_sec;
    uint32_t atime_usec;
    uint32_t mtime_sec;
    uint32_t mtime_usec;
    uint32_t rdev;
    
    FileAttrs(const struct stat& stat);
    FileAttrs(const FileAttrs& attrs);
    FileAttrs(const std::string& serialized);
    
    void        update(const FileAttrs& attrs);
    std::string serialize(void) const;
    
    static bool valid(uint32_t statval);
};

class VirtualFS {
    VFSPath                     basePathAlias;
    HostPath                    basePath;

    VFSPath                     removeAlias(const VFSPath& absoluteVFSpath);
public:
    VirtualFS(const HostPath& basePath, const VFSPath& basePathAlias);
    virtual ~VirtualFS(void);
    
    virtual HostPath  getBasePath     (void);
    virtual VFSPath   getBasePathAlias(void);
    virtual int       stat            (const VFSPath& absoluteVFSpath, struct stat& stat);
    virtual void      move            (const VFSPath& absoluteVFSpathFrom, const VFSPath& absoluteVFSpathTo);
    virtual void      remove          (const VFSPath& absoluteVFSpath);
    virtual uint64_t  getFileHandle   (const VFSPath& absoluteVFSpath);
    virtual void      setFileAttrs    (const VFSPath& absoluteVFSpath, const FileAttrs& fstat);
    virtual FileAttrs getFileAttrs    (const VFSPath& path);
    virtual uint32_t  fileId          (uint64_t ino);
    virtual void      touch           (const VFSPath& absoluteVFSpath);
    virtual HostPath  toHostPath      (const VFSPath& absoluteVFSpath);

    int                   vfsChmod   (const VFSPath& absoluteVFSpath, mode_t mode);
    int                   vfsAccess  (const VFSPath& absoluteVFSpath, int mode);
    DIR*                  vfsOpendir (const VFSPath& absoluteVFSpath);
    int                   vfsRemove  (const VFSPath& absoluteVFSpath);
    int                   vfsRename  (const VFSPath& absoluteVFSpath, const VFSPath& to);
    int                   vfsReadlink(const VFSPath& absoluteVFSpath1, VFSPath& result);
    int                   vfsLink    (const VFSPath& absoluteVFSpath1, const VFSPath& absoluteVFSpath2, bool soft);
    int                   vfsMkdir   (const VFSPath& absoluteVFSpath, mode_t mode);
    int                   vfsNftw    (const VFSPath& absoluteVFSpath, int (*fn)(const char *, const struct stat *ptr, int flag, struct FTW *), int depth, int flags);
    int                   vfsStatvfs (const VFSPath& absoluteVFSpath, struct statvfs& fsstat);
    int                   vfsStat    (const VFSPath& absoluteVFSpath, struct stat& fstat);
    int                   vfsUtimes  (const VFSPath& absoluteVFSpath, const struct timeval times[2]);
    
    static int            remove(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf);
    
    friend class FileAttrDB;
    friend class VFSFile;
};

class VFSFile {
    VirtualFS&     ft;
    const VFSPath& path;
    struct stat    fstat;
    bool           restoreStat;
public:
    FILE*          file;
    
    VFSFile(VirtualFS& ft, const VFSPath& absoluteVFSpath, const std::string& mode);
    ~VFSFile(void);
    bool   isOpen(void);
    size_t read(size_t fileOffset, void* dst, size_t count);
    size_t write(size_t fileOffset, void* src, size_t count);
};

#endif
