//
// Copyright (C) 1993-1996 Id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

// R_draw.c

extern int _wp1, _wp2, _wp3, _wp4, _wp5, _wp6, _wp7, _wp8, _wp9, _wp10;
extern int _wp11, _wp12, _wp13, _wp14, _wp15, _wp16, _wp17, _wp18, _wp19, _wp20, _wp21;
#include "DoomDef.h"
extern int _wp22, _wp23, _wp24, _wp25, _wp26, _wp27, _wp28, _wp29, _wp30, _wp31, _wp32;
extern int _wp33, _wp34, _wp35, _wp36, _wp37, _wp38, _wp39, _wp40, _wp41;
#include "R_local.h"

#define SC_INDEX			0x3c4
#define SC_MAPMASK			2
#define GC_INDEX			0x3ce
#define GC_READMAP			4
#define GC_MODE				5

/*

All drawing to the view buffer is accomplished in this file.  The other refresh
files only know about ccordinates, not the architecture of the frame buffer.

*/

byte *viewimage;
int viewwidth, scaledviewwidth, viewheight, viewwindowx, viewwindowy;
byte *ylookup[MAXHEIGHT];
int columnofs[MAXWIDTH];
byte translations[3][256]; // color tables for different players

/*
==================
=
= R_DrawColumn
=
= Source is the top of the column to scale
=
==================
*/

lighttable_t	*dc_colormap;
int				dc_x;
int				dc_yl;
int				dc_yh;
fixed_t			dc_iscale;
fixed_t			dc_texturemid;
int             dc_texheight;
byte			*dc_source;		// first pixel in a column (possibly virtual)

int				dccount;		// just for profiling

// -----------------------------------------------------------------------------
// R_DrawColumn
// A column is a vertical slice/span from a wall texture that, given the DOOM
// style restrictions on the view orientation, will always have constant z depth.
// Thus a special case loop for very fast rendering can be used.
// It has also been used with Wolfenstein 3D.
// 
// [crispy] replace R_DrawColumn() with Lee Killough's implementation
// found in MBF to fix Tutti-Frutti, taken from mbfsrc/R_DRAW.C:99-1979
// -----------------------------------------------------------------------------
// 
void R_DrawColumn (void) 
{ 
    int      count = dc_yh - dc_yl + 1;
    int      heightmask = dc_texheight-1;
    byte    *dest;
    fixed_t  frac;

    if (count <= 0)  // Zero length, column does not exceed a pixel.
    {
        return;
    }
				
#ifdef RANGECHECK
	if ((unsigned)dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
		I_Error ("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

 // [JN] Write bytes to the graphical output.
    outp (SC_INDEX+1 , 1 << (dc_x&3));

    // Framebuffer destination address.
    // Use ylookup LUT to avoid multiply with ScreenWidth.
    // Use columnofs LUT for subwindows?

    dest = destview + dc_yl*80 + (dc_x>>2); 

    // Determine scaling, which is the only mapping to be done.

    frac = dc_texturemid + (dc_yl-centery)*dc_iscale;

    // Inner loop that does the actual texture mapping, e.g. a DDA-lile scaling.
    // This is as fast as it gets.
    if (dc_texheight & heightmask)  // not a power of 2 -- killough
    {
        heightmask++;
        heightmask <<= FRACBITS;

        if (frac < 0)
            while ((frac += heightmask) < 0);
        else
            while (frac >= heightmask)
                   frac -= heightmask;

        do
        {
            // Re-map color indices from wall texture column
            //  using a lighting/special effects LUT.

           *dest = dc_colormap[dc_source[(frac>>FRACBITS)&127]];
            dest += SCREENWIDTH/4;
            if ((frac += dc_iscale) >= heightmask)
            {
                frac -= heightmask;
            }
        } while (--count);
    }
    else
    {
        while ((count-=2)>=0)   // texture height is a power of 2 -- killough
        {
            *dest = dc_colormap[dc_source[(frac>>FRACBITS) & heightmask]];
            dest += SCREENWIDTH/4;
            frac += dc_iscale;
            *dest = dc_colormap[dc_source[(frac>>FRACBITS) & heightmask]];
            dest += SCREENWIDTH/4;
            frac += dc_iscale;
        }

        if (count & 1)
        {
            *dest = dc_colormap[dc_source[(frac>>FRACBITS) & heightmask]];
        }
    }
}

void R_DrawColumnLow (void)
{
    int       count = dc_yh - dc_yl + 1;
    int       heightmask = dc_texheight-1;
    byte     *dest;
    fixed_t   frac;

    if (count <= 0)  // Zero length, column does not exceed a pixel.
    {
        return;
    }
				
#ifdef RANGECHECK
	if ((unsigned)dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
		I_Error ("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
//	dccount++;
#endif

    // [JN] Write bytes to the graphical output
    if (dc_x & 1)
    {
        outp (SC_INDEX+1,12);
    }
    else
    {
        outp (SC_INDEX+1,3);
    }

    dest = destview + dc_yl*80 + (dc_x>>1);
    frac = dc_texturemid + (dc_yl-centery)*dc_iscale;

    if (dc_texheight & heightmask)   // not a power of 2 -- killough
    {
        heightmask++;
        heightmask <<= FRACBITS;

        if (frac < 0)
          while ((frac += heightmask) < 0);
        else
          while (frac >= heightmask)
                 frac -= heightmask;

        do
        {
            // Re-map color indices from wall texture column
            //  using a lighting/special effects LUT.

            // heightmask is the Tutti-Frutti fix -- killough

            *dest = dc_colormap[dc_source[(frac>>FRACBITS)&127]];
            dest += SCREENWIDTH/4;
            if ((frac += dc_iscale) >= heightmask)
            {
                frac -= heightmask;
            }
        } while (--count);
    }
    else
    {
        while ((count-=2)>=0)   // texture height is a power of 2 -- killough
        {
            *dest = dc_colormap[dc_source[(frac>>FRACBITS) & heightmask]];
            dest += SCREENWIDTH/4;
            frac += dc_iscale;
            *dest = dc_colormap[dc_source[(frac>>FRACBITS) & heightmask]];
            dest += SCREENWIDTH/4;
            frac += dc_iscale;
        }

        if (count & 1)
        {
            *dest = dc_colormap[dc_source[(frac>>FRACBITS) & heightmask]];
        }
    }
}


#define FUZZTABLE	50

#ifdef __WATCOMC__
#define FUZZOFF	(SCREENWIDTH/4)
#else
#define FUZZOFF	(SCREENWIDTH)
#endif
int		fuzzoffset[FUZZTABLE] = {
FUZZOFF,-FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF
};
int fuzzpos = 0;

void R_DrawFuzzColumn (void)
{
	int			count;
	byte		*dest;
	fixed_t		frac, fracstep;	

	if (!dc_yl)
		dc_yl = 1;
	if (dc_yh == viewheight-1)
		dc_yh = viewheight - 2;
		
	count = dc_yh - dc_yl;
	if (count < 0)
		return;
				
#ifdef RANGECHECK
	if ((unsigned)dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
		I_Error ("R_DrawFuzzColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

#ifdef __WATCOMC__
	if (detailshift)
	{
		if (dc_x & 1)
		{
			outpw (GC_INDEX,GC_READMAP+(2<<8) ); 
			outp (SC_INDEX+1,12); 
		}
		else
		{
			outpw (GC_INDEX,GC_READMAP); 
			outp (SC_INDEX+1,3); 
		}
		dest = destview + dc_yl*80 + (dc_x>>1); 
	}
	else
	{
		outpw (GC_INDEX,GC_READMAP+((dc_x&3)<<8) ); 
		outp (SC_INDEX+1,1<<(dc_x&3)); 
		dest = destview + dc_yl*80 + (dc_x>>2); 
	}
#else
	dest = ylookup[dc_yl] + columnofs[dc_x];
#endif

	fracstep = dc_iscale;
	frac = dc_texturemid + (dc_yl-centery)*fracstep;

	do
	{
		*dest = colormaps[6*256+dest[fuzzoffset[fuzzpos]]];
		if (++fuzzpos == FUZZTABLE)
			fuzzpos = 0;
#ifdef __WATCOMC__
		dest += SCREENWIDTH/4;
#else
		dest += SCREENWIDTH;
#endif
		frac += fracstep;
	} while (count--);
}

/*
========================
=
= R_DrawTranslatedColumn
=
========================
*/

byte *dc_translation;
byte *translationtables;

void R_DrawTranslatedColumn (void)
{
	int			count;
	byte		*dest;
	fixed_t		frac, fracstep;	

	count = dc_yh - dc_yl;
	if (count < 0)
		return;
				
#ifdef RANGECHECK
	if ((unsigned)dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
		I_Error ("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

#ifdef __WATCOMC__
	if (detailshift)
	{
		if (dc_x & 1)
			outp (SC_INDEX+1,12); 
		else
			outp (SC_INDEX+1,3);
	
		dest = destview + dc_yl*80 + (dc_x>>1); 
	}
	else
	{
		outp (SC_INDEX+1,1<<(dc_x&3)); 
		dest = destview + dc_yl*80 + (dc_x>>2); 
	}
#else
	dest = ylookup[dc_yl] + columnofs[dc_x];
#endif
	
	fracstep = dc_iscale;
	frac = dc_texturemid + (dc_yl-centery)*fracstep;

	do
	{
		*dest = dc_colormap[dc_translation[dc_source[frac>>FRACBITS]]];
#ifdef __WATCOMC__
		dest += SCREENWIDTH/4;
#else
		dest += SCREENWIDTH;
#endif
		frac += fracstep;
	} while (count--);
}

/*
========================
=
= R_InitTranslationTables
=
========================
*/

void R_InitTranslationTables (void)
{
	int		i;

	translationtables = Z_Malloc (256*3+255, PU_STATIC, 0);
	translationtables = (byte *)(( (int)translationtables + 255 )& ~255);

//
// translate just the 16 green colors
//
	for(i = 0; i < 256; i++)
	{
		if (i >= 0x70 && i<= 0x7f)
		{	// map green ramp to gray, brown, red
			translationtables[i] = 0x60 + (i&0xf);
			translationtables [i+256] = 0x40 + (i&0xf);
			translationtables [i+512] = 0x20 + (i&0xf);
		}
		else
		{
			translationtables[i] = translationtables[i+256] 
			= translationtables[i+512] = i;
		}
	}
}

/*
================
=
= R_DrawSpan
=
================
*/

int				ds_y;
int				ds_x1;
int				ds_x2;
lighttable_t	*ds_colormap;
fixed_t			ds_xfrac;
fixed_t			ds_yfrac;
fixed_t			ds_xstep;
fixed_t			ds_ystep;
byte			*ds_source;		// start of a 64*64 tile image

int				dscount;		// just for profiling

// -----------------------------------------------------------------------------
// R_DrawSpan 
// With DOOM style restrictions on view orientation, the floors and ceilings
// consist of horizontal slices or spans with constant z depth. However,
// rotation around the world z axis is possible, thus this mapping, while
// simpler and faster than perspective correct texture mapping, has to traverse
// the texture at an angle in all but a few cases. In consequence, flats are
// not stored by column (like walls), and the inner loop has to step in 
// texture space u and v.
// -----------------------------------------------------------------------------

void R_DrawSpan (void) 
{ 
    int         spot; 
    int         i;
    int         prt;
    int         dsp_x1;
    int         dsp_x2;
    int         countp;
    byte       *dest; 
    fixed_t     xfrac;
    fixed_t     yfrac; 
	
#ifdef RANGECHECK
	if (ds_x2 < ds_x1 || ds_x1<0 || ds_x2>=SCREENWIDTH 
	|| (unsigned)ds_y>SCREENHEIGHT)
		I_Error ("R_DrawSpan: %i to %i at %i",ds_x1,ds_x2,ds_y);
//	dscount++;
#endif
	
for (i = 0; i < 4; i++)
    {
        outp (SC_INDEX+1,1<<i); 
        dsp_x1 = (ds_x1-i)/4;

        if (dsp_x1*4+i<ds_x1)
        {
            dsp_x1++;
        }

        dest = destview + ds_y*80 + dsp_x1;
        dsp_x2 = (ds_x2-i)/4;
        countp = dsp_x2 - dsp_x1;

        xfrac = ds_xfrac; 
        yfrac = ds_yfrac;

        prt = dsp_x1*4-ds_x1+i;

        xfrac += ds_xstep*prt;
        yfrac += ds_ystep*prt;

        if (countp < 0)
        {
            continue;
        }

        do
        {
            // Current texture index in u,v.
            spot = ((yfrac>>(16-6))&(63*64)) + ((xfrac>>16)&63);

            // Lookup pixel from flat texture tile,
            //  re-index using light/colormap.
            *dest++ = ds_colormap[ds_source[spot]];

            // Next step in u,v.
            xfrac += ds_xstep*4; 
            yfrac += ds_ystep*4;
        } while (countp--);
    }
}

// -----------------------------------------------------------------------------
// R_DrawSpanLow
// Again..
// -----------------------------------------------------------------------------

void R_DrawSpanLow (void) 
{
    int         spot; 
    int         i;
    int         prt;
    int         dsp_x1;
    int         dsp_x2;
    int         countp;
    fixed_t     xfrac;
    fixed_t     yfrac; 
    byte       *dest; 
	
#ifdef RANGECHECK
	if (ds_x2 < ds_x1 || ds_x1<0 || ds_x2>=SCREENWIDTH 
	|| (unsigned)ds_y>SCREENHEIGHT)
		I_Error ("R_DrawSpan: %i to %i at %i",ds_x1,ds_x2,ds_y);
//	dscount++;
#endif
	
    for (i = 0; i < 2; i++)
    {
        outp (SC_INDEX+1,3<<(i*2)); 
        dsp_x1 = (ds_x1-i)/2;

        if (dsp_x1*2+i<ds_x1)
        {
            dsp_x1++;
        }

        dest = destview + ds_y*80 + dsp_x1;
        dsp_x2 = (ds_x2-i)/2;
        countp = dsp_x2 - dsp_x1;

        xfrac = ds_xfrac; 
        yfrac = ds_yfrac;

        prt = dsp_x1*2-ds_x1+i;

        xfrac += ds_xstep*prt;
        yfrac += ds_ystep*prt;

        if (countp < 0)
        {
            continue;
        }

        do
        {
            // Current texture index in u,v.
            spot = ((yfrac>>(16-6))&(63*64)) + ((xfrac>>16)&63);

            // Lookup pixel from flat texture tile,
            //  re-index using light/colormap.
            *dest++ = ds_colormap[ds_source[spot]];

            // Next step in u,v.
            xfrac += ds_xstep*2; 
            yfrac += ds_ystep*2;
        } while (countp--);
    }
}

/*
================
=
= R_InitBuffer
=
=================
*/

void R_InitBuffer (int width, int height)
{
	int		i;
	
	viewwindowx = (SCREENWIDTH-width) >> 1;
	for (i=0 ; i<width ; i++)
		columnofs[i] = viewwindowx + i;
	if (width == SCREENWIDTH)
		viewwindowy = 0;
	else
		viewwindowy = (SCREENHEIGHT-SBARHEIGHT-height) >> 1;
	for (i=0 ; i<height ; i++)
		ylookup[i] = screens[0] + (i+viewwindowy)*SCREENWIDTH;
}

 


/*
================
=
= R_FillBackScreen
=
= Fills the back screen with a pattern for variable screen sizes
= Also draws a beveled edge.
=================
*/

void R_FillBackScreen (void)
{
	byte		*src, *dest;
	int			i, j;
	int			x, y;
	patch_t		*patch;
	char		name1[] = "FLOOR7_2";
	char		name2[] = "GRNROCK";	
	char		*name;
	
	if (scaledviewwidth == 320)
		return;
	
	if (commercial)
		name = name2;
	else
		name = name1;
    
	src = W_CacheLumpName (name, PU_CACHE);
	dest = screens[1];
	 
	for (y=0 ; y<SCREENHEIGHT-SBARHEIGHT ; y++)
	{
		for (x=0 ; x<SCREENWIDTH/64 ; x++)
		{
			memcpy (dest, src+((y&63)<<6), 64);
			dest += 64;
		}
		if (SCREENWIDTH&63)
		{
			memcpy (dest, src+((y&63)<<6), SCREENWIDTH&63);
			dest += (SCREENWIDTH&63);
		}
	}
	
	patch = W_CacheLumpName ("brdr_t",PU_CACHE);
	for (x=0 ; x<scaledviewwidth ; x+=8)
		V_DrawPatch (viewwindowx+x,viewwindowy-8,1,patch);
	patch = W_CacheLumpName ("brdr_b",PU_CACHE);
	for (x=0 ; x<scaledviewwidth ; x+=8)
		V_DrawPatch (viewwindowx+x,viewwindowy+viewheight,1,patch);
	patch = W_CacheLumpName ("brdr_l",PU_CACHE);
	for (y=0 ; y<viewheight ; y+=8)
		V_DrawPatch (viewwindowx-8,viewwindowy+y,1,patch);
	patch = W_CacheLumpName ("brdr_r",PU_CACHE);
	for (y=0 ; y<viewheight ; y+=8)
		V_DrawPatch (viewwindowx+scaledviewwidth,viewwindowy+y,1,patch);

	V_DrawPatch (viewwindowx-8, viewwindowy-8, 1,
		W_CacheLumpName ("brdr_tl",PU_CACHE));
	V_DrawPatch (viewwindowx+scaledviewwidth, viewwindowy-8, 1,
		W_CacheLumpName ("brdr_tr",PU_CACHE));
	V_DrawPatch (viewwindowx-8, viewwindowy+viewheight, 1,
		W_CacheLumpName ("brdr_bl",PU_CACHE));
	V_DrawPatch (viewwindowx+scaledviewwidth, viewwindowy+viewheight, 1,
		W_CacheLumpName ("brdr_br",PU_CACHE));

#ifdef __WATCOMC__
	dest = (byte*)0xac000;
	src = screens[1];
	for (i = 0; i < 4; i++, src++)
	{
		outp (SC_INDEX, 2);
		outp (SC_INDEX+1, 1<<i);
		for (j = 0; j < (SCREENHEIGHT-SBARHEIGHT)*SCREENWIDTH/4; j++)
			dest[j] = src[j*4];
	}
#endif
}


void R_VideoErase (unsigned ofs, int count)
{ 
#ifdef __WATCOMC__
	int		i;
	byte	*src, *dest;
	outp (SC_INDEX, SC_MAPMASK);
	outp (SC_INDEX+1, 15);
	outp (GC_INDEX, GC_MODE);
	outp (GC_INDEX+1, inp (GC_INDEX+1)|1);
	src = (byte*)0xac000+(ofs>>2);
	dest = destscreen+(ofs>>2);
	for (i = (count>>2)-1; i >= 0; i--)
	{
		dest[i] = src[i];
	}
	outp (GC_INDEX, GC_MODE);
	outp (GC_INDEX+1, inp (GC_INDEX+1)&~1);
#else
	memcpy (screens[0]+ofs, screens[1]+ofs, count);
#endif
}


/*
================
=
= R_DrawViewBorder
=
= Draws the border around the view for different size windows
=
=================
*/

void V_MarkRect (int x, int y, int width, int height);
 
void R_DrawViewBorder (void)
{ 
    int		top, side, ofs, i;
 
	if (scaledviewwidth == SCREENWIDTH)
		return;
  
	top = ((SCREENHEIGHT-SBARHEIGHT)-viewheight)/2;
	side = (SCREENWIDTH-scaledviewwidth)/2;

//
// copy top and one line of left side
//
	R_VideoErase (0, top*SCREENWIDTH+side);
 
//
// copy one line of right side and bottom
//
	ofs = (viewheight+top)*SCREENWIDTH-side;
	R_VideoErase (ofs, top*SCREENWIDTH+side);
 
//
// copy sides using wraparound
//
	ofs = top*SCREENWIDTH + SCREENWIDTH-side;
	side <<= 1;
    
	for (i=1 ; i<viewheight ; i++)
	{
		R_VideoErase (ofs, side);
		ofs += SCREENWIDTH;
	}

#ifndef __WATCOMC__
	V_MarkRect (0,0,SCREENWIDTH, SCREENHEIGHT-SBARHEIGHT);
#endif
}
 
 
