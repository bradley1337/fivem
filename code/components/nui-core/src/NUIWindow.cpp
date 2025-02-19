/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include "NUIWindow.h"

#include "NUIClient.h"
#include "NUIWindowManager.h"

#include <Error.h>

#include <LaunchMode.h>
#include <CoreConsole.h>

#include <include/base/cef_bind.h>
#include <include/wrapper/cef_closure_task.h>
#include <include/cef_request_context_handler.h>

#include <CefOverlay.h>

extern nui::GameInterface* g_nuiGi;

#include "memdbgon.h"

extern std::wstring GetNUIStoragePath();

namespace nui
{
extern bool g_rendererInit;

extern void AddSchemeHandlerFactories(CefRefPtr<CefRequestContext> rc);
}

NUIWindow::NUIWindow(bool rawBlit, int width, int height, const std::string& windowContext)
	: m_rawBlit(rawBlit), m_width(width), m_height(height), m_renderBuffer(nullptr), m_dirtyFlag(0), m_onClientCreated(nullptr), m_nuiTexture(nullptr), m_popupTexture(nullptr), m_swapTexture(nullptr),
	  m_swapRtv(nullptr), m_swapSrv(nullptr), m_dereferencedNuiTexture(false), m_lastFrameTime(0), m_lastMessageTime(0), m_roundedHeight(0), m_roundedWidth(0),
	  m_syncKey(0), m_paintType(NUIPaintTypeDummy), m_windowContext(windowContext)
{
	memset(&m_lastDirtyRect, 0, sizeof(m_lastDirtyRect));

	Instance<NUIWindowManager>::Get()->AddWindow(this);
}

static void CloseBrowser(CefRefPtr<CefBrowser> browser)
{
	browser->GetHost()->CloseBrowser(true);
}

NUIWindow::~NUIWindow()
{
	auto nuiClient = ((NUIClient*)m_client.get());

	if (nuiClient)
	{
		nuiClient->SetWindowValid(false);
		nuiClient->ClearWindow();

		std::unique_lock _(nuiClient->GetWindowLock());
		if (nuiClient->GetBrowser() && nuiClient->GetBrowser()->GetHost())
		{
			if (!CefCurrentlyOn(TID_UI))
			{
				CefPostTask(TID_UI, base::Bind(&::CloseBrowser, nuiClient->GetBrowser()));
			}
			else
			{
				nuiClient->GetBrowser()->GetHost()->CloseBrowser(true);
			}
		}
	}

	if (m_swapTexture)
	{
		m_swapTexture->Release();
	}

	if (m_swapRtv)
	{
		m_swapRtv->Release();
	}

	if (m_swapSrv)
	{
		m_swapSrv->Release();
	}

	if (m_renderBuffer)
	{
		delete[] m_renderBuffer;
	}

	Instance<NUIWindowManager>::Get()->RemoveWindow(this);
}

fwRefContainer<NUIWindow> NUIWindow::Create(bool primary, int width, int height, CefString url, bool instant, const std::string& context)
{
	auto window = new NUIWindow(primary, width, height, context);

	if (instant)
	{
		window->Initialize(url);
	}
	else
	{
		window->m_initUrl = url;
	}

	return window;
}

void NUIWindow::DeferredCreate()
{
	if (!m_client)
	{
		Initialize(m_initUrl);
	}
}

#pragma region shaders
const BYTE quadPS[] =
{
	68,  88,  66,  67,  58,  78,
	234,  91, 133,  80, 171, 186,
	78,  67, 133,  59, 192,  44,
	182,  57,   1,   0,   0,   0,
	60,   2,   0,   0,   5,   0,
	0,   0,  52,   0,   0,   0,
	200,   0,   0,   0,  32,   1,
	0,   0,  84,   1,   0,   0,
	192,   1,   0,   0,  82,  68,
	69,  70, 140,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   2,   0,   0,   0,
	28,   0,   0,   0,   0,   4,
	255, 255,   0,   1,   0,   0,
	99,   0,   0,   0,  92,   0,
	0,   0,   3,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   1,   0,
	0,   0,   0,   0,   0,   0,
	96,   0,   0,   0,   2,   0,
	0,   0,   5,   0,   0,   0,
	4,   0,   0,   0, 255, 255,
	255, 255,   0,   0,   0,   0,
	1,   0,   0,   0,  12,   0,
	0,   0, 115, 109, 112,   0,
	116, 120,   0,  77, 105,  99,
	114, 111, 115, 111, 102, 116,
	32,  40,  82,  41,  32,  72,
	76,  83,  76,  32,  83, 104,
	97, 100, 101, 114,  32,  67,
	111, 109, 112, 105, 108, 101,
	114,  32,  49,  48,  46,  49,
	0, 171,  73,  83,  71,  78,
	80,   0,   0,   0,   2,   0,
	0,   0,   8,   0,   0,   0,
	56,   0,   0,   0,   0,   0,
	0,   0,   1,   0,   0,   0,
	3,   0,   0,   0,   0,   0,
	0,   0,  15,   0,   0,   0,
	68,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	3,   0,   0,   0,   1,   0,
	0,   0,   3,   3,   0,   0,
	83,  86,  95,  80,  79,  83,
	73,  84,  73,  79,  78,   0,
	84,  69,  88,  67,  79,  79,
	82,  68,   0, 171, 171, 171,
	79,  83,  71,  78,  44,   0,
	0,   0,   1,   0,   0,   0,
	8,   0,   0,   0,  32,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   3,   0,
	0,   0,   0,   0,   0,   0,
	15,   0,   0,   0,  83,  86,
	95,  84,  65,  82,  71,  69,
	84,   0, 171, 171,  83,  72,
	68,  82, 100,   0,   0,   0,
	64,   0,   0,   0,  25,   0,
	0,   0,  90,   0,   0,   3,
	0,  96,  16,   0,   0,   0,
	0,   0,  88,  24,   0,   4,
	0, 112,  16,   0,   0,   0,
	0,   0,  85,  85,   0,   0,
	98,  16,   0,   3,  50,  16,
	16,   0,   1,   0,   0,   0,
	101,   0,   0,   3, 242,  32,
	16,   0,   0,   0,   0,   0,
	69,   0,   0,   9, 242,  32,
	16,   0,   0,   0,   0,   0,
	70,  16,  16,   0,   1,   0,
	0,   0,  70, 126,  16,   0,
	0,   0,   0,   0,   0,  96,
	16,   0,   0,   0,   0,   0,
	62,   0,   0,   1,  83,  84,
	65,  84, 116,   0,   0,   0,
	2,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	2,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   1,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   1,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0
};

const BYTE quadVS[] =
{
	68,  88,  66,  67, 203, 141,
	78, 146,   5, 246, 239, 246,
	166,  36, 242, 232,  80,   1,
	231, 115,   1,   0,   0,   0,
	208,   2,   0,   0,   5,   0,
	0,   0,  52,   0,   0,   0,
	128,   0,   0,   0, 180,   0,
	0,   0,  12,   1,   0,   0,
	84,   2,   0,   0,  82,  68,
	69,  70,  68,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	28,   0,   0,   0,   0,   4,
	254, 255,   0,   1,   0,   0,
	28,   0,   0,   0,  77, 105,
	99, 114, 111, 115, 111, 102,
	116,  32,  40,  82,  41,  32,
	72,  76,  83,  76,  32,  83,
	104,  97, 100, 101, 114,  32,
	67, 111, 109, 112, 105, 108,
	101, 114,  32,  49,  48,  46,
	49,   0,  73,  83,  71,  78,
	44,   0,   0,   0,   1,   0,
	0,   0,   8,   0,   0,   0,
	32,   0,   0,   0,   0,   0,
	0,   0,   6,   0,   0,   0,
	1,   0,   0,   0,   0,   0,
	0,   0,   1,   1,   0,   0,
	83,  86,  95,  86,  69,  82,
	84,  69,  88,  73,  68,   0,
	79,  83,  71,  78,  80,   0,
	0,   0,   2,   0,   0,   0,
	8,   0,   0,   0,  56,   0,
	0,   0,   0,   0,   0,   0,
	1,   0,   0,   0,   3,   0,
	0,   0,   0,   0,   0,   0,
	15,   0,   0,   0,  68,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   3,   0,
	0,   0,   1,   0,   0,   0,
	3,  12,   0,   0,  83,  86,
	95,  80,  79,  83,  73,  84,
	73,  79,  78,   0,  84,  69,
	88,  67,  79,  79,  82,  68,
	0, 171, 171, 171,  83,  72,
	68,  82,  64,   1,   0,   0,
	64,   0,   1,   0,  80,   0,
	0,   0,  96,   0,   0,   4,
	18,  16,  16,   0,   0,   0,
	0,   0,   6,   0,   0,   0,
	103,   0,   0,   4, 242,  32,
	16,   0,   0,   0,   0,   0,
	1,   0,   0,   0, 101,   0,
	0,   3,  50,  32,  16,   0,
	1,   0,   0,   0, 104,   0,
	0,   2,   2,   0,   0,   0,
	54,   0,   0,   8, 194,  32,
	16,   0,   0,   0,   0,   0,
	2,  64,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	128,  63,   1,   0,   0,   7,
	18,   0,  16,   0,   0,   0,
	0,   0,  10,  16,  16,   0,
	0,   0,   0,   0,   1,  64,
	0,   0,   1,   0,   0,   0,
	85,   0,   0,   7, 130,   0,
	16,   0,   0,   0,   0,   0,
	10,  16,  16,   0,   0,   0,
	0,   0,   1,  64,   0,   0,
	1,   0,   0,   0,  86,   0,
	0,   5,  50,   0,  16,   0,
	0,   0,   0,   0, 198,   0,
	16,   0,   0,   0,   0,   0,
	0,   0,   0,  10,  50,   0,
	16,   0,   1,   0,   0,   0,
	70,   0,  16,   0,   0,   0,
	0,   0,   2,  64,   0,   0,
	0,   0,   0, 191,   0,   0,
	0, 191,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   8,  66,   0,  16,   0,
	0,   0,   0,   0,  26,   0,
	16, 128,  65,   0,   0,   0,
	0,   0,   0,   0,   1,  64,
	0,   0,   0,   0, 128,  63,
	54,   0,   0,   5,  50,  32,
	16,   0,   1,   0,   0,   0,
	134,   0,  16,   0,   0,   0,
	0,   0,   0,   0,   0,   7,
	18,  32,  16,   0,   0,   0,
	0,   0,  10,   0,  16,   0,
	1,   0,   0,   0,  10,   0,
	16,   0,   1,   0,   0,   0,
	56,   0,   0,   7,  34,  32,
	16,   0,   0,   0,   0,   0,
	26,   0,  16,   0,   1,   0,
	0,   0,   1,  64,   0,   0,
	0,   0,   0, 192,  62,   0,
	0,   1,  83,  84,  65,  84,
	116,   0,   0,   0,  10,   0,
	0,   0,   2,   0,   0,   0,
	0,   0,   0,   0,   3,   0,
	0,   0,   4,   0,   0,   0,
	0,   0,   0,   0,   2,   0,
	0,   0,   1,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   2,   0,   0,   0,
	0,   0,   0,   0,   1,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,
	0,   0,   0,   0,   0,   0
}; 
#pragma endregion

static auto roundUp(int x, int y)
{
	return x + (y - (x % y));
}

void NUIWindow::Initialize(CefString url)
{
#if defined(GTA_NY)
	static bool nuiSharedResourcesEnabled = false;
#else
	static bool nuiSharedResourcesEnabled = true;
	static ConVar<bool> nuiSharedResources("nui_useSharedResources", ConVar_Archive, true, &nuiSharedResourcesEnabled);
#endif

	// Vulkan/D3D12(on7) don't support shared resources before Windows 10
#if defined(IS_RDR3)
	if (!IsWindows10OrGreater())
	{
		nuiSharedResourcesEnabled = false;
	}
#endif

	if (m_renderBuffer)
	{
		delete[] m_renderBuffer;
	}

	// create the temporary backing store
	m_roundedHeight = roundUp(m_height, 16);
	m_roundedWidth = roundUp(m_width, 16);

	InitializeRenderBacking();

	// create the client/browser instance
	m_client = new NUIClient(this);

	CefWindowInfo info;
	info.SetAsWindowless(NULL);
	info.shared_texture_enabled = (!CfxIsWine() && nuiSharedResourcesEnabled);
	info.external_begin_frame_enabled = nuiSharedResourcesEnabled;
	info.width = m_width;
	info.height = m_height;

	CefBrowserSettings settings;
	settings.javascript_close_windows = STATE_DISABLED;
	//settings.web_security = STATE_DISABLED;
	settings.windowless_frame_rate = 240;
	CefString(&settings.default_encoding).FromString("utf-8");

	CefRefPtr<CefRequestContext> rc;

	if (m_windowContext.empty())
	{
		rc = CefRequestContext::GetGlobalContext();
	}
	else
	{
		auto cachePath = fmt::sprintf(L"%s\\context-%s", GetNUIStoragePath(), ToWide(m_windowContext));
		CreateDirectory(cachePath.c_str(), nullptr);

		CefRequestContextSettings rcConfig;
		CefString(&rcConfig.cache_path).FromWString(cachePath);
		rc = CefRequestContext::CreateContext(rcConfig, {});

		nui::AddSchemeHandlerFactories(rc);
	}

	CefBrowserHost::CreateBrowser(info, m_client, url, settings, {}, rc);

	if (!info.shared_texture_enabled)
	{
		m_renderBuffer = new char[4 * m_roundedWidth * m_roundedHeight];
	}
}

void NUIWindow::InitializeRenderBacking()
{
	if (!nui::g_rendererInit)
	{
		return;
	}

	// create the backing texture
	{
		std::lock_guard<std::shared_mutex> _(m_textureMutex);
		m_nuiTexture = g_nuiGi->CreateTextureBacking(m_width, m_height, nui::GITextureFormat::ARGB);
	}

	if (!m_rawBlit)
	{
		D3D11_TEXTURE2D_DESC tgtDesc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_B8G8R8A8_UNORM, m_width, m_height, 1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);

		auto d3d = g_nuiGi->GetD3D11Device();
		d3d->CreateTexture2D(&tgtDesc, nullptr, &m_swapTexture);

		D3D11_RENDER_TARGET_VIEW_DESC rtDesc = CD3D11_RENDER_TARGET_VIEW_DESC(m_swapTexture, D3D11_RTV_DIMENSION_TEXTURE2D);

		d3d->CreateRenderTargetView(m_swapTexture, &rtDesc, &m_swapRtv);
	}
}

void NUIWindow::AddDirtyRect(const CefRect& rect)
{
	m_dirtyRects.push(rect);
}

CefBrowser* NUIWindow::GetBrowser()
{
	if (!m_client)
	{
		return nullptr;
	}

	return ((NUIClient*)m_client.get())->GetBrowser();
}

void NUIWindow::UpdateSharedResource(void* sharedHandle, uint64_t syncKey, const CefRenderHandler::RectList& rects, CefRenderHandler::PaintElementType type)
{
	if (!m_rawBlit && type != CefRenderHandler::PaintElementType::PET_VIEW)
	{
		return;
	}

	if (!m_nuiTexture.GetRef())
	{
		// hope we'll resize soon
		return;
	}

	HANDLE parentHandle = (void*)sharedHandle;
	m_syncKey = syncKey;
	
	{
		if (sharedHandle != m_lastParentHandle[type])
		{
			m_lastParentHandle[type] = parentHandle;

			auto& texRef = (type == CefRenderHandler::PaintElementType::PET_VIEW) ? m_nuiTexture : m_popupTexture;

			int w, h;

			if (type == CefRenderHandler::PaintElementType::PET_VIEW)
			{
				w = m_width;
				h = m_height;
			}
			else
			{
				w = m_popupRect.width;
				h = m_popupRect.height;
			}

			if (!m_rawBlit)
			{
				auto oldRef = m_parentTextures[type];

				auto fakeTexRef = g_nuiGi->CreateTextureFromShareHandle(parentHandle, w, h);
				SetParentTexture(type, fakeTexRef);

				auto oldSrv = m_swapSrv;

				struct
				{
					void* vtbl;
					ID3D11Device* rawDevice;
				}*deviceStuff = (decltype(deviceStuff))g_nuiGi->GetD3D11Device();

				auto nativeTexture = GetParentTexture(CefRenderHandler::PaintElementType::PET_VIEW)->GetNativeTexture();

				if (nativeTexture)
				{
					deviceStuff->rawDevice->CreateShaderResourceView((ID3D11Resource*)nativeTexture, nullptr, &m_swapSrv);
				}
				else
				{
					m_swapSrv = {};
				}

				if (oldSrv)
				{
					oldSrv->Release();
				}
			}
			else
			{
				std::lock_guard<std::shared_mutex> _(m_textureMutex);

				texRef = g_nuiGi->CreateTextureFromShareHandle(parentHandle, w, h);
				SetParentTexture(type, texRef);
			}
		}
	}

	for (auto& rect : rects)
	{
		int x = rect.x;
		int y = rect.y;
		int width = rect.width;
		int height = rect.height;

		RECT newRect;
		newRect.left = x;
		newRect.right = x + width;
		newRect.top = GetHeight() - y - height;
		newRect.bottom = GetHeight() - y;
		//newRect.top = y;
		//newRect.bottom = y + height;
		
		RECT oldRect = m_lastDirtyRect;

		UnionRect(&m_lastDirtyRect, &newRect, &oldRect);
	}

	MarkRenderBufferDirty();
}

#include <d3d11_1.h>
#include <mmsystem.h>

extern bool HasFocus();

void NUIWindow::SendBeginFrame()
{
	bool isFocus = false;
	auto curTime = timeGetTime();

	if (m_client)
	{
		auto client = ((NUIClient*)m_client.get());

		auto browser = client->GetBrowser();

		if (browser)
		{
			if (nui::GetFocusBrowser() == browser)
			{
				isFocus = true;
			}
		}
	}

	if (m_client)
	{
		auto client = ((NUIClient*)m_client.get());

		auto browser = client->GetBrowser();

		if (browser)
		{
			auto host = browser->GetHost();

			if (host)
			{
#ifndef GTA_NY
				host->SendExternalBeginFrame();
#endif
				m_lastFrameTime = curTime;
			}
		}
	}
}

void NUIWindow::TouchMessage()
{
	m_lastMessageTime = timeGetTime();
}

void NUIWindow::UpdateFrame()
{
	if (m_client)
	{
		auto browser = ((NUIClient*)m_client.get())->GetBrowser();

		if (browser)
		{
			// the CEF API has a 'is muted' getter but it doesn't work
			bool shouldMute = false;
			g_nuiGi->QueryShouldMute(shouldMute);

			if (!m_isMuted && shouldMute)
			{
				browser->GetHost()->SetAudioMuted(true);
				m_isMuted = true;
			}
			else if (m_isMuted && !shouldMute)
			{
				browser->GetHost()->SetAudioMuted(false);
				m_isMuted = false;
			}
		}
	}

#if !defined(IS_RDR3) && !defined(GTA_NY)
	if (GetPaintType() != NUIPaintTypePostRender)
#endif
	{
		SendBeginFrame();
	}

	if (!GetTexture().GetRef())
	{
		return;
	}

	if (m_rawBlit)
	{		
		int resX, resY;
		g_nuiGi->GetGameResolution(&resX, &resY);

		if (m_width != resX || m_height != resY)
		{
			m_width = resX;
			m_height = resY;

			{
				auto _ = GetRenderBufferLock();
				m_roundedHeight = roundUp(m_height, 16);
				m_roundedWidth = roundUp(m_width, 16);

				if (m_renderBuffer)
				{
					delete[] m_renderBuffer;
					m_renderBuffer = new char[4 * m_roundedWidth * m_roundedHeight];
				}
			}

			if (m_client)
			{
				auto browser = ((NUIClient*)m_client.get())->GetBrowser();

				if (browser)
				{
					((NUIClient*)m_client.get())->GetBrowser()->GetHost()->WasResized();
				}
				else
				{
					m_onLoadQueue.push([this]()
					{
						((NUIClient*)m_client.get())->GetBrowser()->GetHost()->WasResized();
					});
				}

				std::lock_guard<std::shared_mutex> _(m_textureMutex);
				m_nuiTexture = g_nuiGi->CreateTextureBacking(m_width, m_height, nui::GITextureFormat::ARGB);
			}
		}

		for (auto& item : m_pollQueue)
		{
			NUIClient* client = static_cast<NUIClient*>(m_client.get());
			auto browser = client->GetBrowser();

			auto message = CefProcessMessage::Create("doPoll");
			auto argList = message->GetArgumentList();

			argList->SetSize(1);
			argList->SetString(0, item);

			browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
		}
	}

	m_pollQueue.clear();

	NUIWindowManager* wm = Instance<NUIWindowManager>::Get();
	auto texture = GetParentTexture(CefRenderHandler::PaintElementType::PET_VIEW);

	if (texture.GetRef())
	{
		//
		// dirty flag checking and CopySubresourceRegion are disabled here due to some issue
		// with scheduling frame copies from the OnAcceleratedPaint handler.
		//
		// this'll use a bunch of additional GPU memory bandwidth, but this should not be an
		// issue on any modern GPU.
		//
		//if (InterlockedExchange(&m_dirtyFlag, 0) > 0)
		if (!m_rawBlit)
		{
			HRESULT hr = S_OK;

			if (hr == S_OK)
			{
				ID3D11Device* device = g_nuiGi->GetD3D11Device();

				if (device)
				{
					ID3D11DeviceContext* deviceContext = g_nuiGi->GetD3D11DeviceContext();
					assert(deviceContext);

					D3D11_BOX box = CD3D11_BOX(m_lastDirtyRect.left,
											   m_lastDirtyRect.top,
											   0,
											   m_lastDirtyRect.right,
											   m_lastDirtyRect.bottom,
											   1);

					if (m_rawBlit)
					{
						g_nuiGi->BlitTexture(GetTexture(), texture);
					}
					else
					{
						//
						// LOTS of D3D11 garbage to flip a texture...
						//
						static ID3D11BlendState* bs;
						static ID3D11SamplerState* ss;
						static ID3D11VertexShader* vs;
						static ID3D11PixelShader* ps;

						static std::once_flag of;
						std::call_once(of, []()
						{
							D3D11_SAMPLER_DESC sd = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
							g_nuiGi->GetD3D11Device()->CreateSamplerState(&sd, &ss);

							D3D11_BLEND_DESC bd = CD3D11_BLEND_DESC(CD3D11_DEFAULT());
							g_nuiGi->GetD3D11Device()->CreateBlendState(&bd, &bs);

							g_nuiGi->GetD3D11Device()->CreateVertexShader(quadVS, sizeof(quadVS), nullptr, &vs);
							g_nuiGi->GetD3D11Device()->CreatePixelShader(quadPS, sizeof(quadPS), nullptr, &ps);
						});

						ID3DUserDefinedAnnotation* pPerf;
						deviceContext->QueryInterface(__uuidof(pPerf), reinterpret_cast<void**>(&pPerf));

						pPerf->BeginEvent(L"DRAWSHIT");

						ID3D11RenderTargetView* oldRtv = nullptr;
						ID3D11DepthStencilView* oldDsv = nullptr;
						deviceContext->OMGetRenderTargets(1, &oldRtv, &oldDsv);

						ID3D11SamplerState* oldSs;
						ID3D11BlendState* oldBs;
						ID3D11PixelShader* oldPs;
						ID3D11VertexShader* oldVs;
						ID3D11ShaderResourceView* oldSrv;

						D3D11_VIEWPORT oldVp;
						UINT numVPs = 1;

						D3D11_RECT oldSr;
						UINT numSRs = 1;

						deviceContext->RSGetScissorRects(&numSRs, &oldSr);
						deviceContext->RSGetViewports(&numVPs, &oldVp);

						deviceContext->OMGetBlendState(&oldBs, nullptr, nullptr);

						deviceContext->PSGetShader(&oldPs, nullptr, nullptr);
						deviceContext->PSGetSamplers(0, 1, &oldSs);
						deviceContext->PSGetShaderResources(0, 1, &oldSrv);

						deviceContext->VSGetShader(&oldVs, nullptr, nullptr);

						deviceContext->OMSetRenderTargets(1, &m_swapRtv, nullptr);
						deviceContext->OMSetBlendState(bs, nullptr, 0xffffffff);

						CD3D11_VIEWPORT vp = CD3D11_VIEWPORT(0.0f, 0.0f, m_width, m_height);
						deviceContext->RSSetViewports(1, &vp);

						CD3D11_RECT sr = CD3D11_RECT(0, 0, m_width, m_height);
						deviceContext->RSSetScissorRects(1, &sr);

						deviceContext->PSSetShader(ps, nullptr, 0);
						deviceContext->PSSetSamplers(0, 1, &ss);
						deviceContext->PSSetShaderResources(0, 1, &m_swapSrv);

						deviceContext->VSSetShader(vs, nullptr, 0);

						D3D11_PRIMITIVE_TOPOLOGY oldTopo;
						deviceContext->IAGetPrimitiveTopology(&oldTopo);

						ID3D11InputLayout* oldLayout;
						deviceContext->IAGetInputLayout(&oldLayout);

						deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
						deviceContext->IASetInputLayout(nullptr);

						deviceContext->Draw(4, 0);

						deviceContext->CopyResource((ID3D11Resource*)GetTexture()->GetNativeTexture(), m_swapTexture);

						deviceContext->OMSetRenderTargets(1, &oldRtv, oldDsv);

						deviceContext->IASetPrimitiveTopology(oldTopo);
						deviceContext->IASetInputLayout(oldLayout);

						deviceContext->VSSetShader(oldVs, nullptr, 0);
						deviceContext->PSSetShader(oldPs, nullptr, 0);
						deviceContext->PSSetSamplers(0, 1, &oldSs);
						deviceContext->PSSetShaderResources(0, 1, &oldSrv);
						deviceContext->OMSetBlendState(oldBs, nullptr, 0xffffffff);
						deviceContext->RSSetViewports(1, &oldVp);
						deviceContext->RSSetScissorRects(numSRs, &oldSr);

						if (oldVs)
						{
							oldVs->Release();
						}

						if (oldPs)
						{
							oldPs->Release();
						}

						if (oldBs)
						{
							oldBs->Release();
						}

						if (oldSs)
						{
							oldSs->Release();
						}

						if (oldSrv)
						{
							oldSrv->Release();
						}

						if (oldRtv)
						{
							oldRtv->Release();
						}

						if (oldDsv)
						{
							oldDsv->Release();
						}

						if (oldLayout)
						{
							oldLayout->Release();
						}

						pPerf->EndEvent();

						pPerf->Release();
					}

					memset(&m_lastDirtyRect, 0, sizeof(m_lastDirtyRect));
				}
			}
			else
			{
				MarkRenderBufferDirty();
			}
		}
	}
	else if (m_renderBuffer)
	{
		if (InterlockedExchange(&m_dirtyFlag, 0) > 0)
		{
			void* pBits = nullptr;
			int pitch;
			bool discarded = false;

			nui::GILockedTexture lockedTexture;

			if (GetTexture()->Map(0, 0, &lockedTexture, nui::GILockFlags::Write))
			{
				pBits = lockedTexture.pBits;
				pitch = lockedTexture.pitch;
			}
			else if (GetTexture()->Map(0, 0, &lockedTexture, nui::GILockFlags::WriteDiscard))
			{
				pBits = lockedTexture.pBits;
				pitch = lockedTexture.pitch;

				discarded = true;
			}
			else
			{
				// really
				pBits = nullptr;
			}

			if (pBits)
			{
				if (!discarded)
				{
					while (!m_dirtyRects.empty())
					{
						auto _ = GetRenderBufferLock();
						CefRect rect = m_dirtyRects.front();
						m_dirtyRects.pop();

						if (pitch >= (rect.width * 4))
						{
							int height = m_height;

							// ignore invalid height/width
							if (((rect.y + rect.height) > height) || ((rect.x + rect.width) > m_roundedWidth))
							{
								continue;
							}

							for (int y = rect.y; y < (rect.y + rect.height); y++)
							{
								int dy = height - y - 1;

								int* src = &((int*)(m_renderBuffer))[(y * m_roundedWidth) + rect.x];
								int* dest = &((int*)(pBits))[(dy * (pitch / 4)) + rect.x];

								memcpy(dest, src, (rect.width * 4));
							}
						}
					}
				}
				else
				{
					auto _ = GetRenderBufferLock();
					m_dirtyRects = std::queue<CefRect>();

					memcpy(pBits, m_renderBuffer, m_height * pitch);
				}

				GetTexture()->Unmap(&lockedTexture);
			}
		}
	}
}

void NUIWindow::SignalPoll(std::string& argument)
{
	if (m_pollQueue.find(argument) == m_pollQueue.end())
	{
		m_pollQueue.insert(argument);
	}
}

void NUIWindow::HandlePopupShow(bool show)
{
	if (!show)
	{
		if (m_parentTextures[CefRenderHandler::PaintElementType::PET_POPUP].GetRef())
		{
			m_parentTextures[CefRenderHandler::PaintElementType::PET_POPUP] = nullptr;

			std::lock_guard<std::shared_mutex> _(m_textureMutex);
			m_popupTexture = nullptr;
		}
	}
}

void NUIWindow::SetPopupRect(const CefRect& rect)
{
	m_popupRect = rect;

	std::lock_guard<std::shared_mutex> _(m_textureMutex);
	m_popupTexture = g_nuiGi->CreateTextureBacking(rect.width, rect.height, nui::GITextureFormat::ARGB);
}

void NUIWindow::SetPaintType(NUIPaintType type)
{
	m_paintType = type;
}

void NUIWindow::Invalidate()
{
	((NUIClient*)m_client.get())->GetBrowser()->GetHost()->Invalidate(PET_VIEW);
}
