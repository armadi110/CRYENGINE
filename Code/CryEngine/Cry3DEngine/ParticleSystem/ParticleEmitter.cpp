// Copyright 2014-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  Created:     23/09/2014 by Filipe amim
//  Description:
// -------------------------------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "ParticleEmitter.h"
#include "ParticleComponentRuntime.h"
#include "ParticleManager.h"
#include "ParticleSystem.h"
#include "ParticleDataTypes.h"
#include "ParticleFeature.h"
#include "FogVolumeRenderNode.h"
#include <CryEntitySystem/IEntitySystem.h>

namespace pfx2
{

//////////////////////////////////////////////////////////////////////////
// CParticleEmitter

CParticleEmitter::CParticleEmitter(CParticleEffect* pEffect, uint emitterId)
	: m_pEffect(pEffect)
	, m_pEffectOriginal(pEffect)
	, m_registered(false)
	, m_reRegister(false)
	, m_realBounds(AABB::RESET)
	, m_bounds(AABB::RESET)
	, m_viewDistRatio(1.0f)
	, m_active(false)
	, m_alive(true)
	, m_location(IDENTITY)
	, m_emitterEditVersion(-1)
	, m_effectEditVersion(-1)
	, m_entityOwner(nullptr)
	, m_entitySlot(-1)
	, m_emitterGeometrySlot(-1)
	, m_time(0.0f)
	, m_deltaTime(0.0f)
	, m_primeTime(0.0f)
	, m_timeCreated(0.0f)
	, m_timeLastRendered(0.0f)
	, m_initialSeed(0)
	, m_emitterId(emitterId)
{
	m_currentSeed = m_initialSeed;
	m_nInternalFlags |= IRenderNode::REQUIRES_FORWARD_RENDERING;
	m_nInternalFlags |= IRenderNode::REQUIRES_NEAREST_CUBEMAP;

	auto smoothstep = [](float x) { return x*x*(3 - 2 * x); };
	auto contrast = [](float x) { return x * 0.75f + 0.25f; };
	float x = smoothstep(cry_random(0.0f, 1.0f));
	float r = contrast(smoothstep(max(0.0f, -x * 2 + 1)));
	float g = contrast(smoothstep(1 - abs((x - 0.5f) * 2)));
	float b = contrast(smoothstep(max(0.0f, x * 2 - 1)));
	m_profilerColor = ColorF(r, g, b);

	if (m_pEffect)
		m_attributeInstance.Reset(m_pEffect->GetAttributeTable());
}

CParticleEmitter::~CParticleEmitter()
{
	Unregister();
	gEnv->p3DEngine->FreeRenderNodeState(this);
	ResetRenderObjects();
}

EERType CParticleEmitter::GetRenderNodeType()
{
	return eERType_ParticleEmitter;
}

const char* CParticleEmitter::GetEntityClassName() const
{
	return "ParticleEmitter";
}

const char* CParticleEmitter::GetName() const
{
	return "";
}

void CParticleEmitter::SetMatrix(const Matrix34& mat)
{
	if (mat.IsValid())
		SetLocation(QuatTS(mat));
}

Vec3 CParticleEmitter::GetPos(bool bWorldOnly) const
{
	return m_location.t;
}

void CParticleEmitter::Render(const struct SRendParams& rParam, const SRenderingPassInfo& passInfo)
{
	CRY_PFX2_PROFILE_DETAIL;
	PARTICLE_LIGHT_PROFILER();

	if (!passInfo.RenderParticles() || passInfo.IsRecursivePass())
		return;
	if (passInfo.IsShadowPass())
		return;

	m_timeLastRendered = m_time;

	CLightVolumesMgr& lightVolumeManager = m_p3DEngine->GetLightVolumeManager();
	SRenderContext renderContext(rParam, passInfo);
	renderContext.m_lightVolumeId = lightVolumeManager.RegisterVolume(GetPos(), GetBBox().GetRadius() * 0.5f, rParam.nClipVolumeStencilRef, passInfo);
	renderContext.m_distance = GetPos().GetDistance(passInfo.GetCamera().GetPosition());
	ColorF fogVolumeContrib;
	CFogVolumeRenderNode::TraceFogVolumes(GetPos(), fogVolumeContrib, passInfo);
	renderContext.m_fogVolumeId = passInfo.GetIRenderView()->PushFogVolumeContribution(fogVolumeContrib, passInfo);
	
	auto& stats = GetPSystem()->GetThreadData().statsCPU;
	stats.emitters.rendered ++;
	for (auto& pRuntime : m_componentRuntimes)
	{
		if (pRuntime->GetComponent()->IsVisible() && passInfo.GetCamera().IsAABBVisible_E(pRuntime->GetBounds()))
		{
			pRuntime->GetComponent()->RenderAll(this, pRuntime, renderContext);
			stats.components.rendered ++;
		}
	}
}

void CParticleEmitter::Update()
{
	CRY_PFX2_PROFILE_DETAIL;

	m_deltaTime = gEnv->pTimer->GetFrameTime() * GetTimeScale();
	m_deltaTime = max(m_deltaTime, m_primeTime);
	m_primeTime = 0.0f;
	m_time += m_deltaTime;
	++m_currentSeed;

	if (m_active && m_effectEditVersion != m_pEffectOriginal->GetEditVersion() + m_emitterEditVersion)
	{
		m_pEffectOriginal->Compile();
		m_attributeInstance.Reset(m_pEffectOriginal->GetAttributeTable());
		UpdateRuntimes();
	}

	UpdateFromEntity();

	for (auto const& elem: GetCEffect()->MainPreUpdate)
	{
		if (auto pRuntime = GetRuntimeFor(elem.pComponent))
			elem.pFeature->MainPreUpdate(pRuntime);
	}

	if (m_reRegister)
	{
		Unregister();
		Register();
		m_reRegister = false;
	}
}

namespace Bounds
{
	static const float RoundOutPrecision = 1 / 32.0f;
	static const float ShrinkThreshold   = 0.5f;
}

void CParticleEmitter::UpdateBoundingBox(const float frameTime)
{
	m_reRegister = false;

	if (m_realBounds.IsReset())
	{
		m_bounds.Reset();
		m_reRegister = true;
		return;
	}

	if (!m_registered)
		m_reRegister = true;
	else if (!m_bounds.ContainsBox(m_realBounds))
		m_reRegister = true;
	else if (m_realBounds.GetVolume() < m_bounds.GetVolume() * Bounds::ShrinkThreshold)
		m_reRegister = true;
	if (m_reRegister)
	{
		// Expand bounds to rounded borders
		m_bounds = m_realBounds;
		for (int a = 0; a < 3; ++a)
		{
			const float sideLen = (m_bounds.max[a] - m_bounds.min[a]);
			if (sideLen > 0.0f)
			{
				const float round = exp2(ceil(log2(sideLen))) * Bounds::RoundOutPrecision;
				const float invRound = 1.0f / round;

				m_bounds.min[a] = floor(m_bounds.min[a] * invRound) * round;
				m_bounds.max[a] = ceil(m_bounds.max[a] * invRound) * round;
			}
		}
	}
}

void CParticleEmitter::UpdateAll()
{
	// Update all components, and accumulate bounds and stats
	CRY_PFX2_PROFILE_DETAIL;

	m_alive = false;
	m_realBounds = AABB::RESET;
	for (auto& pRuntime : m_componentRuntimes)
	{
		pRuntime->UpdateAll();
		m_alive = m_alive || pRuntime->IsAlive();
		m_realBounds.Add(pRuntime->GetBounds());
	}

	PostUpdate();
	UpdateBoundingBox(m_deltaTime);
	CRY_PFX2_ASSERT(IsAlive() || !HasBounds());
}

void CParticleEmitter::DebugRender(const SRenderingPassInfo& passInfo) const
{
	if (!GetRenderer())
		return;

	IRenderAuxGeom* pRenderAux = gEnv->pRenderer->GetIRenderAuxGeom();

	if (m_bounds.IsReset())
		return;

	const bool visible = (m_timeLastRendered == m_time);

	const float maxDist = non_const(*this).GetMaxViewDist();
	const float dist = (GetPos() - passInfo.GetCamera().GetPosition()).GetLength();
	const float alpha = sqr(crymath::saturate(1.0f - dist / maxDist));
	const float ac = alpha * 0.75f + 0.25f;
	const ColorF alphaColor(ac, ac, ac, alpha);

	const ColorB emitterColor = ColorF(1, visible, visible) * alphaColor;
	pRenderAux->DrawAABB(m_bounds, false, emitterColor, eBBD_Faceted);
	if (visible)
	{
		const ColorB componentColor = ColorF(1, 0.5, 0) * alphaColor;
		for (auto& pRuntime : m_componentRuntimes)
			pRenderAux->DrawAABB(pRuntime->GetBounds(), false, componentColor, eBBD_Faceted);

		ColorF labelColor = alphaColor;
		IRenderAuxText::DrawLabelEx(m_location.t, 1.5f, (float*)&labelColor, true, true, m_pEffect->GetShortName());
	}
}

void CParticleEmitter::PostUpdate()
{
	m_parentContainer.GetIOVec3Stream(EPVF_Velocity).Store(TParticleGroupId(0), Vec3v(ZERO));
	m_parentContainer.GetIOVec3Stream(EPVF_AngularVelocity).Store(TParticleGroupId(0), Vec3v(ZERO));
}

IPhysicalEntity* CParticleEmitter::GetPhysics() const
{
	return 0;
}

void CParticleEmitter::SetPhysics(IPhysicalEntity*)
{
}

void CParticleEmitter::SetMaterial(IMaterial* pMat)
{
}

IMaterial* CParticleEmitter::GetMaterial(Vec3* pHitPos) const
{
	return 0;
}

IMaterial* CParticleEmitter::GetMaterialOverride()
{
	return 0;
}

float CParticleEmitter::GetMaxViewDist()
{
	IRenderer* pRenderer = GetRenderer();
	const float angularDensity =
		(pRenderer ? GetPSystem()->GetMaxAngularDensity(GetISystem()->GetViewCamera()) : 1080.0f)
		* m_viewDistRatio;

	float maxViewDist = 0.0f;
	for (auto& pRuntime : m_componentRuntimes)
	{
		const auto* pComponent = pRuntime->GetComponent();
		if (pComponent->IsEnabled())
		{
			const auto& params = pComponent->GetComponentParams();
			const float sizeDist = params.m_maxParticleSize * angularDensity * params.m_visibility.m_viewDistanceMultiple;
			const float dist = min(sizeDist, +params.m_visibility.m_maxCameraDistance);
			maxViewDist = max(maxViewDist, dist);
		}
	}
	return maxViewDist;
}

void CParticleEmitter::Precache()
{
}

void CParticleEmitter::GetMemoryUsage(ICrySizer* pSizer) const
{
}

const AABB CParticleEmitter::GetBBox() const
{
	if (m_bounds.IsReset())
		return AABB(m_location.t, 0.05f);
	else
		return m_bounds;
}

void CParticleEmitter::FillBBox(AABB& aabb)
{
	aabb = GetBBox();
}

void CParticleEmitter::SetBBox(const AABB& WSBBox)
{
}

void CParticleEmitter::OffsetPosition(const Vec3& delta)
{
}

bool CParticleEmitter::IsAllocatedOutsideOf3DEngineDLL()
{
	return false;
}

void CParticleEmitter::SetViewDistRatio(int nViewDistRatio)
{
	IRenderNode::SetViewDistRatio(nViewDistRatio);
	m_viewDistRatio = GetViewDistRatioNormilized();
}

void CParticleEmitter::ReleaseNode(bool bImmediate)
{
	Activate(false);
	Unregister();
}

void CParticleEmitter::InitSeed()
{
	const int forcedSeed = GetCVars()->e_ParticlesForceSeed;
	if (m_spawnParams.nSeed != -1)
	{
		m_initialSeed = uint32(m_spawnParams.nSeed);
		m_time = 0.0f;
	}
	else if (forcedSeed != 0)
	{
		m_initialSeed = forcedSeed;
		m_time = 0.0f;
	}
	else
	{
		m_initialSeed = cry_random_uint32();
		m_time = gEnv->pTimer->GetCurrTime();
	}
	m_timeCreated = m_time;
	m_timeLastRendered = m_time;
	m_currentSeed = m_initialSeed;
}

void CParticleEmitter::Activate(bool activate)
{
	if (!m_pEffect || activate == m_active)
		return;

	if (activate)
	{
		m_parentContainer.AddParticleData(EPVF_Position);
		m_parentContainer.AddParticleData(EPQF_Orientation);
		m_parentContainer.AddParticleData(EPVF_Velocity);
		m_parentContainer.AddParticleData(EPVF_AngularVelocity);
		m_parentContainer.AddParticleData(EPDT_NormalAge);

		InitSeed();

		UpdateRuntimes();

		if (m_spawnParams.bPrime)
		{
			if (!(GetCVars()->e_ParticlesDebug & AlphaBit('p')))
				m_primeTime = m_pEffect->GetEquilibriumTime();
		}
	}
	else
	{
		for (auto& pRuntime : m_componentRuntimes)
		{
			if (!pRuntime->IsChild())
				pRuntime->RemoveAllSubInstances();
		}
	}

	m_active = activate;
}

void CParticleEmitter::Restart()
{
	Activate(false);
	Activate(true);
}

void CParticleEmitter::Kill()
{
	m_active = false;
	m_alive = false;
	m_componentRuntimes.clear();
	m_componentRuntimesFor.clear();
}

bool CParticleEmitter::IsActive() const
{
	return m_active;
}

void CParticleEmitter::SetLocation(const QuatTS& loc)
{
	const Vec3 prevPos = m_location.t;
	const Quat prevQuat = m_location.q;

	m_location = loc;
	if (m_spawnParams.bPlaced && m_pEffect->IsSubstitutedPfx1())
	{
		ParticleLoc::RotateYtoZ(m_location.q);
	}

	const Vec3 newPos = m_location.t;
	const Quat newQuat = m_location.q;
	m_parentContainer.GetIOVec3Stream(EPVF_Position).Store(0, newPos);
	m_parentContainer.GetIOQuatStream(EPQF_Orientation).Store(0, newQuat);

	if (m_registered)
	{
		const float deltaTime = gEnv->pTimer->GetFrameTime();
		const float invDeltaTime = abs(deltaTime) > FLT_EPSILON ? rcp_fast(deltaTime) : 0.0f;
		const Vec3 velocity0 = m_parentContainer.GetIOVec3Stream(EPVF_Velocity).Load(0);
		const Vec3 angularVelocity0 = m_parentContainer.GetIOVec3Stream(EPVF_AngularVelocity).Load(0);

		const Vec3 velocity1 =
		  velocity0 +
		  (newPos - prevPos) * invDeltaTime;
		const Vec3 angularVelocity1 =
		  angularVelocity0 +
		  2.0f * Quat::log(newQuat * prevQuat.GetInverted()) * invDeltaTime;

		m_parentContainer.GetIOVec3Stream(EPVF_Velocity).Store(0, velocity1);
		m_parentContainer.GetIOVec3Stream(EPVF_AngularVelocity).Store(0, angularVelocity1);
		m_visEnviron.Invalidate();
	}
}

void CParticleEmitter::EmitParticle(const EmitParticleData* pData)
{
	// #PFX2_TODO : handle EmitParticleData (create new instances)
	SSpawnEntry spawn = {1, m_parentContainer.GetLastParticleId()};
	for (auto pRuntime: m_componentRuntimes)
	{
		if (!pRuntime->IsChild())
		{
			pRuntime->AddSpawnEntry(spawn);
		}
	}
}

void CParticleEmitter::SetEmitterFeatures(TParticleFeatures& features)
{
	if (features.m_editVersion != m_emitterEditVersion)
	{
		m_emitterFeatures = features;
		m_emitterEditVersion = features.m_editVersion;
	}
}

void CParticleEmitter::SetEntity(IEntity* pEntity, int nSlot)
{
	m_entityOwner = pEntity;
	m_entitySlot = nSlot;
}

//////////////////////////////////////////////////////////////////////////
void CParticleEmitter::InvalidateCachedEntityData()
{
	UpdateFromEntity();
}

void CParticleEmitter::SetTarget(const ParticleTarget& target)
{
	if ((int)target.bPriority >= (int)m_target.bPriority)
		m_target = target;
}

void CParticleEmitter::UpdateStreamingPriority(const SUpdateStreamingPriorityContext& context)
{
	FUNCTION_PROFILER_3DENGINE;

	for (auto& pRuntime : m_componentRuntimes)
	{
		const auto* pComponent = pRuntime->GetComponent();
		const SComponentParams& params = pComponent->GetComponentParams();
		const float normalizedDist = context.distance * rcp_safe(params.m_maxParticleSize);
		const bool bHighPriority = !!(params.m_renderObjectFlags & FOB_NEAREST);

		if (CMatInfo* pMatInfo  = static_cast<CMatInfo*>(params.m_pMaterial.get()))
		{
			const float adjustedDist = normalizedDist
				* min(params.m_shaderData.m_tileSize[0], params.m_shaderData.m_tileSize[1]);
			pMatInfo->PrecacheMaterial(adjustedDist, nullptr, context.bFullUpdate, bHighPriority);
		}

		if (CStatObj* pStatObj = static_cast<CStatObj*>(params.m_pMesh.get()))
			m_pObjManager->PrecacheStatObj(pStatObj, context.lod, Matrix34A(m_location),
				pStatObj->GetMaterial(), context.importance, normalizedDist, context.bFullUpdate, bHighPriority);
	}
}

void CParticleEmitter::GetSpawnParams(SpawnParams& spawnParams) const
{
	spawnParams = m_spawnParams;
}

void CParticleEmitter::SetEmitGeom(const GeomRef& geom)
{
	// If emitter has an OwnerEntity, it will override this GeomRef on the next frame.
	// So SetOwnerEntity(nullptr) should be called as well.
	m_emitterGeometry = geom;
}

void CParticleEmitter::SetSpawnParams(const SpawnParams& spawnParams)
{
	if (spawnParams.bIgnoreVisAreas != m_spawnParams.bIgnoreVisAreas || spawnParams.bRegisterByBBox != m_spawnParams.bRegisterByBBox)
		Unregister();
	m_spawnParams = spawnParams;	
}

uint CParticleEmitter::GetAttachedEntityId()
{
	return m_entityOwner ? m_entityOwner->GetId() : INVALID_ENTITYID;
}

void CParticleEmitter::UpdateRuntimes()
{
	ResetRenderObjects();

	m_componentRuntimesFor.clear();
	TRuntimes newRuntimes;

	if (!m_emitterFeatures.empty())
	{
		// clone and modify effect
		m_pEffect = new CParticleEffect(*m_pEffectOriginal);
		for (auto& pComponent : m_pEffect->GetComponents())
		{
			if (!pComponent->GetParentComponent())
			{
				// clone component and add features
				pComponent = new CParticleComponent(*pComponent);
				pComponent->SetEffect(m_pEffect);
				for (auto& feature : m_emitterFeatures)
					pComponent->AddFeature(static_cast<CParticleFeature*>(feature.get()));
			}
		}
		m_pEffect->SetChanged();
		m_pEffect->Compile();
	}

	for (const auto& pComponent: m_pEffect->GetComponents())
	{
		if (!pComponent->CanMakeRuntime(this))
		{
			m_componentRuntimesFor.push_back(nullptr);
			continue;
		}

		CParticleComponentRuntime* pRuntime = stl::find_value_if(m_componentRuntimes,
			[&](CParticleComponentRuntime* pRuntime)
			{
				return pRuntime->GetComponent() == pComponent;
			}
		);

		if (!pRuntime || !pRuntime->IsValidForComponent())
			pRuntime = new CParticleComponentRuntime(this, pComponent);

		newRuntimes.push_back(pRuntime);
		m_componentRuntimesFor.push_back(pRuntime);
	}

	m_componentRuntimes = newRuntimes;

	m_effectEditVersion = m_pEffectOriginal->GetEditVersion() + m_emitterEditVersion;

	m_parentContainer.AddParticle();

	for (auto& pRuntime : m_componentRuntimes)
	{
		pRuntime->RemoveAllSubInstances();
		pRuntime->Initialize();
		if (!pRuntime->IsChild())
		{
			SInstance instance;
			pRuntime->AddSubInstances({&instance, 1});
		}
		CParticleComponent* pComponent = pRuntime->GetComponent();
		if (GetRenderer())
		{
			pComponent->PrepareRenderObjects(this, pComponent, true);
		}
	}

	m_alive = true;
}

void CParticleEmitter::ResetRenderObjects()
{
	if (!GetRenderer())
		return;

	if (!m_pEffect)
		return;

	const uint numROs = m_pEffect->GetNumRenderObjectIds();
	for (uint threadId = 0; threadId < RT_COMMAND_BUF_COUNT; ++threadId)
		m_pRenderObjects[threadId].resize(numROs, { nullptr, nullptr });

	for (auto& pRuntime : m_componentRuntimes)
	{
		CParticleComponent* pComponent = pRuntime->GetComponent();
		pComponent->PrepareRenderObjects(this, pComponent, false);
	}
}

void CParticleEmitter::AddInstance()
{
	SInstance instance(m_parentContainer.GetLastParticleId());

	m_parentContainer.AddParticle();

	for (auto& pRuntime : m_componentRuntimes)
	{
		if (!pRuntime->IsChild())
			pRuntime->AddSubInstances({&instance, 1});
	}
}

ILINE void CParticleEmitter::UpdateFromEntity()
{
	if (m_entityOwner && m_target.bPriority)
		UpdateTargetFromEntity(m_entityOwner);
}

IEntity* CParticleEmitter::GetEmitGeometryEntity() const
{
	IEntity* pEntity = m_entityOwner;
	if (pEntity)
	{
		// Override m_emitterGeometry with geometry of owning or attached entity if it exists
		if (IEntity* pParent = pEntity->GetParent())
			pEntity = pEntity->GetParent();
	}
	return pEntity;
}

void CParticleEmitter::UpdateEmitGeomFromEntity()
{
	IEntity* pEntity = GetEmitGeometryEntity();
	m_emitterGeometrySlot = m_emitterGeometry.Set(pEntity);
}

QuatTS CParticleEmitter::GetEmitterGeometryLocation() const
{
	if (IEntity* pEntity = GetEmitGeometryEntity())
	{
		SEntitySlotInfo slotInfo;
		if (pEntity->GetSlotInfo(m_emitterGeometrySlot, slotInfo))
		{
			if (slotInfo.pWorldTM)
				return QuatTS(*slotInfo.pWorldTM);
		}

	}
	return m_location;
}

CRenderObject* CParticleEmitter::GetRenderObject(uint threadId, uint renderObjectIdx)
{
	CRY_PFX2_ASSERT(threadId < RT_COMMAND_BUF_COUNT);
	if (m_pRenderObjects[threadId].empty())
		return nullptr;
	return m_pRenderObjects[threadId][renderObjectIdx].first;
}

void CParticleEmitter::SetRenderObject(CRenderObject* pRenderObject, _smart_ptr<IMaterial>&& material, uint threadId, uint renderObjectIdx)
{
	CRY_PFX2_ASSERT(threadId < RT_COMMAND_BUF_COUNT);
	m_pRenderObjects[threadId][renderObjectIdx] = std::make_pair(pRenderObject, std::move(material));
}

void CParticleEmitter::UpdateTargetFromEntity(IEntity* pEntity)
{
	if (m_target.bPriority)
		return;

	ParticleTarget target;
	for (IEntityLink* pLink = pEntity->GetEntityLinks(); pLink; pLink = pLink->next)
	{
		IEntity* pTarget = gEnv->pEntitySystem->GetEntity(pLink->entityId);
		if (pTarget)
		{
			target.bTarget = true;
			target.vTarget = pTarget->GetPos();

			target.vVelocity = Vec3(ZERO);  // PFX2_TODO : grab velocity from entity's physical object

			AABB boundingBox;
			pTarget->GetLocalBounds(boundingBox);
			target.fRadius = boundingBox.GetRadius();

			break;
		}
	}
	m_target = target;
}

void CParticleEmitter::Register()
{
	CRY_PFX2_PROFILE_DETAIL;

	if (m_registered)
		return;
	if (m_bounds.IsEmpty() || m_bounds.IsReset())
		return;

	bool posContained = GetBBox().IsContainPoint(GetPos());
	SetRndFlags(ERF_REGISTER_BY_POSITION, posContained);
	SetRndFlags(ERF_REGISTER_BY_BBOX, m_spawnParams.bRegisterByBBox);
	SetRndFlags(ERF_RENDER_ALWAYS, m_spawnParams.bIgnoreVisAreas);
	SetRndFlags(ERF_CASTSHADOWMAPS, false);
	gEnv->p3DEngine->RegisterEntity(this);
	m_registered = true;

	m_visEnviron.Invalidate();
	m_visEnviron.Update(GetPos(), m_bounds);
	m_physEnviron.GetPhysAreas(
		CParticleManager::Instance()->GetPhysEnviron(), m_bounds,
		m_visEnviron.OriginIndoors(), m_pEffect->GetEnvironFlags() | ENV_WATER, true, 0);
}

void CParticleEmitter::Unregister()
{
	if (!m_registered)
		return;
	gEnv->p3DEngine->UnRegisterEntityDirect(this);
	m_registered = false;
}

bool CParticleEmitter::HasParticles() const
{
	for (auto& pRuntime : m_componentRuntimes)
	{
		if (pRuntime->HasParticles())
			return true;
	}

	return false;
}

uint CParticleEmitter::GetParticleSpec() const
{
	if (m_spawnParams.eSpec != EParticleSpec::Default)
		return uint(m_spawnParams.eSpec);

	CVars* pCVars = static_cast<C3DEngine*>(gEnv->p3DEngine)->GetCVars();
	if (pCVars->e_ParticlesQuality != 0)
		return pCVars->e_ParticlesQuality;

	const ESystemConfigSpec configSpec = gEnv->pSystem->GetConfigSpec();
	return uint(configSpec);
}

}
