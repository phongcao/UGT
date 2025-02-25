#include "PlatformPrecomp.h"
#include "TextAreaComponent.h"
#include "App.h"
#include "Entity/EntityUtils.h"
#include "util/MiscUtils.h"
#include "Network/NetUtils.h"
#include "util/cJSON.h"
#include "util/utf8.h"
#include "AutoPlayManager.h"
#include "Gamepad/GamepadManager.h"
#include "util/TextScanner.h"

int g_counter = 0;
 

AudioHandle g_lastAudioHandle = AUDIO_HANDLE_BLANK;

TextAreaComponent::TextAreaComponent()
{
	m_pSpeakerIconSrc = NULL;
	m_pSpeakerIconDest = NULL;
	
	SetName("TextArea");

	GetApp()->m_pGameLogicComp->AddTextBox(this);
}

bool TextAreaComponent::FinishedWithTranslation()
{
	return !m_bWaitingForTranslation;
}



TextAreaComponent::~TextAreaComponent()
{
	SAFE_DELETE(m_pSourceLanguageSurf);
	SAFE_DELETE(m_pDestLanguageSurf);

	if (m_pSpeakerIconDest)
	{
		m_pSpeakerIconDest->SetTaggedForDeletion();
		m_pSpeakerIconDest = NULL;
	}

	if (m_pSpeakerIconSrc)
	{
		m_pSpeakerIconSrc->SetTaggedForDeletion();
		m_pSpeakerIconSrc = NULL;
	}

	if (!m_fileNameToRemove.empty())
	{
		RemoveFile(m_fileNameToRemove);
	}

	if (IsBaseAppInitted())
	{
		GetApp()->m_pGameLogicComp->RemoveTextBox(this);
	}
	
}

void TextAreaComponent::StopSoundIfItWasPlaying()
{
	if (g_lastAudioHandle != AUDIO_HANDLE_BLANK)
	{
		GetAudioManager()->Stop(g_lastAudioHandle);
		m_audioHandle = AUDIO_HANDLE_BLANK;
		g_lastAudioHandle = AUDIO_HANDLE_BLANK;
	}
}

void TextAreaComponent::RequestAudio(bool bUseSrcLanguage, bool bShowMessage)
{
	if (GetApp()->GetShared()->GetVar("check_src_audio")->GetUINT32() == 0)
	{
		bUseSrcLanguage = !bUseSrcLanguage;
	}

	string url = "https://texttospeech.googleapis.com";
	string urlappend = "/v1/text:synthesize?key=" + GetApp()->GetGoogleKey();

	unsigned int originalFileSize = 0;

	string voiceGender = "FEMALE";

	bool bGuessedAtLanguage = false;

	//To translate, we need to know the language, but also choose the region.
	//Google doesn't give us a default region for a language, so the below table is a hacky way
	//to choose regions that match languages, as well as choose default voices in some cases where there is
	//a clear difference.
	//See https://cloud.google.com/text-to-speech/docs/voices for the table

	string language = m_textArea.language;
	string textToTranslate;

	if (!bUseSrcLanguage && m_pDestLanguageSurf != NULL)
	{
		language = GetApp()->m_target_language;
		textToTranslate = m_translatedString;
	}
	else
	{
		if (IsDialog(true))
		{
			textToTranslate = m_textArea.rawText;
		}
		else
		{
			textToTranslate = m_textArea.text;
		}
	}
	
	if (language == "")
	{
		bGuessedAtLanguage = true;
		language = "ja";
	}

	if (language == "zh")
	{
		language = "cmn";  //zh doesn't exist for text to speech at this time.  To avoid an error I force
		//mandarin chinese instead, not sure if that makes sense to do or not. This should be removed later
		//when google properly supports it.  I should really be using Google's API to build a list of
		//supported languages on the fly but.. yeah.
	}
	if (language == "zh-CN")
	{
		language = "cmn";  //zh doesn't exist for text to speech at this time.  To avoid an error I force
		//mandarin chinese instead, not sure if that makes sense to do or not. This should be removed later
		//when google properly supports it.  I should really be using Google's API to build a list of
		//supported languages on the fly but.. yeah.
	}

	/*
	if (language == "ro" || language == "gd" || language == "jv" || language == "ms")
	{
		language = "en";  //Unsupported text to speech languages, remove when they become supported later?
	}
	*/
	string languageRegion = language;
	
	//hacky fixes to match up correction region with language
	if (languageRegion == "en") languageRegion = "us";
	if (languageRegion == "ar") languageRegion = "xa";
	if (languageRegion == "da") languageRegion = "dk";
	if (languageRegion == "hi") languageRegion = "in";
	if (languageRegion == "ja") languageRegion = "jp";
	if (languageRegion == "ko") languageRegion = "kr";
	if (languageRegion == "cmn") languageRegion = "cn";
	if (languageRegion == "nb") languageRegion = "no";
	if (languageRegion == "el") languageRegion = "gr";
	if (languageRegion == "sm") languageRegion = "sa"; //samoa
	
	string languageType = "-Wavenet-";

	if (languageRegion == "es")
	{
		languageType = "-Standard-";
	}

	string languageCode = language +"-"+ToUpperCaseString(languageRegion);
	string languageLetter = "A";
	string audioEncoding = "MP3";

	//voice overrides for better sounding voices given the state of WaveNet on 9/12/2019
	if (languageCode == "en-US") languageLetter = "F";
	if (languageCode == "ja-JP") languageLetter = "B";

	string finalVoice = languageCode + languageType + languageLetter;
	
	m_lastTTSLanguageTarget = languageCode;

	//create json
	cJSON* root = cJSON_CreateObject();
	cJSON* pInput = cJSON_AddObjectToObject(root, "input");
	cJSON_AddItemToObject(pInput, "text", cJSON_CreateString(textToTranslate.c_str()));
	cJSON* pVoice = cJSON_AddObjectToObject(root, "voice");

	if (bShowMessage)
	{
		if (bGuessedAtLanguage)
		{
			ShowQuickMessage("Not sure of Language, trying " + languageCode);
		}
		else
		{
			ShowQuickMessage("Reading as `$" + languageCode + "``"); // with " + finalVoice);
		}
	}

	cJSON_AddItemToObject(pVoice, "languageCode", cJSON_CreateString(languageCode.c_str()));
	cJSON_AddItemToObject(pVoice, "name", cJSON_CreateString(finalVoice.c_str()));
	cJSON_AddItemToObject(pVoice, "ssmlGender", cJSON_CreateString(voiceGender.c_str()));

	cJSON* pAudioConfig = cJSON_AddObjectToObject(root, "audioConfig");
	cJSON_AddItemToObject(pAudioConfig, "audioEncoding", cJSON_CreateString(audioEncoding.c_str()));

	string postData(cJSON_Print(root));

#ifdef _DEBUG
	//LogMsg(postData.c_str());
#endif

	m_netAudioHTTP.Setup(url, 80, urlappend, NetHTTP::END_OF_DATA_SIGNAL_HTTP);
	m_netAudioHTTP.AddPostData("", (const byte*)postData.c_str(), (int)postData.length());
	m_netAudioHTTP.Start();
}

bool TextAreaComponent::IsStillPlayingOrPlanningToPlay()
{
	if (m_netAudioHTTP.GetState() == NetHTTP::STATE_ACTIVE || m_netAudioHTTP.GetState() == NetHTTP::STATE_FORWARD)
	{
		//waiting on data
		return true;
	}
	
	return GetAudioManager()->IsPlaying(m_audioHandle);
}

bool TextAreaComponent::IsDownloadingAudio()
{
	if (m_netAudioHTTP.GetState() == NetHTTP::STATE_ACTIVE || m_netAudioHTTP.GetState() == NetHTTP::STATE_FORWARD)
	{
		//waiting on data
		return true;
	}

	return false;
}

void TextAreaComponent::RequestTranslationGoogleBasic()
{
	string url = "https://translation.googleapis.com";
	string urlappend = "language/translate/v2?key=" + GetApp()->GetGoogleKey();

	unsigned int originalFileSize = 0;

	string sourceLanguage = m_textArea.language;
	string destLanguage = GetApp()->m_target_language;

	string textToTranslate;
	
	if (IsDialog(true))
	{
		textToTranslate = m_textArea.rawText;
	}
	else
	{
		for (int i = 0; i < m_textArea.m_lines.size(); i++)
		{
			textToTranslate += m_textArea.m_lines[i].m_text + "\n";
		}
	}

	//create json
	cJSON *root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "q", cJSON_CreateString(textToTranslate.c_str()));
	//cJSON_AddItemToObject(root, "source", cJSON_CreateString(sourceLanguage.c_str()));
	cJSON_AddItemToObject(root, "target", cJSON_CreateString(destLanguage.c_str()));
	cJSON_AddItemToObject(root, "format", cJSON_CreateString("text"));

	string postData(cJSON_Print(root));
	 
#ifdef _DEBUG
	//LogMsg(postData.c_str());
	//let's see what we're sending, write it to a txt file so notepad can read the kanji or whatever right
	FILE* fp = fopen("translation_request.txt", "wb");
	fwrite(postData.c_str(), postData.length(), 1, fp);
	fclose(fp);
#endif

	m_netHTTP.Setup(url, 80, urlappend, NetHTTP::END_OF_DATA_SIGNAL_HTTP);
	m_netHTTP.AddPostData("", (const byte*)postData.c_str(), (int)postData.length());
	m_netHTTP.Start();
	m_bWaitingForTranslation = true;
}


// Advanced
void TextAreaComponent::RequestTranslationGoogleAdvanced()
{
	string url = "https://translation.googleapis.com";
	string urlappend = "/v3/projects/compact-lacing-260204:translateText";
	string destLanguage = GetApp()->m_target_language;
	string textToTranslate;

	if (IsDialog(true))
	{
		textToTranslate = m_textArea.rawText;
	}
	else
	{
		for (int i = 0; i < m_textArea.m_lines.size(); i++)
		{
			textToTranslate += m_textArea.m_lines[i].m_text + "\n";
		}
	}

	// reate json
	cJSON* contents = cJSON_CreateArray();
	cJSON_AddItemToArray(contents, cJSON_CreateString(textToTranslate.c_str()));
	cJSON* root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "contents", contents);
	cJSON_AddItemToObject(root, "sourceLanguageCode", cJSON_CreateString("ja"));
	cJSON_AddItemToObject(root, "targetLanguageCode", cJSON_CreateString(destLanguage.c_str()));
	cJSON_AddItemToObject(root, "mimeType", cJSON_CreateString("text/plain"));

	string postData(cJSON_Print(root));

#ifdef _DEBUG
	//LogMsg(postData.c_str());
	//let's see what we're sending, write it to a txt file so notepad can read the kanji or whatever right
	FILE* fp = fopen("translation_request.txt", "wb");
	fwrite(postData.c_str(), postData.length(), 1, fp);
	fclose(fp);
#endif

	m_netHTTP.Setup(url, 80, urlappend, NetHTTP::END_OF_DATA_SIGNAL_HTTP);
	vector<string> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("x-goog-user-project: compact-lacing-260204");
	headers.push_back("Authorization: Bearer " + GetApp()->GetGoogleToken());
	m_netHTTP.SetCustomHeaders(headers);
	m_netHTTP.AddPostData("", (const byte*)postData.c_str(), (int)postData.length());
	m_netHTTP.Start();
	m_bWaitingForTranslation = true;
}

void TextAreaComponent::RequestTranslationDeepL()
{
	if (GetApp()->GetDeepLKey().empty())
	{
		string error = "Can't do deepl translation, API key missing";
		
		ShowQuickMessage(error);

		return;
	}

	unsigned int originalFileSize = 0;

	string sourceLanguage = m_textArea.language;
	string destLanguage = GetApp()->m_target_language;

	string textToTranslate;

	if (IsDialog(true))
	{
		textToTranslate = m_textArea.rawText;
	}
	else
	{
	
		for (int i = 0; i < m_textArea.m_lines.size(); i++)
		{
			if (i > 0)
			{
				textToTranslate += "\n";
			}
			textToTranslate += m_textArea.m_lines[i].m_text;
		}
	}
	string urlappend = "v2/translate";

#ifdef _DEBUG
	//LogMsg(textToTranslate.c_str());
	//let's see what we're sending, write it to a txt file so notepad can read the kanji or whatever right
	FILE* fp = fopen("translation_request.txt", "wb");
	fwrite(textToTranslate.c_str(), textToTranslate.length(), 1, fp);
	fclose(fp);

#endif
		
	m_netHTTP.Setup(GetApp()->m_deepl_api_url, 80, urlappend, NetHTTP::END_OF_DATA_SIGNAL_HTTP);
	m_netHTTP.AddPostData("auth_key", (const byte*)GetApp()->GetDeepLKey().c_str(), (int)GetApp()->GetDeepLKey().length());
	m_netHTTP.AddPostData("text", (const byte*)textToTranslate.c_str(), (int)textToTranslate.length());
	m_netHTTP.AddPostData("target_lang", (const byte*)ToUpperCaseString(destLanguage).c_str(), (int)destLanguage.length());

	m_netHTTP.Start();
	m_bWaitingForTranslation = true;
}

void TextAreaComponent::RequestTranslationGpt()
{
	string url = "https://api.openai.com";
	string urlappend = "v1/chat/completions";
	string textToTranslate;

	if (IsDialog(true))
	{
		textToTranslate = m_textArea.rawText;
	}
	else
	{
		for (int i = 0; i < m_textArea.m_lines.size(); i++)
		{
			textToTranslate += m_textArea.m_lines[i].m_text + "\n";
		}
	}

	//create json
	textToTranslate = "Translate the following texts to English. Only response with translated texts.\n\n" + textToTranslate;
	cJSON* userMessage = cJSON_CreateObject();
	cJSON_AddItemToObject(userMessage, "role", cJSON_CreateString("user"));
	cJSON_AddItemToObject(userMessage, "content", cJSON_CreateString(textToTranslate.c_str()));
	cJSON* messages = cJSON_CreateArray();
	cJSON_AddItemToArray(messages, userMessage);
	cJSON* root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "model", cJSON_CreateString("gpt-4o-mini-2024-07-18"));
	cJSON_AddItemToObject(root, "n", cJSON_CreateNumber(1));
	cJSON_AddItemToObject(root, "messages", messages);
	cJSON_AddItemToObject(root, "temperature", cJSON_CreateNumber(1));
	cJSON_AddItemToObject(root, "max_tokens", cJSON_CreateNumber(256));
	cJSON_AddItemToObject(root, "top_p", cJSON_CreateNumber(1));
	cJSON_AddItemToObject(root, "frequency_penalty", cJSON_CreateNumber(0));
	cJSON_AddItemToObject(root, "presence_penalty", cJSON_CreateNumber(0));
	cJSON* response_format = cJSON_CreateObject();
	cJSON_AddItemToObject(response_format, "type", cJSON_CreateString("text"));
	cJSON_AddItemToObject(root, "response_format", response_format);
	char* postData = cJSON_Print(root);

	m_netHTTP.Setup(url, 80, urlappend, NetHTTP::END_OF_DATA_SIGNAL_HTTP);
	vector<string> headers;
	headers.push_back("Content-Type: application/json; charset=utf-8");
	headers.push_back("Authorization: Bearer " + GetApp()->GetGptKey());
	headers.push_back("Accept: application/json, text/plain");
	m_netHTTP.SetCustomHeaders(headers);
	m_netHTTP.AddPostData("", (const byte*)postData, (int)strlen(postData));
	m_netHTTP.Start();
	m_bWaitingForTranslation = true;
}

void TextAreaComponent::RequestTranslation()
{
	if (GetApp()->m_target_language == "00")
	{
		m_bWaitingForTranslation = false;
		return;
	}

	if (GetApp()->GetTranslationEngine() == TRANSLATION_ENGINE_GOOGLE)
	{
		RequestTranslationGoogleBasic();
	}
	else if (GetApp()->GetTranslationEngine() == TRANSLATION_ENGINE_DEEPL)
	{
		RequestTranslationDeepL();
	}
	else if(GetApp()->GetTranslationEngine() == TRANSLATION_ENGINE_GPT)
	{
		RequestTranslationGpt();
	}
	else if (GetApp()->GetTranslationEngine() == TRANSLATION_ENGINE_GOOGLE_ADVANCED)
	{
		RequestTranslationGoogleAdvanced();
	}
}

glColorBytes TextAreaComponent::GetTextColor(bool bIsDialog)
{
	//what am I even doing here.  I guess I stopped using coloring here because of how the font rendering to texture works, we color it during the blit or something now
	if (bIsDialog)
	{
		return glColorBytes(255, 255, 255, 255);
	} 

	return glColorBytes(255, 255, 255, 255);
}

bool TextAreaComponent::IsDialog(bool bIsTranslating)
{
	if (!bIsTranslating) return false;

	if (GetApp()->GetGlobalTextHinting() == HINTING_DIALOG)
	{
		return true;
	}
	if (GetApp()->GetGlobalTextHinting() == HINTING_LINE_BY_LINE)
	{
		return false;
	}

	return m_textArea.m_bIsDialog;
}

void TextAreaComponent::OnSelected(VariantList* pVList) //0=vec2 point of click, 1=entity sent from
{
	Entity* pEntClicked = pVList->m_variant[1].GetEntity();
	LogMsg("Clicked %s entity at %s", pEntClicked->GetName().c_str(), pVList->m_variant[1].Print().c_str());

	int fingerID = pVList->m_variant[2].GetUINT32();
	//LogMsg("Clicked %s entity at %s", pEntClicked->GetName().c_str(),pVList->m_variant[0].Print().c_str());

	if (pEntClicked->GetName() == "SrcSpeakerIcon")
	{

		if (GetAudioManager()->IsPlaying(m_audioHandle))
		{
			StopSoundIfItWasPlaying();
			return;

		}
			if (fingerID == 0)
			{
				RequestAudio(true, true);
			}
			else
			{
				RequestAudio(false, true);
			}
		
		return;
	}

	//GetEntityRoot()->PrintTreeAsText(); //useful for debugging
}

void TextAreaComponent::Init(TextArea textArea)
{
	m_textArea = textArea;
	//assert(m_textArea.m_rect.left >= 0);

	SetSize2DEntity(GetParent(), textArea.m_rect.get_size_vec2());
	SetPos2DEntity(GetParent(), m_textArea.m_rect.get_top_left());
	if (m_textArea.language != GetApp()->m_target_language)
	{
		RequestTranslation();
	}
	else
	{
		//no need to translation, languages are the same
		m_bWaitingForTranslation = false;
	}

	float extraPaddingForHighlightBottom = 7;
	float extraPaddingForHighlightRight = 7;

	m_textAreaRect = textArea.m_rect;

	/*
	unsigned int color = MAKE_RGBA(0, 0, 0, 200);
	Entity *pParagraph = CreateOverlayRectEntity(GetParent()->GetParent(), rect, color);
	pParagraph->GetParent()->MoveEntityToBottomByAddress(pParagraph);
	eFont fontID;
	float fontScale = 1.0f;
	*/
	BuildSourceLanguageSurface();

	if (m_textArea.m_bIsDialog && GetApp()->GetVar("check_autoplay_audio")->GetUINT32() != 0)
	{
		GetApp()->GetAutoPlayManager()->OnAddDialog(this);
	}
	

	//RequestAudio();
	//GetFontAndScaleToFitThisPixelHeight(&fontID, &fontScale, rect.get_height());

//	m_pTextBox = CreateTextBoxEntity(pParagraph, "TextBox", CL_Vec2f(0, 0), rect.get_size_vec2(), "Translating...", fontScale);
//	SetupTextEntity(m_pTextBox, fontID, fontScale);

	//add speaker icon
	
	/*
	m_pSpeakerIconSrc = CreateOverlayButtonEntity(GetEntityRoot(), "SrcSpeakerIcon", "interface/speaker.rttex", tempRect.left, tempRect.top);
	SetAlignmentEntity(m_pSpeakerIconSrc, ALIGNMENT_DOWN_RIGHT);
	m_pSpeakerIconSrc->GetFunction("OnButtonSelected")->sig_function.connect(1, boost::bind(&TextAreaComponent::OnSelected, this, _1));
	SetTouchPaddingEntity(m_pSpeakerIconSrc, CL_Rectf(0, 0, 0, 0));

	*/
	/*
	m_pSpeakerIconDest = CreateOverlayButtonEntity(GetEntityRoot(), "DestSpeakerIcon", "interface/speaker.rttex", tempRect.left, tempRect.top);
	SetAlignmentEntity(m_pSpeakerIconDest, ALIGNMENT_DOWN_LEFT);
	m_pSpeakerIconDest->GetVar("rotation")->Set(180.0f);
	m_pSpeakerIconDest->GetFunction("OnButtonSelected")->sig_function.connect(1, boost::bind(&TextAreaComponent::OnSelected, this, _1));
	SetTouchPaddingEntity(m_pSpeakerIconDest, CL_Rectf(0, 0, 0, 0));
	*/

}

void TextAreaComponent::BuildSourceLanguageSurface()
{
	float height = 0;
	CL_Rectf tempRect = m_textAreaRect;

	TweakForSending(m_textArea.text, tempRect, height, false);

	vector<CL_Vec2f> offsets = ComputeLocalLineOffsets();
	float wordWrapX = 0;

	//rebuild version with line feeds as we're just displaying this as close as to the original as possible, no translation here as this is the source
	string versionWithLineFeeds;
	for (int i = 0; i < m_textArea.m_lines.size(); i++)
	{
		versionWithLineFeeds += m_textArea.m_lines[i].m_text + "\n";
	}

	m_pSourceLanguageSurf = GetApp()->GetFreeTypeManager(m_textArea.language)->GetFont()->TextToSurface(tempRect.get_size_vec2(), versionWithLineFeeds,
		height, glColorBytes(0, 0, 0, 0), GetTextColor(IsDialog(true)), m_textArea.language == "ja", &offsets,
		wordWrapX);

}

void TextAreaComponent::OnAdd(Entity *pEnt)
{
	EntityComponent::OnAdd(pEnt);

	GetParent()->AddComponent(new TouchHandlerComponent);
	SetTouchPaddingEntity(GetParent(), CL_Rectf(0, 0, 0, 0));
	GetParent()->GetFunction("OnTouchStart")->sig_function.connect(1, boost::bind(&TextAreaComponent::OnTouchStart, this, _1));
	m_pSize2d = &GetParent()->GetVar("size2d")->GetVector2();
	//LogMsg("TextArea added");
	m_pPos2d = &GetParent()->GetVar("pos2d")->GetVector2();
	GetParent()->GetFunction("OnUpdate")->sig_function.connect(1, boost::bind(&TextAreaComponent::OnUpdate, this, _1));
	GetParent()->GetFunction("OnRender")->sig_function.connect(1, boost::bind(&TextAreaComponent::OnRender, this, _1));
	GetApp()->m_sig_target_language_changed.connect(1, boost::bind(&TextAreaComponent::OnTargetLanguageChanged, this));
	GetApp()->m_sig_kill_all_text.connect(1, boost::bind(&TextAreaComponent::OnKillAllText, this));
}

extern void TurnOffRenderDisplay(VariantList* pVList);

void TextAreaComponent::OnTouchStart(VariantList *pVList)
{
	// Hack: Turn off overlay on click
	GetMessageManager()->CallStaticFunction(TurnOffRenderDisplay, 200, NULL);
	return;

	TouchTrackInfo *pTouch = GetBaseApp()->GetTouch(pVList->Get(2).GetUINT32());
	if (pTouch->WasHandled()) return;
	uint32 fingerID = pVList->Get(2).GetUINT32();

	pTouch->SetWasHandled(true, GetParent());
	
	if (fingerID == 0)
	{
		GetApp()->GetAutoPlayManager()->Reset();
		bool bToggleLanguage = (GetKeyState(VK_SHIFT) & 0x8000);
		
		//oh, check joystick button too
		Gamepad* pPad = GetGamepadManager()->GetDefaultGamepad();
		if (pPad)
		{
			GamepadButton* selectButton = pPad->GetVirtualButton(VIRTUAL_DPAD_SELECT);

			if (selectButton && selectButton->m_bDown)
			{
				bToggleLanguage = true;
			}
		}

		if (GetAudioManager()->IsPlaying(m_audioHandle))
		{
			StopSoundIfItWasPlaying();
			return;

		}
		
		RequestAudio(!bToggleLanguage, true);
		return;
	}
	
	if (GetKeyState(VK_DOWN) & 0x8000)
	{
		//right mouse button
		vector<unsigned short> utf16line;
		utf8::utf8to16(m_textArea.text.begin(), m_textArea.text.end(), back_inserter(utf16line));
		SetClipboardTextW(&utf16line[0], (int)utf16line.size());
		ShowQuickMessage("Copied original text to clipboard");
		return;
	}
	else if (! (GetKeyState(VK_SHIFT) & 0x8000))
	{
		//right mouse button
		vector<unsigned short> utf16line;
		string textToTranslate = m_translatedString;
		if (m_translatedString.empty())
		{
			//no translation was done, maybe because it was already the desired language.  Let's use the original for the cut and paste
			textToTranslate = m_textArea.text;
		}
		utf8::utf8to16(textToTranslate.begin(), textToTranslate.end(), back_inserter(utf16line));
		SetClipboardTextW(&utf16line[0], (int)utf16line.size());
		ShowQuickMessage("Copied translated text to clipboard");
		return;
	}
	else
	{
		//LogMsg("Clicked %s", PrintVector2(pTouch->GetPos()).c_str());

		//scroll through our letters?
		for (int l = 0; l < m_textArea.m_lines.size(); l++)
		{
			for (int i = 0; i < m_textArea.m_lines[l].m_words.size(); i++)
			{
				if (m_textArea.m_lines[l].m_words[i].m_rect.contains(pTouch->GetPos()))
				{
					//m_textArea.m_lines[l].m_words[i].m_word.c_str()
					//LogMsg("Clicked something at %s", PrintVector2(pTouch->GetPos()).c_str());

					vector<unsigned short> utf16line;

					string url = GetApp()->m_kanji_lookup_website + m_textArea.m_lines[l].m_words[i].m_word;

					utf8::utf8to16(url.begin(), url.end(), back_inserter(utf16line));

					utf16line.push_back(0);

					if (GetApp()->m_oldHWND != 0)
					{
						GetApp()->m_forceHWND = GetApp()->m_oldHWND;
					}
					LaunchURLW((uint16*)& utf16line[0]);

					return;
				}
			}
		}
	}

}

void TextAreaComponent::OnTargetLanguageChanged()
{
	SAFE_DELETE(m_pDestLanguageSurf);
	SAFE_DELETE(m_pSourceLanguageSurf);
	BuildSourceLanguageSurface();
	RequestTranslation();
}

void TextAreaComponent::OnKillAllText()
{
	GetParent()->SetTaggedForDeletion();
}

bool TextAreaComponent::TranslatingToAsianLanguage()
{
	return IsAsianLanguage(GetApp()->m_target_language);
}

bool TextAreaComponent::TranslatingFromAsianLanguage()
{
	return IsAsianLanguage(m_textArea.language);
}

void TextAreaComponent::FitText(float *pHeightInOut, float widthMod, int trueCharCount)
{

	float bestHeightSoFar = *pHeightInOut;
	float charWidth = 9999999.0f;
	float verticalLinesAvailable = 0;
	float canvasWidthTotalCharRoom = 0;

	bool bFirstTime = true;
	
	while (trueCharCount > canvasWidthTotalCharRoom)
	{
		if (!bFirstTime)
		{
			//too big. Make it smaller and try again
			*pHeightInOut -= ((*pHeightInOut) / 100.0f);
		}
		bFirstTime = false;
		charWidth = *pHeightInOut * widthMod;
		verticalLinesAvailable = m_textArea.m_rect.get_height() / *pHeightInOut;
		canvasWidthTotalCharRoom = (m_textArea.m_rect.get_width() / charWidth)*verticalLinesAvailable;
	}

}

void TextAreaComponent::TweakForSending(const string &text, CL_Rectf &rect, float &height, bool isTranslated)
{
	vector<string> lines;
	
	if (isTranslated)
	{
		//well, we can't be sure how many \n's have been inserted by the translation, so we'll process it
		lines = StringTokenize(text, "\n");
		if (lines.size() > 0 && lines[lines.size() - 1] == "")
		{
			//you know, we don't really need that blank entry after the \n. Just remove it
			lines.resize(lines.size() - 1);
		}
	}
	else
	{
		for (int i = 0; i < m_textArea.m_lines.size(); i++)
		{
			lines.push_back(m_textArea.m_lines[i].m_text);
		}
	}

	
	
	height = m_textArea.m_averageTextHeight;
	float widthMod = 0.60f;
	float originalHeight = m_textArea.m_averageTextHeight;
	float temp = 0;

	if (isTranslated)
	{
		temp = GetApp()->GetFreeTypeManager(GetApp()->m_target_language)->m_widthOverride;

		if (IsAsianLanguage(GetApp()->m_target_language) && !GetApp()->DoesFontHaveOverride(GetApp()->m_target_language))
		{
			//no override and it's an asian language?  Hack it to work better with fat kanji
			temp = 1.0f; //width is 0.8 of height on average
		}
		else
		{
			//default or override is fine
			temp = GetApp()->GetFreeTypeManager(GetApp()->m_target_language)->m_widthOverride;
		}
	}
	else
	{
		if (IsAsianLanguage(m_textArea.language) && !GetApp()->DoesFontHaveOverride(m_textArea.language))
		{
			//no override and it's an asian language?  Hack it to work better with fat kanji
			temp = 1.0f; //width is 0.8 of height on average
		}
		else
		{
			//default or override is fine
			temp = GetApp()->GetFreeTypeManager(m_textArea.language)->m_widthOverride;
		}

	}

	if (temp != 0.0f)
	{
		widthMod = temp;
	}
	
		//we might need more space
		float maxWidth = 0;
		int trueCharCount = 0;

		for (int i = 0; i < lines.size(); i++)
		{
			vector<unsigned short> utf16line;
			utf8::utf8to16(lines[i].begin(), lines[i].end(), back_inserter(utf16line));
			float width = utf16line.size()* widthMod * m_textArea.m_averageTextHeight;
			trueCharCount += (int)utf16line.size();
			maxWidth = rt_max(width, maxWidth);
		}

		if (IsDialog(isTranslated))
		{
			//word wrap mode
			FitText(&height, widthMod, trueCharCount);

			rtRectf textRect;
			GetApp()->GetFreeTypeManager(GetApp()->m_target_language)->GetFont()->MeasureText(&textRect, (WCHAR*) &m_textArea.wideText.at(0), (int)m_textArea.wideText.size(), height, true);
#ifdef _DEBUG
			LogMsg("Rect: %s", PrintRect(textRect).c_str());
#endif

			//     Mr. Tanaka

			if (height > m_textArea.m_averageTextHeight)
			{
				height = m_textArea.m_averageTextHeight;
			}
		}
		else
		{
			//what is this?!
			if (maxWidth > rect.get_width())
			{
				float ratio = rect.get_width() / maxWidth;
				height = height * ratio;
			}
		}
	

	if (!isTranslated)
	{
		height *= GetApp()->GetFreeTypeManager(m_textArea.language)->m_preTranslatedHeightMod;
	}

	//let's add extra space for no reason
	float expanderRatio = 1.5f;
	rect.set_height(rect.get_height()*expanderRatio);
	rect.set_width(rect.get_width()*expanderRatio);
}

vector<CL_Vec2f> TextAreaComponent::ComputeLocalLineOffsets()
{
	vector<CL_Vec2f> offsets;

	for (int i = 0; i < m_textArea.m_lineStarts.size(); i++)
	{
		offsets.push_back(m_textArea.m_lineStarts[i]- m_textAreaRect.get_top_left());
		//offsets.at(i).y += 20;
		
		assert(offsets.at(i).x >= 0 && offsets.at(i).y >= 0 &&  "Huh?  These shouldn't be negative");
	}

	return offsets;
}

void TextAreaComponent::RenderLineByLine()
{

	float height = 0;
	CL_Rectf tempRect = m_textAreaRect;

	TweakForSending(m_translatedString, tempRect, height, true);

	vector<CL_Vec2f> offsets = ComputeLocalLineOffsets();
	float wordWrapX = 0;
	if (IsDialog(true))
	{
		wordWrapX = m_textAreaRect.get_width();
	}
	SAFE_DELETE(m_pDestLanguageSurf);

	m_pDestLanguageSurf = GetApp()->GetFreeTypeManager(GetApp()->m_target_language)->GetFont()->TextToSurface(tempRect.get_size_vec2(), m_translatedString,
		height, glColorBytes(0, 0, 0, 0), GetTextColor(IsDialog(true)), GetApp()->m_target_language == "ja",
		&offsets, wordWrapX);
}

void TextAreaComponent::FitAndWordWrapToRect(const CL_Rectf &tempRect,  wstring &wtext, deque<wstring> &wlinesOut,
	CL_Vec2f &wrappedSizeOut, bool bUseActualWidthForSpacing, float &pixelHeightOut)
{
	if (pixelHeightOut == 0)
		pixelHeightOut = m_textArea.m_averageTextHeight;

	wrappedSizeOut = CL_Vec2f(2000000, 200000);
	bool bFirstTime = true;

	while (wrappedSizeOut.y > (tempRect.get_height()*1.0f))
	{

		if (pixelHeightOut < 0)
		{
			assert(!"The heck");
		}
		if (!bFirstTime)
		{
			//make it smaller, it still doesn't fit
			pixelHeightOut = pixelHeightOut *= 0.95f;
		}
		bFirstTime = false;
		wlinesOut.clear();
		//LogMsg("Trying size %.2f", pixelHeightOut);
		GetApp()->GetFreeTypeManager(GetApp()->m_target_language)->GetFont()->MeasureTextAndAddByLinesIntoDeque(tempRect.get_size_vec2(), wtext, &wlinesOut,
			pixelHeightOut, wrappedSizeOut, GetApp()->m_target_language == "ja");
	}
}

void TextAreaComponent::RenderAsDialog(float defaultFontHeightOrZeroForAuto)
{
	//build version with word wrapping
 	CL_Rectf tempRect = m_textAreaRect;

	vector<unsigned short> utf16line;
	utf8::utf8to16(m_translatedString.begin(), m_translatedString.end(), back_inserter(utf16line));

	deque<wstring> wlines;
	wstring wtext(utf16line.begin(), utf16line.end());
	
	CL_Vec2f wrappedSize;
	float pixelHeight = defaultFontHeightOrZeroForAuto;

	FitAndWordWrapToRect(tempRect, wtext, wlines, wrappedSize, GetApp()->m_target_language == "ja", pixelHeight);

	//LogMsg("Final output size: %s", PrintVector2(wrappedSize));

	//move deque into a single wide string
	wstring finalSingle;
	for (int i = 0; i < wlines.size(); i++)
	{
		finalSingle += wlines[i]+L"\n";
	}
	tempRect.bottom += tempRect.get_height();
	tempRect.right += tempRect.get_width();

	vector<uint16> vec (finalSingle.begin(), finalSingle.end());

	//Render it at that size
	m_pDestLanguageSurf = GetApp()->GetFreeTypeManager(GetApp()->m_target_language)->GetFont()->TextToSurface(tempRect.get_size_vec2(), vec,
		pixelHeight, glColorBytes(0, 0, 0, 0), GetTextColor(IsDialog(true)), GetApp()->m_target_language == "ja",
		NULL, 0);

}

bool TextAreaComponent::ReadTranslationFromJSONGoogleBasic(char *pData)
{
	m_bWaitingForTranslation = false;
	cJSON *root = cJSON_Parse(pData);
	cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
	cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
	cJSON *translations = cJSON_GetObjectItemCaseSensitive(data, "translations");
	cJSON *translation;

	if (error != NULL)
	{
		ShowQuickMessage("Error parsing json reply from google.  View error.txt!");
		FILE *fp = fopen("error.txt", "wb");
		fwrite(m_netHTTP.GetDownloadedData(), m_netHTTP.GetDownloadedBytes(), 1, fp);
		fclose(fp);
		return false;
	}

	cJSON_ArrayForEach(translation, translations)
	{
		cJSON *translatedText = cJSON_GetObjectItemCaseSensitive(translation, "translatedText");
		if (m_pTextBox)
			SetTextEntity(m_pTextBox, translatedText->valuestring);

		SAFE_DELETE(m_pDestLanguageSurf);

		float desiredHeight = m_textAreaRect.get_height();
		m_translatedString = translatedText->valuestring;
		float height = 0;
		CL_Rectf tempRect = m_textAreaRect;
		TweakForSending(m_translatedString, tempRect, height, true);

		if (IsDialog(true))
		{
			RenderAsDialog(height);
		}
		else
		{
			RenderLineByLine();
		}
	}
	
	return true;
}

bool TextAreaComponent::ReadTranslationFromJSONGoogleAdvanced(char* pData)
{
	m_bWaitingForTranslation = false;
	cJSON* root = cJSON_Parse(pData);
	cJSON* translations = cJSON_GetObjectItemCaseSensitive(root, "translations");
	cJSON* translation;

	cJSON_ArrayForEach(translation, translations)
	{
		cJSON* translatedText = cJSON_GetObjectItemCaseSensitive(translation, "translatedText");
		if (m_pTextBox)
			SetTextEntity(m_pTextBox, translatedText->valuestring);

		SAFE_DELETE(m_pDestLanguageSurf);

		float desiredHeight = m_textAreaRect.get_height();
		m_translatedString = translatedText->valuestring;
		float height = 0;
		CL_Rectf tempRect = m_textAreaRect;
		TweakForSending(m_translatedString, tempRect, height, true);

		if (IsDialog(true))
		{
			RenderAsDialog(height);
		}
		else
		{
			RenderLineByLine();
		}
	}

	return true;
}

bool TextAreaComponent::ReadTranslationFromJSONDeepl(char* pData)
{
	m_bWaitingForTranslation = false;
	cJSON* root = cJSON_Parse(pData);
	cJSON* error = cJSON_GetObjectItemCaseSensitive(root, "message");
	//cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
	cJSON* translations = cJSON_GetObjectItemCaseSensitive(root, "translations");
	cJSON* translation;

	if (m_netHTTP.GetDownloadedBytes() < 5)
	{
		ShowQuickMessage("Deepl sent a blank reply?  Probably bad API key!");
		return false;
	}

	if (error != NULL)
	{
		ShowQuickMessage(string("Deepl says: ")+error->valuestring+" View error.txt!");
		FILE* fp = fopen("error.txt", "wb");
		fwrite(m_netHTTP.GetDownloadedData(), m_netHTTP.GetDownloadedBytes(), 1, fp);
		fclose(fp);
		return false;
	}


	cJSON_ArrayForEach(translation, translations)
	{
		cJSON* translatedText = cJSON_GetObjectItemCaseSensitive(translation, "text");
		if (m_pTextBox)
			SetTextEntity(m_pTextBox, translatedText->valuestring);

		SAFE_DELETE(m_pDestLanguageSurf);

		float desiredHeight = m_textAreaRect.get_height();
		m_translatedString = translatedText->valuestring;

	
		float height = 0;
		CL_Rectf tempRect = m_textAreaRect;
		TweakForSending(m_translatedString , tempRect, height, true);

		if (IsDialog(true))
		{
			RenderAsDialog(height);
		}
		else
		{
			RenderLineByLine();
		}
	}

	return true;
}


bool TextAreaComponent::ReadTranslationFromJSONGpt(char* pData)
{
	m_bWaitingForTranslation = false;
	cJSON* root = cJSON_Parse(pData);

	// jsonResponse["choices"][0]["message"]["content"].get<std::string>();
	cJSON* translation = cJSON_GetObjectItemCaseSensitive(root, "choices");
	translation = cJSON_GetArrayItem(translation, 0)->child->next; // "message"
	translation = translation->child->next; // "content"

	if (m_netHTTP.GetDownloadedBytes() < 5)
	{
		ShowQuickMessage("Deepl sent a blank reply?  Probably bad API key!");
		return false;
	}

	if (m_pTextBox)
		SetTextEntity(m_pTextBox, translation->valuestring);

	SAFE_DELETE(m_pDestLanguageSurf);

	float desiredHeight = m_textAreaRect.get_height();
	m_translatedString = translation->valuestring;


	float height = 0;
	CL_Rectf tempRect = m_textAreaRect;
	TweakForSending(m_translatedString, tempRect, height, true);

	if (IsDialog(true))
	{
		RenderAsDialog(height);
	}
	else
	{
		RenderLineByLine();
	}

	return true;
}


bool TextAreaComponent::ReadAudioFromJSON(char* pData)
{
	cJSON* root = cJSON_Parse(pData);
	cJSON* error = cJSON_GetObjectItemCaseSensitive(root, "error");
	cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "audioContent");

	if (error != NULL)
	{
		TextScanner s;
		s.AppendFromMemoryAddress(pData);
		s.StripLeadingSpaces();

		string error = s.GetParmString("\"message\"", 1, ":");
		int code = StringToInt(s.GetParmString("\"code\"", 1, ":"));

		string msg = "Error.txt written: " + error;

		if (code == 400)
		{
			msg = m_lastTTSLanguageTarget +" unsupported for speech";
		}

		//UpdateStatusMessage(msg);
		GetApp()->m_pGameLogicComp->UpdateStatusMessage(msg);

		ShowQuickMessage(msg);
		FILE* fp = fopen("error.txt", "wb");
		fwrite(m_netAudioHTTP.GetDownloadedData(), m_netAudioHTTP.GetDownloadedBytes(), 1, fp);
		fclose(fp);
		return false;
	}

	if (!m_fileNameToRemove.empty())
	{
		RemoveFile(m_fileNameToRemove);
	}

	string m_translatedString = data->valuestring;
	string audioFile = base64_decode(m_translatedString.c_str(), m_translatedString.length());
	
	string fName = "temp_audio_" + toString(g_counter++)+".mp3";
	FILE* fp = fopen(fName.c_str(), "wb");
	fwrite(audioFile.c_str(), audioFile.length(), 1, fp);
	fclose(fp);

	StopSoundIfItWasPlaying();

	g_lastAudioHandle = m_audioHandle = GetAudioManager()->Play(fName, false, false, true, false);
	m_fileNameToRemove = fName;
	RemoveFile(fName);

	return true;
}

void TextAreaComponent::OnUpdate(VariantList *pVList)
{
	static bool bDidFirstTime = false;

	if (!bDidFirstTime)
	{
		//m_smartCurl.Start();
		bDidFirstTime = true;
	}

	m_netHTTP.Update();

	if (m_netHTTP.GetError() != NetHTTP::ERROR_NONE)
	{
		//Big error, show message
		LogMsg("NetHTTP error: %d", m_netHTTP.GetError());
	}

	if (m_netHTTP.GetState() == NetHTTP::STATE_FINISHED)
	{
#ifdef _DEBUG
		FILE *fp = fopen("language.json", "wb");
		fwrite(m_netHTTP.GetDownloadedData(), m_netHTTP.GetDownloadedBytes(), 1, fp);
		fclose(fp);
#endif

		if (GetApp()->GetTranslationEngine() == TRANSLATION_ENGINE_GOOGLE)
		{
			if (!ReadTranslationFromJSONGoogleBasic((char*)m_netHTTP.GetDownloadedData()))
			{
				LogMsg("Error parsing json translation reply from google");
			}
		}
		else if (GetApp()->GetTranslationEngine() == TRANSLATION_ENGINE_DEEPL)
		{
			if (!ReadTranslationFromJSONDeepl((char*)m_netHTTP.GetDownloadedData()))
			{
				LogMsg("Error parsing json translation reply from deepl");
			}
		}
		else if (GetApp()->GetTranslationEngine() == TRANSLATION_ENGINE_GPT)
		{
			if (!ReadTranslationFromJSONGpt((char*)m_netHTTP.GetDownloadedData()))
			{
				LogMsg("Error parsing json translation reply from deepl");
			}
		}
		else if (GetApp()->GetTranslationEngine() == TRANSLATION_ENGINE_GOOGLE_ADVANCED)
		{
			if (!ReadTranslationFromJSONGoogleAdvanced((char*)m_netHTTP.GetDownloadedData()))
			{
				LogMsg("Error parsing json translation reply from deepl");
			}
		}
		
		m_netHTTP.Reset(true);
	}

	m_netAudioHTTP.Update();

	if (m_netAudioHTTP.GetError() != NetHTTP::ERROR_NONE)
	{
		//Big error, show message
		LogMsg("m_netAudioHTTP error: %d", m_netAudioHTTP.GetError());
	}

	if (m_netAudioHTTP.GetState() == NetHTTP::STATE_FINISHED)
	{
#ifdef _DEBUG
		FILE* fp = fopen("audio.json", "wb");
		fwrite(m_netAudioHTTP.GetDownloadedData(), m_netAudioHTTP.GetDownloadedBytes(), 1, fp);
		fclose(fp);
#endif

		if (!ReadAudioFromJSON((char*)m_netAudioHTTP.GetDownloadedData()))
		{
			LogMsg("Error parsing json audio from google");
		}
		
		m_netAudioHTTP.Reset(true);
	}
}

void TextAreaComponent::DrawWordRectsForLine(LineInfo line)
{
	for (int i = 0; i < line.m_words.size(); i++)
	{
		DrawRect(line.m_words[i].m_rect, MAKE_RGBA(0, 0, 255, 200));
	}
}

void TextAreaComponent::DrawHighlightRectIfAudioIsPlaying()
{
	uint32 lineColor = MAKE_RGBA(255, 0, 0, 255);

	if (m_audioHandle != AUDIO_HANDLE_BLANK && GetAudioManager()->IsPlaying(m_audioHandle))
	{
		DrawRect(m_textAreaRect, lineColor, 3.0f);
	}

}

void TextAreaComponent::OnRender(VariantList *pVList)
{
	/*
	GetApp()->GetFont(FONT_LARGE)->DrawScaled(20, GetScreenSizeYf() - 200, );
	GetApp()->GetFont(FONT_LARGE)->DrawScaled(20, GetScreenSizeYf() - 100, "" + toString(GetApp()->m_energy), 2.0f, MAKE_RGBA(255, 255, 255, 255));
	*/

	if (GetApp()->IsHidingOverlays())
	{
		DrawHighlightRectIfAudioIsPlaying();
		return;
	}

	if (GetApp()->GetViewMode() == VIEW_MODE_HIDE_ALL)
	{

		if (GetBaseApp()->GetTouch(0)->IsDown())
		{
			for (int i = 0; i < m_textArea.m_lines.size(); i++)
			{
				DrawWordRectsForLine(m_textArea.m_lines[i]);
			}
		}

		DrawHighlightRectIfAudioIsPlaying();
		return;
	}

	CL_Vec2f vFinalPos = pVList->m_variant[0].GetVector2() + m_textArea.m_rect.get_top_left();

	//render our text areas

	//g_globalBatcher.Flush();

	/*
	DrawLine(lineColor, m_textArea.m_vPoints[0].x, m_textArea.m_vPoints[0].y,
		m_textArea.m_vPoints[1].x, m_textArea.m_vPoints[1].y);
	DrawLine(lineColor, m_textArea.m_vPoints[1].x, m_textArea.m_vPoints[1].y,
		m_textArea.m_vPoints[2].x, m_textArea.m_vPoints[2].y);

	DrawLine(lineColor, m_textArea.m_vPoints[2].x, m_textArea.m_vPoints[2].y,
		m_textArea.m_vPoints[3].x, m_textArea.m_vPoints[3].y);
	DrawLine(lineColor, m_textArea.m_vPoints[3].x, m_textArea.m_vPoints[3].y,
		m_textArea.m_vPoints[0].x, m_textArea.m_vPoints[0].y);
		*/
	DrawFilledRect(m_textAreaRect, MAKE_RGBA(0, 0, 0, 200));

	DrawHighlightRectIfAudioIsPlaying();
	
	if (GetApp()->GetViewMode() == VIEW_MODE_SHOW_SOURCE /*|| GetAudioManager()->IsPlaying(m_audioHandle)*/)
	{
		if (m_pSourceLanguageSurf)
		{
			m_pSourceLanguageSurf->Blit(vFinalPos.x, vFinalPos.y);
		}
		return;
	}

	//default

	if (m_pDestLanguageSurf)
	{
		if (IsDialog(true))
		{
			m_pDestLanguageSurf->Blit(vFinalPos.x, vFinalPos.y, MAKE_RGBA(0,255,0,255));
		}
		else
		{
			m_pDestLanguageSurf->Blit(vFinalPos.x, vFinalPos.y);
		}
		
	}
	else
	{
		if (IsDialog(true))
		{
			if (m_pSourceLanguageSurf)
			{
				m_pSourceLanguageSurf->Blit(vFinalPos.x, vFinalPos.y, MAKE_RGBA(0, 255, 0, 255));
			}
		} else
		if (m_pSourceLanguageSurf)
		{
			m_pSourceLanguageSurf->Blit(vFinalPos.x, vFinalPos.y);
		}

	}

	
	
	//if (GetBaseApp()->GetTouch(0)->IsDown())
	//{
	//	for (int i = 0; i < m_textArea.m_lines.size(); i++)
	//	{
	//		DrawWordRectsForLine(m_textArea.m_lines[i]);
	//	}
	//}
	

}

