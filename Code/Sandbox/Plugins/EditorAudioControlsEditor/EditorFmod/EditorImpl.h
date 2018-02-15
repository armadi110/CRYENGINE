// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include <IEditorImpl.h>

#include "ImplItem.h"

#include <CryAudio/IAudioSystem.h>
#include <CrySystem/File/CryFile.h>

namespace ACE
{
namespace Fmod
{
class CImplSettings final : public IImplSettings
{
public:

	CImplSettings();

	// IImplSettings
	virtual char const* GetAssetsPath() const override    { return m_assetsPath.c_str(); }
	virtual char const* GetProjectPath() const override   { return m_projectPath.c_str(); }
	virtual void        SetProjectPath(char const* szPath) override;
	virtual bool        SupportsProjects() const override { return true; }
	// ~IImplSettings

	void Serialize(Serialization::IArchive& ar);

private:

	string       m_projectPath;
	string const m_assetsPath;
};

class CEditorImpl final : public IEditorImpl
{
public:

	CEditorImpl();
	virtual ~CEditorImpl() override;

	// IEditorImpl
	virtual void            Reload(bool const preserveConnectionStatus = true) override;
	virtual IImplItem*      GetRoot() override { return &m_rootItem; }
	virtual IImplItem*      GetImplItem(CID const id) const override;
	virtual char const*     GetTypeIcon(IImplItem const* const pImplItem) const override;
	virtual string const&   GetName() const override;
	virtual string const&   GetFolderName() const override;
	virtual IImplSettings*  GetSettings() override                                                 { return &m_implSettings; }
	virtual bool            IsSystemTypeSupported(ESystemItemType const systemType) const override { return true; }
	virtual bool            IsTypeCompatible(ESystemItemType const systemType, IImplItem const* const pImplItem) const override;
	virtual ESystemItemType ImplTypeToSystemType(IImplItem const* const pImplItem) const override;
	virtual ConnectionPtr   CreateConnectionToControl(ESystemItemType const controlType, IImplItem* const pImplItem) override;
	virtual ConnectionPtr   CreateConnectionFromXMLNode(XmlNodeRef pNode, ESystemItemType const controlType) override;
	virtual XmlNodeRef      CreateXMLNodeFromConnection(ConnectionPtr const pConnection, ESystemItemType const controlType) override;
	virtual void            EnableConnection(ConnectionPtr const pConnection) override;
	virtual void            DisableConnection(ConnectionPtr const pConnection) override;
	// ~IEditorImpl

private:

	void       Clear();
	CImplItem* CreateItem(string const& name, EImplItemType const type, CImplItem* const pParent);
	CImplItem* GetItemFromPath(string const& fullpath);
	CImplItem* CreatePlaceholderFolderPath(string const& path);

	using ConnectionsMap = std::map<CID, int>;

	CImplItem           m_rootItem { "", ACE_INVALID_ID, AUDIO_SYSTEM_INVALID_TYPE };
	ItemCache           m_itemCache; // cache of the items stored by id for faster access
	ConnectionsMap      m_connectionsByID;
	CImplSettings       m_implSettings;
	CryAudio::SImplInfo m_implInfo;
	string              m_implName;
	string              m_implFolderName;
};
} // namespace Fmod
} // namespace ACE
