/*
** v_font.cpp
** Font management
**
**---------------------------------------------------------------------------
** Copyright 1998-2016 Randy Heit
** Copyright 2005-2019 Christoph Oelckers
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

// HEADER FILES ------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "templates.h"
#include "doomtype.h"
#include "m_swap.h"
#include "v_font.h"
#include "v_video.h"
#include "w_wad.h"
#include "gi.h"
#include "cmdlib.h"
#include "sc_man.h"
#include "hu_stuff.h"
#include "textures/textures.h"
#include "gstrings.h"
#include "v_text.h"
#include "vm.h"
#include "utf8.h"
#include "textures/formats/fontchars.h"

#include "fontinternals.h"



//==========================================================================
//
// FFont :: FFont
//
// Loads a multi-texture font.
//
//==========================================================================

FFont::FFont (const char *name, const char *nametemplate, const char *filetemplate, int lfirst, int lcount, int start, int fdlump, int spacewidth, bool notranslate)
{
	int i;
	FTextureID lump;
	char buffer[12];
	int maxyoffs;
	bool doomtemplate = (nametemplate && (gameinfo.gametype & GAME_DoomChex)) ? strncmp (nametemplate, "STCFN", 5) == 0 : false;
	DVector2 Scale = { 1, 1 };

	noTranslate = notranslate;
	Lump = fdlump;
	PatchRemap = new uint8_t[256];
	FontHeight = 0;
	GlobalKerning = false;
	FontName = name;
	Next = FirstFont;
	FirstFont = this;
	Cursor = '_';
	ActiveColors = 0;
	SpaceWidth = 0;
	FontHeight = 0;
	int FixedWidth = 0;

	maxyoffs = 0;

	TMap<int, FTexture*> charMap;
	int minchar = INT_MAX;
	int maxchar = INT_MIN;
	
	// Read the font's configuration.
	// This will not be done for the default fonts, because they are not atomic and the default content does not need it.
	
	TArray<FolderEntry> folderdata;
	if (filetemplate != nullptr)
	{
		FStringf path("fonts/%s/", filetemplate);
		// If a name template is given, collect data from all resource files.
		// For anything else, each folder is being treated as an atomic, self-contained unit and mixing from different glyph sets is blocked.
		Wads.GetLumpsInFolder(path, folderdata, nametemplate == nullptr);
		
		if (nametemplate == nullptr)
		{
			// Only take font.inf from the actual folder we are processing but not from an older folder that may have been superseded.
			FStringf infpath("fonts/%s/font.inf", filetemplate);
			
			unsigned index = folderdata.FindEx([=](const FolderEntry &entry)
			{
				return infpath.CompareNoCase(entry.name) == 0;
			});
			
			if (index < folderdata.Size())
			{
				FScanner sc;
				sc.OpenLumpNum(folderdata[index].lumpnum);
				while (sc.GetToken())
				{
					sc.TokenMustBe(TK_Identifier);
					if (sc.Compare("Kerning"))
					{
						sc.MustGetValue(false);
						GlobalKerning = sc.Number;
					}
					else if (sc.Compare("Scale"))
					{
						sc.MustGetValue(true);
						Scale.Y = Scale.X = sc.Float;
						if (sc.CheckToken(','))
						{
							sc.MustGetValue(true);
							Scale.Y = sc.Float;
						}
					}
					else if (sc.Compare("SpaceWidth"))
					{
						sc.MustGetValue(false);
						SpaceWidth = sc.Number;
					}
					else if (sc.Compare("FontHeight"))
					{
						sc.MustGetValue(false);
						FontHeight = sc.Number;
					}
					else if (sc.Compare("CellSize"))
					{
						sc.MustGetValue(false);
						FixedWidth = sc.Number;
						sc.MustGetToken(',');
						sc.MustGetValue(false);
						FontHeight = sc.Number;
					}
					else if (sc.Compare("Translationtype"))
					{
						sc.MustGetToken(TK_Identifier);
						if (sc.Compare("console"))
						{
							TranslationType = 1;
						}
						else if (sc.Compare("standard"))
						{
							TranslationType = 0;
						}
						else
						{
							sc.ScriptError("Unknown translation type %s", sc.String);
						}
					}
				}
			}
		}
	}
	
	if (FixedWidth <= 0)
	{
		if (nametemplate != nullptr)
		{
			for (i = 0; i < lcount; i++)
			{
				int position = '!' + i;
				mysnprintf(buffer, countof(buffer), nametemplate, i + start);

				lump = TexMan.CheckForTexture(buffer, ETextureType::MiscPatch);
				if (doomtemplate && lump.isValid() && i + start == 121)
				{ // HACKHACK: Don't load STCFN121 in doom(2), because
				  // it's not really a lower-case 'y' but a '|'.
				  // Because a lot of wads with their own font seem to foolishly
				  // copy STCFN121 and make it a '|' themselves, wads must
				  // provide STCFN120 (x) and STCFN122 (z) for STCFN121 to load as a 'y'.
					if (!TexMan.CheckForTexture("STCFN120", ETextureType::MiscPatch).isValid() ||
						!TexMan.CheckForTexture("STCFN122", ETextureType::MiscPatch).isValid())
					{
						// insert the incorrectly named '|' graphic in its correct position.
						position = 124;
					}
				}
				if (lump.isValid())
				{
					Type = Multilump;
					if (position < minchar) minchar = position;
					if (position > maxchar) maxchar = position;
					charMap.Insert(position, TexMan[lump]);
				}
			}
		}
		if (folderdata.Size() > 0)
		{
			// all valid lumps must be named with a hex number that represents its Unicode character index.
			for (auto &entry : folderdata)
			{
				char *endp;
				auto base = ExtractFileBase(entry.name);
				auto position = strtoll(base.GetChars(), &endp, 16);
				if ((*endp == 0 || (*endp == '.' && position >= '!' && position < 0xffff)))
				{
					auto lump = TexMan.CheckForTexture(entry.name, ETextureType::MiscPatch);
					if (lump.isValid())
					{
						if ((int)position < minchar) minchar = (int)position;
						if ((int)position > maxchar) maxchar = (int)position;
						auto tex = TexMan[lump];
						tex->SetScale(Scale);
						charMap.Insert((int)position, tex);
						Type = Folder;
					}
				}
			}
		}
		FirstChar = minchar;
		LastChar = maxchar;
		auto count = maxchar - minchar + 1;
		Chars = new CharData[count];
		int fontheight = 0;

		for (i = 0; i < count; i++)
		{
			auto lump = charMap.CheckKey(FirstChar + i);
			if (lump != nullptr)
			{
				FTexture *pic = *lump;
				if (pic != nullptr)
				{
					int height = pic->GetScaledHeight();
					int yoffs = pic->GetScaledTopOffset(0);

					if (yoffs > maxyoffs)
					{
						maxyoffs = yoffs;
					}
					height += abs(yoffs);
					if (height > fontheight)
					{
						fontheight = height;
					}
				}

				if (!noTranslate) Chars[i].Pic = new FFontChar1 (pic);
				else Chars[i].Pic = pic;
				Chars[i].XMove = Chars[i].Pic->GetScaledWidth();
			}
			else
			{
				Chars[i].Pic = nullptr;
				Chars[i].XMove = INT_MIN;
			}
		}

		if (SpaceWidth == 0) // An explicit override from the .inf file must always take precedence
		{
			if (spacewidth != -1)
			{
				SpaceWidth = spacewidth;
			}
			else if ('N' - FirstChar >= 0 && 'N' - FirstChar < count && Chars['N' - FirstChar].Pic != nullptr)
			{
				SpaceWidth = (Chars['N' - FirstChar].XMove + 1) / 2;
			}
			else
			{
				SpaceWidth = 4;
			}
		}
		if (FontHeight == 0) FontHeight = fontheight;

		FixXMoves();
	}

	if (!noTranslate) LoadTranslations();
}

//==========================================================================
//
// FFont :: ~FFont
//
//==========================================================================

FFont::~FFont ()
{
	if (Chars)
	{
		int count = LastChar - FirstChar + 1;

		// A noTranslate font directly references the original textures.
		if (!noTranslate)
		{
			for (int i = 0; i < count; ++i)
			{
				if (Chars[i].Pic != NULL && Chars[i].Pic->Name[0] == 0)
				{
					delete Chars[i].Pic;
				}
			}
		}
		delete[] Chars;
		Chars = NULL;
	}
	if (PatchRemap)
	{
		delete[] PatchRemap;
		PatchRemap = NULL;
	}

	FFont **prev = &FirstFont;
	FFont *font = *prev;

	while (font != NULL && font != this)
	{
		prev = &font->Next;
		font = *prev;
	}

	if (font != NULL)
	{
		*prev = font->Next;
	}
}


//==========================================================================
//
// FFont :: FindFont
//
// Searches for the named font in the list of loaded fonts, returning the
// font if it was found. The disk is not checked if it cannot be found.
//
//==========================================================================

FFont *FFont::FindFont (FName name)
{
	if (name == NAME_None)
	{
		return nullptr;
	}
	FFont *font = FirstFont;

	while (font != nullptr)
	{
		if (font->FontName == name) return font;
		font = font->Next;
	}
	return nullptr;
}

//==========================================================================
//
// RecordTextureColors
//
// Given a 256 entry buffer, sets every entry that corresponds to a color
// used by the texture to 1.
//
//==========================================================================

void RecordTextureColors (FTexture *pic, uint32_t *usedcolors)
{
	int x;

	for (x = pic->GetWidth() - 1; x >= 0; x--)
	{
		const FTexture::Span *spans;
		const uint8_t *column = pic->GetColumn(DefaultRenderStyle(), x, &spans);	// This shouldn't use the spans...

		while (spans->Length != 0)
		{
			const uint8_t *source = column + spans->TopOffset;
			int count = spans->Length;

			do
			{
				usedcolors[*source++] = 1;
			} while (--count);

			spans++;
		}
	}
}

//==========================================================================
//
// compare
//
// Used for sorting colors by brightness.
//
//==========================================================================

static int compare (const void *arg1, const void *arg2)
{
	if (RPART(GPalette.BaseColors[*((uint8_t *)arg1)]) * 299 +
		GPART(GPalette.BaseColors[*((uint8_t *)arg1)]) * 587 +
		BPART(GPalette.BaseColors[*((uint8_t *)arg1)]) * 114  <
		RPART(GPalette.BaseColors[*((uint8_t *)arg2)]) * 299 +
		GPART(GPalette.BaseColors[*((uint8_t *)arg2)]) * 587 +
		BPART(GPalette.BaseColors[*((uint8_t *)arg2)]) * 114)
		return -1;
	else
		return 1;
}

//==========================================================================
//
// FFont :: SimpleTranslation
//
// Colorsused, translation, and reverse must all be 256 entry buffers.
// Colorsused must already be filled out.
// Translation be set to remap the source colors to a new range of
// consecutive colors based at 1 (0 is transparent).
// Reverse will be just the opposite of translation: It maps the new color
// range to the original colors.
// *Luminosity will be an array just large enough to hold the brightness
// levels of all the used colors, in consecutive order. It is sorted from
// darkest to lightest and scaled such that the darkest color is 0.0 and
// the brightest color is 1.0.
// The return value is the number of used colors and thus the number of
// entries in *luminosity.
//
//==========================================================================

int FFont::SimpleTranslation (uint32_t *colorsused, uint8_t *translation, uint8_t *reverse, double **luminosity)
{
	double min, max, diver;
	int i, j;

	memset (translation, 0, 256);

	reverse[0] = 0;
	for (i = 1, j = 1; i < 256; i++)
	{
		if (colorsused[i])
		{
			reverse[j++] = i;
		}
	}

	qsort (reverse+1, j-1, 1, compare);

	*luminosity = new double[j];
	(*luminosity)[0] = 0.0; // [BL] Prevent uninitalized memory
	max = 0.0;
	min = 100000000.0;
	for (i = 1; i < j; i++)
	{
		translation[reverse[i]] = i;

		(*luminosity)[i] = RPART(GPalette.BaseColors[reverse[i]]) * 0.299 +
						   GPART(GPalette.BaseColors[reverse[i]]) * 0.587 +
						   BPART(GPalette.BaseColors[reverse[i]]) * 0.114;
		if ((*luminosity)[i] > max)
			max = (*luminosity)[i];
		if ((*luminosity)[i] < min)
			min = (*luminosity)[i];
	}
	diver = 1.0 / (max - min);
	for (i = 1; i < j; i++)
	{
		(*luminosity)[i] = ((*luminosity)[i] - min) * diver;
	}

	return j;
}

//==========================================================================
//
// FFont :: BuildTranslations
//
// Build color translations for this font. Luminosity is an array of
// brightness levels. The ActiveColors member must be set to indicate how
// large this array is. Identity is an array that remaps the colors to
// their original values; it is only used for CR_UNTRANSLATED. Ranges
// is an array of TranslationParm structs defining the ranges for every
// possible color, in order. Palette is the colors to use for the
// untranslated version of the font.
//
//==========================================================================

void FFont::BuildTranslations (const double *luminosity, const uint8_t *identity,
							   const void *ranges, int total_colors, const PalEntry *palette)
{
	int i, j;
	const TranslationParm *parmstart = (const TranslationParm *)ranges;

	FRemapTable remap(total_colors);

	// Create different translations for different color ranges
	Ranges.Clear();
	for (i = 0; i < NumTextColors; i++)
	{
		if (i == CR_UNTRANSLATED)
		{
			if (identity != nullptr)
			{
				memcpy (remap.Remap, identity, ActiveColors);
				if (palette != nullptr)
				{
					memcpy (remap.Palette, palette, ActiveColors*sizeof(PalEntry));
				}
				else
				{
					remap.Palette[0] = GPalette.BaseColors[identity[0]] & MAKEARGB(0,255,255,255);
					for (j = 1; j < ActiveColors; ++j)
					{
						remap.Palette[j] = GPalette.BaseColors[identity[j]] | MAKEARGB(255,0,0,0);
					}
				}
			}
			else
			{
				remap = Ranges[0];
			}
			Ranges.Push(remap);
			continue;
		}

		assert(parmstart->RangeStart >= 0);

		remap.Remap[0] = 0;
		remap.Palette[0] = 0;

		for (j = 1; j < ActiveColors; j++)
		{
			int v = int(luminosity[j] * 256.0);

			// Find the color range that this luminosity value lies within.
			const TranslationParm *parms = parmstart - 1;
			do
			{
				parms++;
				if (parms->RangeStart <= v && parms->RangeEnd >= v)
					break;
			}
			while (parms[1].RangeStart > parms[0].RangeEnd);

			// Linearly interpolate to find out which color this luminosity level gets.
			int rangev = ((v - parms->RangeStart) << 8) / (parms->RangeEnd - parms->RangeStart);
			int r = ((parms->Start[0] << 8) + rangev * (parms->End[0] - parms->Start[0])) >> 8; // red
			int g = ((parms->Start[1] << 8) + rangev * (parms->End[1] - parms->Start[1])) >> 8; // green
			int b = ((parms->Start[2] << 8) + rangev * (parms->End[2] - parms->Start[2])) >> 8; // blue
			r = clamp(r, 0, 255);
			g = clamp(g, 0, 255);
			b = clamp(b, 0, 255);
			remap.Remap[j] = ColorMatcher.Pick(r, g, b);
			remap.Palette[j] = PalEntry(255,r,g,b);
		}
		Ranges.Push(remap);

		// Advance to the next color range.
		while (parmstart[1].RangeStart > parmstart[0].RangeEnd)
		{
			parmstart++;
		}
		parmstart++;
	}
}

//==========================================================================
//
// FFont :: GetColorTranslation
//
//==========================================================================

FRemapTable *FFont::GetColorTranslation (EColorRange range, PalEntry *color) const
{
	if (noTranslate)
	{
		PalEntry retcolor = PalEntry(255, 255, 255, 255);
		if (range >= 0 && range < NumTextColors && range != CR_UNTRANSLATED)
		{
			retcolor = TranslationColors[range];
			retcolor.a = 255;
		}
		if (color != nullptr) *color = retcolor;
	}
	if (ActiveColors == 0)
		return nullptr;
	else if (range >= NumTextColors)
		range = CR_UNTRANSLATED;
	//if (range == CR_UNTRANSLATED && !translateUntranslated) return nullptr;
	return &Ranges[range];
}

//==========================================================================
//
// FFont :: GetCharCode
//
// If the character code is in the font, returns it. If it is not, but it
// is lowercase and has an uppercase variant present, return that. Otherwise
// return -1.
//
//==========================================================================

int FFont::GetCharCode(int code, bool needpic) const
{
	if (code < 0 && code >= -128)
	{
		// regular chars turn negative when the 8th bit is set.
		code &= 255;
	}
	if (code >= FirstChar && code <= LastChar && (!needpic || Chars[code - FirstChar].Pic != nullptr))
	{
		return code;
	}

	// Use different substitution logic based on the fonts content:
	// In a font which has both upper and lower case, prefer unaccented small characters over capital ones.
	// In a pure upper-case font, do not check for lower case replacements.
	if (!MixedCase)
	{
		// Try converting lowercase characters to uppercase.
		if (myislower(code))
		{
			code = upperforlower[code];
			if (code >= FirstChar && code <= LastChar && (!needpic || Chars[code - FirstChar].Pic != nullptr))
			{
				return code;
			}
		}
		// Try stripping accents from accented characters.
		int newcode = stripaccent(code);
		if (newcode != code)
		{
			code = newcode;
			if (code >= FirstChar && code <= LastChar && (!needpic || Chars[code - FirstChar].Pic != nullptr))
			{
				return code;
			}
		}
	}
	else
	{
		int originalcode = code;
		int newcode;

		// Try stripping accents from accented characters. This may repeat to allow multi-step fallbacks.
		while ((newcode = stripaccent(code)) != code)
		{
			code = newcode;
			if (code >= FirstChar && code <= LastChar && (!needpic || Chars[code - FirstChar].Pic != nullptr))
			{
				return code;
			}
		}

		code = originalcode;
		if (myislower(code))
		{
			int upper = upperforlower[code];
			// Stripping accents did not help - now try uppercase for lowercase
			if (upper != code) return GetCharCode(upper, needpic);
		}

		// Same for the uppercase character. Since we restart at the accented version this must go through the entire thing again.
		while ((newcode = stripaccent(code)) != code)
		{
			code = newcode;
			if (code >= FirstChar && code <= LastChar && (!needpic || Chars[code - FirstChar].Pic != nullptr))
			{
				return code;
			}
		}

	}

	return -1;
}

//==========================================================================
//
// FFont :: GetChar
//
//==========================================================================

FTexture *FFont::GetChar (int code, int *const width) const
{
	code = GetCharCode(code, false);
	int xmove = SpaceWidth;

	if (code >= 0)
	{
		code -= FirstChar;
		xmove = Chars[code].XMove;
		if (Chars[code].Pic == nullptr)
		{
			code = GetCharCode(code + FirstChar, true);
			if (code >= 0)
			{
				code -= FirstChar;
				xmove = Chars[code].XMove;
			}
		}
	}
	if (width != nullptr)
	{
		*width = xmove;
	}
	return (code < 0) ? nullptr : Chars[code].Pic;
}

//==========================================================================
//
// FFont :: GetCharWidth
//
//==========================================================================

int FFont::GetCharWidth (int code) const
{
	code = GetCharCode(code, false);
	return (code < 0) ? SpaceWidth : Chars[code - FirstChar].XMove;
}

//==========================================================================
//
// 
//
//==========================================================================

double GetBottomAlignOffset(FFont *font, int c)
{
	int w;
	FTexture *tex_zero = font->GetChar('0', &w);
	FTexture *texc = font->GetChar(c, &w);
	double offset = 0;
	if (texc) offset += texc->GetScaledTopOffsetDouble(0);
	if (tex_zero) offset += -tex_zero->GetScaledTopOffsetDouble(0) + tex_zero->GetScaledHeightDouble();
	return offset;
}

//==========================================================================
//
// Find string width using this font
//
//==========================================================================

int FFont::StringWidth(const uint8_t *string) const
{
	int w = 0;
	int maxw = 0;

	while (*string)
	{
		auto chr = GetCharFromString(string);
		if (chr == TEXTCOLOR_ESCAPE)
		{
			// We do not need to check for UTF-8 in here.
			if (*string == '[')
			{
				while (*string != '\0' && *string != ']')
				{
					++string;
				}
			}
			if (*string != '\0')
			{
				++string;
			}
			continue;
		}
		else if (chr == '\n')
		{
			if (w > maxw)
				maxw = w;
			w = 0;
		}
		else
		{
			w += GetCharWidth(chr) + GlobalKerning;
		}
	}

	return MAX(maxw, w);
}

//==========================================================================
//
// FFont :: LoadTranslations
//
//==========================================================================

void FFont::LoadTranslations()
{
	unsigned int count = LastChar - FirstChar + 1;
	uint32_t usedcolors[256] = {};
	uint8_t identity[256];
	double *luminosity;

	for (unsigned int i = 0; i < count; i++)
	{
		FFontChar1 *pic = static_cast<FFontChar1 *>(Chars[i].Pic);
		if(pic)
		{
			pic->SetSourceRemap(NULL); // Force the FFontChar1 to return the same pixels as the base texture
			RecordTextureColors (pic, usedcolors);
		}
	}

	ActiveColors = SimpleTranslation (usedcolors, PatchRemap, identity, &luminosity);

	for (unsigned int i = 0; i < count; i++)
	{
		if(Chars[i].Pic)
			static_cast<FFontChar1 *>(Chars[i].Pic)->SetSourceRemap(PatchRemap);
	}

	BuildTranslations (luminosity, identity, &TranslationParms[TranslationType][0], ActiveColors, nullptr);

	delete[] luminosity;
}

//==========================================================================
//
// FFont :: FFont - default constructor
//
//==========================================================================

FFont::FFont (int lump)
{
	Lump = lump;
	Chars = nullptr;
	PatchRemap = nullptr;
	FontName = NAME_None;
	Cursor = '_';
	noTranslate = false;
}

//==========================================================================
//
// FFont :: FixXMoves
//
// If a font has gaps in its characters, set the missing characters'
// XMoves to either SpaceWidth or the unaccented or uppercase variant's
// XMove. Missing XMoves must be initialized with INT_MIN beforehand.
//
//==========================================================================

void FFont::FixXMoves()
{
	for (int i = 0; i <= LastChar - FirstChar; ++i)
	{
		if (Chars[i].XMove == INT_MIN)
		{
			// Try an uppercase character.
			if (myislower(i + FirstChar))
			{
				int upper = upperforlower[FirstChar + i];
				if (upper >= FirstChar && upper <= LastChar )
				{
					Chars[i].XMove = Chars[upper - FirstChar].XMove;
					continue;
				}
			}
			// Try an unnaccented character.
			int noaccent = stripaccent(i + FirstChar);
			if (noaccent != i + FirstChar)
			{
				noaccent -= FirstChar;
				if (noaccent >= 0)
				{
					Chars[i].XMove = Chars[noaccent].XMove;
					continue;
				}
			}
			Chars[i].XMove = SpaceWidth;
		}
	}
}


