// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "ProjectLoader.h"

#include "EditorImpl.h"

#include <CrySystem/File/CryFile.h>
#include <CrySystem/ISystem.h>
#include <CryCore/CryCrc32.h>

namespace ACE
{
namespace SDLMixer
{
//////////////////////////////////////////////////////////////////////////
CProjectLoader::CProjectLoader(string const& assetsPath, CImplItem& rootItem)
	: m_assetsPath(assetsPath)
{
	LoadFolder("", rootItem);
}

//////////////////////////////////////////////////////////////////////////
void CProjectLoader::LoadFolder(string const& folderPath, CImplItem& parent)
{
	_finddata_t fd;
	ICryPak* const pCryPak = gEnv->pCryPak;
	intptr_t const handle = pCryPak->FindFirst(m_assetsPath + "/" + folderPath + "/*.*", &fd);

	if (handle != -1)
	{
		do
		{
			string const name = fd.name;

			if ((name != ".") && (name != "..") && !name.empty())
			{
				if (fd.attrib & _A_SUBDIR)
				{
					if (folderPath.empty())
					{
						LoadFolder(name, *CreateItem(name, folderPath, EImpltemType::Folder, parent));
					}
					else
					{
						LoadFolder(folderPath + "/" + name, *CreateItem(name, folderPath, EImpltemType::Folder, parent));
					}
				}
				else
				{
					string::size_type const posExtension = name.rfind('.');

					if (posExtension != string::npos)
					{
						if ((stricmp(name.data() + posExtension, ".mp3") == 0) ||
						    (stricmp(name.data() + posExtension, ".ogg") == 0) ||
						    (stricmp(name.data() + posExtension, ".wav") == 0))
						{
							// Create the event with the same name as the file
							CreateItem(name, folderPath, EImpltemType::Event, parent);
						}
					}
				}
			}
		}
		while (pCryPak->FindNext(handle, &fd) >= 0);

		pCryPak->FindClose(handle);
	}
}

//////////////////////////////////////////////////////////////////////////
CImplItem* CProjectLoader::CreateItem(string const& name, string const& path, EImpltemType const type, CImplItem& rootItem)
{
	CID id;
	string filePath = m_assetsPath + "/";

	if (path.empty())
	{
		id = CryAudio::StringToId(name);
		filePath += name;
	}
	else
	{
		id = CryAudio::StringToId(path + "/" + name);
		filePath += (path + "/" + name);
	}

	EImplItemFlags const flags = type == EImpltemType::Folder ? EImplItemFlags::IsContainer : EImplItemFlags::None;
	auto const pImplItem = new CImplItem(name, id, static_cast<ItemType>(type), flags, filePath);

	rootItem.AddChild(pImplItem);
	return pImplItem;
}
} // namespace SDLMixer
} // namespace ACE
