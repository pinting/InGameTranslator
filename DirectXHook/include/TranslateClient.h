#pragma once

#include <Windows.h>
#include <winhttp.h>
#include <DirectXTex.h>
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

#include "Config.h"
#include "Logger.h"

using namespace DirectX;
using json = nlohmann::json;

#pragma comment(lib, "winhttp.lib")

namespace TranslateClient 
{
	static Logger logger{ "TranslateClient" };

	struct TranslationEntry {
		int x;
		int y;
		float w;
		float h;
		std::string translation;
		std::string message;
	};

	static std::vector<TranslationEntry> entries = std::vector<TranslationEntry>();
	static std::mutex mutex = std::mutex();

	static void PullEntries(std::vector<TranslationEntry>* target)
	{
		std::lock_guard<std::mutex> lock(mutex);

		*target = entries;
	}

	static void ClearEntries()
	{
		std::lock_guard<std::mutex> lock(mutex);

		entries.clear();
	}

	static void PushEntries(std::vector<TranslationEntry> &newEntries)
	{
		std::lock_guard<std::mutex> lock(mutex);

		for (TranslationEntry entry : newEntries)
		{
			entries.push_back(entry);
		}
	}

	static void ParseResponse(const char *buffer, size_t size)
	{
		if (size <= 1)
		{
			return;
		}

		std::vector<TranslationEntry> newEntries;
		
		try
		{
			json parsed = json::parse(buffer);

			for (json& pe : parsed)
			{
				TranslationEntry entry;

				pe.at("message").get_to(entry.message);
				pe.at("x").get_to(entry.x);
				pe.at("y").get_to(entry.y);
				pe.at("w").get_to(entry.w);
				pe.at("h").get_to(entry.h);
				pe.at("translation").get_to(entry.translation);

				newEntries.push_back(entry);
			}

			ClearEntries();
			PushEntries(newEntries);
		}
		catch (const json::parse_error& e)
		{
			logger.Log("Error while parsing JSON: %s %s", buffer, e.what());
		}
	}

	static DWORD SendRequest(LPVOID pBlob)
	{
		Blob* blob = (Blob*)pBlob;
		DWORD size = 0;
		DWORD received = 0;
		LPSTR outBuffer;
		BOOL responseReceving = FALSE;

		HINTERNET session = NULL;
		HINTERNET connect = NULL;
		HINTERNET request = NULL;

		session = WinHttpOpen(
			UserAgent,
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);

		if (session)
		{
			connect = WinHttpConnect(
				session,
				ServerAddress,
				ServerPort,
				0);
		}

		if (connect)
		{
			request = WinHttpOpenRequest(
				connect,
				L"POST",
				L"/",
				NULL,
				WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES,
				NULL);
		}

		if (request)
		{
			LPVOID data = blob->GetBufferPointer();
			DWORD dataSize = blob->GetBufferSize();

			responseReceving = WinHttpSendRequest(
				request,
				WINHTTP_NO_ADDITIONAL_HEADERS,
				0,
				data,
				dataSize,
				dataSize,
				0);
		}

		if (responseReceving)
		{
			responseReceving = WinHttpReceiveResponse(
				request,
				NULL);
		}

		if (responseReceving)
		{
			do
			{
				size = 0;

				if (!WinHttpQueryDataAvailable(request, &size))
				{
					logger.Log("Error in WinHttpQueryDataAvailable: %u", GetLastError());
					return 1;
				}

				outBuffer = new char[size + 1];

				if (!outBuffer)
				{
					logger.Log("Out of memory");
					size = 0;
				}
				else
				{
					RtlZeroMemory(outBuffer, size + 1);

					if (!WinHttpReadData(request, (LPVOID)outBuffer, size, &received))
					{
						logger.Log("Error in WinHttpReadData: %u", GetLastError());
						return 1;
					}
					else
					{
						logger.Log("Response received from server: %s", outBuffer);
						ParseResponse(outBuffer, size);
					}

					delete[] outBuffer;
				}
			} while (size > 0);
		}

		delete blob;

		if (!responseReceving)
		{
			logger.Log("Error has occurred: %u", GetLastError());
			return 1;
		}

		if (request) WinHttpCloseHandle(request);
		if (connect) WinHttpCloseHandle(connect);
		if (session) WinHttpCloseHandle(session);

		return 0;
	}
}