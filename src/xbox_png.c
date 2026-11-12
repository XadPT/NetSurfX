/* NetSurfX: o libpng do nxdk so tem a API de virgula fixa; o NetSurf usa a de virgula flutuante */
#include <png.h>

void png_set_gamma(png_structrp png_ptr, double screen_gamma, double file_gamma)
{
	png_set_gamma_fixed(png_ptr, (png_fixed_point)(screen_gamma * 100000.0 + 0.5),
			    (png_fixed_point)(file_gamma * 100000.0 + 0.5));
}

png_uint_32 png_get_gAMA(png_const_structrp png_ptr, png_const_inforp info_ptr, double *file_gamma)
{
	png_fixed_point g = 0;
	png_uint_32 r = png_get_gAMA_fixed(png_ptr, info_ptr, &g);
	if (r && file_gamma) *file_gamma = g / 100000.0;
	return r;
}
