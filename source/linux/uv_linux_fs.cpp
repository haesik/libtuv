/* Copyright 2015 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* Caveat emptor: this file deviates from the libuv convention of returning
 * negated errno codes. Most uv_fs_*() functions map directly to the system
 * call of the same name. For more complex wrappers, it's easier to just
 * return -1 with errno set. The dispatcher in uv__fs_work() takes care of
 * getting the errno to the right place (req->result or as the return value.)
 */

#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <stdio.h>

#include <uv.h>
#include "uv_internal.h"


//-----------------------------------------------------------------------------

#define INIT(type)                                                            \
  do {                                                                        \
    uv__req_init((loop), (req), UV_FS);                                       \
    (req)->fs_type = UV_FS_ ## type;                                          \
    (req)->result = 0;                                                        \
    (req)->ptr = NULL;                                                        \
    (req)->loop = loop;                                                       \
    (req)->path = NULL;                                                       \
    (req)->cb = (cb);                                                         \
  }                                                                           \
  while (0)
/*
    (req)->fs_type = UV_FS_ ## type;                                          \
    (req)->result = 0;                                                        \
    (req)->ptr = NULL;                                                        \
    (req)->loop = loop;                                                       \
    (req)->path = NULL;                                                       \
    (req)->new_path = NULL;                                                   \
    (req)->cb = (cb);                                                         \
*/


#define PATH                                                                  \
  do {                                                                        \
    (req)->path = strdup(path);                                               \
    if ((req)->path == NULL)                                                  \
      return -ENOMEM;                                                         \
  }                                                                           \
  while (0)


#define POST                                                                  \
  do {                                                                        \
    if ((cb) != NULL) {                                                       \
      uv__work_submit((loop), &(req)->work_req, uv__fs_work, uv__fs_done);    \
      return 0;                                                               \
    }                                                                         \
    else {                                                                    \
      uv__fs_work(&(req)->work_req);                                          \
      uv__fs_done(&(req)->work_req, 0);                                       \
      return (req)->result;                                                   \
    }                                                                         \
  }                                                                           \
  while (0)


//-----------------------------------------------------------------------------
//

static ssize_t uv__fs_fdatasync(uv_fs_t* req) {
  return fdatasync(req->file);
}


static void uv__to_stat(struct stat* src, uv_stat_t* dst) {
  dst->st_dev = src->st_dev;
  dst->st_nlink = src->st_nlink;
  dst->st_uid = src->st_uid;
  dst->st_gid = src->st_gid;
  dst->st_rdev = src->st_rdev;
  dst->st_ino = src->st_ino;
  dst->st_mode = src->st_mode;
  dst->st_size = src->st_size;
  dst->st_blksize = src->st_blksize;
  dst->st_blocks = src->st_blocks;

  dst->st_atim.tv_sec = src->st_atime;
  dst->st_atim.tv_nsec = 0;
  dst->st_mtim.tv_sec = src->st_mtime;
  dst->st_mtim.tv_nsec = 0;
  dst->st_ctim.tv_sec = src->st_ctime;
  dst->st_ctim.tv_nsec = 0;
  dst->st_birthtim.tv_sec = src->st_ctime;
  dst->st_birthtim.tv_nsec = 0;
  dst->st_flags = 0;
  dst->st_gen = 0;
}


static int uv__fs_fstat(int fd, uv_stat_t *buf) {
  struct stat pbuf;
  int ret;
  ret = fstat(fd, &pbuf);
  uv__to_stat(&pbuf, buf);
  return ret;
}


static ssize_t uv__fs_read(uv_fs_t* req) {
  static int no_preadv;
  ssize_t result;

  if (req->off < 0) {
    if (req->nbufs == 1)
      result = read(req->file, req->bufs[0].base, req->bufs[0].len);
    else
      result = readv(req->file, (struct iovec*) req->bufs, req->nbufs);
  } else {
    if (req->nbufs == 1) {
      result = pread(req->file, req->bufs[0].base, req->bufs[0].len, req->off);
      goto done;
    }

#if HAVE_PREADV
    result = preadv(req->file, (struct iovec*) req->bufs, req->nbufs, req->off);
#else
    if (no_preadv) retry:
    {
      off_t nread;
      size_t index;

      nread = 0;
      index = 0;
      result = 1;
      do {
        if (req->bufs[index].len > 0) {
          result = pread(req->file,
                         req->bufs[index].base,
                         req->bufs[index].len,
                         req->off + nread);
          if (result > 0)
            nread += result;
        }
        index++;
      } while (index < req->nbufs && result > 0);
      if (nread > 0)
        result = nread;
    }
    else {
      result = uv__preadv(req->file,
                          (struct iovec*)req->bufs,
                          req->nbufs,
                          req->off);
      if (result == -1 && errno == ENOSYS) {
        no_preadv = 1;
        goto retry;
      }
    }
#endif
  }

done:
  if (req->bufs != req->bufsml)
    free(req->bufs);
  return result;
}


static ssize_t uv__fs_write(uv_fs_t* req) {
  static int no_pwritev;
  ssize_t r;

  if (req->off < 0) {
    if (req->nbufs == 1)
      r = write(req->file, req->bufs[0].base, req->bufs[0].len);
    else
      r = writev(req->file, (struct iovec*) req->bufs, req->nbufs);
  } else {
    if (req->nbufs == 1) {
      r = pwrite(req->file, req->bufs[0].base, req->bufs[0].len, req->off);
      goto done;
    }
#if HAVE_PREADV
    r = pwritev(req->file, (struct iovec*) req->bufs, req->nbufs, req->off);
#else
    if (no_pwritev) retry:
    {
      off_t written;
      size_t index;

      written = 0;
      index = 0;
      r = 0;
      do {
        if (req->bufs[index].len > 0) {
          r = pwrite(req->file,
                     req->bufs[index].base,
                     req->bufs[index].len,
                     req->off + written);
          if (r > 0)
            written += r;
        }
        index++;
      } while (index < req->nbufs && r >= 0);
      if (written > 0)
        r = written;
    }
    else {
      r = uv__pwritev(req->file,
                      (struct iovec*) req->bufs,
                      req->nbufs,
                      req->off);
      if (r == -1 && errno == ENOSYS) {
        no_pwritev = 1;
        goto retry;
      }
    }
#endif
  }

done:
  if (req->bufs != req->bufsml)
    free(req->bufs);

  return r;
}


static void uv__fs_done(struct uv__work* w, int status) {
  uv_fs_t* req;

  req = container_of(w, uv_fs_t, work_req);
  uv__req_unregister(req->loop, req);

  if (status == -ECANCELED) {
    assert(req->result == 0);
    req->result = -ECANCELED;
  }

  if (req->cb != NULL)
    req->cb(req);
}


//-----------------------------------------------------------------------------
//

static void uv__fs_work(struct uv__work* w) {
  int retry_on_eintr;
  uv_fs_t* req;
  ssize_t r;
#ifdef O_CLOEXEC
  static int no_cloexec_support;
#endif  /* O_CLOEXEC */

  req = container_of(w, uv_fs_t, work_req);
  retry_on_eintr = !(req->fs_type == UV_FS_CLOSE);

  do {
    errno = 0;

#define X(type, action)                                                       \
  case UV_FS_ ## type:                                                        \
    r = action;                                                               \
    break;

    switch (req->fs_type) {
    //X(CHMOD, chmod(req->path, req->mode));
    //X(CHOWN, chown(req->path, req->uid, req->gid));
    //X(FCHMOD, fchmod(req->file, req->mode));
    //X(FCHOWN, fchown(req->file, req->uid, req->gid));
    //X(FTRUNCATE, ftruncate(req->file, req->off));
    //X(LINK, link(req->path, req->new_path));
    //X(SYMLINK, symlink(req->path, req->new_path));
    //X(ACCESS, access(req->path, req->flags));
    X(CLOSE, close(req->file));
    X(FDATASYNC, uv__fs_fdatasync(req));
    X(FSTAT, uv__fs_fstat(req->file, &req->statbuf));
    X(FSYNC, fsync(req->file));
    //X(FUTIME, uv__fs_futime(req));
    //X(LSTAT, uv__fs_lstat(req->path, &req->statbuf));
    //X(MKDIR, mkdir(req->path, req->mode));
    //X(MKDTEMP, uv__fs_mkdtemp(req));
    X(READ, uv__fs_read(req));
    //X(SCANDIR, uv__fs_scandir(req));
    //X(READLINK, uv__fs_readlink(req));
    //X(RENAME, rename(req->path, req->new_path));
    //X(RMDIR, rmdir(req->path));
    //X(SENDFILE, uv__fs_sendfile(req));
    //X(STAT, uv__fs_stat(req->path, &req->statbuf));
    //X(UNLINK, unlink(req->path));
    //X(UTIME, uv__fs_utime(req));
    X(WRITE, uv__fs_write(req));

    case UV_FS_OPEN:
#ifdef O_CLOEXEC
      /* Try O_CLOEXEC before entering locks */
      if (!no_cloexec_support) {
        r = open(req->path, req->flags | O_CLOEXEC, req->mode);
        if (r >= 0)
          break;
        if (errno != EINVAL)
          break;
        no_cloexec_support = 1;
      }
#endif  /* O_CLOEXEC */
      if (req->cb != NULL)
        uv_rwlock_rdlock(&req->loop->cloexec_lock);
      r = open(req->path, req->flags, req->mode);

      /*
       * In case of failure `uv__cloexec` will leave error in `errno`,
       * so it is enough to just set `r` to `-1`.
       */
      if (r >= 0 && uv__cloexec(r, 1) != 0) {
        r = uv__close(r);
        if (r != 0 && r != -EINPROGRESS)
          abort();
        r = -1;
      }
      if (req->cb != NULL)
        uv_rwlock_rdunlock(&req->loop->cloexec_lock);
      break;
    default: abort();
    }

#undef X
  }
  while (r == -1 && errno == EINTR && retry_on_eintr);

  if (r == -1)
    req->result = -errno;
  else
    req->result = r;

  if (r == 0 && (req->fs_type == UV_FS_STAT ||
                 req->fs_type == UV_FS_FSTAT ||
                 req->fs_type == UV_FS_LSTAT)) {
    req->ptr = &req->statbuf;
  }
}


//-----------------------------------------------------------------------------
//

int uv_fs_open(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags,
               int mode, uv_fs_cb cb) {
  INIT(OPEN);
  PATH;
  req->flags = flags;
  req->mode = mode;
  POST;
}


int uv_fs_fdatasync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  INIT(FDATASYNC);
  req->file = file;
  POST;
}


int uv_fs_fsync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  INIT(FSYNC);
  req->file = file;
  POST;
}


int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  INIT(CLOSE);
  req->file = file;
  POST;
}


int uv_fs_write(uv_loop_t* loop, uv_fs_t* req, uv_file file,
                const uv_buf_t bufs[], unsigned int nbufs, int64_t off,
                uv_fs_cb cb) {
  INIT(WRITE);
  req->file = file;

  req->nbufs = nbufs;
  req->bufs = req->bufsml;
  if (nbufs > ARRAY_SIZE(req->bufsml))
    req->bufs = (uv_buf_t*)malloc(nbufs * sizeof(*bufs));

  if (req->bufs == NULL)
    return -ENOMEM;

  memcpy(req->bufs, bufs, nbufs * sizeof(*bufs));

  req->off = off;
  POST;
}


void uv_fs_req_cleanup(uv_fs_t* req) {
  free((void*) req->path);
  req->path = NULL;
  //req->new_path = NULL;

  if (req->fs_type == UV_FS_SCANDIR && req->ptr != NULL)
    uv__fs_scandir_cleanup(req);

  if (req->ptr != &req->statbuf)
    free(req->ptr);
  req->ptr = NULL;
}

