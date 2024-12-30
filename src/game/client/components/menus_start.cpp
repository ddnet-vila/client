/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/textrender.h>

#include <engine/client/updater.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <game/generated/client_data.h>
#include <game/localization.h>
#include <game/version.h>

#include "menus.h"

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

using namespace FontIcons;

void CMenus::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	const float Rounding = 10.0f;
	const float VMargin = 60;
	const float ButtonSize = 260;

	// render logo
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, 1);
	IGraphics::CQuadItem QuadItem(VMargin, MainView.h / 2 - 25, 360, 103);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();

	CUIRect Button;
	int NewPage = -1;

	CUIRect Menu;
	MainView.VSplitLeft(MainView.w - VMargin - ButtonSize, 0, &Menu);
	Menu.VSplitRight(VMargin, &Menu, 0);
	Menu.HSplitBottom(60.0f, &Menu, 0);

	Menu.HSplitBottom(40.0f, &Menu, &Button);

	CUIRect QuitButton;
	MainView.VMargin(MainView.w / 2 - ButtonSize / 2, &QuitButton);
	QuitButton.HSplitBottom(60.0f, &QuitButton, 0);
	QuitButton.HSplitBottom(40.0f, 0, &QuitButton);

	static CButtonContainer s_QuitButton;
	bool UsedEscape = false;
	if(DoButton_Menu(&s_QuitButton, Localize("Quit"), 0, &QuitButton, 0, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
	{
		if(UsedEscape || m_pClient->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
		{
			m_Popup = POPUP_QUIT;
		}
		else
		{
			Client()->Quit();
		}
	}

	Menu.HSplitBottom(100.0f, &Menu, nullptr);
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_SettingsButton;
	if(DoButton_Menu(&s_SettingsButton, Localize("Settings"), 0, &Button, g_Config.m_ClShowStartMenuImages ? "settings" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_S))
		NewPage = PAGE_SETTINGS;

	Menu.HSplitBottom(5.0f, &Menu, 0); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_DemoButton;
	if(DoButton_Menu(&s_DemoButton, Localize("Demos"), 0, &Button, g_Config.m_ClShowStartMenuImages ? "demos" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_D))
	{
		NewPage = PAGE_DEMOS;
	}

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_PlayButton;
	if(DoButton_Menu(&s_PlayButton, Localize("Play", "Start menu"), 0, &Button, g_Config.m_ClShowStartMenuImages ? "play_game" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
	{
		NewPage = g_Config.m_UiPage >= PAGE_INTERNET && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : PAGE_INTERNET;
	}

	// render version
	CUIRect CurVersion, ConsoleButton;
	MainView.HSplitBottom(45.0f, nullptr, &CurVersion);
	CurVersion.VSplitRight(40.0f, &CurVersion, nullptr);
	CurVersion.HSplitTop(20.0f, &ConsoleButton, &CurVersion);
	CurVersion.HSplitTop(5.0f, nullptr, &CurVersion);
	ConsoleButton.VSplitRight(40.0f, nullptr, &ConsoleButton);
	Ui()->DoLabel(&CurVersion, GAME_RELEASE_VERSION, 14.0f, TEXTALIGN_MR);

	if(NewPage != -1)
	{
		m_ShowStart = false;
		SetMenuPage(NewPage);
	}
}

void CMenus::RunServer(const char **ppArguments, const size_t NumArguments)
{
#if defined(CONF_PLATFORM_ANDROID)
	if(StartAndroidServer(ppArguments, NumArguments))
	{
		m_ForceRefreshLanPage = true;
	}
	else
	{
		Client()->AddWarning(SWarning(Localize("Server could not be started. Make sure to grant the notification permission in the app settings so the server can run in the background.")));
	}
#else
	char aBuf[IO_MAX_PATH_LENGTH];
	Storage()->GetBinaryPath(PLAT_SERVER_EXEC, aBuf, sizeof(aBuf));
	// No / in binary path means to search in $PATH, so it is expected that the file can't be opened. Just try executing anyway.
	if(str_find(aBuf, "/") == nullptr || fs_is_file(aBuf))
	{
		m_ServerProcess.m_Process = shell_execute(aBuf, EShellExecuteWindowState::BACKGROUND, ppArguments, NumArguments);
		m_ForceRefreshLanPage = true;
	}
	else
	{
		Client()->AddWarning(SWarning(Localize("Server executable not found, can't run server")));
	}
#endif
}

void CMenus::KillServer()
{
#if defined(CONF_PLATFORM_ANDROID)
	ExecuteAndroidServerCommand("shutdown");
	m_ForceRefreshLanPage = true;
#else
	if(m_ServerProcess.m_Process && kill_process(m_ServerProcess.m_Process))
	{
		m_ServerProcess.m_Process = INVALID_PROCESS;
		m_ForceRefreshLanPage = true;
	}
#endif
}

bool CMenus::IsServerRunning() const
{
#if defined(CONF_PLATFORM_ANDROID)
	return IsAndroidServerRunning();
#else
	return m_ServerProcess.m_Process != INVALID_PROCESS;
#endif
}
