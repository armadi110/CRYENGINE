// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#include "stdafx.h"
#include "AudioImpl.h"
#include "AudioImplCVars.h"
#include "SoundEngineUtil.h"
#include "SoundEngineTypes.h"
#include "GlobalData.h"
#include <Logger.h>
#include <CrySystem/File/CryFile.h>
#include <CryAudio/IAudioSystem.h>
#include <CrySystem/IProjectManager.h>

// SDL Mixer
#include <SDL_mixer.h>

namespace CryAudio
{
namespace Impl
{
namespace SDL_mixer
{
///////////////////////////////////////////////////////////////////////////
void OnEventFinished(CATLEvent& audioEvent)
{
	gEnv->pAudioSystem->ReportFinishedEvent(audioEvent, true);
}

///////////////////////////////////////////////////////////////////////////
void OnStandaloneFileFinished(CATLStandaloneFile& standaloneFile, const char* szFile)
{
	gEnv->pAudioSystem->ReportStoppedFile(standaloneFile);
}

///////////////////////////////////////////////////////////////////////////
CImpl::CImpl()
	: m_pCVarFileExtension(nullptr)
	, m_isMuted(false)
{
#if defined(INCLUDE_SDLMIXER_IMPL_PRODUCTION_CODE)
	m_name = "SDL Mixer 2.0.2";
#endif  // INCLUDE_SDLMIXER_IMPL_PRODUCTION_CODE

#if CRY_PLATFORM_WINDOWS
	m_memoryAlignment = 16;
#elif CRY_PLATFORM_MAC
	m_memoryAlignment = 16;
#elif CRY_PLATFORM_LINUX
	m_memoryAlignment = 16;
#elif CRY_PLATFORM_IOS
	m_memoryAlignment = 16;
#elif CRY_PLATFORM_ANDROID
	m_memoryAlignment = 16;
#else
	#error "Undefined platform."
#endif
}

///////////////////////////////////////////////////////////////////////////
void CImpl::Update()
{
	SoundEngine::Update();
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::Init(uint32 const objectPoolSize, uint32 const eventPoolSize)
{
	MEMSTAT_CONTEXT(EMemStatContextTypes::MSC_Other, 0, "SDL Mixer Object Pool");
	CObject::CreateAllocator(objectPoolSize);

	MEMSTAT_CONTEXT(EMemStatContextTypes::MSC_Other, 0, "SDL Mixer Event Pool");
	CEvent::CreateAllocator(eventPoolSize);

	m_pCVarFileExtension = REGISTER_STRING("s_SDLMixerStandaloneFileExtension", ".mp3", 0, "the expected file extension for standalone files, played via the sdl_mixer");

	if (SoundEngine::Init())
	{
		SoundEngine::RegisterEventFinishedCallback(OnEventFinished);
		SoundEngine::RegisterStandaloneFileFinishedCallback(OnStandaloneFileFinished);
		return ERequestStatus::Success;
	}
	return ERequestStatus::Failure;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::OnBeforeShutDown()
{
	return ERequestStatus::Success;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::ShutDown()
{
	return ERequestStatus::Success;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::Release()
{
	if (m_pCVarFileExtension)
	{
		m_pCVarFileExtension->Release();
		m_pCVarFileExtension = nullptr;
	}

	SoundEngine::Release();

	delete this;
	g_cvars.UnregisterVariables();

	CObject::FreeMemoryPool();
	CEvent::FreeMemoryPool();

	return ERequestStatus::Success;
}

///////////////////////////////////////////////////////////////////////////
void CImpl::OnRefresh()
{
	SoundEngine::Refresh();
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::OnLoseFocus()
{
	if (!m_isMuted)
	{
		SoundEngine::Mute();
	}

	return ERequestStatus::Success;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::OnGetFocus()
{
	if (!m_isMuted)
	{
		SoundEngine::UnMute();
	}

	return ERequestStatus::Success;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::MuteAll()
{
	SoundEngine::Mute();
	m_isMuted = true;
	return ERequestStatus::Success;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::UnmuteAll()
{
	SoundEngine::UnMute();
	m_isMuted = false;
	return ERequestStatus::Success;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::PauseAll()
{
	SoundEngine::Pause();
	return ERequestStatus::Success;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::ResumeAll()
{
	SoundEngine::Resume();
	return ERequestStatus::Success;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::StopAllSounds()
{
	SoundEngine::Stop();
	return ERequestStatus::Success;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::RegisterInMemoryFile(SFileInfo* const pFileInfo)
{
	ERequestStatus result = ERequestStatus::Failure;

	if (pFileInfo != nullptr)
	{
		SFile* const pFileData = static_cast<SFile*>(pFileInfo->pImplData);

		if (pFileData != nullptr)
		{
			pFileData->sampleId = SoundEngine::LoadSampleFromMemory(pFileInfo->pFileData, pFileInfo->size, pFileInfo->szFileName);
			result = ERequestStatus::Success;
		}
		else
		{
			Cry::Audio::Log(ELogType::Error, "Invalid AudioFileEntryData passed to the SDL Mixer implementation of RegisterInMemoryFile");
		}
	}

	return result;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::UnregisterInMemoryFile(SFileInfo* const pFileInfo)
{
	ERequestStatus result = ERequestStatus::Failure;

	if (pFileInfo != nullptr)
	{
		SFile* const pFileData = static_cast<SFile*>(pFileInfo->pImplData);

		if (pFileData != nullptr)
		{
			SoundEngine::UnloadSample(pFileData->sampleId);
			result = ERequestStatus::Success;
		}
		else
		{
			Cry::Audio::Log(ELogType::Error, "Invalid AudioFileEntryData passed to the SDL Mixer implementation of UnregisterInMemoryFile");
		}
	}
	return result;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::ConstructFile(XmlNodeRef const pRootNode, SFileInfo* const pFileInfo)
{
	ERequestStatus result = ERequestStatus::Failure;

	if ((_stricmp(pRootNode->getTag(), s_szFileTag) == 0) && (pFileInfo != nullptr))
	{
		char const* const szFileName = pRootNode->getAttr(s_szNameAttribute);
		char const* const szPath = pRootNode->getAttr(s_szPathAttribute);
		CryFixedStringT<MaxFilePathLength> fullFilePath;
		if (szPath)
		{
			fullFilePath = szPath;
			fullFilePath += "/";
			fullFilePath += szFileName;
		}
		else
		{
			fullFilePath = szFileName;
		}

		// Currently the SDLMixer Implementation does not support localized files.
		pFileInfo->bLocalized = false;

		if (!fullFilePath.empty())
		{
			pFileInfo->szFileName = fullFilePath.c_str();
			pFileInfo->memoryBlockAlignment = m_memoryAlignment;
			pFileInfo->pImplData = new SFile;
			result = ERequestStatus::Success;
		}
		else
		{
			pFileInfo->szFileName = nullptr;
			pFileInfo->memoryBlockAlignment = 0;
			pFileInfo->pImplData = nullptr;
		}
	}

	return result;
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructFile(IFile* const pIFile)
{
	delete pIFile;
}

///////////////////////////////////////////////////////////////////////////
char const* const CImpl::GetFileLocation(SFileInfo* const pFileInfo)
{
	static CryFixedStringT<MaxFilePathLength> s_path;
	s_path = AUDIO_SYSTEM_DATA_ROOT "/";
	s_path += s_szImplFolderName;
	s_path += "/";
	s_path += s_szAssetsFolderName;
	s_path += "/";

	return s_path.c_str();
}

//////////////////////////////////////////////////////////////////////////
void CImpl::GetInfo(SImplInfo& implInfo) const
{
#if defined(INCLUDE_SDLMIXER_IMPL_PRODUCTION_CODE)
	implInfo.name = m_name.c_str();
#else
	implInfo.name = "name-not-present-in-release-mode";
#endif  // INCLUDE_SDLMIXER_IMPL_PRODUCTION_CODE
	implInfo.folderName = s_szImplFolderName;
}

///////////////////////////////////////////////////////////////////////////
ITrigger const* CImpl::ConstructTrigger(XmlNodeRef const pRootNode)
{
	CTrigger* pTrigger = nullptr;

	if (_stricmp(pRootNode->getTag(), s_szEventTag) == 0)
	{
		pTrigger = SoundEngine::CreateTrigger();

		if (pTrigger != nullptr)
		{
			char const* const szFileName = pRootNode->getAttr(s_szNameAttribute);
			char const* const szPath = pRootNode->getAttr(s_szPathAttribute);
			string fullFilePath;

			if (szPath != nullptr && szPath[0] != '\0')
			{
				fullFilePath = szPath;
				fullFilePath += "/";
				fullFilePath += szFileName;
			}
			else
			{
				fullFilePath = szFileName;
			}

			pTrigger->SetSampleId(SoundEngine::LoadSample(fullFilePath, true));

			if (_stricmp(pRootNode->getAttr(s_szTypeAttribute), s_szStopValue) == 0)
			{
				pTrigger->SetType(EEventType::Stop);
			}
			else if (_stricmp(pRootNode->getAttr(s_szTypeAttribute), s_szPauseValue) == 0)
			{
				pTrigger->SetType(EEventType::Pause);
			}
			else if (_stricmp(pRootNode->getAttr(s_szTypeAttribute), s_szResumeValue) == 0)
			{
				pTrigger->SetType(EEventType::Resume);
			}
			else
			{
				pTrigger->SetType(EEventType::Start);
			}

			if (pTrigger->GetType() == EEventType::Start)
			{
				pTrigger->SetPanningEnabled(_stricmp(pRootNode->getAttr(s_szPanningEnabledAttribute), s_szTrueValue) == 0);
				bool const isAttenuationEnabled = (_stricmp(pRootNode->getAttr(s_szAttenuationEnabledAttribute), s_szTrueValue) == 0);

				if (isAttenuationEnabled)
				{
					float minDistance = 0.0f;
					pRootNode->getAttr(s_szAttenuationMinDistanceAttribute, minDistance);

					float maxDistance = 0.0f;
					pRootNode->getAttr(s_szAttenuationMaxDistanceAttribute, maxDistance);

					minDistance = std::max(0.0f, minDistance);
					maxDistance = std::max(0.0f, maxDistance);

					if (minDistance > maxDistance)
					{
						Cry::Audio::Log(ELogType::Warning, "Min distance (%f) was greater than max distance (%f) of %s", minDistance, maxDistance, szFileName);
						pTrigger->SetAttenuationMinDistance(maxDistance);
						pTrigger->SetAttenuationMaxDistance(minDistance);
					}
					else
					{
						pTrigger->SetAttenuationMinDistance(minDistance);
						pTrigger->SetAttenuationMaxDistance(maxDistance);
					}
				}
				else
				{
					pTrigger->SetAttenuationMinDistance(-1.0f);
					pTrigger->SetAttenuationMaxDistance(-1.0f);
				}

				// Translate decibel to normalized value.
				static const int maxVolume = 128;
				float volume = 0.0f;
				pRootNode->getAttr(s_szVolumeAttribute, volume);
				pTrigger->SetVolume(static_cast<int>(pow_tpl(10.0f, volume / 20.0f) * maxVolume));

				int numLoops = 0;
				pRootNode->getAttr(s_szLoopCountAttribute, numLoops);
				// --numLoops because -1: play infinite, 0: play once, 1: play twice, etc...
				--numLoops;
				// Max to -1 to stay backwards compatible.
				numLoops = std::max(-1, numLoops);
				pTrigger->SetNumLoops(numLoops);

				float fadeInTime = 0.0f;
				pRootNode->getAttr(s_szFadeInTimeAttribute, fadeInTime);
				pTrigger->SetFadeInTime(static_cast<int>(fadeInTime * 1000.0f));

				float fadeOutTime = 0.0f;
				pRootNode->getAttr(s_szFadeOutTimeAttribute, fadeOutTime);
				pTrigger->SetFadeOutTime(static_cast<int>(fadeOutTime * 1000.0f));
			}
		}
	}
	else
	{
		Cry::Audio::Log(ELogType::Warning, "Unknown SDL Mixer tag: %s", pRootNode->getTag());
	}

	return static_cast<ITrigger*>(pTrigger);
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructTrigger(ITrigger const* const pITrigger)
{
	if (pITrigger != nullptr)
	{
		CTrigger const* const pTrigger = static_cast<CTrigger const* const>(pITrigger);
		SoundEngine::StopTrigger(pTrigger);
		delete pTrigger;
	}
}

///////////////////////////////////////////////////////////////////////////
IParameter const* CImpl::ConstructParameter(XmlNodeRef const pRootNode)
{
	return static_cast<IParameter*>(new SParameter);
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructParameter(IParameter const* const pIParameter)
{
	delete pIParameter;
}

///////////////////////////////////////////////////////////////////////////
ISwitchState const* CImpl::ConstructSwitchState(XmlNodeRef const pRootNode)
{
	return static_cast<ISwitchState*>(new SSwitchState);
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructSwitchState(ISwitchState const* const pISwitchState)
{
	delete pISwitchState;
}

///////////////////////////////////////////////////////////////////////////
IEnvironment const* CImpl::ConstructEnvironment(XmlNodeRef const pRootNode)
{
	return static_cast<IEnvironment*>(new SEnvironment);
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructEnvironment(IEnvironment const* const pIEnvironment)
{
	delete pIEnvironment;
}

///////////////////////////////////////////////////////////////////////////
IObject* CImpl::ConstructGlobalObject()
{
	return static_cast<IObject*>(new CObject(0));
}

///////////////////////////////////////////////////////////////////////////
IObject* CImpl::ConstructObject(char const* const szName /*= nullptr*/)
{
	static uint32 id = 1;
	CObject* pObject = new CObject(id++);

	SoundEngine::RegisterObject(pObject);

#if defined(INCLUDE_SDLMIXER_IMPL_PRODUCTION_CODE)
	m_idToName[pObject->m_id] = szName;
#endif  // INCLUDE_SDLMIXER_IMPL_PRODUCTION_CODE

	return static_cast<IObject*>(pObject);
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructObject(IObject const* const pIObject)
{
	if (pIObject != nullptr)
	{
		CObject const* pObject = static_cast<CObject const*>(pIObject);
		SoundEngine::UnregisterObject(pObject);
		delete pObject;
	}
}

///////////////////////////////////////////////////////////////////////////
IListener* CImpl::ConstructListener(char const* const szName /*= nullptr*/)
{
	static ListenerId id = 0;
	return static_cast<IListener*>(new CListener(id++));
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructListener(IListener* const pIListener)
{
	delete pIListener;
}

///////////////////////////////////////////////////////////////////////////
IEvent* CImpl::ConstructEvent(CATLEvent& event)
{
	return static_cast<IEvent*>(new CEvent(event));
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructEvent(IEvent const* const pIEvent)
{
	delete pIEvent;
}

///////////////////////////////////////////////////////////////////////////
IStandaloneFile* CImpl::ConstructStandaloneFile(CATLStandaloneFile& standaloneFile, char const* const szFile, bool const bLocalized, ITrigger const* pITrigger /*= nullptr*/)
{
	static string s_localizedfilesFolder = PathUtil::GetLocalizationFolder() + "/" + m_language + "/";
	static string filePath;

	if (bLocalized)
	{
		filePath = s_localizedfilesFolder + szFile + m_pCVarFileExtension->GetString();
	}
	else
	{
		filePath = string(szFile) + m_pCVarFileExtension->GetString();
	}

	return static_cast<IStandaloneFile*>(new CStandaloneFile(filePath, standaloneFile));
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructStandaloneFile(IStandaloneFile const* const pIStandaloneFile)
{
#if defined(INCLUDE_SDLMIXER_IMPL_PRODUCTION_CODE)
	const CStandaloneFile* const pStandaloneEvent = static_cast<const CStandaloneFile*>(pIStandaloneFile);
	CRY_ASSERT_MESSAGE(pStandaloneEvent->m_channels.size() == 0, "Events always have to be stopped/finished before they get deleted");
#endif  // INCLUDE_SDLMIXER_IMPL_PRODUCTION_CODE
	delete pIStandaloneFile;
}

///////////////////////////////////////////////////////////////////////////
void CImpl::GamepadConnected(DeviceId const deviceUniqueID)
{}

///////////////////////////////////////////////////////////////////////////
void CImpl::GamepadDisconnected(DeviceId const deviceUniqueID)
{}

///////////////////////////////////////////////////////////////////////////
void CImpl::SetLanguage(char const* const szLanguage)
{
	m_language = szLanguage;
}

///////////////////////////////////////////////////////////////////////////
void CImpl::GetMemoryInfo(SMemoryInfo& memoryInfo) const
{
	CryModuleMemoryInfo memInfo;
	ZeroStruct(memInfo);
	CryGetMemoryInfoForModule(&memInfo);

	memoryInfo.totalMemory = static_cast<size_t>(memInfo.allocated - memInfo.freed);
	memoryInfo.secondaryPoolSize = 0;
	memoryInfo.secondaryPoolUsedSize = 0;
	memoryInfo.secondaryPoolAllocations = 0;

	{
		auto& allocator = CObject::GetAllocator();
		auto mem = allocator.GetTotalMemory();
		auto pool = allocator.GetCounts();
		memoryInfo.poolUsedObjects = pool.nUsed;
		memoryInfo.poolConstructedObjects = pool.nAlloc;
		memoryInfo.poolUsedMemory = mem.nUsed;
		memoryInfo.poolAllocatedMemory = mem.nAlloc;
	}

	{
		auto& allocator = CEvent::GetAllocator();
		auto mem = allocator.GetTotalMemory();
		auto pool = allocator.GetCounts();
		memoryInfo.poolUsedObjects += pool.nUsed;
		memoryInfo.poolConstructedObjects += pool.nAlloc;
		memoryInfo.poolUsedMemory += mem.nUsed;
		memoryInfo.poolAllocatedMemory += mem.nAlloc;
	}
}

///////////////////////////////////////////////////////////////////////////
void CImpl::GetFileData(char const* const szName, SFileData& fileData) const
{}
} // namespace SDL_mixer
} // namespace Impl
} // namespace CryAudio
