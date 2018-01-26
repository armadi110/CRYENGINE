// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  Created:     24/09/2014 by Filipe amim
//  Description:
// -------------------------------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "ParticleContainer.h"

CRY_PFX2_DBG

namespace
{

static std::set<void*> g_allocMems;

void* ParticleAlloc(uint32 sz)
{
	void* ptr = CryModuleMemalign(sz, CRY_PFX2_PARTICLES_ALIGNMENT);
	memset(ptr, 0, sz);
	return ptr;
}

void ParticleFree(void* ptr)
{
	CryModuleMemalignFree(ptr);
}

}

namespace pfx2
{

CParticleContainer::CParticleContainer()
{
	Clear();
}

CParticleContainer::CParticleContainer(const CParticleContainer& copy)
{
	Clear();
}

CParticleContainer::~CParticleContainer()
{
	Clear();
}

void CParticleContainer::Resize(uint32 newSize)
{
	CRY_PFX2_PROFILE_DETAIL;

	newSize = CRY_PFX2_PARTICLESGROUP_ALIGN(newSize);
	if (newSize <= m_maxParticles)
		return;

	const size_t newMaxParticles = CRY_PFX2_PARTICLESGROUP_ALIGN(newSize + min(newSize >> 1, m_maxParticles));

	auto prevBuffers = m_pData;
	for (auto type : EParticleDataType::indices())
	{
		const size_t stride = type.info().typeSize;
		if (m_useData[type] && newMaxParticles > 0)
		{
			void* pNew = ParticleAlloc(newMaxParticles * stride);
			if (m_pData[type])
			{
				memcpy(pNew, m_pData[type], m_lastId * stride);
				ParticleFree(m_pData[type]);
			}
			m_pData[type] = pNew;
		}
		else
		{
			if (m_pData[type])
				ParticleFree(m_pData[type]);
			m_pData[type] = 0;
		}
	}

	m_maxParticles = newMaxParticles;
}

void CParticleContainer::ResetUsedData()
{
	m_useData.fill(false);
}

void CParticleContainer::AddParticleData(EParticleDataType type)
{
	CRY_PFX2_PROFILE_DETAIL;

	const size_t allocSize = m_maxParticles * type.info().typeSize;
	uint dim = type.info().dimension;
	for (uint i = 0; i < dim; ++i)
	{
		m_useData[type + i] = true;
		if (!m_pData[type + i])
			m_pData[type + i] = ParticleAlloc(allocSize);
		else if (type.info().needsClear)
			memset(m_pData[type + i], 0, allocSize);
	}
}

void CParticleContainer::Trim()
{
	for (auto type : EParticleDataType::indices())
	{
		if (!m_useData[type] && m_pData[type] != 0)
		{
			ParticleFree(m_pData[type]);
			m_pData[type] = 0;
		}
	}
}

void CParticleContainer::Clear()
{
	CRY_PFX2_PROFILE_DETAIL;

	for (auto i : EParticleDataType::indices())
	{
		if (m_pData[i] != 0)
			ParticleFree(m_pData[i]);
		m_pData[i] = 0;
		m_useData[i] = false;
	}
	m_maxParticles = 0;
	m_lastId = 0;
	m_firstSpawnId = 0;
	m_lastSpawnId = 0;
	m_nextSpawnId = 0;
}

template<typename TData, typename FnCopy>
ILINE void SwapToEndRemove(TParticleId lastParticleId, TConstArray<TParticleId> toRemove, TData* pData, size_t stride, FnCopy fnCopy)
{
	const uint finalSize = lastParticleId - toRemove.size();
	uint end = lastParticleId - 1;
	uint i = 0;
	uint j = toRemove.size() - 1;
	for (; i < toRemove.size() && toRemove[i] < finalSize; ++i, --end)
	{
		for (; end == toRemove[j]; --j, --end)
			;
		fnCopy(pData + stride * toRemove[i], pData + stride * end, stride * sizeof(TData));
	}
}

void SwapToEndRemove(TParticleId lastParticleId, TConstArray<TParticleId> toRemove, void* pData, size_t stride)
{
	auto copyFn = [](uint8* pDest, uint8* pSrc, uint stride)
	{
		memcpy(pDest, pSrc, stride);
	};
	SwapToEndRemove(lastParticleId, toRemove, reinterpret_cast<uint8*>(pData), stride, copyFn);
}

template<typename TData>
void SwapToEndRemove(TParticleId lastParticleId, TConstArray<TParticleId> toRemove, TData* pData)
{
	auto copyFn = [](TData* pDest, TData* pSrc, uint stride)
	{
		*pDest = *pSrc;
	};
	SwapToEndRemove(lastParticleId, toRemove, pData, 1, copyFn);
}

void CParticleContainer::AddParticle()
{
	SSpawnEntry entry = { 1 };
	AddParticles({&entry, 1});
}

void CParticleContainer::AddParticles(TConstArray<SSpawnEntry> spawnEntries)
{
	CRY_PFX2_PROFILE_DETAIL;

	uint32 newCount = 0;
	for (const auto& spawnEntry : spawnEntries)
		newCount += spawnEntry.m_count;

	if (newCount == 0)
	{
		m_firstSpawnId = m_lastSpawnId = m_lastId;
		return;
	}

	m_firstSpawnId = m_lastSpawnId = CRY_PFX2_PARTICLESGROUP_ALIGN(m_lastId);

	Resize(m_firstSpawnId + newCount);

	uint32 currentId = m_firstSpawnId;
	for (const auto& spawnEntry : spawnEntries)
	{
		const uint32 toAddCount = spawnEntry.m_count;

		if (HasData(EPDT_ParentId))
		{
			TParticleId* pParentIds = GetData<TParticleId>(EPDT_ParentId);
			for (uint32 i = currentId; i < currentId + toAddCount; ++i)
				pParentIds[i] = spawnEntry.m_parentId;
		}

		if (HasData(EPDT_SpawnId))
		{
			uint32* pSpawnIds = GetData<TParticleId>(EPDT_SpawnId);
			for (uint32 i = currentId; i < currentId + toAddCount; ++i)
				pSpawnIds[i] = m_nextSpawnId++;
		}
		else
		{
			m_nextSpawnId += toAddCount;
		}

		if (HasData(EPDT_NormalAge))
		{
			// Store newborn ages
			float age = spawnEntry.m_ageBegin;
			float* pNormalAges = GetData<float>(EPDT_NormalAge);
			for (uint32 i = currentId; i < currentId + toAddCount; ++i, age += spawnEntry.m_ageIncrement)
				pNormalAges[i] = age;
		}

		if (HasData(EPDT_SpawnFraction))
		{
			float fraction = spawnEntry.m_fractionBegin;
			float* pSpawnFractions = GetData<float>(EPDT_SpawnFraction);
			for (uint32 i = currentId; i < currentId + toAddCount; ++i, fraction += spawnEntry.m_fractionIncrement)
				pSpawnFractions[i] = min(fraction, 1.0f);
		}

		currentId += toAddCount;
		m_lastSpawnId += toAddCount;
		assert(m_lastSpawnId <= m_maxParticles);
	}
}

void CParticleContainer::RemoveParticles(TVarArray<TParticleId> toRemove, TVarArray<TParticleId> swapIds)
{
	CRY_PFX2_PROFILE_DETAIL;

	if (toRemove.empty())
		return;

	if (!swapIds.empty())
		MakeSwapIds(toRemove, swapIds);

	const TParticleId lastParticleId = GetLastParticleId();

	for (auto dataTypeId : EParticleDataType::indices())
	{
		if (!m_useData[dataTypeId])
			continue;

		void* pData = m_pData[dataTypeId];
		const uint stride = dataTypeId.info().typeSize;
		switch (stride)
		{
		case 1:
			SwapToEndRemove(lastParticleId, toRemove, reinterpret_cast<uint8*>(pData));
			break;
		case 4:
			SwapToEndRemove(lastParticleId, toRemove, reinterpret_cast<uint32*>(pData));
			break;
		case 8:
			SwapToEndRemove(lastParticleId, toRemove, reinterpret_cast<uint64*>(pData));
			break;
		default:
			SwapToEndRemove(lastParticleId, toRemove, pData, stride);
		}
	}

	m_lastId -= toRemove.size();
}

void CParticleContainer::MakeSwapIds(TVarArray<TParticleId> toRemove, TVarArray<TParticleId> swapIds)
{
	CRY_PFX2_PROFILE_DETAIL;

	const TParticleId lastParticleId = GetLastParticleId();
	const uint finalSize = lastParticleId - toRemove.size();
	CRY_PFX2_ASSERT(uint(swapIds.size()) >= lastParticleId);    // swapIds not big enough

	for (TParticleId j = 0; j < lastParticleId; ++j)
		swapIds[j] = j;

	SwapToEndRemove(lastParticleId, toRemove, swapIds.data());
	for (uint i = finalSize; i < lastParticleId; ++i)
		swapIds[i] = gInvalidId;

	for (uint i = 0; i < finalSize; ++i)
	{
		TParticleId v0 = swapIds[i];
		TParticleId v1 = swapIds[v0];
		swapIds[i] = v1;
		swapIds[v0] = i;
	}
}

void CParticleContainer::ResetSpawnedParticles()
{
	CRY_PFX2_PROFILE_DETAIL;

	CRY_PFX2_ASSERT(m_firstSpawnId >= m_lastId);

	const uint numSpawn = GetNumSpawnedParticles();
	const uint gapSize = m_firstSpawnId - m_lastId;
	const uint movingId = m_lastSpawnId - min(gapSize, numSpawn);
	if (gapSize != 0)
	{
		for (auto dataTypeId : EParticleDataType::indices())
		{
			const size_t stride = dataTypeId.info().typeSize;
			byte* pBytes = reinterpret_cast<byte*>(m_pData[dataTypeId]);
			if (!pBytes)
				continue;
			memcpy(pBytes + m_lastId * stride, pBytes + movingId * stride, gapSize * stride);
		}
	}

	m_lastId = m_lastSpawnId - gapSize;
	m_firstSpawnId = m_lastSpawnId = m_lastId;
}

void CParticleContainer::RemoveNewBornFlags()
{
	void* pBegin = m_pData[EPDT_State];
	void* pCursor = pBegin;

	const uint8 mask = ~ESB_NewBorn;
	const uint32 flag32 = (mask << 24) | (mask << 16) | (mask << 8) | mask;
#ifdef CRY_PFX2_USE_SSE
	const u32v4 flag128 = convert<u32v4>(flag32);
	u32v4* pBegin128 = static_cast<u32v4*>(pCursor);
	u32v4* pEnd128 = static_cast<u32v4*>(pBegin) + m_lastId / sizeof(u32v4);
	for (; pBegin128 != pEnd128; ++pBegin128)
		*pBegin128 &= flag128;
	pCursor = pEnd128;
#endif

	uint32* pBegin32 = static_cast<uint32*>(pCursor);
	uint32* pEnd32 = static_cast<uint32*>(pBegin) + m_lastId / sizeof(uint32);
	for (; pBegin32 != pEnd32; ++pBegin32)
		*pBegin32 &= flag32;
	pCursor = pEnd32;

	uint8* pBegin8 = static_cast<uint8*>(pCursor);
	uint8* pEnd8 = static_cast<uint8*>(pBegin) + m_lastId / sizeof(uint8);
	for (; pBegin8 != pEnd8; ++pBegin8)
		*pBegin8 &= mask;
}

}
