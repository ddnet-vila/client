/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "flow.h"

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/layers.h>
#include <game/mapitems.h>

#include <game/client/gameclient.h>

CFlow::CFlow()
{
	m_pCells = nullptr;
	m_Height = 0;
	m_Width = 0;
	m_Spacing = 0;
}

void CFlow::OnMapLoad()
{
	Clear();
	Init();
}

void CFlow::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
		Clear();
}

void CFlow::OnRender()
{
	if(g_Config.m_DbgFlow)
		DbgRender();
}

void CFlow::DbgRender()
{
	if(!m_pCells)
		return;

	IGraphics::CLineItem Array[1024];
	int NumItems = 0;
	Graphics()->TextureClear();
	Graphics()->LinesBegin();

	auto Func = [&Array, &NumItems](CFlow *pFlow, int x, int y) {
		vec2 Pos(x * pFlow->m_Spacing, y * pFlow->m_Spacing);
		vec2 Vel = pFlow->m_pCells[y * pFlow->m_Width + x].m_Vel * 0.01f;
		Array[NumItems++] = IGraphics::CLineItem(Pos.x, Pos.y, Pos.x + Vel.x, Pos.y + Vel.y);
		if(NumItems == 1024)
		{
			pFlow->Graphics()->LinesDraw(Array, 1024);
			NumItems = 0;
		}
	};

	ApplyToFlowZone(Func);

	if(NumItems)
		Graphics()->LinesDraw(Array, NumItems);
	Graphics()->LinesEnd();
}

void CFlow::Clear()
{
	free(m_pCells);
	m_pCells = nullptr;
}

void CFlow::Init()
{
	Clear();

	if(!g_Config.m_FlEnable)
		return;

	m_Spacing = g_Config.m_FlSpacing;

	CMapItemLayerTilemap *pTilemap = Layers()->GameLayer();
	m_Width = pTilemap->m_Width * 32 / m_Spacing;
	m_Height = pTilemap->m_Height * 32 / m_Spacing;

	// allocate and clear
	m_pCells = (CCell *)calloc((size_t)m_Width * m_Height, sizeof(CCell));
	for(int y = 0; y < m_Height; y++)
		for(int x = 0; x < m_Width; x++)
			m_pCells[y * m_Width + x].m_Vel = vec2(0.0f, 0.0f);
}

void CFlow::Update()
{
	if(!m_pCells)
		return;

	auto Func = [](CFlow *pFlow, int x, int y) {
		pFlow->m_pCells[y * pFlow->m_Width + x].m_Vel *= 0.85f;
	};

	ApplyToFlowZone(Func);
}

vec2 CFlow::Get(vec2 Pos)
{
	if(!m_pCells)
		return vec2(0, 0);

	int x = (int)(Pos.x / m_Spacing);
	int y = (int)(Pos.y / m_Spacing);
	if(x < 0 || y < 0 || x >= m_Width || y >= m_Height)
		return vec2(0, 0);

	return m_pCells[y * m_Width + x].m_Vel / m_Spacing;
}

void CFlow::Add(vec2 Pos, vec2 Vel, float Size)
{
	if(!m_pCells)
		return;

	if(distance(Pos, GameClient()->m_LocalCharacterPos) > 640)
		return;

	int x = (int)(Pos.x / m_Spacing);
	int y = (int)(Pos.y / m_Spacing);

	int Diameter = Size / m_Spacing;

	for(int i = -Diameter / 2; i <= Diameter / 2; i++)
	{
		for(int j = -Diameter / 2; j <= Diameter / 2; j++)
		{
			int Nx = x + i;
			int Ny = y + j;

			if(Nx < 0 || Ny < 0 || Nx >= m_Width || Ny >= m_Height)
				continue;

			if(distance(vec2(x, y), vec2(Nx, Ny)) > Diameter / 2)
				continue;

			m_pCells[Ny * m_Width + Nx].m_Vel += Vel / m_Spacing;
		}
	}
}

void CFlow::SetSpacing(int Spacing)
{
	Clear();
	m_Spacing = Spacing;

	Init();
}

int CFlow::GetSpacing() const
{
	return m_Spacing;
}

void CFlow::ApplyToFlowZone(std::function<void(CFlow *, int, int)> &&Function)
{
	if(!m_pCells)
		return;

	vec2 PlayerPos = GameClient()->m_LocalCharacterPos / m_Spacing;
	vec2 ShowDistancePos;

	const float ShowDistanceZoom = GameClient()->m_Camera.m_Zoom;

	RenderTools()->CalcScreenParams(Graphics()->ScreenAspect(), ShowDistanceZoom, &ShowDistancePos.x, &ShowDistancePos.y);

	int Nx = PlayerPos.x;
	int Ny = PlayerPos.y;

	const int ApplyDistance = distance(PlayerPos, ShowDistancePos) / m_Spacing;

	const int CurrentY = clamp(Ny - ApplyDistance, 0, m_Height);
	const int MaxY = clamp(Ny + ApplyDistance, 0, m_Height);

	const int CurrentX = clamp(Nx - ApplyDistance, 0, m_Width);
	const int MaxX = clamp(Nx + ApplyDistance, 0, m_Width);

	for(int y = CurrentY; y < MaxY; y++)
		for(int x = CurrentX; x < MaxX; x++)
			Function(this, x, y);
}