// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include "SystemTypes.h"

#include <CryString/CryString.h>
#include <CrySerialization/IArchive.h>
#include <CrySerialization/STL.h>
#include <CrySystem/ISystem.h>
#include <CrySandbox/CrySignal.h>

namespace ACE
{
using PlatformIndexType = uint16;

class CImplConnection
{
public:

	CImplConnection(CID const id)
		: m_id(id)
	{
		m_configurationsMask = std::numeric_limits<PlatformIndexType>::max();
	}

	virtual ~CImplConnection() = default;

	CID          GetID() const                          { return m_id; }
	virtual bool HasProperties() const                  { return false; }
	virtual void Serialize(Serialization::IArchive& ar) {};

	void         EnableForPlatform(PlatformIndexType const platformIndex, bool const isEnabled)
	{
		if (isEnabled)
		{
			m_configurationsMask |= 1 << platformIndex;
		}
		else
		{
			m_configurationsMask &= ~(1 << platformIndex);
		}

		SignalConnectionChanged();
	}

	bool IsPlatformEnabled(PlatformIndexType const platformIndex)
	{
		return (m_configurationsMask & (1 << platformIndex)) > 0;
	}

	void ClearPlatforms()
	{
		if (m_configurationsMask != 0)
		{
			m_configurationsMask = 0;
			SignalConnectionChanged();
		}
	}

	CCrySignal<void()> SignalConnectionChanged;

private:

	CID const         m_id;
	PlatformIndexType m_configurationsMask;
};
} // namespace ACE
