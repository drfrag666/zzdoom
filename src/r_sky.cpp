//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1994-1996 Raven Software
// Copyright 1999-2016 Randy Heit
// Copyright 2002-2016 Christoph Oelckers
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//	Sky rendering. The DOOM sky is a texture map like any
//	wall, wrapping around. 1024 columns equal 360 degrees.
//	The default sky map is 256 columns and repeats 4 times
//	on a 320 screen.
//	
//
//-----------------------------------------------------------------------------


// Needed for FRACUNIT.
#include "m_fixed.h"
#include "c_cvars.h"
#include "g_level.h"
#include "r_sky.h"
#include "r_utility.h"
#include "v_text.h"
#include "g_levellocals.h"

//
// sky mapping
//
FTextureID	skyflatnum;
double		skytexturemid;
double		skyscale;
float		skyiscale;

fixed_t		sky1cyl,		sky2cyl;

CUSTOM_CVAR(Int, testskyoffset, 0, 0)
{
	R_InitSkyMap();
}

// [RH] Stretch sky texture if not taller than 128 pixels?
// Also now controls capped skies. 0 = normal, 1 = stretched, 2 = capped
CUSTOM_CVAR (Int, r_skymode, 2, CVAR_ARCHIVE)
{
	R_InitSkyMap ();
}


int			freelookviewheight;

//==========================================================================
//
// R_InitSkyMap
//
// Called whenever the view size changes.
//
//==========================================================================

void InitSkyMap(FLevelLocals *Level)
{
	int skyheight;
	FTexture *skytex1, *skytex2;

	// Do not allow the null texture which has no bitmap and will crash.
	if (Level->skytexture1.isNull())
	{
		Level->skytexture1 = TexMan.CheckForTexture("-noflat-", ETextureType::Any);
	}
	if (Level->skytexture2.isNull())
	{
		Level->skytexture2 = TexMan.CheckForTexture("-noflat-", ETextureType::Any);
	}

	skytex1 = TexMan(Level->skytexture1, true);
	skytex2 = TexMan(Level->skytexture2, true);

	if (skytex1 == nullptr)
		return;

	if ((Level->flags & LEVEL_DOUBLESKY) && skytex1->GetHeight() != skytex2->GetHeight())
	{
		Printf (TEXTCOLOR_BOLD "Both sky textures must be the same height." TEXTCOLOR_NORMAL "\n");
		Level->flags &= ~LEVEL_DOUBLESKY;
		Level->skytexture1 = Level->skytexture2;
	}

	// There are various combinations for sky rendering depending on how tall the sky is:
	//        h <  128: Unstretched and tiled, centered on horizon
	// 128 <= h <  200: Can possibly be stretched. When unstretched, the baseline is
	//                  28 rows below the horizon so that the top of the texture
	//                  aligns with the top of the screen when looking straight ahead.
	//                  When stretched, it is scaled to 228 pixels with the baseline
	//                  in the same location as an unstretched 128-tall sky, so the top
	//					of the texture aligns with the top of the screen when looking
	//                  fully up.
	//        h == 200: Unstretched, baseline is on horizon, and top is at the top of
	//                  the screen when looking fully up.
	//        h >  200: Unstretched, but the baseline is shifted down so that the top
	//                  of the texture is at the top of the screen when looking fully up.
	skyheight = skytex1->GetScaledHeight();
	Level->skystretch = false;
	skytexturemid = 0;
	if (skyheight >= 128 && skyheight < 200)
	{
		Level->skystretch = (r_skymode == 1
			&& skyheight >= 128
			&& Level->IsFreelookAllowed()
			&& !(Level->flags & LEVEL_FORCETILEDSKY)) ? 1 : 0;
		skytexturemid = -28;
	}
	else if (skyheight > 200)
	{
		skytexturemid = (200 - skyheight) * skytex1->Scale.Y +((r_skymode == 2 && !(Level->flags & LEVEL_FORCETILEDSKY)) ? skytex1->SkyOffset + testskyoffset : 0);
	}

	if (viewwidth != 0 && viewheight != 0)
	{
		skyiscale = float(r_Yaspect / freelookviewheight);
		skyscale = freelookviewheight / r_Yaspect;

		skyiscale *= float(r_viewpoint.FieldOfView.Degrees / 90.);
		skyscale *= float(90. / r_viewpoint.FieldOfView.Degrees);
	}

	if (Level->skystretch)
	{
		skyscale *= (double)SKYSTRETCH_HEIGHT / skyheight;
		skyiscale *= skyheight / (float)SKYSTRETCH_HEIGHT;
		skytexturemid *= skyheight / (double)SKYSTRETCH_HEIGHT;
	}

	// The standard Doom sky texture is 256 pixels wide, repeated 4 times over 360 degrees,
	// giving a total sky width of 1024 pixels. So if the sky texture is no wider than 1024,
	// we map it to a cylinder with circumfrence 1024. For larger ones, we use the width of
	// the texture as the cylinder's circumfrence.
	sky1cyl = MAX(skytex1->GetWidth(), fixed_t(skytex1->Scale.X * 1024));
	sky2cyl = MAX(skytex2->GetWidth(), fixed_t(skytex2->Scale.Y * 1024));
}

void R_InitSkyMap()
{
	for(auto Level : AllLevels())
	{
		InitSkyMap(Level);
	}
}


//==========================================================================
//
// R_UpdateSky
//
// Performs sky scrolling
//
//==========================================================================

void R_UpdateSky (uint64_t mstime)
{
	double ms = (double)mstime * FRACUNIT;
	for(auto Level : AllLevels())
	{
		// Scroll the sky
		Level->sky1pos = ms * Level->skyspeed1;
		Level->sky2pos = ms * Level->skyspeed2;
	}
}

