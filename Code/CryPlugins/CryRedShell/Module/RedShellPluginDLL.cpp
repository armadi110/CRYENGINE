// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved. 

#include "StdAfx.h"

#include <CrySystem/ICryPlugin.h>
#include <CryGamePlatform/Interface/IGamePlatform.h>
#include <RedShell.h>

// Included only once per DLL module.
#include <CryCore/Platform/platform_impl.inl>


namespace Cry
{

namespace RedShell
{

class CPlugin final : public Cry::IEnginePlugin
{
	CRYINTERFACE_BEGIN()
		CRYINTERFACE_ADD(Cry::IEnginePlugin)
	CRYINTERFACE_END()

	CRYGENERATE_SINGLETONCLASS_GUID(CPlugin, "Plugin_RedShell", "{676C0D42-DD53-4658-ACA7-8CAD01C805D5}"_cry_guid)

public:
	virtual ~CPlugin() = default;

	// Cry::IEnginePlugin
	virtual const char* GetName() const override { return "RedShellPlugin"; }
	virtual bool Initialize(SSystemGlobalEnvironment& env, const SSystemInitParams& initParams) override
	{
		if (env.IsEditor())
		{
			// Don't use RedShell SDK integration in Sandbox
			return true;
		}

		CryLogAlways("[RedShell] Initializing ...");

		Cry::GamePlatform::IPlugin* pSteamPlugin = env.pSystem->GetIPluginManager()->QueryPlugin<Cry::GamePlatform::IPlugin>();
		if ((pSteamPlugin == nullptr) || (pSteamPlugin->GetType() != Cry::GamePlatform::EType::Steam))
		{
			CryFatalError("[RedShell] RedShell plugin requires Steam. Make sure that GamePlatform plugin is enabled and loaded before loading RedShell plugin !");
			return false;
		}

		Cry::GamePlatform::IUser* pSteamUser = pSteamPlugin->GetLocalClient();
		if (pSteamUser == nullptr)
		{
			CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "[RedShell] Failed to query steam user to initialize RedShell SDK");
			return true;
		}

		ICVar* pApiKey = REGISTER_STRING("redShell_ApiKey", "", VF_REQUIRE_APP_RESTART | VF_INVISIBLE | VF_CHEAT_ALWAYS_CHECK, "Sets the API key needed by RedShell SDK, so that the SDK knows for which game to register a conversion.");
		if (pApiKey == nullptr)
		{
			CryWarning(VALIDATOR_MODULE_SYSTEM, VALIDATOR_WARNING, "[RedShell] Failed to register redShell_ApiKey CVar. User conversion likely won't be registered");
			return true;
		}

		const Cry::GamePlatform::IUser::Identifier steamUserId = pSteamUser->GetIdentifier();
		const char* szSteamUserName = pSteamUser->GetNickname();

		// Initialize the SDK with API_KEY and a unique id for the current user's Steam Id
		RedShell_init(pApiKey->GetString(), string().Format("%" PRIu64, steamUserId).c_str());

		// Send game launch Event to the RedShell API
		RedShell_logEvent("game_launch");

		CryLogAlways("[RedShell] Registered steam user '%s' with user id %" PRIu64 " using api key '%s'", szSteamUserName, steamUserId, pApiKey->GetString());

		env.pConsole->UnregisterVariable("redShell_ApiKey");

		return true;
	}
	// ~Cry::IEnginePlugin
};

CRYREGISTER_SINGLETON_CLASS(CPlugin)

} // End of namespace RedShell

} // End of namespace Cry

#include <CryCore/CrtDebugStats.h>
