#include <stdio.h>
#include <mini-os/types.h>
#include <mini-os/xmalloc.h>
#include <string.h>
#include <sqlite3.h>
#include <mini-os/blkfront.h>

#define VFS_NAME "mirage"

/* First sector of VBD is used for database metadata */
#define DB_NAME_MAXLENGTH 32
struct db_metadata {
  uint32_t version;
  uint64_t size;
  char name[DB_NAME_MAXLENGTH];
};

struct blkfront_dev *blk_dev;
struct blkfront_info *blk_info;
struct db_metadata *db_metadata;

static int 
mirClose(sqlite3_file *file)
{
  printf("mirClose: ERR\n");
  return SQLITE_ERROR;
}

/* Write database metadata to the first sector of the block device.
   Buffer must have come from readMetadata */
static void
writeMetadata(struct db_metadata *m)
{
  struct blkfront_aiocb req;
  bzero(&req, sizeof(struct blkfront_aiocb));
  ASSERT(blk_info->sector_size >= sizeof(struct db_metadata));
  printf("writeMetadata: ");
  req.aio_dev = blk_dev;
  req.aio_buf = (void *)m;
  req.aio_nbytes = blk_info->sector_size;
  req.aio_offset = 0;
  req.data = &req;
  blkfront_write(&req);
  printf(" OK\n");
}

/* Read database metadata from first sector of blk device.
   If it is uninitialized, then set the version field and
   sync it to disk.
   Returns a whole sector so it can be easily written again */
static struct db_metadata *
readMetadata(void)
{
  struct blkfront_aiocb req;
  struct db_metadata *m;
  bzero(&req, sizeof(struct blkfront_aiocb));
  ASSERT(blk_info->sector_size >= sizeof(struct db_metadata));
  printf("readMetadata: ");
  req.aio_dev = blk_dev;
  req.aio_buf = _xmalloc(blk_info->sector_size, blk_info->sector_size);
  req.aio_nbytes = blk_info->sector_size;
  req.aio_offset = 0;
  req.data = &req;
  blkfront_read(&req);
  m = (struct db_metadata *)req.aio_buf;
  if (m->version == 0) {
    printf(" NEW\n");
    m->version = 1;
    strcpy(m->name, "unknown");
    writeMetadata(m);
  } else
    printf(" '%s' v=%lu sz=%Lu OK\n", m->name, m->version, m->size);
  return m;
}

/*
** Read data from a file into a buffer.  Return SQLITE_OK if all
** bytes were read successfully and SQLITE_IOERR if anything goes
** wrong.
*/
static int 
mirRead(sqlite3_file *id, void *pBuf, int amt, sqlite3_int64 offset) {
  struct blkfront_aiocb *req;
  uint64_t sector, start_offset, nsectors, nbytes;

  /* Our reads to blkfront must be sector-aligned */
  start_offset = offset % blk_info->sector_size;
  /* Add 1 to sector to skip metadata block */
  sector = 1 + (offset / blk_info->sector_size);
  nsectors = 1 + ((amt + start_offset) / blk_info->sector_size);
  nbytes = nsectors * blk_info->sector_size;
 
  printf("mirRead: amt=%d off=%Lu start_offset=%Lu sector=%Lu nsectors=%Lu nbytes=%Lu\n", amt, offset, start_offset, sector, nsectors, nbytes); 
  req = xmalloc(struct blkfront_aiocb);
  req->aio_dev = blk_dev;
  req->aio_buf = _xmalloc(nbytes, blk_info->sector_size);
  req->aio_nbytes = nbytes;
  req->aio_offset = sector * blk_info->sector_size;
  req->data = req;

  /* XXX modify blkfront.c:blkfront_io to return error code if any */
  blkfront_read(req);
  bcopy(req->aio_buf + start_offset, pBuf, amt);

  free(req->aio_buf);
  free(req);

#if 1
  printf("mirRead: ");
  for (int i = 0; i < amt; i++) printf("%Lx ", ((char *)pBuf)[i]);
  printf("\n");
#endif

  return SQLITE_OK;
}

/*
** Write data from a buffer into a file.  Return SQLITE_OK on success
** or some other error code on failure.
*/
static int
mirWrite(sqlite3_file *id, const void *pBuf, int amt, sqlite3_int64 offset) {
  printf("mirWrite: ERR\n");
  return SQLITE_ERROR;
}

/*
** Truncate an open file to a specified size
*/
static int 
mirTruncate(sqlite3_file *id, sqlite3_int64 nByte) {
  printf("mirTruncate: ERR\n");
  return SQLITE_ERROR;
}

/*
** Make sure all writes to a particular file are committed to disk.
**
** If dataOnly==0 then both the file itself and its metadata (file
** size, access time, etc) are synced.  If dataOnly!=0 then only the
** file data is synced.
**
*/
static int 
mirSync(sqlite3_file *id, int flags) {
  int isDataOnly = (flags & SQLITE_SYNC_DATAONLY);
  int isFullSync = (flags & 0x0F) == SQLITE_SYNC_FULL;
  printf("mirSync: data=%d fullsync=%d ERR\n", isDataOnly, isFullSync);
  return SQLITE_ERROR;
}

/*
** Determine the current size of a file in bytes
*/
static int 
mirFileSize(sqlite3_file *id, sqlite3_int64 *pSize) {
  ASSERT(db_metadata != NULL);
  printf("mirFileSize: %Lu OK\n", db_metadata->size);
  *pSize = db_metadata->size;
  return SQLITE_OK;
}

static int
mirCheckLock(sqlite3_file *NotUsed, int *pResOut) {
  printf("mirCheckLock: OK\n");
  *pResOut = 0;
  return SQLITE_OK;
}

static int
mirLock(sqlite3_file *NotUsed, int NotUsed2) {
  printf("mirLock: OK\n");
  return SQLITE_OK;
}

static int 
mirUnlock(sqlite3_file *NotUsed, int NotUsed2) {
  printf("mirUnlock: OK\n");
  return SQLITE_OK;
}

/*
** Information and control of an open file handle.
*/
static int
mirFileControl(sqlite3_file *id, int op, void *pArg) {
  printf("mirFileControl: ERR\n");
  return SQLITE_ERROR;
}

/*
** Return the sector size in bytes of the underlying block device for
** the specified file. This is almost always 512 bytes, but may be
** larger for some devices.
**
** SQLite code assumes this function cannot fail. It also assumes that
** if two files are created in the same file-system directory (i.e.
** a database and its journal file) that the sector size will be the
** same for both.
*/
static int 
mirSectorSize(sqlite3_file *NotUsed) {
  printf("mirSectorSize: %d OK\n", blk_info->sector_size);
  return blk_info->sector_size;
}

/*
** Return the device characteristics for the file. This is always 0 for unix.
*/
static int 
mirDeviceCharacteristics(sqlite3_file *NotUsed) {
  printf("mirDeviceCharacteristics: NOOP\n");
  return 0;
}

static sqlite3_io_methods mirIoMethods = {
   1,                          /* iVersion */
   mirClose,                   /* xClose */
   mirRead,                    /* xRead */
   mirWrite,                   /* xWrite */
   mirTruncate,                /* xTruncate */
   mirSync,                    /* xSync */
   mirFileSize,                /* xFileSize */
   mirLock,                    /* xLock */
   mirUnlock,                  /* xUnlock */
   mirCheckLock,               /* xCheckReservedLock */
   mirFileControl,             /* xFileControl */
   mirSectorSize,              /* xSectorSize */
   mirDeviceCharacteristics    /* xDeviceCapabilities */
};

struct mirFile {
    sqlite3_io_methods const *pMethods;
    int reserved;
};

static sqlite3_file mirFileIO;

/** Open the file zPath.
** 
**     ReadWrite() ->     (READWRITE | CREATE)
**     ReadOnly()  ->     (READONLY) 
**     OpenExclusive() -> (READWRITE | CREATE | EXCLUSIVE)
**
** The old OpenExclusive() accepted a boolean argument - "delFlag". If
** true, the file was configured to be automatically deleted when the
** file handle closed. To achieve the same effect using this new 
** interface, add the DELETEONCLOSE flag to those specified above for 
** OpenExclusive().
*/

static int mirOpen(
  sqlite3_vfs *pVfs,           /* The VFS for which this is the xOpen method */
  const char *zPath,           /* Pathname of file to be opened */
  sqlite3_file *pFile,         /* The file descriptor to be filled in */
  int flags,                   /* Input flags to control the opening */
  int *pOutFlags               /* Output flags returned to SQLite core */
) {
    int eType = flags & 0xFFFFFF00;  /* Type of file to open */
    printf("mirOpen: path=%s type ", zPath);
    switch (eType) {
    case SQLITE_OPEN_MAIN_DB:
      printf("main_db");
      break;
    case SQLITE_OPEN_MAIN_JOURNAL:
      printf("main_journal");
      break;
    case SQLITE_OPEN_TEMP_DB:
      printf("temp_db");
      break;
    case SQLITE_OPEN_TEMP_JOURNAL:
      printf("temp_journal");
      break;
    case SQLITE_OPEN_TRANSIENT_DB:
      printf("transient_db");
      break;
    case SQLITE_OPEN_SUBJOURNAL:
      printf("subjournal");
      break;
    case SQLITE_OPEN_MASTER_JOURNAL:
      printf("master_journal");
      break;
    default:
      printf("???");
    }
    if (eType != SQLITE_OPEN_MAIN_DB) {
      printf(" ERR\n");
      return SQLITE_ERROR;
    } else
      printf(" OK\n");
    bcopy(&mirFileIO, pFile, sizeof(mirFileIO));
    db_metadata = readMetadata();
    return SQLITE_OK;
}

static int mirDelete(
  sqlite3_vfs *NotUsed,     /* VFS containing this as the xDelete method */
  const char *zPath,        /* Name of file to be deleted */
  int dirSync               /* If true, fsync() directory after deleting file */
) {
    ASSERT(db_metadata != NULL);
    printf("mirDelete: %s\n", zPath);
    db_metadata->size = 0;
    writeMetadata(db_metadata);
    return SQLITE_OK;
}

/*
** Test the existance of or access permissions of file zPath. The
** test performed depends on the value of flags:
**
**     SQLITE_ACCESS_EXISTS: Return 1 if the file exists
**     SQLITE_ACCESS_READWRITE: Return 1 if the file is read and writable.
**     SQLITE_ACCESS_READONLY: Return 1 if the file is readable.
**
** Otherwise return 0.
*/
static int mirAccess(
  sqlite3_vfs *NotUsed,   /* The VFS containing this xAccess method */
  const char *zPath,      /* Path of the file to examine */
  int flags,              /* What do we want to learn about the zPath file? */
  int *pResOut            /* Write result boolean here */
) {
    printf("mirAccess: OK\n");
    *pResOut = 1;
    return SQLITE_OK;
}

/*
** Turn a relative pathname into a full pathname. The relative path
** is stored as a nul-terminated string in the buffer pointed to by
** zPath. 
**
** zOut points to a buffer of at least sqlite3_vfs.mxPathname bytes 
** (in this case, MAX_PATHNAME bytes). The full-path is written to
** this buffer before returning.
*/
static int mirFullPathname(
  sqlite3_vfs *pVfs,            /* Pointer to vfs object */
  const char *zPath,            /* Possibly relative input path */
  int nOut,                     /* Size of output buffer in bytes */
  char *zOut                    /* Output buffer */
) {
    printf("mirFullPathname: %s OK\n", zPath);
    strcpy(zOut, zPath);
    return SQLITE_OK;
}

/*
** Write nBuf bytes of random data to the supplied buffer zBuf.
*/
static int mirRandomness(
  sqlite3_vfs *NotUsed, 
  int nBuf, 
  char *zBuf
) {
    printf("mirRandomness: ");
    for (int i=0; i < nBuf; i++)
      zBuf[i] = rand();
    printf (" OK\n");
    return SQLITE_OK;
}

static int mirSleep(
  sqlite3_vfs *NotUsed, 
  int microseconds
) {
    printf("mirSleep: ERR\n");
    return SQLITE_ERROR;
}

static int mirCurrentTime(
  sqlite3_vfs *NotUsed, 
  double *prNow
) {
    printf("mirCurrentTime: ERR\n");
    return SQLITE_ERROR;
}

static int mirGetLastError(
  sqlite3_vfs *NotUsed, 
  int NotUsed2, 
  char *NotUsed3
) {
  printf("mirGetLastError: ERR\n");
  return SQLITE_ERROR;
}

struct sqlite3_vfs mirVfs = {
    1,                    /* iVersion */
    sizeof(sqlite3_file), /* szOsFile */
    1024,                 /* mxPathname */
    0,                    /* pNext */ 
    VFS_NAME,             /* zName */
    NULL,                 /* pAppData */
    mirOpen,              /* xOpen */
    mirDelete,            /* xDelete */
    mirAccess,            /* xAccess */
    mirFullPathname,      /* xFullPathname */
    NULL,                 /* xDlOpen */
    NULL,                 /* xDlError */
    NULL,                 /* xDlSym */
    NULL,                 /* xDlClose */
    mirRandomness,        /* xRandomness */
    mirSleep,             /* xSleep */
    mirCurrentTime,       /* xCurrentTime */
    mirGetLastError       /* xGetLastError */
};

int sqlite3_os_init(void) {
  int rc;
  printf("sqlite3_os_init: ");
  mirFileIO.pMethods = &mirIoMethods;
  rc = sqlite3_vfs_register(&mirVfs, 1);
  if (rc != SQLITE_OK)
    printf("error registering VFS\n");
  else
    printf("ok\n");
  return SQLITE_OK;
}

int sqlite3_os_end(void) {
  printf("sqlite3_os_end\n");
  return SQLITE_OK;
}


static int sqlite3_test_cb(void *arg, int argc, char **argv, char **col) {
  for (int i=0; i<argc; i++)
    printf("SELECT: %s = %s\n", col[i], argv[i] ? argv[i] : "NULL");
  return 0;
}

void sqlite3_test(struct blkfront_dev *dev, struct blkfront_info *info) {
  sqlite3 *db;
  int ret;
  char *errmsg;
  blk_dev = dev;
  blk_info = info;
  ret = sqlite3_open("test.db", &db);
  if (ret) {
     printf("OPEN err: %s\n", sqlite3_errmsg(db));
  } else {
     printf("OPEN ok\n");
     ret = sqlite3_exec(db, "create table foo (bar1 TEXT, bar2 INTEGER)", NULL, NULL, &errmsg);
     if (ret) {
       printf("CREATE err: %s\n", sqlite3_errmsg(db));
       exit(1);
     } else
       printf("CREATE ok\n");
     ret = sqlite3_exec(db, "insert into foo VALUES(\"hello\", 1000)", NULL, NULL, &errmsg);
     if (ret) {
       printf("INSERT1 err: %s\n", sqlite3_errmsg(db));
       exit(1);
     } else
       printf("INSERT1 ok\n");
     ret = sqlite3_exec(db, "insert into foo VALUES(\"world\", 2000)", NULL, NULL, &errmsg);
     if (ret) {
       printf("INSERT2 err: %s\n", sqlite3_errmsg(db));
       exit(1);
     } else
       printf("INSERT2 ok\n");
     ret = sqlite3_exec(db, "select * from foo", sqlite3_test_cb, NULL , &errmsg);
     if (ret) {
       printf("SELECT err: %s\n", sqlite3_errmsg(db));
       exit(1);
     } else
       printf("INSERT ok\n");
     ret = sqlite3_close(db);
  }
  exit(1);
}