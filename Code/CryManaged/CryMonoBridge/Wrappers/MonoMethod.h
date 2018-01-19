// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#ifndef HAVE_MONO_API
namespace MonoInternals
{
	struct MonoMethod;
}
#endif

class CMonoObject;

class CMonoMethod
{
	friend class CMonoClass;

	// Begin public API
public:
	CMonoMethod(MonoInternals::MonoMethod* pMethod);

	std::shared_ptr<CMonoObject> Invoke(const CMonoObject* const pObject, void** pParameters, bool &bEncounteredException) const;
	std::shared_ptr<CMonoObject> Invoke(const CMonoObject* const pObject = nullptr, void** pParameters = nullptr) const
	{
		bool bEncounteredException;
		return Invoke(pObject, pParameters, bEncounteredException);
	}

	std::shared_ptr<CMonoObject> Invoke(const CMonoObject* const pObject, MonoInternals::MonoArray* pParameters, bool &bEncounteredException) const;
	std::shared_ptr<CMonoObject> Invoke(const CMonoObject* const pObject, MonoInternals::MonoArray* pParameters) const
	{
		bool bEncounteredException;
		return Invoke(pObject, pParameters, bEncounteredException);
	}
	
	inline std::shared_ptr<CMonoObject> InvokeStatic(void** pParameters) const { return Invoke(nullptr, pParameters); }

	void VisitParameters(std::function<void(int index, MonoInternals::MonoType* pType, const char* szName)> func) const;

	MonoInternals::MonoMethodSignature* GetSignature() const { return MonoInternals::mono_method_signature(m_pMethod); }

	string GetSignatureDescription(bool bIncludeNamespace = true, bool bForceSkipCache = false) const;
	const char* GetName() const { return MonoInternals::mono_method_get_name(m_pMethod); }
	
	MonoInternals::MonoMethod* GetHandle() const { return m_pMethod; }
	void* GetUnmanagedThunk() const { return MonoInternals::mono_method_get_unmanaged_thunk(m_pMethod); }

protected:
	std::shared_ptr<CMonoObject> InvokeInternal(MonoInternals::MonoObject* pMonoObject, void** pParameters, bool &bEncounteredException) const;
	std::shared_ptr<CMonoObject> InvokeInternal(MonoInternals::MonoObject* pMonoObject, MonoInternals::MonoArray* pParameters, bool &bEncounteredException) const;

	void PrepareForSerialization();
	const char* GetSerializedDescription() const { return m_description; }

	void OnDeserialized(MonoInternals::MonoMethod* pMethod) { m_pMethod = pMethod; }

protected:
	MonoInternals::MonoMethod* m_pMethod;
	
	string m_description;
};