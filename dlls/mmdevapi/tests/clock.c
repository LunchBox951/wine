/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS
#include "wine/test.h"
#ifdef STANDALONE
#include "initguid.h"
#endif
#include "mmdeviceapi.h"
#include "audioclient.h"

static UINT64 qpc_to_100ns(LARGE_INTEGER counter, LARGE_INTEGER frequency)
{
    return counter.QuadPart / frequency.QuadPart * 10000000
            + counter.QuadPart % frequency.QuadPart * 10000000 / frequency.QuadPart;
}

static void test_timestamps(IMMDeviceEnumerator *enumerator, EDataFlow flow)
{
    IAudioRenderClient *render = NULL;
    IAudioCaptureClient *capture = NULL;
    IAudioClient *client = NULL;
    IAudioClock *clock = NULL;
    IMMDevice *device = NULL;
    WAVEFORMATEX *format = NULL;
    LARGE_INTEGER frequency, before, after;
    UINT64 position, timestamp, lower, upper;
    UINT32 frames;
    DWORD flags;
    BYTE *data;
    unsigned int i;
    HRESULT hr;

    winetest_push_context("flow %u", flow);
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, flow, eMultimedia, &device);
    if (hr == E_NOTFOUND)
    {
        skip("No audio endpoint available.\n");
        goto done;
    }
    ok(hr == S_OK, "GetDefaultAudioEndpoint failed: %#lx.\n", hr);
    if (FAILED(hr)) goto done;
    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, (void **)&client);
    ok(hr == S_OK, "Activate failed: %#lx.\n", hr);
    if (FAILED(hr)) goto done;
    hr = IAudioClient_GetMixFormat(client, &format);
    ok(hr == S_OK, "GetMixFormat failed: %#lx.\n", hr);
    if (FAILED(hr)) goto done;
    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, 0, 5000000, 0, format, NULL);
    ok(hr == S_OK, "Initialize failed: %#lx.\n", hr);
    if (FAILED(hr)) goto done;
    hr = IAudioClient_GetService(client, &IID_IAudioClock, (void **)&clock);
    ok(hr == S_OK, "GetService failed: %#lx.\n", hr);
    if (FAILED(hr)) goto done;
    if (flow == eRender)
    {
        hr = IAudioClient_GetService(client, &IID_IAudioRenderClient, (void **)&render);
        ok(hr == S_OK, "GetService failed: %#lx.\n", hr);
        if (FAILED(hr)) goto done;
        hr = IAudioClient_GetBufferSize(client, &frames);
        ok(hr == S_OK, "GetBufferSize failed: %#lx.\n", hr);
        if (FAILED(hr)) goto done;
        hr = IAudioRenderClient_GetBuffer(render, frames, &data);
        ok(hr == S_OK, "GetBuffer failed: %#lx.\n", hr);
        if (FAILED(hr)) goto done;
        hr = IAudioRenderClient_ReleaseBuffer(render, frames, AUDCLNT_BUFFERFLAGS_SILENT);
        ok(hr == S_OK, "ReleaseBuffer failed: %#lx.\n", hr);
        if (FAILED(hr)) goto done;
    }
    else
    {
        hr = IAudioClient_GetService(client, &IID_IAudioCaptureClient, (void **)&capture);
        ok(hr == S_OK, "GetService failed: %#lx.\n", hr);
        if (FAILED(hr)) goto done;
    }

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&before);
    hr = IAudioClient_Start(client);
    ok(hr == S_OK, "Start failed: %#lx.\n", hr);
    if (FAILED(hr)) goto done;
    Sleep(100);
    hr = IAudioClock_GetPosition(clock, &position, &timestamp);
    QueryPerformanceCounter(&after);
    ok(SUCCEEDED(hr), "GetPosition failed: %#lx.\n", hr);
    /* Allow cached device positions and rounding at the ends of the interval. */
    lower = qpc_to_100ns(before, frequency);
    lower = lower > 1000000 ? lower - 1000000 : 0;
    upper = qpc_to_100ns(after, frequency) + 1000000;
    if (SUCCEEDED(hr))
        ok(timestamp >= lower && timestamp <= upper,
                "Clock timestamp %I64u outside QPC interval [%I64u, %I64u].\n", timestamp, lower, upper);

    if (capture)
    {
        for (i = 0; i < 100; ++i)
        {
            hr = IAudioCaptureClient_GetBuffer(capture, &data, &frames, &flags, &position, &timestamp);
            if (hr != AUDCLNT_S_BUFFER_EMPTY) break;
            Sleep(10);
        }
        ok(hr == S_OK, "GetBuffer failed: %#lx.\n", hr);
        if (hr == S_OK)
        {
            QueryPerformanceCounter(&after);
            upper = qpc_to_100ns(after, frequency) + 1000000;
            if (!(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR))
                ok(timestamp >= lower && timestamp <= upper,
                        "Capture timestamp %I64u outside QPC interval [%I64u, %I64u].\n", timestamp, lower, upper);
            hr = IAudioCaptureClient_ReleaseBuffer(capture, frames);
            ok(hr == S_OK, "ReleaseBuffer failed: %#lx.\n", hr);
        }
    }
    hr = IAudioClient_Stop(client);
    ok(hr == S_OK, "Stop failed: %#lx.\n", hr);

done:
    if (render) IAudioRenderClient_Release(render);
    if (capture) IAudioCaptureClient_Release(capture);
    if (clock) IAudioClock_Release(clock);
    if (client) IAudioClient_Release(client);
    if (device) IMMDevice_Release(device);
    CoTaskMemFree(format);
    winetest_pop_context();
}

START_TEST(clock)
{
    IMMDeviceEnumerator *enumerator;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
            &IID_IMMDeviceEnumerator, (void **)&enumerator);
    ok(hr == S_OK, "CoCreateInstance failed: %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        test_timestamps(enumerator, eRender);
        test_timestamps(enumerator, eCapture);
        IMMDeviceEnumerator_Release(enumerator);
    }
    CoUninitialize();
}
