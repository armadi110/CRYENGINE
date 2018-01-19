// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include "SystemAssets.h"
#include <CryAudio/IAudioSystem.h>

namespace ACE
{
class CSystemAssetsManager;

// This file is deprecated and only used for backwards compatibility. It will be removed before March 2019.
class CAudioControlsLoader
{
public:

	CAudioControlsLoader(CSystemAssetsManager* const pAssetsManager);
	std::set<string> GetLoadedFilenamesList();
	void             LoadAll(bool const loadOnlyDefaultControls = false);
	void             LoadControls(string const& folderPath);
	void             LoadScopes();
	EErrorCode       GetErrorCodeMask() const { return m_errorCodeMask; }

	

private:

	using SwitchStates = std::vector<char const*>;

	void            LoadAllLibrariesInFolder(string const& folderPath, string const& level);
	void            LoadControlsLibrary(XmlNodeRef const pRoot, string const& filepath, string const& level, string const& filename, uint32 const version);
	CSystemControl* LoadControl(XmlNodeRef const pNode, Scope const scope, uint32 const version, CSystemAsset* const pParentItem);
	CSystemControl* LoadDefaultControl(XmlNodeRef const pNode, Scope const scope, uint32 const version, CSystemAsset* const pParentItem);

	void            LoadPreloadConnections(XmlNodeRef const pNode, CSystemControl* const pControl, uint32 const version);
	void            LoadConnections(XmlNodeRef const root, CSystemControl* const pControl);

	void            LoadScopesImpl(string const& path);

	void            LoadEditorData(XmlNodeRef const pEditorDataNode, CSystemAsset& library);
	void            LoadLibraryEditorData(XmlNodeRef const pLibraryNode, CSystemAsset& library);
	void            LoadAllFolders(XmlNodeRef const pFoldersNode, CSystemAsset& library);
	void            LoadFolderData(XmlNodeRef const pFolderNode, CSystemAsset& parentAsset);
	void            LoadAllControlsEditorData(XmlNodeRef const pControlsNode);
	void            LoadControlsEditorData(XmlNodeRef const pParentNode);

	CSystemAsset*   AddUniqueFolderPath(CSystemAsset* pParent, QString const& path);

	static string const         s_controlsLevelsFolder;
	static string const         s_assetsFolderPath;

	CSystemAssetsManager* const m_pAssetsManager;
	std::set<string>            m_loadedFilenames;
	EErrorCode                  m_errorCodeMask;
	bool                        m_loadOnlyDefaultControls;

	std::set<string>            m_defaultTriggerNames{ CryAudio::s_szGetFocusTriggerName, CryAudio::s_szLoseFocusTriggerName, CryAudio::s_szMuteAllTriggerName, CryAudio::s_szUnmuteAllTriggerName };
	std::set<string>            m_defaultParameterNames{ CryAudio::s_szAbsoluteVelocityParameterName, "object_speed", CryAudio::s_szRelativeVelocityParameterName, "object_doppler" };
};
} // namespace ACE
