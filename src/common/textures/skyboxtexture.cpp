/*
** skyboxtexture.cpp
**
**---------------------------------------------------------------------------
** Copyright 2004-2019 Christoph Oelckers
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

#include "doomtype.h"
#include "sc_man.h"
#include "filesystem.h"
#include "textures.h"
#include "skyboxtexture.h"



//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

FSkyBox::FSkyBox() 
{ 
	faces[0]=faces[1]=faces[2]=faces[3]=faces[4]=faces[5]=NULL; 
	UseType = ETextureType::Override;
	gl_info.bSkybox = true;
	fliptop = false;
}

//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

FSkyBox::~FSkyBox()
{
	// The faces are only referenced but not owned so don't delete them.
}

//-----------------------------------------------------------------------------
//
// If something attempts to use this as a texture just pass the information of the first face.
//
//-----------------------------------------------------------------------------

const uint8_t *FSkyBox::GetColumn(FRenderStyle style, unsigned int column, const Span **spans_out)
{
	if (faces[0]) return faces[0]->GetColumn(style, column, spans_out);
	return NULL;
}

//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

const uint8_t *FSkyBox::GetPixels (FRenderStyle style)
{
	if (faces[0]) return faces[0]->GetPixels(style);
	return NULL;
}

//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

int FSkyBox::CopyTrueColorPixels(FBitmap *bmp, int x, int y, int rotate, FCopyInfo *inf)
{
	if (faces[0]) return faces[0]->CopyTrueColorPixels(bmp, x, y, rotate, inf);
	return 0;
}

//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

bool FSkyBox::UseBasePalette() 
{ 
	return false; // not really but here it's not important.
}	

//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

void FSkyBox::Unload () 
{
	//for(int i=0;i<6;i++) if (faces[i]) faces[i]->Unload();
}

//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

void ParseGldefSkybox(FScanner &sc)
{
	int facecount=0;

	sc.MustGetString();

	FSkyBox * sb = new FSkyBox;
	sb->Name = sc.String;
	sb->Name.ToUpper();
	if (sc.CheckString("fliptop"))
	{
		sb->fliptop = true;
	}
	sc.MustGetStringName("{");
	while (!sc.CheckString("}"))
	{
		sc.MustGetString();
		if (facecount<6) 
		{
			sb->faces[facecount] = TexMan[TexMan.GetTexture(sc.String, ETextureType::Wall, FTextureManager::TEXMAN_TryAny|FTextureManager::TEXMAN_Overridable)];
		}
		facecount++;
	}
	if (facecount != 3 && facecount != 6)
	{
		sc.ScriptError("%s: Skybox definition requires either 3 or 6 faces", sb->Name.GetChars());
	}
	sb->SetSize();
	TexMan.AddTexture(sb);
}

//-----------------------------------------------------------------------------
//
// gl_ParseVavoomSkybox
//
//-----------------------------------------------------------------------------

void ParseVavoomSkybox()
{
	int lump = fileSystem.CheckNumForName("SKYBOXES");

	if (lump < 0) return;

	FScanner sc(lump);
	while (sc.GetString())
	{
		int facecount=0;
		int maplump = -1;
		bool error = false;
		FSkyBox * sb = new FSkyBox;
		sb->Name = sc.String;
		sb->Name.ToUpper();
		sb->fliptop = true;
		sc.MustGetStringName("{");
		while (!sc.CheckString("}"))
		{
			if (facecount<6) 
			{
				sc.MustGetStringName("{");
				sc.MustGetStringName("map");
				sc.MustGetString();

				maplump = fileSystem.CheckNumForFullName(sc.String, true);

				FTexture *tex = TexMan.FindTexture(sc.String, ETextureType::Wall, FTextureManager::TEXMAN_TryAny);
				if (tex == NULL)
				{
					sc.ScriptMessage("Texture '%s' not found in Vavoom skybox '%s'\n", sc.String, sb->Name.GetChars());
					error = true;
				}
				sb->faces[facecount] = tex;
				sc.MustGetStringName("}");
			}
			facecount++;
		}
		if (facecount != 6)
		{
			sc.ScriptError("%s: Skybox definition requires 6 faces", sb->Name.GetChars());
		}
		sb->SetSize();
		if (!error) TexMan.AddTexture(sb);
	}
}

