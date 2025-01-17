/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/color.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/scoreboard.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/render.h>
#include <game/generated/client_data.h>
#include <game/generated/protocol.h>

#include <game/layers.h>
#include <game/localization.h>

#include <cmath>

#include "binds.h"
#include "camera.h"
#include "controls.h"
#include "hud.h"
#include "voting.h"

const float s_DefaultHudBoxHeight = 16.0f;
const float s_DefaultFontSize = 10.0f;

const ColorRGBA s_TeamColor[2] = {{0.975f, 0.17f, 0.17f, 0.3f}, {0.17f, 0.46f, 0.975f, 0.3f}};

CHud::CHud()
{
	m_FPSTextContainerIndex.Reset();
	m_DDRaceEffectsTextContainerIndex.Reset();
	m_PlayerAngleTextContainerIndex.Reset();

	for(int i = 0; i < 2; i++)
	{
		m_aPlayerSpeedTextContainers[i].Reset();
		m_aPlayerPositionContainers[i].Reset();
	}
}

void CHud::ResetHudContainers()
{
	for(auto &ScoreInfo : m_aScoreInfo)
	{
		TextRender()->DeleteTextContainer(ScoreInfo.m_OptionalNameTextContainerIndex);
		TextRender()->DeleteTextContainer(ScoreInfo.m_TextRankContainerIndex);
		TextRender()->DeleteTextContainer(ScoreInfo.m_TextScoreContainerIndex);

		ScoreInfo.Reset();
	}

	TextRender()->DeleteTextContainer(m_FPSTextContainerIndex);
	TextRender()->DeleteTextContainer(m_DDRaceEffectsTextContainerIndex);
	TextRender()->DeleteTextContainer(m_PlayerAngleTextContainerIndex);
	for(int i = 0; i < 2; i++)
	{
		TextRender()->DeleteTextContainer(m_aPlayerSpeedTextContainers[i]);
		TextRender()->DeleteTextContainer(m_aPlayerPositionContainers[i]);
	}
}

void CHud::OnWindowResize()
{
	ResetHudContainers();
}

void CHud::OnReset()
{
	m_TimeCpDiff = 0.0f;
	m_DDRaceTime = 0;
	m_FinishTimeLastReceivedTick = 0;
	m_TimeCpLastReceivedTick = 0;
	m_ShowFinishTime = false;
	m_ServerRecord = -1.0f;
	m_aPlayerRecord[0] = -1.0f;
	m_aPlayerRecord[1] = -1.0f;
	m_aPlayerSpeed[0] = 0;
	m_aPlayerSpeed[1] = 0;
	m_aLastPlayerSpeedChange[0] = ESpeedChange::NONE;
	m_aLastPlayerSpeedChange[1] = ESpeedChange::NONE;
	m_LastSpectatorCountTick = 0;

	ResetHudContainers();
}

void CHud::OnInit()
{
	OnReset();

	Graphics()->SetColor(1.0, 1.0, 1.0, 1.0);

	m_HudQuadContainerIndex = Graphics()->CreateQuadContainer(false);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	PrepareAmmoHealthAndArmorQuads();

	// all cursors for the different weapons
	for(int i = 0; i < NUM_WEAPONS; ++i)
	{
		float ScaleX, ScaleY;
		RenderTools()->GetSpriteScale(g_pData->m_Weapons.m_aId[i].m_pSpriteCursor, ScaleX, ScaleY);
		m_aCursorOffset[i] = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 64.f * ScaleX, 64.f * ScaleY);
	}

	// the flags
	int FlagHeight = s_DefaultHudBoxHeight - 2;
	int FlagWidth = FlagHeight / 2;
	m_FlagOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, FlagWidth, FlagHeight);

	PreparePlayerStateQuads();

	Graphics()->QuadContainerUpload(m_HudQuadContainerIndex);
}

void CHud::RenderGameTimer()
{
	float Half = m_Width / 2.0f;

	if(!(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH))
	{
		char aBuf[32];
		int Time = 0;
		if(m_pClient->m_Snap.m_pGameInfoObj->m_TimeLimit && (m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0))
		{
			Time = m_pClient->m_Snap.m_pGameInfoObj->m_TimeLimit * 60 - ((Client()->GameTick(g_Config.m_ClDummy) - m_pClient->m_Snap.m_pGameInfoObj->m_RoundStartTick) / Client()->GameTickSpeed());

			if(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
				Time = 0;
		}
		else if(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
		{
			// The Warmup timer is negative in this case to make sure that incompatible clients will not see a warmup timer
			Time = (Client()->GameTick(g_Config.m_ClDummy) + m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer) / Client()->GameTickSpeed();
		}
		else
			Time = (Client()->GameTick(g_Config.m_ClDummy) - m_pClient->m_Snap.m_pGameInfoObj->m_RoundStartTick) / Client()->GameTickSpeed();

		str_time((int64_t)Time * 100, TIME_DAYS, aBuf, sizeof(aBuf));
		float FontSize = s_DefaultFontSize;
		static float s_TextWidthM = TextRender()->TextWidth(FontSize, "00:00", -1, -1.0f);
		static float s_TextWidthH = TextRender()->TextWidth(FontSize, "00:00:00", -1, -1.0f);
		static float s_TextWidth0D = TextRender()->TextWidth(FontSize, "0d 00:00:00", -1, -1.0f);
		static float s_TextWidth00D = TextRender()->TextWidth(FontSize, "00d 00:00:00", -1, -1.0f);
		static float s_TextWidth000D = TextRender()->TextWidth(FontSize, "000d 00:00:00", -1, -1.0f);
		float w = Time >= 3600 * 24 * 100 ? s_TextWidth000D : Time >= 3600 * 24 * 10 ? s_TextWidth00D :
							      Time >= 3600 * 24              ? s_TextWidth0D :
							      Time >= 3600                   ? s_TextWidthH :
											       s_TextWidthM;
		// last 60 sec red, last 10 sec blink
		if(m_pClient->m_Snap.m_pGameInfoObj->m_TimeLimit && Time <= 60 && (m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0))
		{
			float Alpha = Time <= 10 && (2 * time() / time_freq()) % 2 ? 0.5f : 1.0f;
			TextRender()->TextColor(1.0f, 0.25f, 0.25f, Alpha);
		}
		TextRender()->Text(Half - w / 2, m_Height - FontSize - (s_DefaultHudBoxHeight - FontSize) / 2, FontSize, aBuf, -1.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void CHud::RenderPauseNotification()
{
	if(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED &&
		!(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
	{
		const char *pText = Localize("Game paused");
		float FontSize = 20.0f;
		float w = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
		TextRender()->Text(150.0f * Graphics()->ScreenAspect() + -w / 2.0f, m_Height / 2 - FontSize / 2, FontSize, pText, -1.0f);
	}
}

void CHud::RenderSuddenDeath()
{
	if(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH)
	{
		float Half = m_Width / 2.0f;
		const char *pText = Localize("Sudden Death");
		float FontSize = 12.0f;
		float w = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
		TextRender()->Text(Half - w / 2, m_Height - FontSize - 2, FontSize, pText, -1.0f);
	}
}

void CHud::RenderScoreHud()
{
	// render small score hud
	if(!(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
	{
		if(m_pClient->m_GameInfo.m_TimeScore || (Client()->IsSixup() && m_pClient->m_Snap.m_pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE))
			return;

		m_aScoreInfo[0].m_Initialized = m_aScoreInfo[1].m_Initialized = true;

		if(m_pClient->IsTeamPlay() && m_pClient->m_Snap.m_pGameDataObj)
		{
			RenderTeamScoreHud();
		}
		else
		{
			RenderDefaultScoreHud();
		}
	}
}

void CHud::RenderWarmupTimer()
{
	// render warmup timer
	if(m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer > 0 && !(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME))
	{
		char aBuf[256];
		float FontSize = 20.0f;
		float w = TextRender()->TextWidth(FontSize, Localize("Warmup"), -1, -1.0f);
		TextRender()->Text(150 * Graphics()->ScreenAspect() + -w / 2, 50, FontSize, Localize("Warmup"), -1.0f);

		int Seconds = m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer / Client()->GameTickSpeed();
		if(Seconds < 5)
			str_format(aBuf, sizeof(aBuf), "%d.%d", Seconds, (m_pClient->m_Snap.m_pGameInfoObj->m_WarmupTimer * 10 / Client()->GameTickSpeed()) % 10);
		else
			str_format(aBuf, sizeof(aBuf), "%d", Seconds);
		w = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);
		TextRender()->Text(150 * Graphics()->ScreenAspect() + -w / 2, 75, FontSize, aBuf, -1.0f);
	}
}

void CHud::RenderTextInfo()
{
	int Showfps = g_Config.m_ClShowfps;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current())
		Showfps = 0;
#endif
	if(Showfps)
	{
		char aBuf[16];
		const int FramesPerSecond = round_to_int(1.0f / Client()->FrameTimeAverage());
		str_format(aBuf, sizeof(aBuf), "%d", FramesPerSecond);

		static float s_TextWidth0 = TextRender()->TextWidth(s_DefaultFontSize, "0", -1, -1.0f);
		static float s_TextWidth00 = TextRender()->TextWidth(s_DefaultFontSize, "00", -1, -1.0f);
		static float s_TextWidth000 = TextRender()->TextWidth(s_DefaultFontSize, "000", -1, -1.0f);
		static float s_TextWidth0000 = TextRender()->TextWidth(s_DefaultFontSize, "0000", -1, -1.0f);
		static float s_TextWidth00000 = TextRender()->TextWidth(s_DefaultFontSize, "00000", -1, -1.0f);
		static const float s_aTextWidth[5] = {s_TextWidth0, s_TextWidth00, s_TextWidth000, s_TextWidth0000, s_TextWidth00000};

		int DigitIndex = GetDigitsIndex(FramesPerSecond, 4);

		CTextCursor Cursor;
		TextRender()->SetCursor(&Cursor, m_Width - 10 - s_aTextWidth[DigitIndex], (s_DefaultHudBoxHeight - s_DefaultFontSize) / 2, s_DefaultFontSize, TEXTFLAG_RENDER);
		Cursor.m_LineWidth = -1;
		auto OldFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(OldFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
		if(m_FPSTextContainerIndex.Valid())
			TextRender()->RecreateTextContainerSoft(m_FPSTextContainerIndex, &Cursor, aBuf);
		else
			TextRender()->CreateTextContainer(m_FPSTextContainerIndex, &Cursor, "0");
		TextRender()->SetRenderFlags(OldFlags);
		if(m_FPSTextContainerIndex.Valid())
		{
			TextRender()->RenderTextContainer(m_FPSTextContainerIndex, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor());
		}
	}
	if(g_Config.m_ClShowpred && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%d", Client()->GetPredictionTime());
		TextRender()->Text(m_Width - 10 - TextRender()->TextWidth(12, aBuf, -1, -1.0f), Showfps ? 20 : 5, 12, aBuf, -1.0f);
	}
}

void CHud::RenderConnectionWarning()
{
	if(Client()->ConnectionProblems())
	{
		const char *pText = Localize("Connection Problems…");
		float w = TextRender()->TextWidth(24, pText, -1, -1.0f);
		TextRender()->Text(150 * Graphics()->ScreenAspect() - w / 2, 50, 24, pText, -1.0f);
	}
}

void CHud::RenderTeambalanceWarning()
{
	// render prompt about team-balance
	bool Flash = time() / (time_freq() / 2) % 2 == 0;
	if(m_pClient->IsTeamPlay())
	{
		int TeamDiff = m_pClient->m_Snap.m_aTeamSize[TEAM_RED] - m_pClient->m_Snap.m_aTeamSize[TEAM_BLUE];
		if(g_Config.m_ClWarningTeambalance && (TeamDiff >= 2 || TeamDiff <= -2))
		{
			const char *pText = Localize("Please balance teams!");
			if(Flash)
				TextRender()->TextColor(1, 1, 0.5f, 1);
			else
				TextRender()->TextColor(0.7f, 0.7f, 0.2f, 1.0f);
			TextRender()->Text(5, 50, 6, pText, -1.0f);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
}

void CHud::RenderCursor()
{
	int CurWeapon = 0;
	vec2 TargetPos;
	float Alpha = 1.0f;

	const vec2 Center = m_pClient->m_Camera.m_Center;
	float aPoints[4];
	RenderTools()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), 1.0f, aPoints);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && m_pClient->m_Snap.m_pLocalCharacter)
	{
		// Render local cursor
		CurWeapon = maximum(0, m_pClient->m_Snap.m_pLocalCharacter->m_Weapon % NUM_WEAPONS);
		TargetPos = m_pClient->m_Controls.m_aTargetPos[g_Config.m_ClDummy];
	}
	else
	{
		// Render spec cursor
		if(!g_Config.m_ClSpecCursor || !m_pClient->m_CursorInfo.IsAvailable())
			return;

		bool RenderSpecCursor = (m_pClient->m_Snap.m_SpecInfo.m_Active && m_pClient->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW) || Client()->State() == IClient::STATE_DEMOPLAYBACK;

		if(!RenderSpecCursor)
			return;

		// Calculate factor to keep cursor on screen
		const vec2 HalfSize = vec2(Center.x - aPoints[0], Center.y - aPoints[1]);
		const vec2 ScreenPos = (m_pClient->m_CursorInfo.WorldTarget() - Center) / m_pClient->m_Camera.m_Zoom;
		const float ClampFactor = maximum(
			1.0f,
			absolute(ScreenPos.x / HalfSize.x),
			absolute(ScreenPos.y / HalfSize.y));

		CurWeapon = maximum(0, m_pClient->m_CursorInfo.Weapon() % NUM_WEAPONS);
		TargetPos = ScreenPos / ClampFactor + Center;
		if(ClampFactor != 1.0f)
			Alpha /= 2.0f;
	}

	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->TextureSet(m_pClient->m_GameSkin.m_aSpriteWeaponCursors[CurWeapon]);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aCursorOffset[CurWeapon], TargetPos.x, TargetPos.y);
}

void CHud::PrepareAmmoHealthAndArmorQuads()
{
	float x = 48;
	float y = 0;
	IGraphics::CQuadItem Array[10];

	// ammo of the different weapons
	for(int i = 0; i < NUM_WEAPONS; ++i)
	{
		// 0.6
		for(int n = 0; n < 10; n++)
			if(n < 5)
				Array[n] = IGraphics::CQuadItem(-x - n * 12, y, 10, 10);
			else
				Array[n] = IGraphics::CQuadItem(x + (n - 5) * 12, y, 10, 10);

		m_aAmmoOffset[i] = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

		// 0.7
		if(i == WEAPON_GRENADE)
		{
			// special case for 0.7 grenade
			for(int n = 0; n < 10; n++)
				Array[n] = IGraphics::CQuadItem(1 + x + n * 12, y, 10, 10);
		}
		else
		{
			for(int n = 0; n < 10; n++)
				Array[n] = IGraphics::CQuadItem(x + n * 12, y, 12, 12);
		}

		Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);
	}

	// health
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(-x - i * 12, y, 10, 10);
	m_HealthOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(-x - i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// empty health
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(-x - i * 12, y, 10, 10);
	m_EmptyHealthOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(-x - i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// armor meter
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 10, 10);
	m_ArmorOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// empty armor meter
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 10, 10);
	m_EmptyArmorOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);
}

void CHud::RenderAmmoHealthAndArmor(const CNetObj_Character *pCharacter)
{
	if(!pCharacter)
		return;

	bool IsSixupGameSkin = m_pClient->m_GameSkin.IsSixup();
	int QuadOffsetSixup = (IsSixupGameSkin ? 10 : 0);

	if(GameClient()->m_GameInfo.m_HudAmmo)
	{
		// ammo display
		int CurWeapon = pCharacter->m_Weapon % NUM_WEAPONS;
		// 0.7 only
		if(CurWeapon == WEAPON_NINJA)
		{
			if(!GameClient()->m_GameInfo.m_HudDDRace && Client()->IsSixup())
			{
				const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000;
				float NinjaProgress = clamp(pCharacter->m_AmmoCount - Client()->GameTick(g_Config.m_ClDummy), 0, Max) / (float)Max;
				RenderNinjaBarPos(5 + 10 * 12, 5, 6.f, 24.f, NinjaProgress);
			}
		}
		else if(CurWeapon >= 0 && m_pClient->m_GameSkin.m_aSpriteWeaponProjectiles[CurWeapon].IsValid())
		{
			Graphics()->TextureSet(m_pClient->m_GameSkin.m_aSpriteWeaponProjectiles[CurWeapon]);
			Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_aAmmoOffset[CurWeapon] + QuadOffsetSixup, minimum(pCharacter->m_AmmoCount, 10), m_Width / 2 - 5, m_Height - 24);
		}
	}

	if(GameClient()->m_GameInfo.m_HudHealthArmor)
	{
		// health display
		Graphics()->TextureSet(m_pClient->m_GameSkin.m_SpriteHealthFull);
		Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_HealthOffset, minimum(pCharacter->m_Health, 10), m_Width / 2 - 6, m_Height - 12); // - 60
		Graphics()->TextureSet(m_pClient->m_GameSkin.m_SpriteHealthEmpty);
		Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_EmptyHealthOffset + minimum(pCharacter->m_Health, 10), 10 - minimum(pCharacter->m_Health, 10), m_Width / 2 - 5, m_Height - 12);

		// armor display
		Graphics()->TextureSet(m_pClient->m_GameSkin.m_SpriteArmorFull);
		Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_ArmorOffset, minimum(pCharacter->m_Armor, 10), m_Width / 2 - 5, m_Height - 12); // 48
		Graphics()->TextureSet(m_pClient->m_GameSkin.m_SpriteArmorEmpty);
		Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_ArmorOffset + minimum(pCharacter->m_Armor, 10), 10 - minimum(pCharacter->m_Armor, 10), m_Width / 2 - 5, m_Height - 12);
	}
}

void CHud::PreparePlayerStateQuads()
{
	float x = 5;
	float y = 5 + 24;
	IGraphics::CQuadItem Array[10];

	// Quads for displaying the available and used jumps
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	m_AirjumpOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	m_AirjumpEmptyOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// Quads for displaying weapons
	for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
	{
		const CDataWeaponspec &WeaponSpec = g_pData->m_Weapons.m_aId[Weapon];
		float ScaleX, ScaleY;
		RenderTools()->GetSpriteScale(WeaponSpec.m_pSpriteBody, ScaleX, ScaleY);
		constexpr float HudWeaponScale = 0.25f;
		float Width = WeaponSpec.m_VisualSize * ScaleX * HudWeaponScale;
		float Height = WeaponSpec.m_VisualSize * ScaleY * HudWeaponScale;
		m_aWeaponOffset[Weapon] = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, Width, Height);

		Graphics()->QuadsSetSubsetFree(1, 0, 0, 0, 0, 1, 1, 1);
		m_aMirroredWeaponOffset[Weapon] = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, Width, Height);
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
	}

	// Quads for displaying capabilities
	m_EndlessJumpOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_EndlessHookOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_JetpackOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportGrenadeOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportGunOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportLaserOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying prohibited capabilities
	m_SoloOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_CollisionDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_HookHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_HammerHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_GunHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_ShotgunHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_GrenadeHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LaserHitDisabledOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying freeze status
	m_DeepFrozenOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LiveFrozenOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying dummy actions
	m_DummyHammerOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_DummyCopyOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying team modes
	m_PracticeModeOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LockModeOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_Team0ModeOffset = RenderTools()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
}

void CHud::RenderPlayerState(const int ClientId)
{
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);

	// pCharacter contains the predicted character for local players or the last snap for players who are spectated
	CCharacterCore *pCharacter = &m_pClient->m_aClients[ClientId].m_Predicted;
	CNetObj_Character *pPlayer = &m_pClient->m_aClients[ClientId].m_RenderCur;
	int TotalJumpsToDisplay = 0;
	if(g_Config.m_ClShowhudJumpsIndicator)
	{
		int AvailableJumpsToDisplay;
		if(m_pClient->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo)
		{
			bool Grounded = false;
			if(Collision()->CheckPoint(pPlayer->m_X + CCharacterCore::PhysicalSize() / 2,
				   pPlayer->m_Y + CCharacterCore::PhysicalSize() / 2 + 5))
			{
				Grounded = true;
			}
			if(Collision()->CheckPoint(pPlayer->m_X - CCharacterCore::PhysicalSize() / 2,
				   pPlayer->m_Y + CCharacterCore::PhysicalSize() / 2 + 5))
			{
				Grounded = true;
			}

			int UsedJumps = pCharacter->m_JumpedTotal;
			if(pCharacter->m_Jumps > 1)
			{
				UsedJumps += !Grounded;
			}
			else if(pCharacter->m_Jumps == 1)
			{
				// If the player has only one jump, each jump is the last one
				UsedJumps = pPlayer->m_Jumped & 2;
			}
			else if(pCharacter->m_Jumps == -1)
			{
				// The player has only one ground jump
				UsedJumps = !Grounded;
			}

			if(pCharacter->m_EndlessJump && UsedJumps >= absolute(pCharacter->m_Jumps))
			{
				UsedJumps = absolute(pCharacter->m_Jumps) - 1;
			}

			int UnusedJumps = absolute(pCharacter->m_Jumps) - UsedJumps;
			if(!(pPlayer->m_Jumped & 2) && UnusedJumps <= 0)
			{
				// In some edge cases when the player just got another number of jumps, UnusedJumps is not correct
				UnusedJumps = 1;
			}
			TotalJumpsToDisplay = maximum(minimum(absolute(pCharacter->m_Jumps), 10), 0);
			AvailableJumpsToDisplay = maximum(minimum(UnusedJumps, TotalJumpsToDisplay), 0);
		}
		else
		{
			TotalJumpsToDisplay = AvailableJumpsToDisplay = absolute(m_pClient->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Jumps);
		}

		// render available and used jumps
		int JumpsOffsetY = ((GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24 : 0) +
				    (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12 : 0));
		if(JumpsOffsetY > 0)
		{
			Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudAirjump);
			Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_AirjumpOffset, AvailableJumpsToDisplay, 0, JumpsOffsetY);
			Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudAirjumpEmpty);
			Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_AirjumpEmptyOffset + AvailableJumpsToDisplay, TotalJumpsToDisplay - AvailableJumpsToDisplay, 0, JumpsOffsetY);
		}
		else
		{
			Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudAirjump);
			Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_AirjumpOffset, AvailableJumpsToDisplay);
			Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudAirjumpEmpty);
			Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_AirjumpEmptyOffset + AvailableJumpsToDisplay, TotalJumpsToDisplay - AvailableJumpsToDisplay);
		}
	}

	float x = 5 + 12;
	float y = (5 + 12 + (GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24 : 0) +
		   (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12 : 0));

	// render weapons
	{
		constexpr float aWeaponWidth[NUM_WEAPONS] = {16, 12, 12, 12, 12, 12};
		constexpr float aWeaponInitialOffset[NUM_WEAPONS] = {-3, -4, -1, -1, -2, -4};
		bool InitialOffsetAdded = false;
		for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
		{
			if(!pCharacter->m_aWeapons[Weapon].m_Got)
				continue;
			if(!InitialOffsetAdded)
			{
				x += aWeaponInitialOffset[Weapon];
				InitialOffsetAdded = true;
			}
			if(pPlayer->m_Weapon != Weapon)
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
			Graphics()->QuadsSetRotation(pi * 7 / 4);
			Graphics()->TextureSet(m_pClient->m_GameSkin.m_aSpritePickupWeapons[Weapon]);
			Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aWeaponOffset[Weapon], x, y);
			Graphics()->QuadsSetRotation(0);
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			x += aWeaponWidth[Weapon];
		}
		if(pCharacter->m_aWeapons[WEAPON_NINJA].m_Got)
		{
			const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000;
			float NinjaProgress = clamp(pCharacter->m_Ninja.m_ActivationTick + g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000 - Client()->GameTick(g_Config.m_ClDummy), 0, Max) / (float)Max;
			if(NinjaProgress > 0.0f && m_pClient->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo)
			{
				RenderNinjaBarPos(x, y - 12, 6.f, 24.f, NinjaProgress);
			}
		}
	}

	// render capabilities
	x = 5;
	y += 12;
	if(TotalJumpsToDisplay > 0)
	{
		y += 12;
	}
	bool HasCapabilities = false;
	if(pCharacter->m_EndlessJump)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudEndlessJump);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_EndlessJumpOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_EndlessHook)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudEndlessHook);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_EndlessHookOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_Jetpack)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudJetpack);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_JetpackOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudTeleportGun);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportGunOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunGrenade && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudTeleportGrenade);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportGrenadeOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunLaser && pCharacter->m_aWeapons[WEAPON_LASER].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudTeleportLaser);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportLaserOffset, x, y);
	}

	// render prohibited capabilities
	x = 5;
	if(HasCapabilities)
	{
		y += 12;
	}
	bool HasProhibitedCapabilities = false;
	if(pCharacter->m_Solo)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudSolo);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_SoloOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_CollisionDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudCollisionDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_CollisionDisabledOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HookHitDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudHookHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_HookHitDisabledOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HammerHitDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudHammerHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_HammerHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_GrenadeHitDisabled && pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudGunHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LaserHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_ShotgunHitDisabled && pCharacter->m_aWeapons[WEAPON_SHOTGUN].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudShotgunHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_ShotgunHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_GrenadeHitDisabled && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudGrenadeHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_GrenadeHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_LaserHitDisabled && pCharacter->m_aWeapons[WEAPON_LASER].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudLaserHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LaserHitDisabledOffset, x, y);
	}

	// render dummy actions and freeze state
	x = 5;
	if(HasProhibitedCapabilities)
	{
		y += 12;
	}
	if(m_pClient->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && m_pClient->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_LOCK_MODE)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudLockMode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LockModeOffset, x, y);
		x += 12;
	}
	if(m_pClient->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && m_pClient->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudPracticeMode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_PracticeModeOffset, x, y);
		x += 12;
	}
	if(m_pClient->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && m_pClient->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_TEAM0_MODE)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudTeam0Mode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_Team0ModeOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_DeepFrozen)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudDeepFrozen);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DeepFrozenOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_LiveFrozen)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudLiveFrozen);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LiveFrozenOffset, x, y);
	}
}

void CHud::RenderNinjaBarPos(const float x, float y, const float Width, const float Height, float Progress, const float Alpha)
{
	Progress = clamp(Progress, 0.0f, 1.0f);

	// what percentage of the end pieces is used for the progress indicator and how much is the rest
	// half of the ends are used for the progress display
	const float RestPct = 0.5f;
	const float ProgPct = 0.5f;

	const float EndHeight = Width; // to keep the correct scale - the width of the sprite is as long as the height
	const float BarWidth = Width;
	const float WholeBarHeight = Height;
	const float MiddleBarHeight = WholeBarHeight - (EndHeight * 2.0f);
	const float EndProgressHeight = EndHeight * ProgPct;
	const float EndRestHeight = EndHeight * RestPct;
	const float ProgressBarHeight = WholeBarHeight - (EndProgressHeight * 2.0f);
	const float EndProgressProportion = EndProgressHeight / ProgressBarHeight;
	const float MiddleProgressProportion = MiddleBarHeight / ProgressBarHeight;

	// beginning piece
	float BeginningPieceProgress = 1;
	if(Progress <= 1)
	{
		if(Progress <= (EndProgressProportion + MiddleProgressProportion))
		{
			BeginningPieceProgress = 0;
		}
		else
		{
			BeginningPieceProgress = (Progress - EndProgressProportion - MiddleProgressProportion) / EndProgressProportion;
		}
	}
	// empty
	Graphics()->WrapClamp();
	Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarEmptyRight);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// Subset: btm_r, top_r, top_m, btm_m | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
	Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, ProgPct - ProgPct * (1.0f - BeginningPieceProgress), 0, ProgPct - ProgPct * (1.0f - BeginningPieceProgress), 1);
	IGraphics::CQuadItem QuadEmptyBeginning(x, y, BarWidth, EndRestHeight + EndProgressHeight * (1.0f - BeginningPieceProgress));
	Graphics()->QuadsDrawTL(&QuadEmptyBeginning, 1);
	Graphics()->QuadsEnd();
	// full
	if(BeginningPieceProgress > 0.0f)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarFullLeft);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// Subset: btm_m, top_m, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(RestPct + ProgPct * (1.0f - BeginningPieceProgress), 1, RestPct + ProgPct * (1.0f - BeginningPieceProgress), 0, 1, 0, 1, 1);
		IGraphics::CQuadItem QuadFullBeginning(x, y + (EndRestHeight + EndProgressHeight * (1.0f - BeginningPieceProgress)), BarWidth, EndProgressHeight * BeginningPieceProgress);
		Graphics()->QuadsDrawTL(&QuadFullBeginning, 1);
		Graphics()->QuadsEnd();
	}

	// middle piece
	y += EndHeight;

	float MiddlePieceProgress = 1;
	if(Progress <= EndProgressProportion + MiddleProgressProportion)
	{
		if(Progress <= EndProgressProportion)
		{
			MiddlePieceProgress = 0;
		}
		else
		{
			MiddlePieceProgress = (Progress - EndProgressProportion) / MiddleProgressProportion;
		}
	}

	const float FullMiddleBarHeight = MiddleBarHeight * MiddlePieceProgress;
	const float EmptyMiddleBarHeight = MiddleBarHeight - FullMiddleBarHeight;

	// empty ninja bar
	if(EmptyMiddleBarHeight > 0.0f)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarEmpty);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// select the middle portion of the sprite so we don't get edge bleeding
		if(EmptyMiddleBarHeight <= EndHeight)
		{
			// prevent pixel puree, select only a small slice
			// Subset: btm_r, top_r, top_m, btm_m | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
			Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, 1.0f - (EmptyMiddleBarHeight / EndHeight), 0, 1.0f - (EmptyMiddleBarHeight / EndHeight), 1);
		}
		else
		{
			// Subset: btm_r, top_r, top_l, btm_l | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
			Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, 0, 0, 0, 1);
		}
		IGraphics::CQuadItem QuadEmpty(x, y, BarWidth, EmptyMiddleBarHeight);
		Graphics()->QuadsDrawTL(&QuadEmpty, 1);
		Graphics()->QuadsEnd();
	}

	// full ninja bar
	Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarFull);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// select the middle portion of the sprite so we don't get edge bleeding
	if(FullMiddleBarHeight <= EndHeight)
	{
		// prevent pixel puree, select only a small slice
		// Subset: btm_m, top_m, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(1.0f - (FullMiddleBarHeight / EndHeight), 1, 1.0f - (FullMiddleBarHeight / EndHeight), 0, 1, 0, 1, 1);
	}
	else
	{
		// Subset: btm_l, top_l, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(0, 1, 0, 0, 1, 0, 1, 1);
	}
	IGraphics::CQuadItem QuadFull(x, y + EmptyMiddleBarHeight, BarWidth, FullMiddleBarHeight);
	Graphics()->QuadsDrawTL(&QuadFull, 1);
	Graphics()->QuadsEnd();

	// ending piece
	y += MiddleBarHeight;
	float EndingPieceProgress = 1;
	if(Progress <= EndProgressProportion)
	{
		EndingPieceProgress = Progress / EndProgressProportion;
	}
	// empty
	if(EndingPieceProgress < 1.0f)
	{
		Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarEmptyRight);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// Subset: btm_l, top_l, top_m, btm_m | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(0, 1, 0, 0, ProgPct - ProgPct * EndingPieceProgress, 0, ProgPct - ProgPct * EndingPieceProgress, 1);
		IGraphics::CQuadItem QuadEmptyEnding(x, y, BarWidth, EndProgressHeight * (1.0f - EndingPieceProgress));
		Graphics()->QuadsDrawTL(&QuadEmptyEnding, 1);
		Graphics()->QuadsEnd();
	}
	// full
	Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudNinjaBarFullLeft);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// Subset: btm_m, top_m, top_l, btm_l | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
	Graphics()->QuadsSetSubsetFree(RestPct + ProgPct * EndingPieceProgress, 1, RestPct + ProgPct * EndingPieceProgress, 0, 0, 0, 0, 1);
	IGraphics::CQuadItem QuadFullEnding(x, y + (EndProgressHeight * (1.0f - EndingPieceProgress)), BarWidth, EndRestHeight + EndProgressHeight * EndingPieceProgress);
	Graphics()->QuadsDrawTL(&QuadFullEnding, 1);
	Graphics()->QuadsEnd();

	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
	Graphics()->WrapNormal();
}

void CHud::RenderSpectatorCount()
{
	if(!g_Config.m_ClShowhudSpectatorCount)
	{
		return;
	}

	int Count = 0;
	if(Client()->IsSixup())
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(i == m_pClient->m_aLocalIds[0] || (m_pClient->Client()->DummyConnected() && i == m_pClient->m_aLocalIds[1]))
				continue;

			if(Client()->m_TranslationContext.m_aClients[i].m_PlayerFlags7 & protocol7::PLAYERFLAG_WATCHING)
			{
				Count++;
			}
		}
	}
	else
	{
		Count = m_pClient->m_Snap.m_SpecInfo.m_SpectatorCount;
	}

	if(Count == 0)
	{
		m_LastSpectatorCountTick = Client()->GameTick(g_Config.m_ClDummy);
		return;
	}

	// 1 second delay
	if(Client()->GameTick(g_Config.m_ClDummy) < m_LastSpectatorCountTick + Client()->GameTickSpeed())
		return;

	char aBuf[16];
	str_format(aBuf, sizeof(aBuf), "%d", Count);

	const float Fontsize = 6.0f;
	const float BoxHeight = 14.f;
	const float BoxWidth = 13.f + TextRender()->TextWidth(Fontsize, aBuf);

	float StartX = m_Width - BoxWidth;
	float StartY = 285.0f - BoxHeight - 4; // 4 units distance to the next display;
	if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
	{
		StartY -= 4;
	}
	StartY -= GetMovementInformationBoxHeight();

	if(g_Config.m_ClShowhudScore)
	{
		StartY -= 56;
	}

	if(g_Config.m_ClShowhudDummyActions && !(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && Client()->DummyConnected())
	{
		StartY = StartY - 29.0f - 4; // dummy actions height and padding
	}

	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_L, 5.0f);

	float y = StartY + BoxHeight / 3;
	float x = StartX + 2;

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->Text(x, y, Fontsize, FontIcons::FONT_ICON_EYE, -1.0f);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->Text(x + Fontsize + 3.f, y, Fontsize, aBuf, -1.0f);
}

void CHud::RenderDummyActions()
{
	if(!g_Config.m_ClShowhudDummyActions || (m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) || !Client()->DummyConnected())
	{
		return;
	}
	// render small dummy actions hud
	const float BoxHeight = 29.0f;
	const float BoxWidth = 16.0f;

	float StartX = m_Width - BoxWidth;
	float StartY = 285.0f - BoxHeight - 4; // 4 units distance to the next display;
	if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
	{
		StartY -= 4;
	}
	StartY -= GetMovementInformationBoxHeight();

	if(g_Config.m_ClShowhudScore)
	{
		StartY -= 56;
	}

	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_L, 5.0f);

	float y = StartY + 2;
	float x = StartX + 2;
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
	if(g_Config.m_ClDummyHammer)
	{
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudDummyHammer);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DummyHammerOffset, x, y);
	y += 13;
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
	if(g_Config.m_ClDummyCopyMoves)
	{
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	Graphics()->TextureSet(m_pClient->m_HudSkin.m_SpriteHudDummyCopy);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DummyCopyOffset, x, y);
}

inline int CHud::GetDigitsIndex(int Value, int Max)
{
	if(Value < 0)
	{
		Value *= -1;
	}
	int DigitsIndex = std::log10((Value ? Value : 1));
	if(DigitsIndex > Max)
	{
		DigitsIndex = Max;
	}
	if(DigitsIndex < 0)
	{
		DigitsIndex = 0;
	}
	return DigitsIndex;
}

inline float CHud::GetMovementInformationBoxHeight()
{
	if(m_pClient->m_Snap.m_SpecInfo.m_Active && (m_pClient->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW || m_pClient->m_aClients[m_pClient->m_Snap.m_SpecInfo.m_SpectatorId].m_SpecCharPresent))
		return g_Config.m_ClShowhudPlayerPosition ? 3 * MOVEMENT_INFORMATION_LINE_HEIGHT + 2 : 0;
	float BoxHeight = 3 * MOVEMENT_INFORMATION_LINE_HEIGHT * (g_Config.m_ClShowhudPlayerPosition + g_Config.m_ClShowhudPlayerSpeed) + 2 * MOVEMENT_INFORMATION_LINE_HEIGHT * g_Config.m_ClShowhudPlayerAngle;
	if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
	{
		BoxHeight += 2;
	}
	return BoxHeight;
}

void CHud::UpdateMovementInformationTextContainer(STextContainerIndex &TextContainer, float FontSize, float Value, char *pPrevValue, size_t Size)
{
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%.2f", Value);

	if(!TextContainer.Valid() || str_comp(pPrevValue, aBuf) != 0)
	{
		CTextCursor Cursor;
		TextRender()->SetCursor(&Cursor, 0, 0, FontSize, TEXTFLAG_RENDER);
		TextRender()->RecreateTextContainer(TextContainer, &Cursor, aBuf);
		str_copy(pPrevValue, aBuf, Size);
	}
}

void CHud::RenderMovementInformationTextContainer(STextContainerIndex &TextContainer, const ColorRGBA &Color, float X, float Y)
{
	if(TextContainer.Valid())
	{
		TextRender()->RenderTextContainer(TextContainer, Color, TextRender()->DefaultTextOutlineColor(), X - TextRender()->GetBoundingBoxTextContainer(TextContainer).m_W, Y);
	}
}

void CHud::RenderMovementInformation()
{
	const int ClientId = m_pClient->m_Snap.m_SpecInfo.m_Active ? m_pClient->m_Snap.m_SpecInfo.m_SpectatorId : m_pClient->m_Snap.m_LocalClientId;
	const bool PosOnly = ClientId == SPEC_FREEVIEW || (m_pClient->m_aClients[ClientId].m_SpecCharPresent);
	// Draw the infomations depending on settings: Position, speed and target angle
	// This display is only to present the available information from the last snapshot, not to interpolate or predict
	if(!g_Config.m_ClShowhudPlayerPosition && (PosOnly || (!g_Config.m_ClShowhudPlayerSpeed && !g_Config.m_ClShowhudPlayerAngle)))
	{
		return;
	}
	const float LineSpacer = 1.0f; // above and below each entry
	const float Fontsize = 6.0f;

	float BoxHeight = GetMovementInformationBoxHeight();
	const float BoxWidth = 62.0f;

	float StartX = m_Width - BoxWidth;
	float StartY = 285.0f - BoxHeight - 4; // 4 units distance to the next display;
	if(g_Config.m_ClShowhudScore)
	{
		StartY -= 56;
	}

	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_L, 5.0f);

	vec2 Pos;
	float DisplaySpeedX{}, DisplaySpeedY{}, DisplayAngle{};

	if(ClientId == SPEC_FREEVIEW)
	{
		Pos = m_pClient->m_Camera.m_Center / 32.f;
	}
	else if(m_pClient->m_aClients[ClientId].m_SpecCharPresent)
	{
		Pos = m_pClient->m_aClients[ClientId].m_SpecChar / 32.f;
	}
	else
	{
		const CNetObj_Character *pPrevChar = &m_pClient->m_Snap.m_aCharacters[ClientId].m_Prev;
		const CNetObj_Character *pCurChar = &m_pClient->m_Snap.m_aCharacters[ClientId].m_Cur;
		const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);

		// To make the player position relative to blocks we need to divide by the block size
		Pos = mix(vec2(pPrevChar->m_X, pPrevChar->m_Y), vec2(pCurChar->m_X, pCurChar->m_Y), IntraTick) / 32.0f;

		const vec2 Vel = mix(vec2(pPrevChar->m_VelX, pPrevChar->m_VelY), vec2(pCurChar->m_VelX, pCurChar->m_VelY), IntraTick);

		float VelspeedX = Vel.x / 256.0f * Client()->GameTickSpeed();
		if(Vel.x >= -1 && Vel.x <= 1)
		{
			VelspeedX = 0;
		}
		float VelspeedY = Vel.y / 256.0f * Client()->GameTickSpeed();
		if(Vel.y >= -128 && Vel.y <= 128)
		{
			VelspeedY = 0;
		}
		// We show the speed in Blocks per Second (Bps) and therefore have to divide by the block size
		DisplaySpeedX = VelspeedX / 32;
		float VelspeedLength = length(vec2(Vel.x, Vel.y) / 256.0f) * Client()->GameTickSpeed();
		// Todo: Use Velramp tuning of each individual player
		// Since these tuning parameters are almost never changed, the default values are sufficient in most cases
		float Ramp = VelocityRamp(VelspeedLength, m_pClient->m_aTuning[g_Config.m_ClDummy].m_VelrampStart, m_pClient->m_aTuning[g_Config.m_ClDummy].m_VelrampRange, m_pClient->m_aTuning[g_Config.m_ClDummy].m_VelrampCurvature);
		DisplaySpeedX *= Ramp;
		DisplaySpeedY = VelspeedY / 32;

		float Angle = m_pClient->m_Players.GetPlayerTargetAngle(pPrevChar, pCurChar, ClientId, IntraTick);
		if(Angle < 0)
		{
			Angle += 2.0f * pi;
		}
		DisplayAngle = Angle * 180.0f / pi;
	}

	float y = StartY + LineSpacer * 2;
	float xl = StartX + 2;
	float xr = m_Width - 2;

	if(g_Config.m_ClShowhudPlayerPosition)
	{
		TextRender()->Text(xl, y, Fontsize, Localize("Position:"), -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		TextRender()->Text(xl, y, Fontsize, "X:", -1.0f);
		UpdateMovementInformationTextContainer(m_aPlayerPositionContainers[0], Fontsize, Pos.x, m_aaPlayerPositionText[0], sizeof(m_aaPlayerPositionText[0]));
		RenderMovementInformationTextContainer(m_aPlayerPositionContainers[0], TextRender()->DefaultTextColor(), xr, y);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		TextRender()->Text(xl, y, Fontsize, "Y:", -1.0f);
		UpdateMovementInformationTextContainer(m_aPlayerPositionContainers[1], Fontsize, Pos.y, m_aaPlayerPositionText[1], sizeof(m_aaPlayerPositionText[1]));
		RenderMovementInformationTextContainer(m_aPlayerPositionContainers[1], TextRender()->DefaultTextColor(), xr, y);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;
	}

	if(PosOnly)
		return;

	if(g_Config.m_ClShowhudPlayerSpeed)
	{
		TextRender()->Text(xl, y, Fontsize, Localize("Speed:"), -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		const char aaCoordinates[][4] = {"X:", "Y:"};
		for(int i = 0; i < 2; i++)
		{
			ColorRGBA Color(1, 1, 1, 1);
			if(m_aLastPlayerSpeedChange[i] == ESpeedChange::INCREASE)
				Color = ColorRGBA(0, 1, 0, 1);
			if(m_aLastPlayerSpeedChange[i] == ESpeedChange::DECREASE)
				Color = ColorRGBA(1, 0.5f, 0.5f, 1);
			TextRender()->Text(xl, y, Fontsize, aaCoordinates[i], -1.0f);
			UpdateMovementInformationTextContainer(m_aPlayerSpeedTextContainers[i], Fontsize, i == 0 ? DisplaySpeedX : DisplaySpeedY, m_aaPlayerSpeedText[i], sizeof(m_aaPlayerSpeedText[i]));
			RenderMovementInformationTextContainer(m_aPlayerSpeedTextContainers[i], Color, xr, y);
			y += MOVEMENT_INFORMATION_LINE_HEIGHT;
		}

		TextRender()->TextColor(1, 1, 1, 1);
	}

	if(g_Config.m_ClShowhudPlayerAngle)
	{
		TextRender()->Text(xl, y, Fontsize, Localize("Angle:"), -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		UpdateMovementInformationTextContainer(m_PlayerAngleTextContainerIndex, Fontsize, DisplayAngle, m_aPlayerAngleText, sizeof(m_aPlayerAngleText));
		RenderMovementInformationTextContainer(m_PlayerAngleTextContainerIndex, TextRender()->DefaultTextColor(), xr, y);
	}
}

void CHud::RenderSpectatorHud()
{
	// draw the box
	Graphics()->DrawRect(0, 0, m_Width, 16.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_NONE, 5.0f);
	Graphics()->DrawRect(0, m_Height - 16.0f, m_Width, 16.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_NONE, 5.0f);

	// draw the text
	char aBuf[128];
	if(GameClient()->m_MultiViewActivated)
	{
		str_copy(aBuf, Localize("Multi-View"));
	}
	else if(m_pClient->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Following %s", "Spectating"), m_pClient->m_aClients[m_pClient->m_Snap.m_SpecInfo.m_SpectatorId].m_aName);
	}
	else
	{
		str_copy(aBuf, Localize("Free-View"));
	}
	TextRender()->Text(8.0f, (15.0f - 8.0f) / 2.0f, 8.0f, aBuf, -1.0f);

	// draw the camera info
	if(m_pClient->m_Camera.SpectatingPlayer() && m_pClient->m_Camera.CanUseAutoSpecCamera() && g_Config.m_ClSpecAutoSync)
	{
		bool AutoSpecCameraEnabled = m_pClient->m_Camera.m_AutoSpecCamera;
		const char *pLabelText = Localize("AUTO", "Spectating Camera Mode Icon");
		const float TextWidth = TextRender()->TextWidth(6.0f, pLabelText);

		constexpr float RightMargin = 4.0f;
		constexpr float IconWidth = 6.0f;
		constexpr float Padding = 3.0f;
		const float TagWidth = IconWidth + TextWidth + Padding * 3.0f;
		const float TagX = RightMargin;
		Graphics()->DrawRect(TagX, 12.0f, TagWidth, 10.0f, ColorRGBA(0.84f, 0.53f, 0.17f, AutoSpecCameraEnabled ? 0.85f : 0.25f), IGraphics::CORNER_ALL, 2.5f);
		TextRender()->TextColor(1, 1, 1, AutoSpecCameraEnabled ? 1.0f : 0.65f);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->Text(TagX + Padding, 10.0f, 6.0f, FontIcons::FONT_ICON_CAMERA, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->Text(TagX + Padding + IconWidth + Padding, 10.0f, 6.0f, pLabelText, -1.0f);
		TextRender()->TextColor(1, 1, 1, 1);
	}
}

void CHud::RenderLocalTime(float x)
{
	if(!g_Config.m_ClShowLocalTimeAlways && !m_pClient->m_Scoreboard.IsActive())
		return;

	// draw the box
	Graphics()->DrawRect(x - 30.0f, 0.0f, 25.0f, 12.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_B, 3.75f);

	// draw the text
	char aTimeStr[6];
	str_timestamp_format(aTimeStr, sizeof(aTimeStr), "%H:%M");
	TextRender()->Text(x - 25.0f, (12.5f - 5.f) / 2.f, 5.0f, aTimeStr, -1.0f);
}

void CHud::OnNewSnapshot()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!m_pClient->m_Snap.m_pGameInfoObj)
		return;

	int ClientId = -1;
	if(m_pClient->m_Snap.m_pLocalCharacter && !m_pClient->m_Snap.m_SpecInfo.m_Active && !(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		ClientId = m_pClient->m_Snap.m_LocalClientId;
	else if(m_pClient->m_Snap.m_SpecInfo.m_Active)
		ClientId = m_pClient->m_Snap.m_SpecInfo.m_SpectatorId;

	if(ClientId == -1)
		return;

	const CNetObj_Character *pPrevChar = &m_pClient->m_Snap.m_aCharacters[ClientId].m_Prev;
	const CNetObj_Character *pCurChar = &m_pClient->m_Snap.m_aCharacters[ClientId].m_Cur;
	const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);
	ivec2 Vel = mix(ivec2(pPrevChar->m_VelX, pPrevChar->m_VelY), ivec2(pCurChar->m_VelX, pCurChar->m_VelY), IntraTick);

	CCharacter *pChar = m_pClient->m_PredictedWorld.GetCharacterById(ClientId);
	if(pChar && pChar->IsGrounded())
		Vel.y = 0;

	int aVels[2] = {Vel.x, Vel.y};

	for(int i = 0; i < 2; i++)
	{
		int AbsVel = abs(aVels[i]);
		if(AbsVel > m_aPlayerSpeed[i])
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::INCREASE;
		}
		if(AbsVel < m_aPlayerSpeed[i])
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::DECREASE;
		}
		if(AbsVel < 2)
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::NONE;
		}
		m_aPlayerSpeed[i] = AbsVel;
	}
}

void CHud::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!m_pClient->m_Snap.m_pGameInfoObj)
		return;

	m_Width = 300.0f * Graphics()->ScreenAspect();
	m_Height = 300.0f;
	Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);

#if defined(CONF_VIDEORECORDER)
	if((IVideo::Current() && g_Config.m_ClVideoShowhud) || (!IVideo::Current() && g_Config.m_ClShowhud))
#else
	if(g_Config.m_ClShowhud)
#endif
	{
		if(m_pClient->m_Snap.m_pLocalCharacter && !m_pClient->m_Snap.m_SpecInfo.m_Active && !(m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		{
			if(g_Config.m_ClShowhudHealthAmmo)
			{
				RenderAmmoHealthAndArmor(m_pClient->m_Snap.m_pLocalCharacter);
			}
			if(m_pClient->m_Snap.m_aCharacters[m_pClient->m_Snap.m_LocalClientId].m_HasExtendedData && g_Config.m_ClShowhudDDRace && GameClient()->m_GameInfo.m_HudDDRace)
			{
				RenderPlayerState(m_pClient->m_Snap.m_LocalClientId);
			}
			RenderSpectatorCount();
			RenderMovementInformation();
			RenderDDRaceEffects();
		}
		else if(m_pClient->m_Snap.m_SpecInfo.m_Active)
		{
			int SpectatorId = m_pClient->m_Snap.m_SpecInfo.m_SpectatorId;
			if(SpectatorId != SPEC_FREEVIEW && g_Config.m_ClShowhudHealthAmmo)
			{
				RenderAmmoHealthAndArmor(&m_pClient->m_Snap.m_aCharacters[SpectatorId].m_Cur);
			}
			if(SpectatorId != SPEC_FREEVIEW &&
				m_pClient->m_Snap.m_aCharacters[SpectatorId].m_HasExtendedData &&
				g_Config.m_ClShowhudDDRace &&
				(!GameClient()->m_MultiViewActivated || GameClient()->m_MultiViewShowHud) &&
				GameClient()->m_GameInfo.m_HudDDRace)
			{
				RenderPlayerState(SpectatorId);
			}
			RenderMovementInformation();
			RenderSpectatorHud();
		}

		if(g_Config.m_ClShowhudTimer)
			RenderGameTimer();
		RenderPauseNotification();
		RenderSuddenDeath();
		if(g_Config.m_ClShowhudScore)
			RenderScoreHud();
		RenderDummyActions();
		RenderWarmupTimer();
		RenderTextInfo();
		RenderLocalTime((m_Width / 7) * 3);
		if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
			RenderConnectionWarning();
		RenderTeambalanceWarning();
		RenderStatBars();
		m_pClient->m_Voting.Render();
		if(g_Config.m_ClShowRecord)
			RenderRecord();
	}
	RenderCursor();
}

void CHud::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType == NETMSGTYPE_SV_DDRACETIME || MsgType == NETMSGTYPE_SV_DDRACETIMELEGACY)
	{
		CNetMsg_Sv_DDRaceTime *pMsg = (CNetMsg_Sv_DDRaceTime *)pRawMsg;

		m_DDRaceTime = pMsg->m_Time;

		m_ShowFinishTime = pMsg->m_Finish != 0;

		if(!m_ShowFinishTime)
		{
			m_TimeCpDiff = (float)pMsg->m_Check / 100;
			m_TimeCpLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
		}
		else
		{
			m_FinishTimeDiff = (float)pMsg->m_Check / 100;
			m_FinishTimeLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_RECORD || MsgType == NETMSGTYPE_SV_RECORDLEGACY)
	{
		CNetMsg_Sv_Record *pMsg = (CNetMsg_Sv_Record *)pRawMsg;

		// NETMSGTYPE_SV_RACETIME on old race servers
		if(MsgType == NETMSGTYPE_SV_RECORDLEGACY && m_pClient->m_GameInfo.m_DDRaceRecordMessage)
		{
			m_DDRaceTime = pMsg->m_ServerTimeBest; // First value: m_Time

			m_FinishTimeLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);

			if(pMsg->m_PlayerTimeBest) // Second value: m_Check
			{
				m_TimeCpDiff = (float)pMsg->m_PlayerTimeBest / 100;
				m_TimeCpLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
			}
		}
		else if(MsgType == NETMSGTYPE_SV_RECORD || m_pClient->m_GameInfo.m_RaceRecordMessage)
		{
			m_ServerRecord = (float)pMsg->m_ServerTimeBest / 100;
			m_aPlayerRecord[g_Config.m_ClDummy] = (float)pMsg->m_PlayerTimeBest / 100;
		}
	}
}

void CHud::RenderDDRaceEffects()
{
	if(m_DDRaceTime)
	{
		char aBuf[64];
		char aTime[32];
		if(m_ShowFinishTime && m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
		{
			str_time(m_DDRaceTime, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "Finish time: %s", aTime);

			// calculate alpha (4 sec 1 than get lower the next 2 sec)
			float Alpha = 1.0f;
			if(m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 4 < Client()->GameTick(g_Config.m_ClDummy) && m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
			{
				// lower the alpha slowly to blend text out
				Alpha = ((float)(m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6) - (float)Client()->GameTick(g_Config.m_ClDummy)) / (float)(Client()->GameTickSpeed() * 2);
			}

			TextRender()->TextColor(1, 1, 1, Alpha);
			CTextCursor Cursor;
			TextRender()->SetCursor(&Cursor, 150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(12, aBuf, -1, -1.0f) / 2, 20, 12, TEXTFLAG_RENDER);
			Cursor.m_LineWidth = -1.0f;
			TextRender()->RecreateTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);
			if(m_FinishTimeDiff != 0.0f && m_DDRaceEffectsTextContainerIndex.Valid())
			{
				if(m_FinishTimeDiff < 0)
				{
					str_time_float(-m_FinishTimeDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
					str_format(aBuf, sizeof(aBuf), "-%s", aTime);
					TextRender()->TextColor(0.5f, 1.0f, 0.5f, Alpha); // green
				}
				else
				{
					str_time_float(m_FinishTimeDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
					str_format(aBuf, sizeof(aBuf), "+%s", aTime);
					TextRender()->TextColor(1.0f, 0.5f, 0.5f, Alpha); // red
				}
				TextRender()->SetCursor(&Cursor, 150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(10, aBuf, -1, -1.0f) / 2, 34, 10, TEXTFLAG_RENDER);
				Cursor.m_LineWidth = -1.0f;
				TextRender()->AppendTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);
			}
			if(m_DDRaceEffectsTextContainerIndex.Valid())
			{
				auto OutlineColor = TextRender()->DefaultTextOutlineColor();
				OutlineColor.a *= Alpha;
				TextRender()->RenderTextContainer(m_DDRaceEffectsTextContainerIndex, TextRender()->DefaultTextColor(), OutlineColor);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else if(g_Config.m_ClShowhudTimeCpDiff && !m_ShowFinishTime && m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
		{
			if(m_TimeCpDiff < 0)
			{
				str_time_float(-m_TimeCpDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
				str_format(aBuf, sizeof(aBuf), "-%s", aTime);
			}
			else
			{
				str_time_float(m_TimeCpDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
				str_format(aBuf, sizeof(aBuf), "+%s", aTime);
			}

			// calculate alpha (4 sec 1 than get lower the next 2 sec)
			float Alpha = 1.0f;
			if(m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 4 < Client()->GameTick(g_Config.m_ClDummy) && m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
			{
				// lower the alpha slowly to blend text out
				Alpha = ((float)(m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6) - (float)Client()->GameTick(g_Config.m_ClDummy)) / (float)(Client()->GameTickSpeed() * 2);
			}

			if(m_TimeCpDiff > 0)
				TextRender()->TextColor(1.0f, 0.5f, 0.5f, Alpha); // red
			else if(m_TimeCpDiff < 0)
				TextRender()->TextColor(0.5f, 1.0f, 0.5f, Alpha); // green
			else if(!m_TimeCpDiff)
				TextRender()->TextColor(1, 1, 1, Alpha); // white

			CTextCursor Cursor;
			TextRender()->SetCursor(&Cursor, 150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(10, aBuf, -1, -1.0f) / 2, 20, 10, TEXTFLAG_RENDER);
			Cursor.m_LineWidth = -1.0f;
			TextRender()->RecreateTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);

			if(m_DDRaceEffectsTextContainerIndex.Valid())
			{
				auto OutlineColor = TextRender()->DefaultTextOutlineColor();
				OutlineColor.a *= Alpha;
				TextRender()->RenderTextContainer(m_DDRaceEffectsTextContainerIndex, TextRender()->DefaultTextColor(), OutlineColor);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
}

void CHud::RenderRecord()
{
	if(m_ServerRecord > 0.0f)
	{
		char aBuf[64];
		TextRender()->Text(5, 75, 6, Localize("Server best:"), -1.0f);
		char aTime[32];
		str_time_float(m_ServerRecord, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
		str_format(aBuf, sizeof(aBuf), "%s%s", m_ServerRecord > 3600 ? "" : "   ", aTime);
		TextRender()->Text(53, 75, 6, aBuf, -1.0f);
	}

	const float PlayerRecord = m_aPlayerRecord[g_Config.m_ClDummy];
	if(PlayerRecord > 0.0f)
	{
		char aBuf[64];
		TextRender()->Text(5, 82, 6, Localize("Personal best:"), -1.0f);
		char aTime[32];
		str_time_float(PlayerRecord, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
		str_format(aBuf, sizeof(aBuf), "%s%s", PlayerRecord > 3600 ? "" : "   ", aTime);
		TextRender()->Text(53, 82, 6, aBuf, -1.0f);
	}
}

void CHud::RenderDefaultScoreHud()
{
	static float s_TextWidth100 = TextRender()->TextWidth(s_DefaultFontSize, "999", -1, -1.0f);
	float ScoreWidthMax = maximum(maximum(m_aScoreInfo[0].m_ScoreTextWidth, m_aScoreInfo[1].m_ScoreTextWidth), s_TextWidth100);
	float Split = 3.0f;

	float StartY = 0.0f; // the height of this display is 56, so EndY is 285
	float BoxWidth = ScoreWidthMax + 16.0f + 2 * Split;
	float Half = m_Width / 2.0f;
	float BoxStart = Half - BoxWidth * 2;

	bool ForceScoreInfoInit = !m_aScoreInfo[0].m_Initialized || !m_aScoreInfo[1].m_Initialized;

	int Local = -1;
	int aPos[2] = {1, 2};
	const CNetObj_PlayerInfo *apPlayerInfo[2] = {0, 0};
	int i = 0;
	for(int t = 0; t < 2 && i < MAX_CLIENTS && m_pClient->m_Snap.m_apInfoByScore[i]; ++i)
	{
		if(m_pClient->m_Snap.m_apInfoByScore[i]->m_Team != TEAM_SPECTATORS)
		{
			apPlayerInfo[t] = m_pClient->m_Snap.m_apInfoByScore[i];
			if(apPlayerInfo[t]->m_ClientId == m_pClient->m_Snap.m_LocalClientId)
				Local = t;
			++t;
		}
	}
	// search local player info if not a spectator, nor within top2 scores
	if(Local == -1 && m_pClient->m_Snap.m_pLocalInfo && m_pClient->m_Snap.m_pLocalInfo->m_Team != TEAM_SPECTATORS)
	{
		for(; i < MAX_CLIENTS && m_pClient->m_Snap.m_apInfoByScore[i]; ++i)
		{
			if(m_pClient->m_Snap.m_apInfoByScore[i]->m_Team != TEAM_SPECTATORS)
				++aPos[1];
			if(m_pClient->m_Snap.m_apInfoByScore[i]->m_ClientId == m_pClient->m_Snap.m_LocalClientId)
			{
				apPlayerInfo[1] = m_pClient->m_Snap.m_apInfoByScore[i];
				Local = 1;
				break;
			}
		}
	}

	char aScore[2][16];
	for(int t = 0; t < 2; ++t)
	{
		if(apPlayerInfo[t])
		{
			str_format(aScore[t], sizeof(aScore[t]), "%d", apPlayerInfo[t]->m_Score);
		}
		else
			aScore[t][0] = 0;
	}

	bool RecreateScores = str_comp(aScore[0], m_aScoreInfo[0].m_aScoreText) != 0 || str_comp(aScore[1], m_aScoreInfo[1].m_aScoreText) != 0 || m_LastLocalClientId != m_pClient->m_Snap.m_LocalClientId;
	m_LastLocalClientId = m_pClient->m_Snap.m_LocalClientId;

	bool RecreateRect = ForceScoreInfoInit;
	for(int t = 0; t < 2; t++)
	{
		if(RecreateScores)
		{
			m_aScoreInfo[t].m_ScoreTextWidth = TextRender()->TextWidth(s_DefaultFontSize, aScore[t], -1, -1.0f);
			str_copy(m_aScoreInfo[t].m_aScoreText, aScore[t]);
			RecreateRect = true;
		}

		if(apPlayerInfo[t])
		{
			int Id = apPlayerInfo[t]->m_ClientId;
			if(Id >= 0 && Id < MAX_CLIENTS)
			{
				const char *pName = m_pClient->m_aClients[Id].m_aName;
				if(str_comp(pName, m_aScoreInfo[t].m_aPlayerNameText) != 0)
					RecreateRect = true;
			}
		}
		else
		{
			if(m_aScoreInfo[t].m_aPlayerNameText[0] != 0)
				RecreateRect = true;
		}

		char aBuf[16];
		str_format(aBuf, sizeof(aBuf), "%d.", aPos[t]);
		if(str_comp(aBuf, m_aScoreInfo[t].m_aRankText) != 0)
			RecreateRect = true;
	}

	// draw score box
	{
		const IGraphics::CFreeformItem Freeform(
			BoxStart - BoxWidth, StartY,
			BoxStart + BoxWidth * 5, StartY,

			BoxStart, StartY + s_DefaultHudBoxHeight,
			BoxStart + BoxWidth * 4, StartY + s_DefaultHudBoxHeight);

		Graphics()->TrianglesBegin();

		Graphics()->SetColor(0.f, 0.f, 0.f, 0.3f);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);

		Graphics()->TrianglesEnd();
	}

	for(int t = 0; t < 2; t++)
	{
		const float SubBoxStart = BoxStart + BoxWidth * 0.5f + t * (BoxWidth * 2);
		if(RecreateScores)
		{
			CTextCursor Cursor;
			TextRender()->SetCursor(&Cursor, SubBoxStart + s_DefaultFontSize / 4, StartY + (s_DefaultHudBoxHeight - s_DefaultFontSize) / 2, s_DefaultFontSize, TEXTFLAG_RENDER);
			Cursor.m_LineWidth = -1;
			TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, &Cursor, aScore[t]);
		}
		// draw score
		if(m_aScoreInfo[t].m_TextScoreContainerIndex.Valid())
		{
			ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
			ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
			TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, TColor, TOutlineColor);
		}

		if(apPlayerInfo[t])
		{
			// draw name
			int Id = apPlayerInfo[t]->m_ClientId;
			if(Id >= 0 && Id < MAX_CLIENTS)
			{
				const char *pName = m_pClient->m_aClients[Id].m_aName;
				if(RecreateRect)
				{
					str_copy(m_aScoreInfo[t].m_aPlayerNameText, pName);

					CTextCursor Cursor;
					// float w = TextRender()->TextWidth(4.0f, pName, -1, -1.0f);
					TextRender()->SetCursor(&Cursor, SubBoxStart + s_DefaultFontSize / 4 + Split * 3, StartY + s_DefaultHudBoxHeight, s_DefaultFontSize / 2, TEXTFLAG_RENDER);
					Cursor.m_LineWidth = -1;
					TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, &Cursor, pName);
				}

				if(m_aScoreInfo[t].m_OptionalNameTextContainerIndex.Valid())
				{
					ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
					ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
					TextRender()->RenderTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, TColor, TOutlineColor);
				}

				// draw tee
				CTeeRenderInfo TeeInfo = m_pClient->m_aClients[Id].m_RenderInfo;
				TeeInfo.m_Size = s_DefaultHudBoxHeight;

				const CAnimState *pIdleState = CAnimState::GetIdle();
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);

				float TeePosX = SubBoxStart + BoxWidth - TeeInfo.m_Size / 2;
				float TeePosY = StartY + s_DefaultHudBoxHeight / 2.0f + OffsetToMid.y;

				vec2 TeeRenderPos(TeePosX, TeePosY);

				RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
			}
		}
		else
		{
			m_aScoreInfo[t].m_aPlayerNameText[0] = 0;
		}

		// draw position
		char aBuf[16];
		str_format(aBuf, sizeof(aBuf), "%d.", aPos[t]);
		if(RecreateRect)
		{
			str_copy(m_aScoreInfo[t].m_aRankText, aBuf);

			CTextCursor Cursor;
			TextRender()->SetCursor(&Cursor, SubBoxStart, StartY + s_DefaultHudBoxHeight, s_DefaultFontSize / 2, TEXTFLAG_RENDER);
			Cursor.m_LineWidth = -1;
			TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextRankContainerIndex, &Cursor, aBuf);
		}
		if(m_aScoreInfo[t].m_TextRankContainerIndex.Valid())
		{
			ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
			ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
			TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextRankContainerIndex, TColor, TOutlineColor);
		}
	}
}

void CHud::RenderTeamScoreHud()
{
	int GameFlags = m_pClient->m_Snap.m_pGameInfoObj->m_GameFlags;

	static float s_TextWidth100 = TextRender()->TextWidth(s_DefaultFontSize, "999", -1, -1.0f);
	float ScoreWidthMax = maximum(maximum(m_aScoreInfo[0].m_ScoreTextWidth, m_aScoreInfo[1].m_ScoreTextWidth), s_TextWidth100);
	float Split = 3.0f;
	float ImageSize = (GameFlags & GAMEFLAG_FLAGS) ? 16.0f : Split;

	float StartY = 0.0f; // the height of this display is 56, so EndY is 285
	float BoxWidth = ScoreWidthMax + ImageSize + 2 * Split;
	float Half = m_Width / 2.0f;
	float BoxStart = Half - BoxWidth * 2;

	bool ForceScoreInfoInit = !m_aScoreInfo[0].m_Initialized || !m_aScoreInfo[1].m_Initialized;
	char aScoreTeam[2][16];
	str_format(aScoreTeam[TEAM_RED], sizeof(aScoreTeam[TEAM_RED]), "%d", m_pClient->m_Snap.m_pGameDataObj->m_TeamscoreRed);
	str_format(aScoreTeam[TEAM_BLUE], sizeof(aScoreTeam[TEAM_BLUE]), "%d", m_pClient->m_Snap.m_pGameDataObj->m_TeamscoreBlue);

	bool aRecreateTeamScore[2] = {str_comp(aScoreTeam[0], m_aScoreInfo[0].m_aScoreText) != 0, str_comp(aScoreTeam[1], m_aScoreInfo[1].m_aScoreText) != 0};

	const int aFlagCarrier[2] = {
		m_pClient->m_Snap.m_pGameDataObj->m_FlagCarrierRed,
		m_pClient->m_Snap.m_pGameDataObj->m_FlagCarrierBlue};

	bool RecreateRect = ForceScoreInfoInit;
	for(int t = 0; t < 2; t++)
	{
		if(aRecreateTeamScore[t])
		{
			m_aScoreInfo[t].m_ScoreTextWidth = TextRender()->TextWidth(s_DefaultFontSize, aScoreTeam[t == 0 ? TEAM_RED : TEAM_BLUE], -1, -1.0f);
			str_copy(m_aScoreInfo[t].m_aScoreText, aScoreTeam[t == 0 ? TEAM_RED : TEAM_BLUE]);
			RecreateRect = true;
		}
	}

	// draw score box
	{
		// SubBoxStart, StartY, BoxWidth, s_DefaultHudBoxHeight
		const IGraphics::CFreeformItem Freeform(
			BoxStart - BoxWidth, StartY,
			BoxStart + BoxWidth * 5, StartY,

			BoxStart, StartY + s_DefaultHudBoxHeight,
			BoxStart + BoxWidth * 4, StartY + s_DefaultHudBoxHeight);

		Graphics()->TrianglesBegin();

		Graphics()->SetColor(0.f, 0.f, 0.f, 0.3f);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);

		Graphics()->TrianglesEnd();
	}

	const char *pTeamNames[] = {GetTeamName(TEAM_RED), GetTeamName(TEAM_BLUE)};
	const int TeamCountryFlags[] = {GetTeamFlag(TEAM_RED), GetTeamFlag(TEAM_BLUE)};

	const bool IsClanWar = pTeamNames[0] && pTeamNames[1];

	for(int t = 0; t < 2; t++)
	{
		const float SubBoxStart = BoxStart + BoxWidth * 0.5f + t * (BoxWidth * 2);

		// country flag
		if(IsClanWar)
		{
			const int Code = TeamCountryFlags[t];

			const auto *pFlag = GameClient()->m_CountryFlags.GetByCountryCode(Code);

			if(pFlag->m_Texture.IsValid())
			{
				const float XScale = Graphics()->ScreenWidth() / m_Width;
				const float YScale = Graphics()->ScreenHeight() / m_Height;

				Graphics()->ClipEnable((int)((SubBoxStart - 0.5f * BoxWidth) * XScale), (int)(StartY * YScale), (int)(2 * BoxWidth * XScale), (int)(s_DefaultHudBoxHeight * YScale));
				Graphics()->BlendNormal();
				Graphics()->TextureSet(pFlag->m_Texture);
				Graphics()->QuadsBegin();

				const float FlagTransparency = 0.2f;
				const ColorRGBA ColorfulEdge = ColorRGBA(1.0f, 1.0f, 1.0f, FlagTransparency);
				const ColorRGBA TransparentEdge = ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f);

				Graphics()->QuadsSetSubset(0, 0, 3, 1);
				if(t)
					Graphics()->SetColor4(TransparentEdge, ColorfulEdge, TransparentEdge, ColorfulEdge);
				else
					Graphics()->SetColor4(ColorfulEdge, TransparentEdge, ColorfulEdge, TransparentEdge);
				IGraphics::CQuadItem QuadItem(SubBoxStart - BoxWidth * 0.5f - BoxWidth * 2 * 2.5f * 1.08f + t * BoxWidth * 2 * 2.5f * 1.08f, StartY - 1.1f * BoxWidth, 3 * 2.5f * BoxWidth, 2.5f * 0.5f * BoxWidth);
				if(t)
					Graphics()->QuadsSetRotation(2 * pi - pi / 12);
				else
					Graphics()->QuadsSetRotation(pi / 12);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
				Graphics()->ClipDisable();
				Graphics()->QuadsSetRotation(0);
			}
		}

		// draw score
		if(aRecreateTeamScore[t])
		{
			CTextCursor Cursor;
			TextRender()->SetCursor(&Cursor, SubBoxStart + s_DefaultFontSize / 4, StartY + (s_DefaultHudBoxHeight - s_DefaultFontSize) / 2, s_DefaultFontSize, TEXTFLAG_RENDER);
			Cursor.m_LineWidth = -1;
			TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, &Cursor, aScoreTeam[t]);
		}
		if(m_aScoreInfo[t].m_TextScoreContainerIndex.Valid())
		{
			ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
			ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
			TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, TColor, TOutlineColor);
		}

		int BlinkTimer = (m_pClient->m_aFlagDropTick[t] != 0 &&
					 (Client()->GameTick(g_Config.m_ClDummy) - m_pClient->m_aFlagDropTick[t]) / Client()->GameTickSpeed() >= 25) ?
					 10 :
					 20;

		const float FlagPosX = SubBoxStart + BoxWidth - s_DefaultFontSize;
		const float FlagPosY = StartY + (s_DefaultHudBoxHeight - (s_DefaultHudBoxHeight - 2)) / 2;

		const float NamePosX = SubBoxStart + s_DefaultFontSize / 4;
		const float NamePosY = StartY + s_DefaultHudBoxHeight;

		if(IsClanWar)
		{
			if(str_comp(pTeamNames[t], m_aScoreInfo[t].m_aClanNameText) != 0 || RecreateRect)
			{
				CTextCursor Cursor;
				TextRender()->SetCursor(&Cursor, NamePosX, NamePosY, s_DefaultFontSize / 2, TEXTFLAG_RENDER);
				Cursor.m_LineWidth = -1;
				TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, &Cursor, pTeamNames[t]);
			}

			if(m_aScoreInfo[t].m_OptionalNameTextContainerIndex.Valid())
			{
				ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
				ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
				TextRender()->RenderTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, TColor, TOutlineColor);
			}
		}

		if(GameFlags & GAMEFLAG_FLAGS)
		{
			if(aFlagCarrier[t] == FLAG_ATSTAND || (aFlagCarrier[t] == FLAG_TAKEN && ((Client()->GameTick(g_Config.m_ClDummy) / BlinkTimer) & 1)))
			{
				// draw flag
				Graphics()->TextureSet(t == 0 ? m_pClient->m_GameSkin.m_SpriteFlagRed : m_pClient->m_GameSkin.m_SpriteFlagBlue);
				Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
				Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_FlagOffset, FlagPosX, FlagPosY);
			}
			else if(aFlagCarrier[t] >= 0)
			{
				// draw name of the flag holder
				int Id = aFlagCarrier[t] % MAX_CLIENTS;

				if(!IsClanWar)
				{
					const char *pName = m_pClient->m_aClients[Id].m_aName;
					if(str_comp(pName, m_aScoreInfo[t].m_aPlayerNameText) != 0 || RecreateRect)
					{
						str_copy(m_aScoreInfo[t].m_aPlayerNameText, pName);

						// float w = TextRender()->TextWidth(4.0f, pName, -1, -1.0f);

						CTextCursor Cursor;
						TextRender()->SetCursor(&Cursor, NamePosX, NamePosY, s_DefaultFontSize / 2, TEXTFLAG_RENDER);
						Cursor.m_LineWidth = -1;
						TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, &Cursor, pName);
					}

					if(m_aScoreInfo[t].m_OptionalNameTextContainerIndex.Valid())
					{
						ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
						ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
						TextRender()->RenderTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, TColor, TOutlineColor);
					}
				}

				// draw tee of the flag holder
				CTeeRenderInfo TeeInfo = m_pClient->m_aClients[Id].m_RenderInfo;
				TeeInfo.m_Size = s_DefaultHudBoxHeight;

				const CAnimState *pIdleState = CAnimState::GetIdle();
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);

				float TeePosX = SubBoxStart + BoxWidth - TeeInfo.m_Size / 2;
				float TeePosY = StartY + s_DefaultHudBoxHeight / 2.0f + OffsetToMid.y;

				vec2 TeeRenderPos(TeePosX, TeePosY);

				RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
			}
		}
	}
}

void CHud::RenderStatBars()
{
	if(!m_pClient->m_Snap.m_SpecInfo.m_Active)
		return;

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
	const int GameFlags = m_pClient->m_Snap.m_pGameInfoObj->m_GameFlags;

	const auto &aTeamSize = GameClient()->m_Snap.m_aTeamSize;
	int MaxTeam = TEAM_RED;

	if(GameFlags & GAMEFLAG_TEAMS)
		MaxTeam = TEAM_BLUE;

	{
		if(aTeamSize[TEAM_RED] > 8 || aTeamSize[TEAM_BLUE] > 8)
			return;

		const float HalfY = m_Height / 2;
		// const float HalfX = m_Width / 2;
		const float Spacing = 4.0f;
		const float BoxHeight = 14.0f;
		const float BoxWidth = 80.0f;

		for(int t = TEAM_RED; t <= MaxTeam; t++)
		{
			const float StartY = HalfY - (aTeamSize[t] * (BoxHeight + Spacing)) / 2 + Spacing;
			const float StartX = t == 0 ? 0.0f : m_Width - BoxWidth;

			for(int i = 0; i < aTeamSize[t]; i++)
			{
				const auto *pClientInfo = m_pClient->m_Snap.m_apInfoByTeamName[aTeamSize[t - 1] * t + i];

				if(!pClientInfo)
					continue;

				int Id = pClientInfo->m_ClientId;

				const auto *pCharInfo = &m_pClient->m_Snap.m_aCharacters[Id].m_Cur;
				const bool IsActive = m_pClient->m_Snap.m_aCharacters[Id].m_Active;

				const float BoxStartY = StartY + i * (BoxHeight + Spacing);
				CUIRect Row{StartX, BoxStartY, BoxWidth, BoxHeight};
				CUIRect AddRow;

				Row.Draw(ColorRGBA(0.f, 0.f, 0.f, IsActive ? 0.2f : 0.1f), t == 0 ? IGraphics::CORNER_R : IGraphics::CORNER_L, 2.0f);

				if(IsActive)
					AddRow.Draw(ColorRGBA(0.f, 0.f, 0.f, 0.1f), IGraphics::CORNER_ALL, 2.0f);

				if(IsActive && pCharInfo->m_Health)
				{
					Row.Margin(vec2(10.0f, 5.0f), &AddRow);
					if(!t)
						AddRow.VSplitLeft(AddRow.w * (pCharInfo->m_Health / 10.0f), &AddRow, 0);
					else
						AddRow.VSplitRight(AddRow.w * pCharInfo->m_Health / 10.0f, 0, &AddRow);

					AddRow.Draw(s_TeamColor[t], IGraphics::CORNER_ALL, 2.0f);
				}
				if(IsActive && pCharInfo->m_Armor)
				{
					Row.Margin(vec2(10.0f, 5.0f), &AddRow);
					if(!t)
						AddRow.VSplitLeft(AddRow.w * (pCharInfo->m_Armor / 10.0f), &AddRow, 0);
					else
						AddRow.VSplitRight(AddRow.w * (pCharInfo->m_Armor / 10.0f), 0, &AddRow);

					AddRow.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.3f), IGraphics::CORNER_ALL, 2.0f);
				}

				// prepare render tee and flag
				const float TeeSize = BoxHeight * 0.75f;
				const float FlagSize = TeeSize * 1.2f;
				CTeeRenderInfo TeeInfo = m_pClient->m_aClients[Id].m_RenderInfo;
				TeeInfo.m_Size = TeeSize;

				const CAnimState *pIdleState = CAnimState::GetIdle();
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);

				const float TeePosX = StartX + (1 - t) * (Spacing / 2) + t * (BoxWidth - Spacing / 2);
				const float TeePosY = BoxStartY + BoxHeight / 2;

				const vec2 TeeRenderPos(TeePosX - 2 * ((float)t - 0.5f) * (TeeSize / 2), TeePosY + OffsetToMid.y);

				const float FlagPosX = TeePosX - t * FlagSize / 2;
				const float FlagPosY = TeePosY - TeeSize;

				// render flags
				{
					if(pGameInfoObj && (pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) &&
						pGameDataObj && (pGameDataObj->m_FlagCarrierRed == pClientInfo->m_ClientId || pGameDataObj->m_FlagCarrierBlue == pClientInfo->m_ClientId))
					{
						Graphics()->BlendNormal();
						Graphics()->TextureSet(pGameDataObj->m_FlagCarrierBlue == pClientInfo->m_ClientId ? GameClient()->m_GameSkin.m_SpriteFlagBlue : GameClient()->m_GameSkin.m_SpriteFlagRed);
						Graphics()->QuadsBegin();
						if(!t)
							Graphics()->QuadsSetSubset(1.0f, 0.0f, 0.0f, 1.0f);
						else
							Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
						IGraphics::CQuadItem QuadItem(FlagPosX, FlagPosY, FlagSize / 2, FlagSize);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
						Graphics()->QuadsEnd();
					}
				}

				// render tee
				RenderTools()->RenderTee(pIdleState, &TeeInfo, pCharInfo->m_Emote, vec2(-2 * ((float)t - 0.5f), 0.0f), TeeRenderPos);

				// render current weapon
				if(IsActive)
				{
					// normal weapons
					int Weapon = clamp(pCharInfo->m_Weapon, 0, NUM_WEAPONS - 1);
					int Offset;

					if(!t)
						Offset = m_aWeaponOffset[Weapon];
					else
						Offset = m_aMirroredWeaponOffset[Weapon];

					Graphics()->QuadsSetRotation(2 * (0.5 - t) * pi * 7 / 4);
					Graphics()->TextureSet(m_pClient->m_GameSkin.m_aSpritePickupWeapons[Weapon]);
					Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, Offset, (1 - t) * (StartX + BoxWidth) + t * (StartX), BoxStartY + BoxHeight / 2);
					Graphics()->QuadsSetRotation(0);
					Graphics()->TextureClear();
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
				}

				// render name
				{
					const char *pName = m_pClient->m_aClients[Id].m_aName;
					TextRender()->Text(t * (StartX + BoxWidth - Spacing * 4 - TextRender()->TextWidth(s_DefaultFontSize / 2, pName)) + (1 - t) * (TeeRenderPos.x + 2 * Spacing), BoxStartY + (BoxHeight - s_DefaultFontSize / 2) / 2.0f, s_DefaultFontSize / 2, pName);
				}
			}
		}
	}
}

const char *CHud::GetTeamName(int Team) const
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	int ClanPlayers = 0;
	const char *pClanName = nullptr;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByScore)
	{
		if(!pInfo || pInfo->m_Team != Team)
			continue;

		if(!pClanName)
		{
			pClanName = GameClient()->m_aClients[pInfo->m_ClientId].m_aClan;
			ClanPlayers++;
		}
		else
		{
			if(str_comp(GameClient()->m_aClients[pInfo->m_ClientId].m_aClan, pClanName) == 0)
				ClanPlayers++;
			else
				return nullptr;
		}
	}

	if(ClanPlayers > 1 && pClanName[0] != '\0')
		return pClanName;
	else
		return nullptr;
}

int CHud::GetTeamFlag(int Team) const
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	int ClanPlayers = 0;
	int ClanFlag = 0;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByScore)
	{
		if(!pInfo || pInfo->m_Team != Team)
			continue;

		if(!ClanFlag)
		{
			ClanFlag = GameClient()->m_aClients[pInfo->m_ClientId].m_Country;
			ClanPlayers++;
		}
		else
		{
			if(ClanFlag == GameClient()->m_aClients[pInfo->m_ClientId].m_Country)
				ClanPlayers++;
			else
				return 0;
		}
	}

	if(ClanPlayers > 1)
		return ClanFlag;
	else
		return 0;
}