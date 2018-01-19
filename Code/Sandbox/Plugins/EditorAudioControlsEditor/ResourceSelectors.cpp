// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"

#include "AudioControlsEditorPlugin.h"
#include "ResourceSelectorDialog.h"

#include <IResourceSelectorHost.h>
#include <ListSelectionDialog.h>
#include <CryGame/IGameFramework.h>
#include <IEditor.h>

namespace ACE
{
dll_string ShowSelectDialog(SResourceSelectorContext const& context, char const* szPreviousValue, ESystemItemType const controlType)
{
	CSystemAssetsManager const* const pAssetsManager = CAudioControlsEditorPlugin::GetAssetsManager();
	CRY_ASSERT_MESSAGE(pAssetsManager != nullptr, "Assets manager is null pointer.");

	char* szLevelName;
	gEnv->pGameFramework->GetEditorLevel(&szLevelName, nullptr);
	CResourceSelectorDialog dialog(controlType, pAssetsManager->GetScope(szLevelName), context.parentWidget);
	dialog.setModal(true);

	return dialog.ChooseItem(szPreviousValue);
}

dll_string AudioTriggerSelector(SResourceSelectorContext const& context, char const* const szPreviousValue)
{
	return ShowSelectDialog(context, szPreviousValue, ESystemItemType::Trigger);
}

dll_string AudioSwitchSelector(SResourceSelectorContext const& context, char const* const  szPreviousValue)
{
	return ShowSelectDialog(context, szPreviousValue, ESystemItemType::Switch);
}

dll_string AudioSwitchStateSelector(SResourceSelectorContext const& context, char const* const  szPreviousValue)
{
	return ShowSelectDialog(context, szPreviousValue, ESystemItemType::State);
}

dll_string AudioParameterSelector(SResourceSelectorContext const& context, char const* const  szPreviousValue)
{
	return ShowSelectDialog(context, szPreviousValue, ESystemItemType::Parameter);
}

dll_string AudioEnvironmentSelector(SResourceSelectorContext const& context, char const* const  szPreviousValue)
{
	return ShowSelectDialog(context, szPreviousValue, ESystemItemType::Environment);
}

dll_string AudioPreloadRequestSelector(SResourceSelectorContext const& context, char const* const  szPreviousValue)
{
	return ShowSelectDialog(context, szPreviousValue, ESystemItemType::Preload);
}

REGISTER_RESOURCE_SELECTOR("AudioTrigger", AudioTriggerSelector, "")
REGISTER_RESOURCE_SELECTOR("AudioSwitch", AudioSwitchSelector, "")
REGISTER_RESOURCE_SELECTOR("AudioSwitchState", AudioSwitchStateSelector, "")
REGISTER_RESOURCE_SELECTOR("AudioRTPC", AudioParameterSelector, "")
REGISTER_RESOURCE_SELECTOR("AudioEnvironment", AudioEnvironmentSelector, "")
REGISTER_RESOURCE_SELECTOR("AudioPreloadRequest", AudioPreloadRequestSelector, "")
} // namespace ACE
