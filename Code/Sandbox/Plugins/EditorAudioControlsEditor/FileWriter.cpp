// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "FileWriter.h"

#include "SystemAssetsManager.h"
#include "ImplementationManager.h"

#include <IEditorImpl.h>
#include <IImplItem.h>
#include <CryString/StringUtils.h>
#include <CrySystem/File/CryFile.h>
#include <CrySystem/ISystem.h>
#include <IEditor.h>
#include <QtUtil.h>
#include <ConfigurationManager.h>

namespace ACE
{
uint32 const CFileWriter::s_currentFileVersion = 3;

//////////////////////////////////////////////////////////////////////////
string TypeToTag(ESystemItemType const eType)
{
	string tag = "";

	switch (eType)
	{
	case ESystemItemType::Parameter:
		tag = CryAudio::s_szParameterTag;
		break;
	case ESystemItemType::Trigger:
		tag = CryAudio::s_szTriggerTag;
		break;
	case ESystemItemType::Switch:
		tag = CryAudio::s_szSwitchTag;
		break;
	case ESystemItemType::State:
		tag = CryAudio::s_szStateTag;
		break;
	case ESystemItemType::Preload:
		tag = CryAudio::s_szPreloadRequestTag;
		break;
	case ESystemItemType::Environment:
		tag = CryAudio::s_szEnvironmentTag;
		break;
	default:
		tag = "";
		break;
	}

	return tag;
}

//////////////////////////////////////////////////////////////////////////
CFileWriter::CFileWriter(CSystemAssetsManager const& pAssetsManager, std::set<string>& previousLibraryPaths)
	: m_assetsManager(pAssetsManager)
	, m_previousLibraryPaths(previousLibraryPaths)
{
}

//////////////////////////////////////////////////////////////////////////
void CFileWriter::WriteAll()
{
	size_t const libCount = m_assetsManager.GetLibraryCount();

	for (size_t i = 0; i < libCount; ++i)
	{
		CSystemLibrary& library = *m_assetsManager.GetLibrary(i);
		WriteLibrary(library);
		library.SetModified(false);
	}

	// Delete libraries that don't exist anymore from disk
	std::set<string> librariesToDelete;
	std::set_difference(m_previousLibraryPaths.begin(), m_previousLibraryPaths.end(), m_foundLibraryPaths.begin(), m_foundLibraryPaths.end(),
	                    std::inserter(librariesToDelete, librariesToDelete.begin()));

	for (auto const& name : librariesToDelete)
	{
		string const fullFilePath = PathUtil::GetGameFolder() + "/" + name;
		DeleteLibraryFile(fullFilePath);
	}

	m_previousLibraryPaths = m_foundLibraryPaths;
}

//////////////////////////////////////////////////////////////////////////
void CFileWriter::WriteLibrary(CSystemLibrary const& library)
{
	if (library.IsModified())
	{
		LibraryStorage libraryXmlNodes;
		size_t const itemCount = library.ChildCount();

		if (library.IsDefaultControl())
		{
			for (size_t i = 0; i < itemCount; ++i)
			{
				CSystemAsset* const pAsset = library.GetChild(i);

				if ((pAsset != nullptr) && !pAsset->IsInternalControl())
				{
					WriteItem(pAsset, "", libraryXmlNodes);
				}
			}
		}
		else
		{
			for (size_t i = 0; i < itemCount; ++i)
			{
				WriteItem(library.GetChild(i), "", libraryXmlNodes);
			}
		}

		// If empty, force it to write an empty library at the root
		if (libraryXmlNodes.empty())
		{
			libraryXmlNodes[Utils::GetGlobalScope()].isDirty = true;
		}

		for (auto const& libraryPair : libraryXmlNodes)
		{
			string libraryPath = m_assetsManager.GetConfigFolderPath();
			Scope const scope = libraryPair.first;

			if (scope == Utils::GetGlobalScope())
			{
				// no scope, file at the root level
				libraryPath += library.GetName();
			}
			else
			{
				// with scope, inside level folder
				libraryPath += CryAudio::s_szLevelsFolderName;
				libraryPath += "/" + m_assetsManager.GetScopeInfo(scope).name + "/" + library.GetName();
			}

			m_foundLibraryPaths.insert(libraryPath.MakeLower() + ".xml");

			SLibraryScope const& libScope = libraryPair.second;

			if (libScope.isDirty)
			{
				XmlNodeRef pFileNode = GetISystem()->CreateXmlNode(CryAudio::s_szRootNodeTag);
				pFileNode->setAttr(CryAudio::s_szNameAttribute, library.GetName());
				pFileNode->setAttr(CryAudio::s_szVersionAttribute, s_currentFileVersion);

				int const numTypes = static_cast<int>(ESystemItemType::NumTypes);

				for (int i = 0; i < numTypes; ++i)
				{
					if (i != static_cast<int>(ESystemItemType::State))   // switch_states are written inside the switches
					{
						XmlNodeRef node = libScope.GetXmlNode((ESystemItemType)i);

						if ((node != nullptr) && (node->getChildCount() > 0))
						{
							pFileNode->addChild(node);
						}
					}
				}

				// Editor data
				XmlNodeRef const pEditorData = pFileNode->createNode(CryAudio::s_szEditorDataTag);

				if (pEditorData != nullptr)
				{
					XmlNodeRef const pLibraryNode = pEditorData->createNode(s_szLibraryNodeTag);

					if (pLibraryNode != nullptr)
					{
						WriteLibraryEditorData(library, pLibraryNode);
						pEditorData->addChild(pLibraryNode);
					}

					XmlNodeRef const pFoldersNode = pEditorData->createNode(s_szFoldersNodeTag);

					if (pFoldersNode != nullptr)
					{
						WriteFolderEditorData(library, pFoldersNode);
						pEditorData->addChild(pFoldersNode);
					}

					XmlNodeRef const pControlsNode = pEditorData->createNode(s_szControlsNodeTag);

					if (pControlsNode != nullptr)
					{
						WriteControlsEditorData(library, pControlsNode);
						pEditorData->addChild(pControlsNode);
					}

					pFileNode->addChild(pEditorData);
				}

				string const fullFilePath = PathUtil::GetGameFolder() + "/" + libraryPath + ".xml";
				DWORD const fileAttributes = GetFileAttributesA(fullFilePath.c_str());

				if ((fileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
				{
					// file is read-only
					SetFileAttributesA(fullFilePath.c_str(), FILE_ATTRIBUTE_NORMAL);
				}

				// TODO: Check out in source control.
				pFileNode->saveToFile(fullFilePath);
			}
		}
	}
	else
	{
		std::unordered_set<Scope> scopes;
		size_t const numChildren = library.ChildCount();

		for (size_t i = 0; i < numChildren; ++i)
		{
			CSystemAsset* const pItem = library.GetChild(i);
			GetScopes(pItem, scopes);
		}

		for (auto const scope : scopes)
		{
			string libraryPath = m_assetsManager.GetConfigFolderPath();

			if (scope == Utils::GetGlobalScope())
			{
				// no scope, file at the root level
				libraryPath += library.GetName();
			}
			else
			{
				// with scope, inside level folder
				libraryPath += CryAudio::s_szLevelsFolderName;
				libraryPath += "/" + m_assetsManager.GetScopeInfo(scope).name + "/" + library.GetName();
			}

			m_foundLibraryPaths.insert(libraryPath.MakeLower() + ".xml");
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CFileWriter::WriteItem(CSystemAsset* const pItem, string const& path, LibraryStorage& library)
{
	if (pItem != nullptr)
	{
		if (pItem->GetType() == ESystemItemType::Folder)
		{
			size_t const itemCount = pItem->ChildCount();

			for (size_t i = 0; i < itemCount; ++i)
			{
				// Use forward slash only to ensure cross platform compatibility.
				string newPath = path.empty() ? "" : path + "/";
				newPath += pItem->GetName();
				WriteItem(pItem->GetChild(i), newPath, library);
			}

			pItem->SetModified(false);
		}
		else
		{
			CSystemControl* const pControl = static_cast<CSystemControl*>(pItem);

			if (pControl != nullptr)
			{
				SLibraryScope& scope = library[pControl->GetScope()];
				scope.isDirty = true;
				WriteControlToXML(scope.GetXmlNode(pControl->GetType()), pControl, path);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CFileWriter::GetScopes(CSystemAsset const* const pItem, std::unordered_set<Scope>& scopes)
{
	if (pItem->GetType() == ESystemItemType::Folder)
	{
		size_t const numChildren = pItem->ChildCount();

		for (size_t i = 0; i < numChildren; ++i)
		{
			GetScopes(pItem->GetChild(i), scopes);
		}
	}
	else
	{
		CSystemControl const* const pControl = static_cast<CSystemControl const*>(pItem);

		if (pControl != nullptr)
		{
			scopes.insert(pControl->GetScope());
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CFileWriter::WriteControlToXML(XmlNodeRef const pNode, CSystemControl* const pControl, string const& path)
{
	ESystemItemType const type = pControl->GetType();
	XmlNodeRef const pChildNode = pNode->createNode(TypeToTag(type));
	pChildNode->setAttr(CryAudio::s_szNameAttribute, pControl->GetName());

	if (!path.empty())
	{
		pChildNode->setAttr(s_szPathAttribute, path);
	}

	if (type == ESystemItemType::Trigger)
	{
		float const radius = pControl->GetRadius();

		if (radius > 0.0f)
		{
			pChildNode->setAttr(CryAudio::s_szRadiusAttribute, radius);
		}
	}

	if (type == ESystemItemType::Switch)
	{
		size_t const size = pControl->ChildCount();

		for (size_t i = 0; i < size; ++i)
		{
			CSystemAsset* const pItem = pControl->GetChild(i);

			if ((pItem != nullptr) && (pItem->GetType() == ESystemItemType::State))
			{
				WriteControlToXML(pChildNode, static_cast<CSystemControl*>(pItem), "");
			}
		}
	}
	else if (type == ESystemItemType::Preload)
	{
		if (pControl->IsAutoLoad())
		{
			pChildNode->setAttr(CryAudio::s_szTypeAttribute, CryAudio::s_szDataLoadType);
		}

		std::vector<dll_string> const& platforms = GetIEditor()->GetConfigurationManager()->GetPlatformNames();
		size_t const numPlatforms = platforms.size();

		for (size_t i = 0; i < numPlatforms; ++i)
		{
			XmlNodeRef pFileNode = pChildNode->createNode(CryAudio::s_szPlatformTag);
			pFileNode->setAttr(CryAudio::s_szNameAttribute, platforms[i].c_str());
			WriteConnectionsToXML(pFileNode, pControl, i);

			if (pFileNode->getChildCount() > 0)
			{
				pChildNode->addChild(pFileNode);
			}
		}
	}
	else
	{
		WriteConnectionsToXML(pChildNode, pControl);
	}

	pControl->SetModified(false);
	pNode->addChild(pChildNode);
}

//////////////////////////////////////////////////////////////////////////
void CFileWriter::WriteConnectionsToXML(XmlNodeRef const pNode, CSystemControl* const pControl, int const platformIndex /*= -1*/)
{
	size_t const numConnections = pControl->GetConnectionCount();

	for (size_t i = 0; i < numConnections; ++i)
	{
		ConnectionPtr const pConnection = pControl->GetConnectionAt(i);

		if (pConnection != nullptr)
		{
			if ((pControl->GetType() != ESystemItemType::Preload) || (pConnection->IsPlatformEnabled(static_cast<PlatformIndexType>(platformIndex))))
			{
				XmlNodeRef const pChild = g_pEditorImpl->CreateXMLNodeFromConnection(pConnection, pControl->GetType());

				if (pChild != nullptr)
				{
					// Don't add identical nodes!
					bool shouldAddNode = true;
					int const numNodeChilds = pNode->getChildCount();

					for (int j = 0; j < numNodeChilds; ++j)
					{
						XmlNodeRef const pTempNode = pNode->getChild(j);

						if ((pTempNode != nullptr) && (string(pTempNode->getTag()) == string(pChild->getTag())))
						{
							int const numAttributes1 = pTempNode->getNumAttributes();
							int const numAttributes2 = pChild->getNumAttributes();

							if (numAttributes1 == numAttributes2)
							{
								shouldAddNode = false;
								char const* key1 = nullptr;
								char const* val1 = nullptr;
								char const* key2 = nullptr;
								char const* val2 = nullptr;

								for (int k = 0; k < numAttributes1; ++k)
								{
									pTempNode->getAttributeByIndex(k, &key1, &val1);
									pChild->getAttributeByIndex(k, &key2, &val2);

									if ((_stricmp(key1, key2) != 0) || (_stricmp(val1, val2) != 0))
									{
										shouldAddNode = true;
										break;
									}
								}

								if (!shouldAddNode)
								{
									break;
								}
							}
						}
					}

					if (shouldAddNode)
					{
						pNode->addChild(pChild);
					}
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CFileWriter::DeleteLibraryFile(string const& filepath)
{
	// TODO: Mark for delete in source control.
	DWORD const fileAttributes = GetFileAttributesA(filepath.c_str());

	if ((fileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
	{
		// file is read-only
		SetFileAttributesA(filepath.c_str(), FILE_ATTRIBUTE_NORMAL);
	}

	if ((fileAttributes == INVALID_FILE_ATTRIBUTES) || !DeleteFile(filepath.c_str()))
	{
		CryWarning(VALIDATOR_MODULE_EDITOR, VALIDATOR_ERROR, "[Audio Controls Editor] Failed to delete file %s", filepath);
	}
}

//////////////////////////////////////////////////////////////////////////
void CFileWriter::WriteLibraryEditorData(CSystemAsset const& library, XmlNodeRef const pParentNode) const
{
	string const description = library.GetDescription();

	if (!description.IsEmpty() && !library.IsDefaultControl())
	{
		pParentNode->setAttr(s_szDescriptionAttribute, description);
	}
}

//////////////////////////////////////////////////////////////////////////
void CFileWriter::WriteFolderEditorData(CSystemAsset const& library, XmlNodeRef const pParentNode) const
{
	size_t const itemCount = library.ChildCount();

	for (size_t i = 0; i < itemCount; ++i)
	{
		CSystemAsset const* const pAsset = library.GetChild(i);

		if (pAsset->GetType() == ESystemItemType::Folder)
		{
			XmlNodeRef const pFolderNode = pParentNode->createNode(s_szFolderTag);

			if (pFolderNode != nullptr)
			{
				pFolderNode->setAttr(CryAudio::s_szNameAttribute, pAsset->GetName());
				string const description = pAsset->GetDescription();

				if (!description.IsEmpty() && !pAsset->IsDefaultControl())
				{
					pFolderNode->setAttr(s_szDescriptionAttribute, description);
				}

				WriteFolderEditorData(*pAsset, pFolderNode);
				pParentNode->addChild(pFolderNode);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CFileWriter::WriteControlsEditorData(CSystemAsset const& parentAsset, XmlNodeRef const pParentNode) const
{
	size_t const itemCount = parentAsset.ChildCount();

	for (size_t i = 0; i < itemCount; ++i)
	{
		CSystemAsset const& asset = *parentAsset.GetChild(i);
		ESystemItemType const type = asset.GetType();
		string const nodeName = TypeToTag(type);

		if (!nodeName.IsEmpty())
		{
			XmlNodeRef const pControlNode = pParentNode->createNode(nodeName);

			if (pControlNode != nullptr)
			{
				string const description = asset.GetDescription();

				if (!description.IsEmpty() && !asset.IsDefaultControl())
				{
					pControlNode->setAttr(CryAudio::s_szNameAttribute, asset.GetName());
					pControlNode->setAttr(s_szDescriptionAttribute, description);
					pParentNode->addChild(pControlNode);
				}
			}
		}

		if ((type == ESystemItemType::Folder) || (type == ESystemItemType::Switch))
		{
			WriteControlsEditorData(asset, pParentNode);
		}
	}
}
} // namespace ACE
