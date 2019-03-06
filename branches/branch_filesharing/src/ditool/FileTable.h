#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <iostream>
#include <string>
#include <map>
#include <set>
#include <dirent.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define FILE_TABLE_NAME ".nfsd_fattrs"

#define DEFAULT_PERM 0755
#define FATTR_INVALID ~0

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
    FileAttrs(FILE* fin, std::string& name);
    FileAttrs(const FileAttrs& attrs);
    void Update(const FileAttrs& attrs);
    void Write(FILE* fout, const std::string& name);
    
    static bool Valid(uint32_t statval);
};

class FileTable;

class FileAttrDB {
    FileTable&                        ft;
    std::string                       path;
    std::map<std::string, FileAttrs*> attrs;
    
public:
    FileAttrDB(FileTable& ft, const std::string& directory);
    ~FileAttrDB();
    
    void       Remove      (const std::string& name);
    FileAttrs *GetFileAttrs(const std::string& name);
    void       SetFileAttrs(const std::string& path, const FileAttrs& fstat);
    void       Write       (void);
};

class FileTable {
    std::map<uint64_t, std::string> handle2path;
    std::string                     basePathAlias;
    std::string                     basePath;

    std::string ResolveAlias       (const std::string& path);
    void        Insert      (uint64_t handle, const std::string& path);
    void        Dirty       (FileAttrDB* db);
    std::string ToHostPath  (const std::string& _path);
protected:
    std::set<FileAttrDB*>           dirty;
    std::map<uint64_t, FileAttrDB*> handle2db;
    
    static std::string filename    (const std::string& path);
    static std::string dirname     (const std::string& path);
    static std::string canonicalize(const std::string& path);

    FileAttrs*  GetFileAttrs(const std::string& path);
    FileAttrDB* GetDB       (uint64_t handle);
public:
    FileTable(const std::string& basePath, const std::string& basePathAlias);
    virtual ~FileTable(void);
    
    void        Write          (void);
    std::string GetBasePath    (void);
    std::string GetBasePathAlias(void);
    int         Stat           (const std::string& path, struct stat& stat);
    bool        GetCanonicalPath(uint64_t fhandle, std::string& result);
    void        Move           (const std::string& pathFrom, const std::string& pathTo);
    void        Remove         (const std::string& path);
    uint64_t    GetFileHandle  (const std::string& path);
    void        SetFileAttrs   (const std::string& path, const FileAttrs& fstat);
    uint32_t    FileId         (uint64_t ino);

    FILE*       fopen   (const std::string& path, const char* mode);
    int         chmod   (const std::string& path, mode_t mode);
    int         access  (const std::string& path, int mode);
    DIR*        opendir (const std::string& path);
    int         remove  (const std::string& path);
    int         rename  (const std::string& from, const std::string& to);
    int         readlink(const std::string& path1, std::string& result);
    int         link    (const std::string& path1, const std::string& path2, bool soft);
    int         mkdir   (const std::string& path, mode_t mode);
    int         nftw    (const std::string& path, int (*fn)(const char *, const struct stat *ptr, int flag, struct FTW *), int depth, int flags);
    int         statvfs (const std::string& path, struct statvfs& fsstat);
    int         stat    (const std::string& path, struct stat& fstat);
    int         utimes  (const std::string& path, const struct timeval times[2]);
    
    static std::string MakePath(const std::string& directory, const std::string& file);
    
    friend class FileAttrDB;
};

#endif