// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  Created:     25/03/2015 by Filipe amim
//  Description:
// -------------------------------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////

#include <CrySerialization/SmartPtr.h>
#include "ParamMod.h"

namespace pfx2
{

ILINE void IModifier::Serialize(Serialization::IArchive& ar)
{
	ar(m_enabled);
}

template<typename TParamModContext, typename T>
CParamMod<TParamModContext, T>::CParamMod(TType defaultValue)
	: m_baseValue(defaultValue)
{
}

template<typename TParamModContext, typename T>
void CParamMod<TParamModContext, T >::AddToComponent(CParticleComponent* pComponent, CParticleFeature* pFeature)
{
	m_modInit.clear();
	m_modUpdate.clear();
	auto it = std::remove_if(m_modifiers.begin(), m_modifiers.end(), [](const PModifier& ptr) { return !ptr; });
	m_modifiers.erase(it, m_modifiers.end());

	if (Context().GetDomain() == EMD_PerParticle)
		pComponent->InitParticles.add(pFeature);

	for (auto& pModifier : m_modifiers)
	{
		if (pModifier && pModifier->IsEnabled())
			pModifier->AddToParam(pComponent, this);
	}
}

template<typename TParamModContext, typename T>
void CParamMod<TParamModContext, T >::AddToComponent(CParticleComponent* pComponent, CParticleFeature* pFeature, EParticleDataType dataType)
{
	AddToComponent(pComponent, pFeature);
	pComponent->AddParticleData(dataType);

	if (dataType.info().hasInit && !m_modUpdate.empty())
	{
		pComponent->AddParticleData(InitType(dataType));
		if (Context().GetDomain() == EMD_PerParticle)
			pComponent->UpdateParticles.add(pFeature);
	}
}

template<typename TParamModContext, typename T>
void CParamMod<TParamModContext, T >::Serialize(Serialization::IArchive& ar)
{
	TParamModContext modContext;
	Serialization::SContext _modContext(ar, static_cast<IParamModContext*>(&modContext));
	ar(m_baseValue, "value", "^");
	ar(m_modifiers, "modifiers", "^");

	if (ar.isInput())
	{
		for (auto& pMod : m_modifiers)
		{
			if (!pMod)
				continue;
			if (TModifier* pNewMod = pMod->VersionFixReplace())
			{
				pMod.reset(pNewMod);
			}
		}
	}
}

template<typename TParamModContext, typename T>
void CParamMod<TParamModContext, T >::InitParticles(const SUpdateContext& context, EParticleDataType dataType) const
{
	CRY_PFX2_PROFILE_DETAIL;

	CParticleContainer& container = context.m_container;
	if (container.GetMaxParticles() == 0 || !container.HasData(dataType))
		return;

	TIOStream<TType> dataStream = container.GetTIOStream<TType>(dataType);
	SUpdateRange spawnRange = container.GetSpawnedRange();

	dataStream.Fill(spawnRange, m_baseValue);

	for (auto & pModifier : m_modInit)
		pModifier->Modify(context, spawnRange, dataStream, dataType, EMD_PerParticle);

	if (dataType.info().hasInit)
		container.CopyData(InitType(dataType), dataType, spawnRange);
}

template<typename TParamModContext, typename T>
void CParamMod<TParamModContext, T >::Update(const SUpdateContext& context, EParticleDataType dataType) const
{
	CParticleContainer& container = context.m_container;
	if (container.GetMaxParticles() == 0 || !container.HasData(dataType))
		return;

	TIOStream<TType> stream = container.GetTIOStream<TType>(dataType);
	for (auto& pModifier : m_modUpdate)
		pModifier->Modify(context, context.m_updateRange, stream, dataType, EMD_PerParticle);
}

template<typename TParamModContext, typename T>
void CParamMod<TParamModContext, T >::ModifyInit(const SUpdateContext& context, TType* data, SUpdateRange range) const
{
	CRY_PFX2_PROFILE_DETAIL;
	TIOStream<TType> dataStream(data);

	dataStream.Fill(range, m_baseValue);

	const EModDomain domain = Context().GetDomain();
	for (auto& pModifier : m_modInit)
		pModifier->Modify(context, range, dataStream, EParticleDataType(), domain);
}

template<typename TParamModContext, typename T>
void CParamMod<TParamModContext, T >::ModifyUpdate(const SUpdateContext& context, TType* data, SUpdateRange range) const
{
	CRY_PFX2_PROFILE_DETAIL;
	TIOStream<TType> dataStream(data);

	const EModDomain domain = Context().GetDomain();
	for (auto& pModifier : m_modUpdate)
		pModifier->Modify(context, range, dataStream, EParticleDataType(), domain);
}

template<typename TParamModContext, typename T>
TRange<typename T::TType> CParamMod<TParamModContext, T >::GetValues(const SUpdateContext& context, TType* data, SUpdateRange range, EModDomain domain, bool updating) const
{
	TRange<TType> minmax(1);
	TIOStream<TType> stream(data);

	stream.Fill(range, m_baseValue);

	for (auto & pMod : m_modInit)
	{
		if (pMod->GetDomain() >= domain)
			pMod->Modify(context, range, stream, EParticleDataType(), domain);
		else
			minmax = minmax * pMod->GetMinMax();
	}
	for (auto& pMod : m_modUpdate)
	{
		if (updating && pMod->GetDomain() >= domain)
			pMod->Modify(context, range, stream, EParticleDataType(), domain);
		else
			minmax = minmax * pMod->GetMinMax();
	}
	return minmax;
}

template<typename TParamModContext, typename T>
TRange<typename T::TType> CParamMod<TParamModContext, T >::GetValues(const SUpdateContext& context, TVarArray<TType> data, EModDomain domain, bool updating) const
{
	return GetValues(context, data.data(), SUpdateRange(0, data.size()), domain, updating);
}

template<typename TParamModContext, typename T>
void CParamMod<TParamModContext, T >::AddToInitParticles(IModifier* pMod)
{
	if (std::find(m_modInit.begin(), m_modInit.end(), pMod) == m_modInit.end())
		m_modInit.push_back(pMod);
}

template<typename TParamModContext, typename T>
void CParamMod<TParamModContext, T >::AddToUpdate(IModifier* pMod)
{
	if (std::find(m_modUpdate.begin(), m_modUpdate.end(), pMod) == m_modUpdate.end())
		m_modUpdate.push_back(pMod);
}

template<typename TParamModContext, typename T>
TRange<typename T::TType> CParamMod<TParamModContext, T >::GetValueRange(const SUpdateContext& context) const
{
	floatv curValue;
	auto minmax = GetValues(context, (float*)&curValue, SUpdateRange(0, 1), EMD_PerEffect, true);
	return minmax * (*(float*)&curValue);
}

template<typename TParamModContext, typename T>
TRange<typename T::TType> CParamMod<TParamModContext, T >::GetValueRange() const
{
	TRange<TType> minmax(m_baseValue);
	for (auto& pMod : m_modInit)
		minmax = minmax * pMod->GetMinMax();
	for (auto& pMod : m_modUpdate)
		minmax = minmax * pMod->GetMinMax();
	return minmax;
}

template<typename TParamModContext, typename T>
void pfx2::CParamMod<TParamModContext, T >::Sample(TType* samples, int numSamples) const
{
	TIOStream<TType> stream(samples);
	stream.Fill(SUpdateRange(0, numSamples), m_baseValue);

	for (auto& pModifier : m_modifiers)
	{
		if (pModifier->IsEnabled())
			pModifier->Sample(samples, numSamples);
	}
}

}
