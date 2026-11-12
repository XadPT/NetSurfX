/* o libpng do nxdk nao tem a API de virgula flutuante; o NetSurfX implementa
 * estas duas funcoes em src/xbox_png.c por cima da API de virgula fixa */
#ifndef NSSHIM_PNG_WRAP_H
#define NSSHIM_PNG_WRAP_H
#include_next <png.h>
#ifndef PNG_FLOATING_POINT_SUPPORTED
void png_set_gamma(png_structrp png_ptr, double screen_gamma, double file_gamma);
png_uint_32 png_get_gAMA(png_const_structrp png_ptr, png_const_inforp info_ptr, double *file_gamma);
#endif
#endif
