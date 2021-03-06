#include "xfile.h"

static int file_read(void *cookie, char *ptr, int size) {
  FILE *file = cookie;
  int r;

  size = 1;                     /* override size */

  r = (int)fread(ptr, 1, (size_t)size, file);
  if (r < size && ferror(file)) {
    return -1;
  }
  if (r == 0 && feof(file)) {
    clearerr(file);
  }
  return r;
}

static int file_write(void *cookie, const char *ptr, int size) {
  FILE *file = cookie;
  int r;

  r = (int)fwrite(ptr, 1, (size_t)size, file);
  if (r < size) {
    return -1;
  }
  fflush(cookie);
  return r;
}

static long file_seek(void *cookie, long pos, int whence) {
  switch (whence) {
  case XSEEK_CUR:
    whence = SEEK_CUR;
    break;
  case XSEEK_SET:
    whence = SEEK_SET;
    break;
  case XSEEK_END:
    whence = SEEK_END;
    break;
  }
  return fseek(cookie, pos, whence);
}

static int file_close(void *cookie) {
  return fclose(cookie);
}

xFILE *xfopen(const char *name, const char *mode) {
  FILE *fp;

  if ((fp = fopen(name, mode)) == NULL) {
    return NULL;
  }

  switch (*mode) {
  case 'r':
    return xfunopen(fp, file_read, NULL, file_seek, file_close);
  default:
    return xfunopen(fp, NULL, file_write, file_seek, file_close);
  }
}

#define FILE_VTABLE { 0, file_read, file_write, file_seek, file_close }

xFILE x_iob[XOPEN_MAX] = {
  { { 0 }, 0, NULL, NULL, FILE_VTABLE, X_READ },
  { { 0 }, 0, NULL, NULL, FILE_VTABLE, X_WRITE | X_LNBUF },
  { { 0 }, 0, NULL, NULL, FILE_VTABLE, X_WRITE | X_UNBUF }
};

xFILE *xfunopen(void *cookie, int (*read)(void *, char *, int), int (*write)(void *, const char *, int), long (*seek)(void *, long, int), int (*close)(void *)) {
  xFILE *fp;

  for (fp = x_iob; fp < x_iob + XOPEN_MAX; fp++)
    if ((fp->flag & (X_READ | X_WRITE)) == 0)
      break;  /* found free slot */

  if (fp >= x_iob + XOPEN_MAX)  /* no free slots */
    return NULL;

  fp->cnt = 0;
  fp->base = NULL;
  fp->flag = read? X_READ : X_WRITE;

  fp->vtable.cookie = cookie;
  fp->vtable.read = read;
  fp->vtable.write = write;
  fp->vtable.seek = seek;
  fp->vtable.close = close;

  return fp;
}

int xfclose(xFILE *fp) {
  extern void free(void *);     /* FIXME */

  xfflush(fp);
  fp->flag = 0;
  if (fp->base != fp->buf)
    free(fp->base);
  return fp->vtable.close(fp->vtable.cookie);
}

int x_fillbuf(xFILE *fp) {
  extern void *malloc(size_t);  /* FIXME */
  int bufsize;

  if ((fp->flag & (X_READ|X_EOF|X_ERR)) != X_READ)
    return EOF;
  if (fp->base == NULL) {
    if ((fp->flag & X_UNBUF) == 0) {
      /* no buffer yet */
      if ((fp->base = malloc(XBUFSIZ)) == NULL) {
        /* can't get buffer, try unbuffered */
        fp->flag |= X_UNBUF;
      }
    }
    if (fp->flag & X_UNBUF) {
      fp->base = fp->buf;
    }
  }
  bufsize = (fp->flag & X_UNBUF) ? sizeof(fp->buf) : XBUFSIZ;

  fp->ptr = fp->base;
  fp->cnt = fp->vtable.read(fp->vtable.cookie, fp->ptr, bufsize);

  if (--fp->cnt < 0) {
    if (fp->cnt == -1)
      fp->flag |= X_EOF;
    else
      fp->flag |= X_ERR;
    fp->cnt = 0;
    return EOF;
  }

  return (unsigned char) *fp->ptr++;
}

int x_flushbuf(int x, xFILE *fp) {
  extern void *malloc(size_t);  /* FIXME */
  int num_written=0, bufsize=0;
  char c = x;

  if ((fp->flag & (X_WRITE|X_EOF|X_ERR)) != X_WRITE)
    return EOF;
  if (fp->base == NULL && ((fp->flag & X_UNBUF) == 0)) {
    /* no buffer yet */
    if ((fp->base = malloc(XBUFSIZ)) == NULL) {
      /* couldn't allocate a buffer, so try unbuffered */
      fp->flag |= X_UNBUF;
    } else {
      fp->ptr = fp->base;
      fp->cnt = XBUFSIZ - 1;
    }
  }
  if (fp->flag & X_UNBUF) {
    /* unbuffered write */
    fp->ptr = fp->base = NULL;
    fp->cnt = 0;
    if (x == EOF)
      return EOF;
    num_written = fp->vtable.write(fp->vtable.cookie, (const char *) &c, 1);
    bufsize = 1;
  } else {
    /* buffered write */
    assert(fp->ptr);
    if (x != EOF) {
      *fp->ptr++ = (unsigned char) c;
    }
    bufsize = (int)(fp->ptr - fp->base);
    while(bufsize - num_written > 0) {
      int t;
      t = fp->vtable.write(fp->vtable.cookie, fp->base + num_written, bufsize - num_written);
      if (t < 0)
        break;
      num_written += t;
    }

    fp->ptr = fp->base;
    fp->cnt = BUFSIZ - 1;
  }

  if (num_written == bufsize) {
    return x;
  } else {
    fp->flag |= X_ERR;
    return EOF;
  }
}

int xfflush(xFILE *f) {
  int retval;
  int i;

  retval = 0;
  if (f == NULL) {
    /* flush all output streams */
    for (i = 0; i < XOPEN_MAX; i++) {
      if ((x_iob[i].flag & X_WRITE) && (xfflush(&x_iob[i]) == -1))
        retval = -1;
    }
  } else {
    if ((f->flag & X_WRITE) == 0)
      return -1;
    x_flushbuf(EOF, f);
    if (f->flag & X_ERR)
      retval = -1;
  }
  return retval;
}

int xfputc(int x, xFILE *fp) {
  return xputc(x, fp);
}

int xfgetc(xFILE *fp) {
  return xgetc(fp);
}

int xfputs(const char *s, xFILE *stream) {
  const char *ptr = s;
  while(*ptr != '\0') {
    if (xputc(*ptr, stream) == EOF)
      return EOF;
    ++ptr;
  }
  return (int)(ptr - s);
}

char *xfgets(char *s, int size, xFILE *stream) {
  int c;
  char *buf;

  xfflush(NULL);

  if (size == 0) {
    return NULL;
  }
  buf = s;
  while (--size > 0 && (c = xgetc(stream)) != EOF) {
    if ((*buf++ = c) == '\n')
      break;
  }
  *buf = '\0';

  return (c == EOF && buf == s) ? NULL : s;
}

int xputs(const char *s) {
  int i = 1;

  while(*s != '\0') {
    if (xputchar(*s++) == EOF)
      return EOF;
    i++;
  }
  if (xputchar('\n') == EOF) {
    return EOF;
  }
  return i;
}

char *xgets(char *s) {
  int c;
  char *buf;

  xfflush(NULL);

  buf = s;
  while ((c = xgetchar()) != EOF && c != '\n') {
    *buf++ = c;
  }
  *buf = '\0';

  return (c == EOF && buf == s) ? NULL : s;
}

int xungetc(int c, xFILE *fp) {
  unsigned char uc = c;

  if (c == EOF || fp->base == fp->ptr) {
    return EOF;
  }
  fp->cnt++;
  return *--fp->ptr = uc;
}

size_t xfread(void *ptr, size_t size, size_t count, xFILE *fp) {
  char *bptr = ptr;
  long nbytes;
  int c;

  nbytes = size * count;
  while (nbytes > fp->cnt) {
    memcpy(bptr, fp->ptr, fp->cnt);
    fp->ptr += fp->cnt;
    bptr += fp->cnt;
    nbytes -= fp->cnt;
    if ((c = x_fillbuf(fp)) == EOF) {
      return (size * count - nbytes) / size;
    } else {
      xungetc(c, fp);
    }
  }
  memcpy(bptr, fp->ptr, nbytes);
  fp->ptr += nbytes;
  fp->cnt -= nbytes;
  return count;
}

size_t xfwrite(const void *ptr, size_t size, size_t count, xFILE *fp) {
  const char *bptr = ptr;
  long nbytes;

  nbytes = size * count;
  while (nbytes > fp->cnt) {
    memcpy(fp->ptr, bptr, fp->cnt);
    fp->ptr += fp->cnt;
    bptr += fp->cnt;
    nbytes -= fp->cnt;
    if (x_flushbuf(EOF, fp) == EOF) {
      return (size * count - nbytes) / size;
    }
  }
  memcpy(fp->ptr, bptr, nbytes);
  fp->ptr += nbytes;
  fp->cnt -= nbytes;
  return count;
}

long xfseek(xFILE *fp, long offset, int whence) {
  long s;

  xfflush(fp);

  fp->ptr = fp->base;
  fp->cnt = 0;

  if ((s = fp->vtable.seek(fp->vtable.cookie, offset, whence)) != 0)
    return s;
  fp->flag &= ~X_EOF;
  return 0;
}

long xftell(xFILE *fp) {
  return xfseek(fp, 0, XSEEK_CUR);
}

void xrewind(xFILE *fp) {
  xfseek(fp, 0, XSEEK_SET);
  xclearerr(fp);
}

int xprintf(const char *fmt, ...) {
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = xvfprintf(xstdout, fmt, ap);
  va_end(ap);
  return n;
}

int xfprintf(xFILE *stream, const char *fmt, ...) {
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = xvfprintf(stream, fmt, ap);
  va_end(ap);
  return n;
}

static int print_int(xFILE *stream, long x, int base) {
  static const char digits[] = "0123456789abcdef";
  char buf[20];
  int i, c, neg;

  neg = 0;
  if (x < 0) {
    neg = 1;
    x = -x;
  }

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (neg) {
    buf[i++] = '-';
  }

  c = i;
  while (i-- > 0) {
    xputc(buf[i], stream);
  }
  return c;
}

int xvfprintf(xFILE *stream, const char *fmt, va_list ap) {
  const char *p;
  char *sval;
  int ival;
  double dval;
  void *vp;
  int cnt = 0;

  for (p = fmt; *p; p++) {
    if (*p != '%') {
      xputc(*p, stream);
      cnt++;
      continue;
    }
    switch (*++p) {
    case 'd':
    case 'i':
      ival = va_arg(ap, int);
      cnt += print_int(stream, ival, 10);
      break;
    case 'f':
      dval = va_arg(ap, double);
      cnt += print_int(stream, dval, 10);
      xputc('.', stream);
      cnt++;
      if ((ival = fabs((dval - floor(dval)) * 1e4) + 0.5) == 0) {
        cnt += xfputs("0000", stream);
      } else {
        int i;
        for (i = 0; i < 3 - (int)log10(ival); ++i) {
          xputc('0', stream);
          cnt++;
        }
        cnt += print_int(stream, ival, 10);
      }
      break;
    case 's':
      sval = va_arg(ap, char*);
      cnt += xfputs(sval, stream);
      break;
    case 'p':
      vp = va_arg(ap, void*);
      cnt += xfputs("0x", stream);
      cnt += print_int(stream, (long)vp, 16);
      break;
    case '%':
      xputc(*(p-1), stream);
      cnt++;
      break;
    default:
      xputc('%', stream);
      xputc(*(p-1), stream);
      cnt += 2;
      break;
    }
  }
  return cnt;
}

#if 0
int main()
{
  char buf[256];

  xgets(buf);

  xprintf("%s\n", buf);
  xprintf("hello\n");
  xprintf("hello\n");
  //  xfflush(0);
}
#endif
