/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include "FontRendererImpl.h"
#include <DrawCommands.h>
#include <grcTexture.h>
#include <ICoreGameInit.h>
#include <CoreConsole.h>
#include <LaunchMode.h>
#include <CrossBuildRuntime.h>
#include <utf8.h>
#include <Hooking.h>
#include <CL2LaunchMode.h>

#include "memdbgon.h"

class GtaGameInterface : public FontRendererGameInterface
{
private:
#ifdef _HAVE_GRCORE_NEWSTATES
	uint32_t m_oldBlendState;

	uint32_t m_oldRasterizerState;

	uint32_t m_oldDepthStencilState;

	uint32_t m_oldSamplerState;

	uint32_t m_pointSamplerState;
#endif

public:
	virtual FontRendererTexture* CreateTexture(int width, int height, FontRendererTextureFormat format, void* pixelData);

	virtual void SetTexture(FontRendererTexture* texture);

	virtual void UnsetTexture();

	virtual void DrawIndexedVertices(int numVertices, int numIndices, FontRendererVertex* vertex, uint16_t* indices);

	virtual void InvokeOnRender(void(*cb)(void*), void* arg);

	virtual void DrawRectangles(int numRectangles, const ResultingRectangle* rectangles);

#ifdef _HAVE_GRCORE_NEWSTATES
	inline void SetPointSamplerState(uint32_t state)
	{
		m_pointSamplerState = state;
	}
#endif
};

class GtaFontTexture : public FontRendererTexture
{
private:
	rage::grcTexture* m_texture;

public:
	GtaFontTexture(rage::grcTexture* texture)
		: m_texture(texture)
	{

	}

	virtual ~GtaFontTexture()
	{

	}

	inline rage::grcTexture* GetTexture() { return m_texture; }
};

FontRendererTexture* GtaGameInterface::CreateTexture(int width, int height, FontRendererTextureFormat format, void* pixelData)
{
	if (!IsRunningTests())
	{
		rage::grcTextureReference reference;
		memset(&reference, 0, sizeof(reference));
		reference.width = width;
		reference.height = height;
		reference.depth = 1;
		reference.stride = width * 4;
#ifdef GTA_NY
		reference.format = 1;
#else
		reference.format = 11;
#endif
		reference.pixelData = (uint8_t*)pixelData;

		rage::grcTexture* texture = rage::grcTextureFactory::getInstance()->createImage(&reference, nullptr);

		return new GtaFontTexture(texture);
	}

	return new GtaFontTexture(nullptr);
}

void GtaGameInterface::SetTexture(FontRendererTexture* texture)
{
#ifdef _HAVE_GRCORE_NEWSTATES
	m_oldSamplerState = GetImDiffuseSamplerState();

	SetImDiffuseSamplerState(m_pointSamplerState);
#endif

	SetTextureGtaIm(static_cast<GtaFontTexture*>(texture)->GetTexture());

#ifndef _HAVE_GRCORE_NEWSTATES
	SetRenderState(0, grcCullModeNone); // 0 in NY, 1 in Payne
	SetRenderState(2, 0); // alpha blending
#else
	m_oldRasterizerState = GetRasterizerState();
	SetRasterizerState(GetStockStateIdentifier(RasterizerStateNoCulling));

	m_oldBlendState = GetBlendState();
	SetBlendState(GetStockStateIdentifier(BlendStateDefault));

	m_oldDepthStencilState = GetDepthStencilState();
	SetDepthStencilState(GetStockStateIdentifier(DepthStencilStateNoDepth));
#endif

	PushDrawBlitImShader();
}

void GtaGameInterface::DrawIndexedVertices(int numVertices, int numIndices, FontRendererVertex* vertices, uint16_t* indices)
{
#if GTA_NY
	IDirect3DDevice9* d3dDevice = GetD3D9Device();

	d3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	d3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
#endif

	/*for (int i = 0; i < numVertices; i++)
	{
		trace("before: %f %f\n", vertices[i].x, vertices[i].y);
		TransformToScreenSpace((float*)&vertices[i], 1);
		trace("aft: %f %f\n", vertices[i].x, vertices[i].y);
	}*/

	rage::grcBegin(3, numIndices);

	for (int j = 0; j < numIndices; j++)
	{
		auto vertex = &vertices[indices[j]];
		uint32_t color = *(uint32_t*)&vertex->color;

#if GTA_NY
		// this swaps ABGR (as CRGBA is ABGR in little-endian) to ARGB by rotating left
		color = (color & 0xFF00FF00) | _rotl(color & 0x00FF00FF, 16);
#endif

		rage::grcVertex(vertex->x, vertex->y, 0.0, 0.0, 0.0, -1.0, color, vertex->u, vertex->v);
	}

	rage::grcEnd();

#if GTA_NY
	d3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_ANISOTROPIC);
	d3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
#endif
}

void GtaGameInterface::DrawRectangles(int numRectangles, const ResultingRectangle* rectangles)
{
	SetTextureGtaIm(rage::grcTextureFactory::GetNoneTexture());

#ifndef _HAVE_GRCORE_NEWSTATES
	SetRenderState(0, grcCullModeNone);
	SetRenderState(2, 0); // alpha blending m8
#else
	auto oldRasterizerState = GetRasterizerState();
	SetRasterizerState(GetStockStateIdentifier(RasterizerStateNoCulling));

	auto oldBlendState = GetBlendState();
	SetBlendState(GetStockStateIdentifier(BlendStateDefault));

	auto oldDepthStencilState = GetDepthStencilState();
	SetDepthStencilState(GetStockStateIdentifier(DepthStencilStateNoDepth));
#endif

	PushDrawBlitImShader();

	for (int i = 0; i < numRectangles; i++)
	{
		auto rectangle = &rectangles[i];

		rage::grcBegin(4, 4);

		auto& rect = rectangle->rectangle;
		uint32_t color = *(uint32_t*)&rectangle->color;

		// this swaps ABGR (as CRGBA is ABGR in little-endian) to ARGB by rotating left
		if (!rage::grcTexture::IsRenderSystemColorSwapped())
		{
			color = (color & 0xFF00FF00) | _rotl(color & 0x00FF00FF, 16);
		}

		rage::grcVertex(rect.fX1, rect.fY1, 0.0f, 0.0f, 0.0f, -1.0f, color, 0.0f, 0.0f);
		rage::grcVertex(rect.fX2, rect.fY1, 0.0f, 0.0f, 0.0f, -1.0f, color, 0.0f, 0.0f);
		rage::grcVertex(rect.fX1, rect.fY2, 0.0f, 0.0f, 0.0f, -1.0f, color, 0.0f, 0.0f);
		rage::grcVertex(rect.fX2, rect.fY2, 0.0f, 0.0f, 0.0f, -1.0f, color, 0.0f, 0.0f);

		rage::grcEnd();
	}

	PopDrawBlitImShader();

#ifdef _HAVE_GRCORE_NEWSTATES
	SetRasterizerState(oldRasterizerState);

	SetBlendState(oldBlendState);

	SetDepthStencilState(oldDepthStencilState);
#endif
}

void GtaGameInterface::UnsetTexture()
{
	PopDrawBlitImShader();

#ifdef _HAVE_GRCORE_NEWSTATES
	SetRasterizerState(m_oldRasterizerState);
	SetBlendState(m_oldBlendState);
	SetDepthStencilState(m_oldDepthStencilState);
	SetImDiffuseSamplerState(m_oldSamplerState);
#endif
}

#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")

void GtaGameInterface::InvokeOnRender(void(*cb)(void*), void* arg)
{
	if (IsRunningTests())
	{
		return;
	}

	if (IsOnRenderThread())
	{
		cb(arg);
	}
	else
	{
#if defined(GTA_NY)
		int argRef = (int)arg;

		auto dc = new(0) CGenericDC1Arg((void(*)(int))cb, &argRef);
		dc->Enqueue();
#else
		uintptr_t argRef = (uintptr_t)arg;

		EnqueueGenericDrawCommand([] (uintptr_t a, uintptr_t b)
		{
			D3DPERF_BeginEvent(D3DCOLOR_ARGB(0, 0, 0, 0), L"FontRenderer");

			auto cb = (void(*)(void*))a;

			cb((void*)b);

			D3DPERF_EndEvent();
		}, (uintptr_t*)&cb, &argRef);
#endif
	}
}

static GtaGameInterface g_gtaGameInterface;

FontRendererGameInterface* CreateGameInterface()
{
	return &g_gtaGameInterface;
}

#include <random>

static bool IsCanary()
{
	wchar_t resultPath[1024];

	static std::wstring fpath = MakeRelativeCitPath(L"CitizenFX.ini");
	GetPrivateProfileString(L"Game", L"UpdateChannel", L"production", resultPath, std::size(resultPath), fpath.c_str());

	return (_wcsicmp(resultPath, L"canary") == 0);
}

#if GTA_NY
static HookFunction hookFunc([]()
#else
static InitFunction initFunction([] ()
#endif
{
	static ConVar<std::string> customBrandingEmoji("ui_customBrandingEmoji", ConVar_Archive, "");

	static std::random_device random_core;
	static std::mt19937 random(random_core());

	static bool shouldDraw = false;

#ifdef GTA_NY
	shouldDraw = true;
#endif

	if (!CfxIsSinglePlayer() && !getenv("CitizenFX_ToolMode"))
	{
		Instance<ICoreGameInit>::Get()->OnGameRequestLoad.Connect([]()
		{
			shouldDraw = true;
		});

		Instance<ICoreGameInit>::Get()->OnShutdownSession.Connect([]()
		{
			shouldDraw = false;
		});
	}
	else
	{
		shouldDraw = true;
	}

#if defined(_HAVE_GRCORE_NEWSTATES) && defined(GTA_FIVE)
	OnGrcCreateDevice.Connect([] ()
	{
		D3D11_SAMPLER_DESC samplerDesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.MaxAnisotropy = 0;

		g_gtaGameInterface.SetPointSamplerState(CreateSamplerState(&samplerDesc));
	});
#elif defined(IS_RDR3)
	OnGrcCreateDevice.Connect([]()
	{
		g_gtaGameInterface.SetPointSamplerState(GetStockStateIdentifier(SamplerStatePoint));
	});
#endif

	srand(GetTickCount());

	OnPostFrontendRender.Connect([=] ()
	{
#if defined(GTA_FIVE) || defined(IS_RDR3) || defined(GTA_NY)
		int gameWidth, gameHeight;
		GetGameResolution(gameWidth, gameHeight);

		static bool isCanary = IsCanary();

		std::wstring brandingString = L"";
		std::wstring brandingEmoji = L"";
		std::wstring brandName = L"";

		if (shouldDraw) {
			SYSTEMTIME systemTime;
			GetLocalTime(&systemTime);

			brandName = L"FiveM";

			switch (systemTime.wHour)
			{
				case 1:
				case 2:
				case 3:
				case 4:
				case 5:
				case 6:
					brandingEmoji = L"\xD83C\xDF19";
					break;
				case 7:
				case 8:
				case 9:
				case 10:
				case 11:
				case 12:
					brandingEmoji = L"\xD83C\xDF42";
					break;
				case 13:
				case 14:
				case 15:
				case 16:
				case 17:
				case 18:
					brandingEmoji = L"\xD83C\xDF50";
					break;
				case 19:
				case 20:
				case 21:
				case 22:
				case 23:
				case 0:
					brandingEmoji = L"\xD83E\xDD59";
					break;
			}

			if (!CfxIsSinglePlayer() && !getenv("CitizenFX_ToolMode"))
			{
#if !defined(IS_RDR3) && !defined(GTA_NY)
				auto emoji = customBrandingEmoji.GetValue();

				if (!emoji.empty())
				{
					if (Instance<ICoreGameInit>::Get()->HasVariable("endUserPremium"))
					{
						try
						{
							auto it = emoji.begin();
							utf8::advance(it, 1, emoji.end());

							std::vector<uint16_t> uchars;
							uchars.reserve(2);

							utf8::utf8to16(emoji.begin(), it, std::back_inserter(uchars));

							brandingEmoji = std::wstring{ uchars.begin(), uchars.end() };
						}
						catch (const utf8::exception& e)
						{

						}
					}
				}

				if (Instance<ICoreGameInit>::Get()->OneSyncEnabled)
				{
					brandName += L"*";
				}

				if (Is2060())
				{
					brandName += L" (b2060)";
				}

				if (Is2189())
				{
					brandName += L" (b2189)";
				}

				if (Is2372())
				{
					brandName += L" (b2372)";
				}

				if (Is2545())
				{
					brandName += L" (b2545)";
				}
#endif 

#if defined(IS_RDR3)
				brandName = L"RedM";

				if (Instance<ICoreGameInit>::Get()->OneSyncEnabled)
				{
					brandName += L"*";
				}

				if (Is1355())
				{
					brandName += L" (b1355)";
				}

				if (Is1436())
				{
					brandName += L" (b1436)";
				}
#endif

#if defined(GTA_NY)
				brandName = L"LibertyM";
				brandingEmoji = L"\U0001F5FD";
#endif

				if (launch::IsSDKGuest())
				{
					brandName += L" (SDK)";
				}
				else if (isCanary)
				{
					brandName += L" (Canary)";
				}
			}
		}
		else
		{
			static auto version = ([]()
			{
				FILE* f = _wfopen(MakeRelativeCitPath(L"citizen/release.txt").c_str(), L"r");
				int version = -1;

				if (f)
				{
					char ver[128];

					fgets(ver, sizeof(ver), f);
					fclose(f);

					version = atoi(ver);
				}
				else
				{
					version = 0;
				}

				return version;
			})();

			brandName = fmt::sprintf(L"Ver. %d", version);
		}

		static int anchorPosBase = ([]()
		{
			if (launch::IsSDKGuest())
			{
				return 1;
			}
			else
			{
				std::random_device rd;
				std::mt19937 gen(rd());
				std::uniform_int_distribution<> dis(0, 2);

				return dis(gen);
			}
		})();

		int anchorPos = anchorPosBase;

		if (!shouldDraw)
		{
			anchorPos = 1;
		}

		// If anchor position is on left-side, make the emoji go on the left side.
		if (anchorPos < 2) {
			brandingString = fmt::sprintf(L"%s %s", brandName, brandingEmoji);
		} else
		{
			brandingString = fmt::sprintf(L"%s %s", brandingEmoji, brandName);
		}

		
		static CRect metrics;
		static fwWString lastString;
		static float lastHeight;

		float gameWidthF = static_cast<float>(gameWidth);
		float gameHeightF = static_cast<float>(gameHeight);
		
		if (metrics.Width() <= 0.1f || lastString != brandingString || lastHeight != gameHeightF)
		{
			g_fontRenderer.GetStringMetrics(brandingString, 22.0f * (gameHeightF / 1440.0f), 1.0f, "Segoe UI", metrics);

			lastString = brandingString;
			lastHeight = gameHeightF;
		}

		CRect drawRect;

		switch (anchorPos)
		{
		case 0: // TR
			drawRect = { gameWidthF - metrics.Width() - 10.0f, 10.0f, gameWidthF, gameHeightF };
			break;
		case 1: // BR
			drawRect = { gameWidthF - metrics.Width() - 10.0f, gameHeightF - metrics.Height() - 10.0f, gameWidthF, gameHeightF };
			break;
		case 2: // TL
			drawRect = { 10.0f, 10.0f, gameWidthF, gameHeightF };
			break;
		}

		CRGBA color(180, 180, 180, 120);

		if (!shouldDraw)
		{
			color = CRGBA(255, 108, 0, 255);
		}

		g_fontRenderer.DrawText(brandingString, drawRect, color, 22.0f * (gameHeightF / 1440.0f), 1.0f, "Segoe UI");
#endif

		g_fontRenderer.DrawPerFrame();
	}, 1000);
});
