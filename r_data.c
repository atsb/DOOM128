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

// R_data.c

#include "DoomDef.h"
extern int _wp1, _wp2, _wp3, _wp4, _wp5, _wp6, _wp7, _wp8, _wp9;
extern int _wp10, _wp11, _wp12, _wp13, _wp14, _wp15, _wp16, _wp17, _wp18, _wp19;
#include "R_local.h"
#include "P_local.h"
extern int _wp20, _wp21, _wp22, _wp23, _wp24, _wp25, _wp26, _wp27, _wp28, _wp29;

typedef struct
{
	int		originx;	// block origin (allways UL), which has allready
	int		originy;	// accounted  for the patch's internal origin
	int		patch;
} texpatch_t;

// a maptexturedef_t describes a rectangular texture, which is composed of one
// or more mappatch_t structures that arrange graphic patches
typedef struct
{
	char		name[8];		// for switch changing, etc
	short		width;
	short		height;
	short		patchcount;
	texpatch_t	patches[1];		// [patchcount] drawn back to front
								//  into the cached texture
} texture_t;



int		firstflat, lastflat, numflats;
int		firstpatch, lastpatch, numpatches;
int		firstspritelump, lastspritelump, numspritelumps;

int			numtextures;
texture_t	**textures;
int			*texturewidthmask;
fixed_t		*textureheight;		// needed for texture pegging
int			*texturecompositesize;
short		**texturecolumnlump;
unsigned	**texturecolumnofs;
unsigned    **texturecolumnofs2; // [crispy] original column offsets for single-patched textures
byte		**texturecomposite;

int			*flattranslation;		// for global animation
int			*texturetranslation;	// for global animation

fixed_t		*spritewidth;		// needed for pre rendering
fixed_t		*spriteoffset;
fixed_t		*spritetopoffset;

lighttable_t	*colormaps;


/*
==============================================================================

						MAPTEXTURE_T CACHING

when a texture is first needed, it counts the number of composite columns
required in the texture and allocates space for a column directory and any
new columns.  The directory will simply point inside other patches if there
is only one patch in a given column, but any columns with multiple patches
will have new column_ts generated.

==============================================================================
*/

//
// R_DrawColumnInCache
// Clip and draw a column
//  from a patch into a cached post.
//
// Rewritten by Lee Killough for performance and to fix Medusa bug
//
static void R_DrawColumnInCache(const column_t *patch, byte *cache,
				                int originy, int cacheheight, byte *marks)
{
    while (patch->topdelta != 0xff)
    {
        int count = patch->length;
        int position = originy + patch->topdelta;

        if (position < 0)
        {
            count += position;
            position = 0;
        }

        if (position + count > cacheheight)
        {
            count = cacheheight - position;
        }

        if (count > 0)
        {
            memcpy (cache + position, (byte *)patch + 3, count);

            // killough 4/9/98: remember which cells in column have been drawn,
            // so that column can later be converted into a series of posts, to
            // fix the Medusa bug.

            memset (marks + position, 0xff, count);
        }

        patch = (column_t *)((byte *) patch + patch->length + 4);
    }
}

//
// R_GenerateComposite
// Using the texture definition,
//  the composite texture is created from the patches,
//  and each column is cached.
//
// Rewritten by Lee Killough for performance and to fix Medusa bug
//
static void R_GenerateComposite(int texnum)
{
    byte *block = Z_Malloc(texturecompositesize[texnum], PU_STATIC,
                          (void **) &texturecomposite[texnum]);
    texture_t *texture = textures[texnum];
    // Composite the columns together.
    texpatch_t *patch = texture->patches;
    short *collump = texturecolumnlump[texnum];
    unsigned *colofs = texturecolumnofs[texnum]; // killough 4/9/98: make 32-bit
    int i = texture->patchcount;
    // killough 4/9/98: marks to identify transparent regions in merged textures
    byte *marks = calloc(texture->width, texture->height), *source;

    // [crispy] initialize composite background to black (index 0)
    memset(block, 0, texturecompositesize[texnum]);

    for ( ; --i >=0 ; patch++)
    {
        patch_t *realpatch = W_CacheLumpNum(patch->patch, PU_CACHE);
        int x, x1 = patch->originx, x2 = x1 + SHORT(realpatch->width);
        const int *cofs = realpatch->columnofs - x1;

        if (x1 < 0)
        {
            x1 = 0;
        }
        if (x2 > texture->width)
        {
            x2 = texture->width;
        }

        for (x = x1; x < x2 ; x++)
        // [crispy] generate composites for single-patched textures as well
        // killough 1/25/98, 4/9/98: Fix medusa bug.
        R_DrawColumnInCache((column_t*)((byte*) realpatch + LONG(cofs[x])),
                             block + colofs[x], patch->originy,
                             texture->height, marks + x*texture->height);
    }

    // killough 4/9/98: Next, convert multipatched columns into true columns,
    // to fix Medusa bug while still allowing for transparent regions.

    source = malloc(texture->height);       // temporary column

    for (i=0 ; i < texture->width ; i++)
        if (collump[i] == -1)                 // process only multipatched columns
        {
            column_t *col = (column_t *)(block + colofs[i] - 3);  // cached column
            const byte *mark = marks + i * texture->height;
            int j = 0;

            // save column in temporary so we can shuffle it around
            memcpy(source, (byte *) col + 3, texture->height);

            for (;;)  // reconstruct the column by scanning transparency marks
            {
                unsigned len;        // killough 12/98

                while (j < texture->height && !mark[j]) // skip transparent cells
                j++;

                if (j >= texture->height)           // if at end of column
                {
                    col->topdelta = -1;             // end-of-column marker
                    break;
                }

                col->topdelta = j;                  // starting offset of post

                // killough 12/98:
                // Use 32-bit len counter, to support tall 1s multipatched textures

                for (len = 0; j < texture->height && mark[j]; j++)
                len++;                    // count opaque cells

                col->length = len; // killough 12/98: intentionally truncate length

                // copy opaque cells from the temporary back into the column
                memcpy((byte *) col + 3, source + col->topdelta, len);
                col = (column_t *)((byte *) col + len + 4); // next post
            }
        }

    free(source);         // free temporary column
    free(marks);          // free transparency marks

    // Now that the texture has been built in column cache,
    // it is purgable from zone memory.

    Z_ChangeTag(block, PU_CACHE);
}


//
// R_GenerateLookup
//
// Rewritten by Lee Killough for performance and to fix Medusa bug
//
static void R_GenerateLookup(int texnum)
{
    const texture_t *texture = textures[texnum];

    // Composited texture not created yet.

    short *collump = texturecolumnlump[texnum];
    unsigned *colofs = texturecolumnofs[texnum]; // killough 4/9/98: make 32-bit
    unsigned *colofs2 = texturecolumnofs2[texnum]; // [crispy] original column offsets

    // killough 4/9/98: keep count of posts in addition to patches.
    // Part of fix for medusa bug for multipatched 2s normals.

    struct {
      unsigned patches, posts;
    } *count = calloc(sizeof *count, texture->width);

    // killough 12/98: First count the number of patches per column.

    const texpatch_t *patch = texture->patches;
    int i = texture->patchcount;

    while (--i >= 0)
    {
        int pat = patch->patch;
        const patch_t *realpatch = W_CacheLumpNum(pat, PU_CACHE);
        int x, x1 = patch++->originx, x2 = x1 + SHORT(realpatch->width);
        const int *cofs = realpatch->columnofs - x1;

        if (x2 > texture->width)
        {
            x2 = texture->width;
        }
        if (x1 < 0)
        {
            x1 = 0;
        }

        for (x = x1 ; x<x2 ; x++)
        {
            count[x].patches++;
            collump[x] = pat;
            colofs[x] = colofs2[x] = LONG(cofs[x])+3;
        }
    }

    // killough 4/9/98: keep a count of the number of posts in column,
    // to fix Medusa bug while allowing for transparent multipatches.
    //
    // killough 12/98:
    // Post counts are only necessary if column is multipatched,
    // so skip counting posts if column comes from a single patch.
    // This allows arbitrarily tall textures for 1s walls.
    //
    // If texture is >= 256 tall, assume it's 1s, and hence it has
    // only one post per column. This avoids crashes while allowing
    // for arbitrarily tall multipatched 1s textures.

    if (texture->patchcount > 1 && texture->height < 256)
    {
        // killough 12/98: Warn about a common column construction bug
        unsigned limit = texture->height*3+3; // absolute column size limit
        int badcol = devparm;                 // warn only if -devparm used

        for (i = texture->patchcount, patch = texture->patches ; --i >= 0 ; )
        {
            int pat = patch->patch;
            const patch_t *realpatch = W_CacheLumpNum(pat, PU_CACHE);
            int x, x1 = patch++->originx, x2 = x1 + SHORT(realpatch->width);
            const int *cofs = realpatch->columnofs - x1;

            if (x2 > texture->width)
            {
                x2 = texture->width;
            }
            if (x1 < 0)
            {
                x1 = 0;
            }

            for (x = x1 ; x<x2 ; x++)
                if (count[x].patches > 1)        // Only multipatched columns
                {
                    const column_t *col = (column_t*)((byte*) realpatch+LONG(cofs[x]));
                    const byte *base = (const byte *) col;

                    // count posts
                    for ( ; col->topdelta != 0xff ; count[x].posts++)
                        if ((unsigned)((byte *) col - base) <= limit)
                        {
                            col = (column_t *)((byte *) col + col->length + 4);
                        }
                        else
                        { // killough 12/98: warn about column construction bug
                            if (badcol)
                            {
                                badcol = 0;
                                if (devparm)
                                {
                                    printf("\nWarning: Texture %8.8s (height %d) has bad column(s) starting at x = %d.", texture->name, texture->height, x);
                                }
                            }
                            break;
                        }
                }
        }
    }

    // Now count the number of columns
    //  that are covered by more than one patch.
    // Fill in the lump / offset, so columns
    //  with only a single patch are all done.

    texturecomposite[texnum] = 0;

    {
        int x = texture->width;
        int height = texture->height;
        int csize = 0, err = 0;        // killough 10/98

        while (--x >= 0)
        {
            if (!count[x].patches)     // killough 4/9/98
            {
                if (devparm)
                {
                    // killough 8/8/98
                    printf("\nR_GenerateLookup: Column %d is without a patch in texture %.8s", x, texture->name);
                }
                else
                {
                    err = 1;               // killough 10/98
                }
            }

            if (count[x].patches > 1)       // killough 4/9/98
            // [crispy] moved up here, the rest in this loop
            // applies to single-patched textures as well
            collump[x] = -1;              // mark lump as multipatched
            {
                // killough 1/25/98, 4/9/98:
                //
                // Fix Medusa bug, by adding room for column header
                // and trailer bytes for each post in merged column.
                // For now, just allocate conservatively 4 bytes
                // per post per patch per column, since we don't
                // yet know how many posts the merged column will
                // require, and it's bounded above by this limit.

                colofs[x] = csize + 3;        // three header bytes in a column
                // killough 12/98: add room for one extra post
                csize += 4*count[x].posts+5;  // 1 stop byte plus 4 bytes per post
            }

            csize += height;                  // height bytes of texture data
        }

        texturecompositesize[texnum] = csize;

        if (err && devparm)       // killough 10/98: non-verbose output
        {
            printf("\nR_GenerateLookup: Column without a patch in texture %.8s", texture->name);
        }
    }

    free(count);                    // killough 4/9/98
}



/*
================
=
= R_GetColumn
=
================
*/

byte *R_GetColumn (int tex, int col)
{
	int	lump, ofs;
	
	col &= texturewidthmask[tex];
	lump = texturecolumnlump[tex][col];
	ofs = texturecolumnofs[tex][col];
	if (lump > 0)
		return (byte *)W_CacheLumpNum(lump,PU_CACHE)+ofs;
	if (!texturecomposite[tex])
		R_GenerateComposite (tex);
	return texturecomposite[tex] + ofs;
}


/*
==================
=
= R_InitTextures
=
= Initializes the texture list with the textures from the world map
=
==================
*/

void R_InitTextures (void)
{
	maptexture_t	*mtexture;
	texture_t		*texture;
	mappatch_t	*mpatch;
	texpatch_t	*patch;
	int			i,j;
	int			*maptex, *maptex2, *maptex1;
	char		name[9], *names, *name_p;
	int			*patchlookup;
	int			totalwidth;
	int			nummappatches;
	int			offset, maxoff, maxoff2;
	int			numtextures1, numtextures2;
	int			*directory;
	int			temp1, temp2, temp3;

//
// load the patch names from pnames.lmp
//
	name[8] = 0;
	names = W_CacheLumpName ("PNAMES", PU_STATIC);
	nummappatches = LONG ( *((int *)names) );
	name_p = names+4;
	patchlookup = alloca (nummappatches*sizeof(*patchlookup));
	for (i=0 ; i<nummappatches ; i++)
	{
		strncpy (name,name_p+i*8, 8);
		patchlookup[i] = W_CheckNumForName (name);
	}
	Z_Free (names);

//
// load the map texture definitions from textures.lmp
//
	maptex = maptex1 = W_CacheLumpName ("TEXTURE1", PU_STATIC);
	numtextures1 = LONG(*maptex);
	maxoff = W_LumpLength (W_GetNumForName ("TEXTURE1"));
	directory = maptex+1;

	if (W_CheckNumForName ("TEXTURE2") != -1)
	{
		maptex2 = W_CacheLumpName ("TEXTURE2", PU_STATIC);
		numtextures2 = LONG(*maptex2);
		maxoff2 = W_LumpLength (W_GetNumForName ("TEXTURE2"));
	}
	else
	{
		maptex2 = NULL;
		numtextures2 = 0;
		maxoff2 = 0;
	}
	numtextures = numtextures1 + numtextures2;

	textures = Z_Malloc (numtextures*4, PU_STATIC, 0);
	texturecolumnlump = Z_Malloc (numtextures*4, PU_STATIC, 0);
	texturecolumnofs = Z_Malloc (numtextures*4, PU_STATIC, 0);
	texturecomposite = Z_Malloc (numtextures*4, PU_STATIC, 0);
	texturecompositesize = Z_Malloc (numtextures*4, PU_STATIC, 0);
	texturewidthmask = Z_Malloc (numtextures*4, PU_STATIC, 0);
	textureheight = Z_Malloc (numtextures*4, PU_STATIC, 0);

	totalwidth = 0;
    
	temp1 = W_GetNumForName ("S_START");
	temp2 = W_GetNumForName ("S_END") - 1;
	temp3 = ((temp2-temp1+63)/64) + ((numtextures+63)/64);
	printf("[");
	for (i = 0; i < temp3; i++)
		printf(" ");
	printf("         ]");
	for (i = 0; i < temp3; i++)
		printf("\x8");
	printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");	

	for (i=0 ; i<numtextures ; i++, directory++)
	{
		if (!(i&63))
			printf (".");
		if (i == numtextures1)
		{	// start looking in second texture file
			maptex = maptex2;
			maxoff = maxoff2;
			directory = maptex+1;
		}

		offset = LONG(*directory);
		if (offset > maxoff)
			I_Error ("R_InitTextures: bad texture directory");
		mtexture = (maptexture_t *) ( (byte *)maptex + offset);
		texture = textures[i] = Z_Malloc (sizeof(texture_t) 
			+ sizeof(texpatch_t)*(SHORT(mtexture->patchcount)-1), PU_STATIC,
			0);
		texture->width = SHORT(mtexture->width);
		texture->height = SHORT(mtexture->height);
		texture->patchcount = SHORT(mtexture->patchcount);
		memcpy (texture->name, mtexture->name, sizeof(texture->name));
		mpatch = &mtexture->patches[0];
		patch = &texture->patches[0];
		for (j=0 ; j<texture->patchcount ; j++, mpatch++, patch++)
		{
			patch->originx = SHORT(mpatch->originx);
			patch->originy = SHORT(mpatch->originy);
			patch->patch = patchlookup[SHORT(mpatch->patch)];
			if (patch->patch == -1)
				I_Error (
				"R_InitTextures: Missing patch in texture %s",texture->name);
		}		
		texturecolumnlump[i] = Z_Malloc (texture->width*2, PU_STATIC,0);
		texturecolumnofs[i] = Z_Malloc (texture->width*2, PU_STATIC,0);
		j = 1;
		while (j*2 <= texture->width)
			j<<=1;
		texturewidthmask[i] = j-1;
		textureheight[i] = texture->height<<FRACBITS;
		
		totalwidth += texture->width;
	}

	Z_Free (maptex1);
	if (maptex2)
		Z_Free (maptex2);

//
// precalculate whatever possible
//		
	for (i=0 ; i<numtextures ; i++)
		R_GenerateLookup(i);

//
// translation table for global animation
//
	texturetranslation = Z_Malloc ((numtextures+1)*4, PU_STATIC, 0);
	for (i=0 ; i<numtextures ; i++)
		texturetranslation[i] = i;
}


/*
================
=
= R_InitFlats
=
=================
*/

void R_InitFlats (void)
{
	int		i;
	
	firstflat = W_GetNumForName ("F_START") + 1;
	lastflat = W_GetNumForName ("F_END") - 1;
	numflats = lastflat - firstflat + 1;
	
// translation table for global animation
	flattranslation = Z_Malloc ((numflats+1)*4, PU_STATIC, 0);
	for (i=0 ; i<numflats ; i++)
		flattranslation[i] = i;
}


/*
================
=
= R_InitSpriteLumps
=
= Finds the width and hoffset of all sprites in the wad, so the sprite doesn't
= need to be cached just for the header during rendering
=================
*/

void R_InitSpriteLumps (void)
{
	int		i;
	patch_t	*patch;

	firstspritelump = W_GetNumForName ("S_START") + 1;
	lastspritelump = W_GetNumForName ("S_END") - 1;
	numspritelumps = lastspritelump - firstspritelump + 1;
	spritewidth = Z_Malloc (numspritelumps*4, PU_STATIC, 0);
	spriteoffset = Z_Malloc (numspritelumps*4, PU_STATIC, 0);
	spritetopoffset = Z_Malloc (numspritelumps*4, PU_STATIC, 0);

	for (i=0 ; i< numspritelumps ; i++)
	{
		if (!(i&63))
			printf (".");
		patch = W_CacheLumpNum (firstspritelump+i, PU_CACHE);
		spritewidth[i] = SHORT(patch->width)<<FRACBITS;
		spriteoffset[i] = SHORT(patch->leftoffset)<<FRACBITS;
		spritetopoffset[i] = SHORT(patch->topoffset)<<FRACBITS;
	}
}


/*
================
=
= R_InitColormaps
=
=================
*/

void R_InitColormaps (void)
{
	int	lump, length;
//
// load in the light tables
// 256 byte align tables
//
	lump = W_GetNumForName("COLORMAP");
	length = W_LumpLength (lump) + 255;
	colormaps = Z_Malloc (length, PU_STATIC, 0);
	colormaps = (byte *)( ((int)colormaps + 255)&~0xff);
	W_ReadLump (lump,colormaps);
}


/*
================
=
= R_InitData
=
= Locates all the lumps that will be used by all views
= Must be called after W_Init
=================
*/

void R_InitData (void)
{
	R_InitTextures ();
	printf (".");
	R_InitFlats ();
	printf (".");
	R_InitSpriteLumps ();
	printf (".");
	R_InitColormaps ();
}


//=============================================================================

/*
================
=
= R_FlatNumForName
=
================
*/

int	R_FlatNumForName (char *name)
{
	int		i;
	char	namet[9];

	i = W_CheckNumForName (name);
	if (i == -1)
	{
		namet[8] = 0;
		memcpy (namet, name,8);
		I_Error ("R_FlatNumForName: %s not found",namet);
	}
	return i - firstflat;
}


/*
================
=
= R_CheckTextureNumForName
=
================
*/

int	R_CheckTextureNumForName (char *name)
{
	int		i;
	
	if (name[0] == '-')		// no texture marker
		return 0;
		
	for (i=0 ; i<numtextures ; i++)
		if (!strncasecmp (textures[i]->name, name, 8) )
			return i;
		
	return -1;
}


/*
================
=
= R_TextureNumForName
=
================
*/

int	R_TextureNumForName (char *name)
{
    int		i;
	
    i = R_CheckTextureNumForName (name);

    if (i==-1)
    {
	// [crispy] fix absurd texture name in error message
	char	namet[9];
	namet[8] = '\0';
	memcpy (namet, name, 8);
	// [crispy] make non-fatal
	fprintf (stderr, "R_TextureNumForName: %s not found\n",
		 namet);
	return 0;
    }
    return i;
}


/*
=================
=
= R_PrecacheLevel
=
= Preloads all relevent graphics for the level
=================
*/

int		flatmemory, texturememory, spritememory;

void R_PrecacheLevel (void)
{
	char			*flatpresent;
	char			*texturepresent;
	char			*spritepresent;
	int				i,j,k, lump;
	texture_t		*texture;
	thinker_t		*th;
	spriteframe_t	*sf;

	if (demoplayback)
		return;
			
//
// precache flats
//	
	flatpresent = alloca(numflats);
	memset (flatpresent,0,numflats);	
	for (i=0 ; i<numsectors ; i++)
	{
		flatpresent[sectors[i].floorpic] = 1;
		flatpresent[sectors[i].ceilingpic] = 1;
	}
	
	flatmemory = 0;
	for (i=0 ; i<numflats ; i++)
		if (flatpresent[i])
		{
			lump = firstflat + i;
			flatmemory += lumpinfo[lump].size;
			W_CacheLumpNum(lump, PU_CACHE);
		}
		
//
// precache textures
//
	texturepresent = alloca(numtextures);
	memset (texturepresent,0, numtextures);
	
	for (i=0 ; i<numsides ; i++)
	{
		texturepresent[sides[i].toptexture] = 1;
		texturepresent[sides[i].midtexture] = 1;
		texturepresent[sides[i].bottomtexture] = 1;
	}
	
	texturepresent[skytexture] = 1;
	
	texturememory = 0;
	for (i=0 ; i<numtextures ; i++)
	{
		if (!texturepresent[i])
			continue;
		texture = textures[i];
		for (j=0 ; j<texture->patchcount ; j++)
		{
			lump = texture->patches[j].patch;
			texturememory += lumpinfo[lump].size;
			W_CacheLumpNum(lump , PU_CACHE);
		}
	}
	
//
// precache sprites
//
	spritepresent = alloca(numsprites);
	memset (spritepresent,0, numsprites);
	
	for (th = thinkercap.next ; th != &thinkercap ; th=th->next)
	{
		if (th->function == P_MobjThinker)
			spritepresent[((mobj_t *)th)->sprite] = 1;
	}
	
	spritememory = 0;
	for (i=0 ; i<numsprites ; i++)
	{
		if (!spritepresent[i])
			continue;
		for (j=0 ; j<sprites[i].numframes ; j++)
		{
			sf = &sprites[i].spriteframes[j];
			for (k=0 ; k<8 ; k++)
			{
				lump = firstspritelump + sf->lump[k];
				spritememory += lumpinfo[lump].size;
				W_CacheLumpNum(lump , PU_CACHE);
			}
		}
	}
}




