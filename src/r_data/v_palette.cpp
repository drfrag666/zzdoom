/*
** v_palette.cpp
** Automatic colormap generation for "colored lights", etc.
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "g_level.h"

#include "templates.h"
#include "v_video.h"
#include "filesystem.h"
#include "i_video.h"
#include "c_dispatch.h"
#include "st_stuff.h"
#include "x86.h"
#include "colormatcher.h"
#include "v_palette.h"
#include "g_levellocals.h"
#include "m_png.h"
#include "v_colortables.h"

FPalette GPalette;
FColorMatcher ColorMatcher;

/* Current color blending values */
int		BlendR, BlendG, BlendB, BlendA;

/**************************/
/* Gamma correction stuff */
/**************************/

uint8_t newgamma[256];
CUSTOM_CVARD(Float, vid_gamma, 1.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "adjusts gamma component of gamma ramp")
{
	if (self == 0.f)
	{ // Gamma values of 0 are illegal.
		self = 1.f;
		return;
	}

	if (screen != NULL)
	{
		screen->SetGamma (self);
	}
}

CCMD (bumpgamma)
{
	// [RH] Gamma correction tables are now generated on the fly for *any* gamma level
	// Q: What are reasonable limits to use here?

	float newgamma = vid_gamma + 0.1f;

	if (newgamma > 3.0)
		newgamma = 1.0;

	vid_gamma = newgamma;
	Printf ("Gamma correction level %g\n", *vid_gamma);
}






FPalette::FPalette ()
{
}

FPalette::FPalette (const uint8_t *colors)
{
	SetPalette (colors);
}

void FPalette::SetPalette (const uint8_t *colors)
{
	for (int i = 0; i < 256; i++, colors += 3)
	{
		BaseColors[i] = PalEntry (colors[0], colors[1], colors[2]);
		Remap[i] = i;
	}

	// Find white and black from the original palette so that they can be
	// used to make an educated guess of the translucency % for a BOOM
	// translucency map.
	WhiteIndex = BestColor ((uint32_t *)BaseColors, 255, 255, 255, 0, 255);
	BlackIndex = BestColor ((uint32_t *)BaseColors, 0, 0, 0, 0, 255);
}

int ReadPalette(int lumpnum, uint8_t *buffer)
{
	if (lumpnum < 0)
	{
		return 0;
	}
	FileData lump = fileSystem.ReadFile(lumpnum);
	uint8_t *lumpmem = (uint8_t*)lump.GetMem();
	memset(buffer, 0, 768);

	FileReader fr;
	fr.OpenMemory(lumpmem, lump.GetSize());
	auto png = M_VerifyPNG(fr);
	if (png)
	{
		uint32_t id, len;
		fr.Seek(33, FileReader::SeekSet);
		fr.Read(&len, 4);
		fr.Read(&id, 4);
		bool succeeded = false;
		while (id != MAKE_ID('I', 'D', 'A', 'T') && id != MAKE_ID('I', 'E', 'N', 'D'))
		{
			len = BigLong((unsigned int)len);
			if (id != MAKE_ID('P', 'L', 'T', 'E'))
				fr.Seek(len, FileReader::SeekCur);
			else
			{
				int PaletteSize = MIN<int>(len, 768);
				fr.Read(buffer, PaletteSize);
				return PaletteSize / 3;
			}
			fr.Seek(4, FileReader::SeekCur);	// Skip CRC
			fr.Read(&len, 4);
			id = MAKE_ID('I', 'E', 'N', 'D');
			fr.Read(&id, 4);
		}
		I_Error("%s contains no palette", fileSystem.GetFileFullName(lumpnum));
	}
	if (memcmp(lumpmem, "JASC-PAL", 8) == 0)
	{
		FScanner sc;
		
		sc.OpenMem(fileSystem.GetFileFullName(lumpnum), (char*)lumpmem, int(lump.GetSize()));
		sc.MustGetString();
		sc.MustGetNumber();	// version - ignore
		sc.MustGetNumber();	
		int colors = MIN(256, sc.Number) * 3;
		for (int i = 0; i < colors; i++)
		{
			sc.MustGetNumber();
			if (sc.Number < 0 || sc.Number > 255)
			{
				sc.ScriptError("Color %d value out of range.", sc.Number);
			}
			buffer[i] = sc.Number;
		}
		return colors / 3;
	}
	else
	{
		memcpy(buffer, lumpmem, MIN<size_t>(768, lump.GetSize()));
		return 256;
	}
}

void InitPalette ()
{
	uint8_t pal[768];
	
	ReadPalette(fileSystem.GetNumForName("PLAYPAL"), pal);

	GPalette.SetPalette (pal);
	MakeGoodRemap ((uint32_t*)GPalette.BaseColors, GPalette.Remap);
	ColorMatcher.SetPalette ((uint32_t *)GPalette.BaseColors);

	if (GPalette.Remap[0] == 0)
	{ // No duplicates, so settle for something close to color 0
		GPalette.Remap[0] = BestColor ((uint32_t *)GPalette.BaseColors,
			GPalette.BaseColors[0].r, GPalette.BaseColors[0].g, GPalette.BaseColors[0].b, 1, 255);
	}

	// Colormaps have to be initialized before actors are loaded,
	// otherwise Powerup.Colormap will not work.
	R_InitColormaps ();
	BuildTransTable (GPalette.BaseColors);

}

void V_SetBlend (int blendr, int blendg, int blendb, int blenda)
{
	// Don't do anything if the new blend is the same as the old
	if (((blenda|BlendA) == 0) ||
		(blendr == BlendR &&
		 blendg == BlendG &&
		 blendb == BlendB &&
		 blenda == BlendA))
		return;

	V_ForceBlend (blendr, blendg, blendb, blenda);
}

void V_ForceBlend (int blendr, int blendg, int blendb, int blenda)
{
	BlendR = blendr;
	BlendG = blendg;
	BlendB = blendb;
	BlendA = blenda;

	screen->SetFlash (PalEntry (BlendR, BlendG, BlendB), BlendA);
}

CCMD (testblend)
{
	FString colorstring;
	int color;
	float amt;

	if (argv.argc() < 3)
	{
		Printf ("testblend <color> <amount>\n");
	}
	else
	{
		if ( !(colorstring = V_GetColorStringByName (argv[1])).IsEmpty() )
		{
			color = V_GetColorFromString (NULL, colorstring);
		}
		else
		{
			color = V_GetColorFromString (NULL, argv[1]);
		}
		amt = (float)atof (argv[2]);
		if (amt > 1.0f)
			amt = 1.0f;
		else if (amt < 0.0f)
			amt = 0.0f;
		BaseBlendR = RPART(color);
		BaseBlendG = GPART(color);
		BaseBlendB = BPART(color);
		BaseBlendA = amt;
	}
}

