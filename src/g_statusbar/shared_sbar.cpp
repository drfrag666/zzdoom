/*
** shared_sbar.cpp
** Base status bar implementation
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** Copyright 2017 Christoph Oelckers
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

#include <assert.h>

#include "templates.h"
#include "sbar.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "c_console.h"
#include "v_video.h"
#include "filesystem.h"
#include "s_sound.h"
#include "gi.h"
#include "doomstat.h"
#include "g_level.h"
#include "d_net.h"
#include "d_player.h"
#include "serializer.h"
#include "serialize_obj.h"
#include "r_utility.h"
#include "cmdlib.h"
#include "g_levellocals.h"
#include "vm.h"
#include "p_acs.h"
#include "sbarinfo.h"
#include "gstrings.h"
#include "events.h"
#include "g_game.h"
#include "utf8.h"
#include "m_fixed.h"

#include "../version.h"

#define XHAIRSHRINKSIZE		(1./18)
#define XHAIRPICKUPSIZE		(2+XHAIRSHRINKSIZE)
#define POWERUPICONSIZE		32

IMPLEMENT_CLASS(DBaseStatusBar, false, true)

IMPLEMENT_POINTERS_START(DBaseStatusBar)
	IMPLEMENT_POINTER(Messages[0])
	IMPLEMENT_POINTER(Messages[1])
	IMPLEMENT_POINTER(Messages[2])
	IMPLEMENT_POINTER(AltHud)
IMPLEMENT_POINTERS_END

EXTERN_CVAR (Bool, am_showmonsters)
EXTERN_CVAR (Bool, am_showsecrets)
EXTERN_CVAR (Bool, am_showitems)
EXTERN_CVAR (Bool, am_showtime)
EXTERN_CVAR (Bool, am_showtotaltime)
EXTERN_CVAR (Bool, noisedebug)
EXTERN_CVAR(Bool, vid_fps)
EXTERN_CVAR(Bool, inter_subtitles)
CVAR(Int, hud_scale, 0, CVAR_ARCHIVE);
CVAR(Bool, log_vgafont, false, CVAR_ARCHIVE)
CVAR(Bool, hud_oldscale, true, CVAR_ARCHIVE)

DBaseStatusBar *StatusBar;

extern int setblocks;

// [RH] Base blending values (for e.g. underwater)
int BaseBlendR, BaseBlendG, BaseBlendB;
float BaseBlendA;

CVAR (Int, paletteflash, 0, CVAR_ARCHIVE)
CVAR (Flag, pf_hexenweaps,	paletteflash, PF_HEXENWEAPONS)
CVAR (Flag, pf_poison,		paletteflash, PF_POISON)
CVAR (Flag, pf_ice,			paletteflash, PF_ICE)
CVAR (Flag, pf_hazard,		paletteflash, PF_HAZARD)

CUSTOM_CVARD(Float, hud_scalefactor, 1, CVAR_ARCHIVE, "changes the hud scale")
{
	if (self < 0.36f) self = 0.36f;
	else if (self > 1) self = 1;
	else if (StatusBar)
	{
		StatusBar->SetScale();
		setsizeneeded = true;
	}
}


// Stretch status bar to full screen width?
CUSTOM_CVAR (Int, st_scale, 0, CVAR_ARCHIVE)
{
	if (self < -1)
	{
		self = -1;
		return;
	}
	if (StatusBar)
	{
		StatusBar->SetScale();
		setsizeneeded = true;
	}
}
CUSTOM_CVAR(Bool, hud_aspectscale, false, CVAR_ARCHIVE)
{
	if (StatusBar)
	{
		StatusBar->SetScale();
		setsizeneeded = true;
	}
}

CVAR (Bool, crosshairon, true, CVAR_ARCHIVE);
CVAR (Int, crosshair, 0, CVAR_ARCHIVE)
CVAR (Bool, crosshairforce, false, CVAR_ARCHIVE)
CUSTOM_CVAR(Int, am_showmaplabel, 2, CVAR_ARCHIVE)
{
	if (self < 0 || self > 2) self = 2;
}

CVAR (Bool, idmypos, false, 0);
CVAR(Float, underwater_fade_scalar, 1.0f, CVAR_ARCHIVE) // [Nash] user-settable underwater blend intensity

//---------------------------------------------------------------------------
//
// Format the map name, include the map label if wanted
//
//---------------------------------------------------------------------------

void ST_FormatMapName(FString &mapname, const char *mapnamecolor)
{
	cluster_info_t *cluster = FindClusterInfo (level.cluster);
	bool ishub = (cluster != NULL && (cluster->flags & CLUSTER_HUB));

	mapname = "";
	if (am_showmaplabel == 1 || (am_showmaplabel == 2 && !ishub))
	{
		mapname << level.MapName << ": ";
	}
	mapname << mapnamecolor << level.LevelName;
}

//---------------------------------------------------------------------------
//
// Load crosshair definitions
//
//---------------------------------------------------------------------------

void ST_LoadCrosshair(bool alwaysload)
{
	int num = 0;

	if (!crosshairforce &&
		players[consoleplayer].camera != NULL &&
		players[consoleplayer].camera->player != NULL &&
		players[consoleplayer].camera->player->ReadyWeapon != NULL)
	{
		num = players[consoleplayer].camera->player->ReadyWeapon->IntVar(NAME_Crosshair);
	}
	if (num == 0)
	{
		num = crosshair;
	}
	ST_LoadCrosshair(num, alwaysload);
}


//---------------------------------------------------------------------------
//
// ST_Clear
//
//---------------------------------------------------------------------------

void ST_Clear()
{
	if (StatusBar != NULL)
	{
		StatusBar->Destroy();
		StatusBar = NULL;
	}
	ST_UnloadCrosshair();
}

//---------------------------------------------------------------------------
//
// create a new status bar
//
//---------------------------------------------------------------------------

static void CreateBaseStatusBar()
{
	assert(nullptr == StatusBar);

	PClass* const statusBarClass = PClass::FindClass("BaseStatusBar");
	assert(nullptr != statusBarClass);

	StatusBar = static_cast<DBaseStatusBar*>(statusBarClass->CreateNew());
	StatusBar->SetSize(0);
}

static void CreateGameInfoStatusBar(bool &shouldWarn)
{
	auto cls = PClass::FindClass(gameinfo.statusbarclass);
	if (cls == nullptr)
	{
		if (shouldWarn)
		{
			Printf(TEXTCOLOR_RED "Unknown status bar class \"%s\"\n", gameinfo.statusbarclass.GetChars());
			shouldWarn = false;
		}
	}
	else
	{
		if (cls->IsDescendantOf(RUNTIME_CLASS(DBaseStatusBar)))
		{
			StatusBar = (DBaseStatusBar *)cls->CreateNew();
		}
		else if (shouldWarn)
		{
			Printf(TEXTCOLOR_RED "Status bar class \"%s\" is not derived from BaseStatusBar\n", gameinfo.statusbarclass.GetChars());
			shouldWarn = false;
		}
	}
}

void ST_CreateStatusBar(bool bTitleLevel)
{
	if (StatusBar != NULL)
	{
		StatusBar->Destroy();
		StatusBar = NULL;
	}
	GC::AddMarkerFunc([]() { GC::Mark(StatusBar); });

	bool shouldWarn = true;

	if (bTitleLevel)
	{
		CreateBaseStatusBar();
	}
	else
	{
		// The old rule of 'what came last wins' goes here, as well.
		// If the most recent SBARINFO definition comes before a status bar class definition it will be picked,
		// if the class is defined later, this will be picked. If both come from the same file, the class definition will win.
		int sbarinfolump = fileSystem.CheckNumForName("SBARINFO");
		int sbarinfofile = fileSystem.GetFileContainer(sbarinfolump);
		if (gameinfo.statusbarclassfile >= gameinfo.statusbarfile && gameinfo.statusbarclassfile >= sbarinfofile)
		{
			CreateGameInfoStatusBar(shouldWarn);
		}
	}
	if (StatusBar == nullptr && SBarInfoScript[SCRIPT_CUSTOM] != nullptr)
	{
		int cstype = SBarInfoScript[SCRIPT_CUSTOM]->GetGameType();

		//Did the user specify a "base"
		if (cstype == GAME_Any) //Use the default, empty or custom.
		{
			StatusBar = CreateCustomStatusBar(SCRIPT_CUSTOM);
		}
		else
		{
			StatusBar = CreateCustomStatusBar(SCRIPT_DEFAULT);
		}
		// SBARINFO failed so try the current statusbarclass again.
		if (StatusBar == nullptr)
		{
			CreateGameInfoStatusBar(shouldWarn);
		}
	}
	if (StatusBar == nullptr)
	{
		FName defname = NAME_None;

		if (gameinfo.gametype & GAME_DoomChex) defname = "DoomStatusBar";
		else if (gameinfo.gametype == GAME_Heretic) defname = "HereticStatusBar";
		else if (gameinfo.gametype == GAME_Hexen) defname = "HexenStatusBar";
		else if (gameinfo.gametype == GAME_Strife) defname = "StrifeStatusBar";
		if (defname != NAME_None)
		{
			auto cls = PClass::FindClass(defname);
			if (cls != nullptr)
			{
				assert(cls->IsDescendantOf(RUNTIME_CLASS(DBaseStatusBar)));
				StatusBar = (DBaseStatusBar *)cls->CreateNew();
			}
		}
	}
	if (StatusBar == nullptr)
	{
		CreateBaseStatusBar();
	}

	IFVIRTUALPTR(StatusBar, DBaseStatusBar, Init)
	{
		VMValue params[] = { StatusBar };
		VMCall(func, params, 1, nullptr, 0);
	}

	GC::WriteBarrier(StatusBar);
	StatusBar->AttachToPlayer(&players[consoleplayer]);
	StatusBar->NewGame();
}
//---------------------------------------------------------------------------
//
// Constructor
//
//---------------------------------------------------------------------------
DBaseStatusBar::DBaseStatusBar ()
{
	CompleteBorder = false;
	Centering = false;
	FixedOrigin = false;
	CrosshairSize = 1.;
	memset(Messages, 0, sizeof(Messages));
	Displacement = 0;
	CPlayer = NULL;
	ShowLog = false;
	defaultScale = { (double)CleanXfac, (double)CleanYfac };
	SetSize(0);

	CreateAltHUD();
}

//---------------------------------------------------------------------------
//
// PROP Destroy
//
//---------------------------------------------------------------------------

void DBaseStatusBar::OnDestroy ()
{
	for (size_t i = 0; i < countof(Messages); ++i)
	{
		DHUDMessageBase *msg = Messages[i];
		while (msg)
		{
			DHUDMessageBase *next = msg->Next;
			msg->Next = nullptr;
			msg->Destroy();
			msg = next;
		}
		Messages[i] = nullptr;
	}
	if (AltHud) AltHud->Destroy();
	Super::OnDestroy();
}

//---------------------------------------------------------------------------
//
// PROC SetScaled
//
//---------------------------------------------------------------------------

void DBaseStatusBar::SetScale ()
{
	if (!hud_oldscale)
	{
		Super::SetScale();
		return;
	}

	ValidateResolution(HorizontalResolution, VerticalResolution);

	int w = SCREENWIDTH;
	int h = SCREENHEIGHT;
	if (st_scale < 0 || ForcedScale)
	{
		// This is the classic fullscreen scale with aspect ratio compensation.
		int sby = VerticalResolution - RelTop;
		float aspect = ActiveRatio(w, h);
		if (!AspectTallerThanWide(aspect))
		{ 
			// Wider or equal than 4:3 
			SBarTop = Scale(sby, h, VerticalResolution);
			double width4_3 = w * 1.333 / aspect;
			ST_X = int((w - width4_3) / 2);
		}
		else
		{ // 5:4 resolution
			ST_X = 0;

			// this was far more obtuse before...
			double height4_3 = h * aspect / 1.333;
			SBarTop = int(h - height4_3 + sby * height4_3 / VerticalResolution);
		}
		Displacement = 0;
		SBarScale.X = -1;
		ST_Y = 0;
	}
	else
	{
		// Since status bars and HUDs can be designed for non 320x200 screens this needs to be factored in here.
		// The global scaling factors are for resources at 320x200, so if the actual ones are higher resolution
		// the resulting scaling factor needs to be reduced accordingly.
		int realscale = clamp((320 * GetUIScale(st_scale)) / HorizontalResolution, 1, w / HorizontalResolution);

		double realscaley = realscale * (hud_aspectscale ? 1.2 : 1.);

		ST_X = (w - HorizontalResolution * realscale) / 2;
		SBarTop = int(h - RelTop * realscaley);
		if (RelTop > 0)
		{
			Displacement = double((SBarTop * VerticalResolution / h) - (VerticalResolution - RelTop))/RelTop/realscaley;
		}
		else
		{
			Displacement = 0;
		}
		SBarScale.X = realscale;
		SBarScale.Y = realscaley;
		ST_Y = int(h - VerticalResolution * realscaley);
	}
}

//---------------------------------------------------------------------------
//
// PROC GetHUDScale
//
//---------------------------------------------------------------------------

DVector2 DBaseStatusBar::GetHUDScale() const
{
	if (!hud_oldscale)
	{
		return Super::GetHUDScale();
	}

	int scale;
	if (hud_scale < 0 || ForcedScale)	// a negative value is the equivalent to the old boolean hud_scale. This can yield different values for x and y for higher resolutions.
	{
		return defaultScale;
	}
	scale = GetUIScale(hud_scale);

	int hres = HorizontalResolution;
	int vres = VerticalResolution;
	ValidateResolution(hres, vres);

	// Since status bars and HUDs can be designed for non 320x200 screens this needs to be factored in here.
	// The global scaling factors are for resources at 320x200, so if the actual ones are higher resolution
	// the resulting scaling factor needs to be reduced accordingly.
	int realscale = MAX<int>(1, (320 * scale) / hres);
	return{ double(realscale), double(realscale * (hud_aspectscale ? 1.2 : 1.)) };
}

//============================================================================
//
// automap HUD common drawer
// This is not called directly to give a status bar the opportunity to
// change the text colors. If you want to do something different,
// override DrawAutomap directly.
//
//============================================================================

void FormatMapName(FLevelLocals *self, int cr, FString *result);

void DBaseStatusBar::DoDrawAutomapHUD(int crdefault, int highlight)
{
	auto scale = GetUIScale(hud_scale);
	auto font = generic_ui ? NewSmallFont : SmallFont;
	auto font2 = font;
	auto vwidth = screen->GetWidth() / scale;
	auto vheight = screen->GetHeight() / scale;
	auto fheight = font->GetHeight();
	FString textbuffer;
	int sec;
	int y = 0;
	int textdist = 4;
	int zerowidth = font->GetCharWidth('0');

	if (!generic_ui)
	{
		// If the original font does not have accents this will strip them - but a fallback to the VGA font is not desirable here for such cases.
		if (!font->CanPrint(GStrings("AM_MONSTERS")) || !font->CanPrint(GStrings("AM_SECRETS")) || !font->CanPrint(GStrings("AM_ITEMS"))) font2 = OriginalSmallFont;
	}

	if (am_showtime)
	{
		if (vid_fps) y += (NewConsoleFont->GetHeight() * active_con_scale() + 5) / scale;
		sec = Tics2Seconds(primaryLevel->time);
		textbuffer.Format("%02d:%02d:%02d", sec / 3600, (sec % 3600) / 60, sec % 60);
		screen->DrawText(font, crdefault, vwidth - zerowidth * 8 - textdist, y, textbuffer, DTA_VirtualWidth, vwidth, DTA_VirtualHeight, vheight,
			DTA_Monospace, EMonospacing::CellCenter, DTA_Spacing, zerowidth, DTA_KeepRatio, true, TAG_END);
		y += fheight;
	}

	if (am_showtotaltime)
	{
		sec = Tics2Seconds(primaryLevel->totaltime);
		textbuffer.Format("%02d:%02d:%02d", sec / 3600, (sec % 3600) / 60, sec % 60);
		screen->DrawText(font, crdefault, vwidth - zerowidth * 8 - textdist, y, textbuffer, DTA_VirtualWidth, vwidth, DTA_VirtualHeight, vheight,
			DTA_Monospace, EMonospacing::CellCenter, DTA_Spacing, zerowidth, DTA_KeepRatio, true, TAG_END);
	}

	if (!deathmatch)
	{
		y = 0;
		if (am_showmonsters)
		{
			textbuffer.Format("%s\34%c %d/%d", GStrings("AM_MONSTERS"), crdefault + 65, primaryLevel->killed_monsters, primaryLevel->total_monsters);
			screen->DrawText(font2, highlight, textdist, y, textbuffer, DTA_KeepRatio, true, DTA_VirtualWidth, vwidth, DTA_VirtualHeight, vheight, TAG_DONE);
			y += fheight;
		}

		if (am_showsecrets)
		{
			textbuffer.Format("%s\34%c %d/%d", GStrings("AM_SECRETS"), crdefault + 65, primaryLevel->found_secrets, primaryLevel->total_secrets);
			screen->DrawText(font2, highlight, textdist, y, textbuffer, DTA_KeepRatio, true, DTA_VirtualWidth, vwidth, DTA_VirtualHeight, vheight, TAG_DONE);
			y += fheight;
		}

		// Draw item count
		if (am_showitems)
		{
			textbuffer.Format("%s\34%c %d/%d", GStrings("AM_ITEMS"), crdefault + 65, primaryLevel->found_items, primaryLevel->total_items);
			screen->DrawText(font2, highlight, textdist, y, textbuffer, DTA_KeepRatio, true, DTA_VirtualWidth, vwidth, DTA_VirtualHeight, vheight, TAG_DONE);
			y += fheight;
		}

	}

	FormatMapName(primaryLevel, crdefault, &textbuffer);

	if (!generic_ui)
	{
		if (!font->CanPrint(textbuffer)) font = OriginalSmallFont;
	}

	auto lines = V_BreakLines(font, vwidth - 32, textbuffer, true);
	auto numlines = lines.Size();
	auto finalwidth = lines.Last().Width;


	// calculate the top of the statusbar including any protrusion and transform it from status bar to screen space.
	double x = 0, yy = 0, w = HorizontalResolution, h = 0;
	StatusbarToRealCoords(x, yy, w, h);

	IFVIRTUAL(DStatusBarCore, GetProtrusion)
	{
		int prot = 0;
		VMValue params[] = { this, double(finalwidth * scale / w) };
		VMReturn ret(&prot);
		VMCall(func, params, 2, &ret, 1);
		h = prot;
	}

	StatusbarToRealCoords(x, yy, w, h);

	// Get the y coordinate for the first line of the map name text.
	y = Scale(GetTopOfStatusbar() - int(h), vheight, screen->GetHeight()) - fheight * numlines;

	// Draw the texts centered above the status bar.
	for (unsigned i = 0; i < numlines; i++)
	{
		int x = (vwidth - font->StringWidth(lines[i].Text)) / 2;
		screen->DrawText(font, highlight, x, y, lines[i].Text, DTA_KeepRatio, true, DTA_VirtualWidth, vwidth, DTA_VirtualHeight, vheight, TAG_DONE);
		y += fheight;
	}
}

DEFINE_ACTION_FUNCTION(DBaseStatusBar, DoDrawAutomapHUD)
{
	PARAM_SELF_PROLOGUE(DBaseStatusBar);
	PARAM_INT(crdefault);
	PARAM_INT(highlight);
	self->DoDrawAutomapHUD(crdefault, highlight);
	return 0;
}

//---------------------------------------------------------------------------
//
// PROC AttachToPlayer
//
//---------------------------------------------------------------------------

void DBaseStatusBar::AttachToPlayer(player_t *player)
{
	IFVIRTUAL(DBaseStatusBar, AttachToPlayer)
	{
		VMValue params[] = { (DObject*)this, player };
		VMCall(func, params, countof(params), nullptr, 0);
	}
}

//---------------------------------------------------------------------------
//
// PROC GetPlayer
//
//---------------------------------------------------------------------------

int DBaseStatusBar::GetPlayer ()
{
	return int(CPlayer - players);
}

//---------------------------------------------------------------------------
//
// PROC Tick
//
//---------------------------------------------------------------------------

void DBaseStatusBar::Tick ()
{
	for (size_t i = 0; i < countof(Messages); ++i)
	{
		DHUDMessageBase *msg = Messages[i];

		while (msg)
		{
			DHUDMessageBase *next = msg->Next;

			if (msg->CallTick ())
			{
				DetachMessage(msg);
				msg->Destroy();
			}
			msg = next;
		}

		// If the crosshair has been enlarged, shrink it.
		if (CrosshairSize > 1.)
		{
			CrosshairSize -= XHAIRSHRINKSIZE;
			if (CrosshairSize < 1.)
			{
				CrosshairSize = 1.;
			}
		}
	}

	if (artiflashTick > 0)
		artiflashTick--;

	if (itemflashFade > 0)
	{
		itemflashFade -= 1 / 14.;
		if (itemflashFade < 0)
		{
			itemflashFade = 0;
		}
	}

}

void DBaseStatusBar::CallTick()
{
	IFVIRTUAL(DBaseStatusBar, Tick)
	{
		VMValue params[] = { (DObject*)this };
		VMCall(func, params, countof(params), nullptr, 0);
	}
	else Tick();
	mugshot.Tick(CPlayer);
}

//---------------------------------------------------------------------------
//
// PROC AttachMessage
//
//---------------------------------------------------------------------------

void DBaseStatusBar::AttachMessage (DHUDMessageBase *msg, uint32_t id, int layer)
{
	DHUDMessageBase *old = NULL;
	DObject* pointing;
	TObjPtr<DHUDMessageBase *>*prevp;
	DHUDMessageBase* prev;

	old = (id == 0 || id == 0xFFFFFFFF) ? NULL : DetachMessage (id);
	if (old != NULL)
	{
		old->Destroy();
	}

	// Merge unknown layers into the default layer.
	if ((size_t)layer >= countof(Messages))
	{
		layer = HUDMSGLayer_Default;
	}

	pointing = this;
	prevp = &Messages[layer];
	prev = *prevp;

	// The ID serves as a priority, where lower numbers appear in front of
	// higher numbers. (i.e. The list is sorted in descending order, since
	// it gets drawn back to front.)
	while (prev != NULL && prev->SBarID > id)
	{
		pointing = prev;
		prevp = &prev->Next;
		prev = *prevp;
	}

	msg->Next = prev;
	msg->SBarID = id;
	*prevp = msg;
	GC::WriteBarrier(msg, prev);
	GC::WriteBarrier(pointing, msg);
}

//---------------------------------------------------------------------------
//
// PROC DetachMessage
//
//---------------------------------------------------------------------------

DHUDMessageBase *DBaseStatusBar::DetachMessage (DHUDMessageBase *msg)
{
	for (size_t i = 0; i < countof(Messages); ++i)
	{
		DHUDMessageBase *probe = Messages[i];
		DObject* pointing = this;
		TObjPtr<DHUDMessageBase *>*prev = &Messages[i];

		while (probe && probe != msg)
		{
			pointing = probe;
			prev = &probe->Next;
			probe = probe->Next;
		}
		if (probe != NULL)
		{
			GC::WriteBarrier(pointing, probe->Next);
			*prev = probe->Next;
			probe->Next = nullptr;
			return probe;
		}
	}
	return NULL;
}

DHUDMessageBase *DBaseStatusBar::DetachMessage (uint32_t id)
{
	for (size_t i = 0; i < countof(Messages); ++i)
	{
		DObject* pointing = this;
		DHUDMessageBase *probe = Messages[i];
		TObjPtr<DHUDMessageBase *>*prev = &Messages[i];

		while (probe && probe->SBarID != id)
		{
			pointing = probe;
			prev = &probe->Next;
			probe = probe->Next;
		}
		if (probe != NULL)
		{
			GC::WriteBarrier(pointing, probe->Next);
			*prev = probe->Next;
			probe->Next = nullptr;
			return probe;
		}
	}
	return NULL;
}

//---------------------------------------------------------------------------
//
// PROC DetachAllMessages
//
//---------------------------------------------------------------------------

void DBaseStatusBar::DetachAllMessages ()
{
	for (size_t i = 0; i < countof(Messages); ++i)
	{
		DHUDMessageBase *probe = Messages[i];

		Messages[i] = nullptr;
		while (probe != NULL)
		{
			DHUDMessageBase *next = probe->Next;
			probe->Destroy();
			probe = next;
		}
	}
}

//---------------------------------------------------------------------------
//
// PROC ShowPlayerName
//
//---------------------------------------------------------------------------

void DBaseStatusBar::ShowPlayerName ()
{
	EColorRange color;

	color = (CPlayer == &players[consoleplayer]) ? CR_GOLD : CR_GREEN;
	AttachMessage (Create<DHUDMessageFadeOut> (nullptr, CPlayer->userinfo.GetName(),
		1.5f, 0.92f, 0, 0, color, 2.f, 0.35f), MAKE_ID('P','N','A','M'));
}

//---------------------------------------------------------------------------
//
// RefreshBackground
//
//---------------------------------------------------------------------------

void DBaseStatusBar::RefreshBackground () const
{
	int x, x2, y;

	float ratio = ActiveRatio (SCREENWIDTH, SCREENHEIGHT);
	x = ST_X;
	y = SBarTop;

	if(!CompleteBorder)
	{
		if(y < SCREENHEIGHT)
		{
			V_DrawBorder (x+1, y, SCREENWIDTH, y+1);
			V_DrawBorder (x+1, SCREENHEIGHT-1, SCREENWIDTH, SCREENHEIGHT);
		}
	}
	else
	{
		x = SCREENWIDTH;
	}

	if (x > 0)
	{
		if(!CompleteBorder)
		{
			x2 = SCREENWIDTH - ST_X;
		}
		else
		{
			x2 = SCREENWIDTH;
		}

		V_DrawBorder (0, y, x+1, SCREENHEIGHT);
		V_DrawBorder (x2-1, y, SCREENWIDTH, SCREENHEIGHT);

		if (setblocks >= 10)
		{
			FTexture *p = TexMan[gameinfo.Border.b];
			if (p != NULL)
			{
				screen->FlatFill(0, y, x, y + p->GetHeight(), p, true);
				screen->FlatFill(x2, y, SCREENWIDTH, y + p->GetHeight(), p, true);
			}
		}
	}
}

//---------------------------------------------------------------------------
//
// DrawCrosshair
//
//---------------------------------------------------------------------------

void DBaseStatusBar::DrawCrosshair ()
{
	if (!crosshairon)
	{
		return;
	}

	// Don't draw the crosshair in chasecam mode
	if (players[consoleplayer].cheats & CF_CHASECAM)
	{
		return;
	}

	ST_LoadCrosshair();

	// Don't draw the crosshair if there is none
	if (gamestate == GS_TITLELEVEL || r_viewpoint.camera->health <= 0)
	{
		return;
	}
	int health = Scale(CPlayer->health, 100, CPlayer->mo->GetDefault()->health);

	ST_DrawCrosshair(health, viewwidth / 2 + viewwindowx, viewheight / 2 + viewwindowy, CrosshairSize);
}

//---------------------------------------------------------------------------
//
// FlashCrosshair
//
//---------------------------------------------------------------------------

void DBaseStatusBar::FlashCrosshair ()
{
	CrosshairSize = XHAIRPICKUPSIZE;
}

//---------------------------------------------------------------------------
//
// DrawMessages
//
//---------------------------------------------------------------------------

void DBaseStatusBar::DrawMessages (int layer, int bottom)
{
	DHUDMessageBase *msg = Messages[layer];
	int visibility = 0;

	if (viewactive)
	{
		visibility |= HUDMSG_NotWith3DView;
	}
	if (automapactive)
	{
		visibility |= viewactive ? HUDMSG_NotWithOverlayMap : HUDMSG_NotWithFullMap;
	}
	while (msg)
	{
		DHUDMessageBase *next = msg->Next;
		msg->CallDraw (bottom, visibility);
		msg = next;
	}
}

//---------------------------------------------------------------------------
//
// Draw
//
//---------------------------------------------------------------------------

void DBaseStatusBar::Draw (EHudState state, double ticFrac)
{
	// HUD_AltHud state is for popups only
	if (state == HUD_AltHud)
		return;

	if (state == HUD_StatusBar)
	{
		RefreshBackground ();
	}

	if (idmypos)
	{ 
		// Draw current coordinates
		IFVIRTUAL(DBaseStatusBar, DrawMyPos)
		{
			VMValue params[] = { (DObject*)this };
			VMCall(func, params, countof(params), nullptr, 0);
		}
		V_SetBorderNeedRefresh();
	}

	if (viewactive)
	{
		if (CPlayer && CPlayer->camera && CPlayer->camera->player)
		{
			DrawCrosshair ();
		}
	}
	else if (automapactive)
	{
		IFVIRTUAL(DBaseStatusBar, DrawAutomapHUD)
		{
			VMValue params[] = { (DObject*)this, r_viewpoint.TicFrac };
			VMCall(func, params, countof(params), nullptr, 0);
		}
	}
}

void DBaseStatusBar::CallDraw(EHudState state, double ticFrac)
{
	IFVIRTUAL(DBaseStatusBar, Draw)
	{
		VMValue params[] = { (DObject*)this, state, ticFrac };
		VMCall(func, params, countof(params), nullptr, 0);
	}
	else Draw(state, ticFrac);
	screen->ClearClipRect();	// make sure the scripts don't leave a valid clipping rect behind.
	BeginStatusBar(BaseSBarHorizontalResolution, BaseSBarVerticalResolution, BaseRelTop, false);
}

void DBaseStatusBar::DrawLog ()
{
	int hudwidth, hudheight;
	const FString & text = (inter_subtitles && CPlayer->SubtitleCounter) ? CPlayer->SubtitleText : CPlayer->LogText;

	if (text.IsNotEmpty())
	{
		// This uses the same scaling as regular HUD messages
		auto scale = active_con_scaletext(generic_ui); // generic_ui || log_vgafont
		hudwidth = SCREENWIDTH / scale;
		hudheight = SCREENHEIGHT / scale;
		FFont *font = (generic_ui || log_vgafont)? NewSmallFont : SmallFont;

		int linelen = hudwidth<640? Scale(hudwidth,9,10)-40 : 560;
		auto lines = V_BreakLines (font, linelen, text[0] == '$'? GStrings(text.GetChars()+1) : text.GetChars());
		int height = 20;

		for (unsigned i = 0; i < lines.Size(); i++) height += font->GetHeight ();

		int x,y,w;

		if (linelen<560)
		{
			x=hudwidth/20;
			y=hudheight/8;
			w=hudwidth-2*x;
		}
		else
		{
			x=(hudwidth>>1)-300;
			y=hudheight/8-(height>>1);
			if (y<0) y=0;
			w=600;
		}
		screen->Dim(0, 0.5f, Scale(x, SCREENWIDTH, hudwidth), Scale(y, SCREENHEIGHT, hudheight), 
							 Scale(w, SCREENWIDTH, hudwidth), Scale(height, SCREENHEIGHT, hudheight));
		x+=20;
		y+=10;
		for (const FBrokenLines &line : lines)
		{
			screen->DrawText (font, CPlayer->SubtitleCounter? CR_CYAN : CR_UNTRANSLATED, x, y, line.Text,
				DTA_KeepRatio, true,
				DTA_VirtualWidth, hudwidth, DTA_VirtualHeight, hudheight, TAG_DONE);
			y += font->GetHeight ();
		}
	}
}

bool DBaseStatusBar::MustDrawLog(EHudState state)
{
	IFVIRTUAL(DBaseStatusBar, MustDrawLog)
	{
		VMValue params[] = { (DObject*)this, int(state) };
		int rv;
		VMReturn ret(&rv);
		VMCall(func, params, countof(params), &ret, 1);
		return !!rv;
	}
	return true;
}

void DBaseStatusBar::SetMugShotState(const char *stateName, bool waitTillDone, bool reset)
{
	IFVIRTUAL(DBaseStatusBar, SetMugShotState)
	{
		FString statestring = stateName;
		VMValue params[] = { (DObject*)this, &statestring, waitTillDone, reset };
		VMCall(func, params, countof(params), nullptr, 0);
	}
}

//---------------------------------------------------------------------------
//
// DrawBottomStuff
//
//---------------------------------------------------------------------------

void DBaseStatusBar::DrawBottomStuff (EHudState state)
{
	primaryLevel->localEventManager->RenderUnderlay(state);
	DrawMessages (HUDMSGLayer_UnderHUD, (state == HUD_StatusBar) ? GetTopOfStatusbar() : SCREENHEIGHT);
}

//---------------------------------------------------------------------------
//
// DrawTopStuff
//
//---------------------------------------------------------------------------

void DBaseStatusBar::DrawTopStuff (EHudState state)
{
	if (demoplayback && demover != DEMOGAMEVERSION)
	{
		screen->DrawText (SmallFont, CR_TAN, 0, GetTopOfStatusbar() - 40 * CleanYfac,
			"Demo was recorded with a different version\n"
			"of " GAMENAME ". Expect it to go out of sync.",
			DTA_CleanNoMove, true, TAG_DONE);
	}

	if (state != HUD_AltHud)
	{
		auto saved = fullscreenOffsets;
		fullscreenOffsets = true;
		IFVIRTUAL(DBaseStatusBar, DrawPowerups)
		{
			VMValue params[] = { (DObject*)this };
			VMCall(func, params, 1, nullptr, 0);
		}
		fullscreenOffsets = saved;
	}

	if (automapactive && !viewactive)
	{
		DrawMessages (HUDMSGLayer_OverMap, (state == HUD_StatusBar) ? GetTopOfStatusbar() : SCREENHEIGHT);
	}
	DrawMessages (HUDMSGLayer_OverHUD, (state == HUD_StatusBar) ? GetTopOfStatusbar() : SCREENHEIGHT);
	primaryLevel->localEventManager->RenderOverlay(state);

	DrawConsistancy ();
	DrawWaiting ();
	if ((ShowLog && MustDrawLog(state)) || (inter_subtitles && CPlayer->SubtitleCounter > 0)) DrawLog ();

	if (noisedebug)
	{
		S_NoiseDebug ();
	}
}

//---------------------------------------------------------------------------
//
// BlendView
//
//---------------------------------------------------------------------------

void DBaseStatusBar::BlendView (float blend[4])
{
	// [Nash] Allow user to set blend intensity
	float cnt = (BaseBlendA * underwater_fade_scalar);

	V_AddBlend (BaseBlendR / 255.f, BaseBlendG / 255.f, BaseBlendB / 255.f, cnt, blend);
	V_AddPlayerBlend(CPlayer, blend, 1.0f, 228);

	if (screen->Accel2D || (CPlayer->camera != NULL && menuactive == MENU_Off && ConsoleState == c_up))
	{
		player_t *player = (CPlayer->camera != NULL && CPlayer->camera->player != NULL) ? CPlayer->camera->player : CPlayer;
		V_AddBlend (player->BlendR, player->BlendG, player->BlendB, player->BlendA, blend);
	}

	V_SetBlend ((int)(blend[0] * 255.0f), (int)(blend[1] * 255.0f),
				(int)(blend[2] * 255.0f), (int)(blend[3] * 256.0f));
}

void DBaseStatusBar::DrawConsistancy () const
{
	static bool firsttime = true;
	int i;
	char conbuff[64], *buff_p;

	if (!netgame)
		return;

	buff_p = NULL;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && players[i].inconsistant)
		{
			if (buff_p == NULL)
			{
				strcpy (conbuff, "Out of sync with:");
				buff_p = conbuff + 17;
			}
			*buff_p++ = ' ';
			*buff_p++ = '1' + i;
			*buff_p = 0;
		}
	}

	if (buff_p != NULL)
	{
		if (firsttime)
		{
			firsttime = false;
			if (debugfile)
			{
				fprintf (debugfile, "%s as of tic %d (%d)\n", conbuff,
					players[1-consoleplayer].inconsistant,
					players[1-consoleplayer].inconsistant/ticdup);
			}
		}
		screen->DrawText (SmallFont, CR_GREEN, 
			(screen->GetWidth() - SmallFont->StringWidth (conbuff)*CleanXfac) / 2,
			0, conbuff, DTA_CleanNoMove, true, TAG_DONE);
		BorderTopRefresh = screen->GetPageCount ();
	}
}

void DBaseStatusBar::DrawWaiting () const
{
	int i;
	char conbuff[64], *buff_p;

	if (!netgame)
		return;

	buff_p = NULL;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && players[i].waiting)
		{
			if (buff_p == NULL)
			{
				strcpy (conbuff, "Waiting for:");
				buff_p = conbuff + 12;
			}
			*buff_p++ = ' ';
			*buff_p++ = '1' + i;
			*buff_p = 0;
		}
	}

	if (buff_p != NULL)
	{
		screen->DrawText (SmallFont, CR_ORANGE, 
			(screen->GetWidth() - SmallFont->StringWidth (conbuff)*CleanXfac) / 2,
			SmallFont->GetHeight()*CleanYfac, conbuff, DTA_CleanNoMove, true, TAG_DONE);
		BorderTopRefresh = screen->GetPageCount ();
	}
}

void DBaseStatusBar::NewGame ()
{
	IFVIRTUAL(DBaseStatusBar, NewGame)
	{
		VMValue params[] = { (DObject*)this };
		VMCall(func, params, countof(params), nullptr, 0);
	}
	mugshot.Reset();
}

void DBaseStatusBar::ShowPop(int pop)
{
	IFVIRTUAL(DBaseStatusBar, ShowPop)
	{
		VMValue params[] = { (DObject*)this, pop };
		VMCall(func, params, countof(params), nullptr, 0);
	}
}



void DBaseStatusBar::SerializeMessages(FSerializer &arc)
{
	arc.Array("hudmessages", Messages, 3, true);
}

void DBaseStatusBar::ScreenSizeChanged ()
{
	// We need to recalculate the sizing info
	SetSize(RelTop, HorizontalResolution, VerticalResolution);

	for (size_t i = 0; i < countof(Messages); ++i)
	{
	DHUDMessageBase *message = Messages[i];
		while (message != NULL)
		{
			message->CallScreenSizeChanged ();
			message = message->Next;
		}
	}
}

void DBaseStatusBar::CallScreenSizeChanged()
{
	IFVIRTUAL(DBaseStatusBar, ScreenSizeChanged)
	{
		VMValue params[] = { (DObject*)this };
		VMCall(func, params, countof(params), nullptr, 0);
	}
	else ScreenSizeChanged();
}

//---------------------------------------------------------------------------
//
// ValidateInvFirst
//
// Returns an inventory item that, when drawn as the first item, is sure to
// include the selected item in the inventory bar.
//
//---------------------------------------------------------------------------

AActor *DBaseStatusBar::ValidateInvFirst (int numVisible) const
{
	IFVM(BaseStatusBar, ValidateInvFirst)
	{
		VMValue params[] = { const_cast<DBaseStatusBar*>(this), numVisible };
		AActor *item;
		VMReturn ret((void**)&item);
		VMCall(func, params, 2, &ret, 1);
		return item;
	}
	return nullptr;
}

uint32_t DBaseStatusBar::GetTranslation() const
{
	if (gameinfo.gametype & GAME_Raven)
		return TRANSLATION(TRANSLATION_PlayersExtra, int(CPlayer - players));
	return TRANSLATION(TRANSLATION_Players, int(CPlayer - players));
}

//============================================================================
//
// CCMD showpop
//
// Asks the status bar to show a pop screen.
//
//============================================================================

CCMD (showpop)
{
	if (argv.argc() != 2)
	{
		Printf ("Usage: showpop <popnumber>\n");
	}
	else if (StatusBar != NULL)
	{
		int popnum = atoi (argv[1]);
		if (popnum < 0)
		{
			popnum = 0;
		}
		StatusBar->ShowPop (popnum);
	}
}

static DObject *InitObject(PClass *type, int paramnum, VM_ARGS)
{
	auto obj =  type->CreateNew();
	// Todo: init
	return obj;
}



//---------------------------------------------------------------------------
//
// Weapons List
//
//---------------------------------------------------------------------------

int GetInventoryIcon(AActor *item, uint32_t flags, int *applyscale)
{
	if (applyscale != NULL)
	{
		*applyscale = false;
	}

	if (item == nullptr) return 0;

	FTextureID picnum, Icon = item->TextureIDVar(NAME_Icon), AltIcon = item->TextureIDVar(NAME_AltHUDIcon);
	FState * state = NULL, *ReadyState;

	picnum.SetNull();
	if (flags & DI_ALTICONFIRST)
	{
		if (!(flags & DI_SKIPALTICON) && AltIcon.isValid())
			picnum = AltIcon;
		else if (!(flags & DI_SKIPICON))
			picnum = Icon;
	}
	else
	{
		if (!(flags & DI_SKIPICON) && Icon.isValid())
			picnum = Icon;
		else if (!(flags & DI_SKIPALTICON))
			picnum = AltIcon;
	}

	if (!picnum.isValid()) //isNull() is bad for checking, because picnum could be also invalid (-1)
	{
		if (!(flags & DI_SKIPSPAWN) && item->SpawnState && item->SpawnState->sprite != 0)
		{
			state = item->SpawnState;

			if (applyscale != NULL && !(flags & DI_FORCESCALE))
			{
				*applyscale = true;
			}
		}
		// no spawn state - now try the ready state if it's weapon
		else if (!(flags & DI_SKIPREADY) && item->GetClass()->IsDescendantOf(NAME_Weapon) && (ReadyState = item->FindState(NAME_Ready)) && ReadyState->sprite != 0)
		{
			state = ReadyState;
		}
		if (state && (unsigned)state->sprite < (unsigned)sprites.Size())
		{
			spritedef_t * sprdef = &sprites[state->sprite];
			spriteframe_t * sprframe = &SpriteFrames[sprdef->spriteframes + state->GetFrame()];

			picnum = sprframe->Texture[0];
		}
	}
	return picnum.GetIndex();
}

