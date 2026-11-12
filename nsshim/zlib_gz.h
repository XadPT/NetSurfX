#ifndef NSSHIM_ZLIB_GZ_H
#define NSSHIM_ZLIB_GZ_H
/* funcoes gz* (o zlib do nxdk e Z_SOLO, sem elas). So leitura, ficheiros normais ou .gz */
typedef struct nsx_gzfile *gzFile;
gzFile gzopen(const char *path, const char *mode);
char *gzgets(gzFile f, char *buf, int len);
int gzread(gzFile f, void *buf, unsigned len);
int gzeof(gzFile f);
int gzclose(gzFile f);
#endif
