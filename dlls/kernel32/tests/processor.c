/*
 * Unit test suite for processor counters
 *
 * Copyright 2026 LunchBox951
 *
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

#include "windows.h"
#include "wine/test.h"

START_TEST(processor)
{
    BOOL (WINAPI *get_cycle_time)(USHORT, ULONG64 *, DWORD *);
    ULONG64 buffer[65], previous[64];
    unsigned int i, count;
    DWORD size, required, error;
    BOOL ret;

    get_cycle_time = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                           "GetProcessorSystemCycleTime");
    ok(!!get_cycle_time, "GetProcessorSystemCycleTime is missing.\n");
    if (!get_cycle_time) return;

    size = sizeof(buffer);
    ret = get_cycle_time(0xffff, buffer, &size);
    ok(!ret, "Invalid processor group succeeded.\n");

    size = 0;
    SetLastError(0xdeadbeef);
    ret = get_cycle_time(0, NULL, &size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER, "Got %d, error %lu.\n", ret, error);
    ok(size && !(size % sizeof(*buffer)) && size <= sizeof(previous), "Got size %lu.\n", size);
    if (!size || size > sizeof(previous)) return;
    required = size;
    count = size / sizeof(*buffer);

    memset(buffer, 0xcc, sizeof(buffer));
    size = required - 1;
    ret = get_cycle_time(0, buffer, &size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER, "Got %d, error %lu.\n", ret, error);
    ok(size == required, "Got size %lu, expected %lu.\n", size, required);
    for (i = 0; i < ARRAY_SIZE(buffer); ++i)
        ok(buffer[i] == 0xcccccccccccccccc, "Short buffer modified at %u.\n", i);

    size = sizeof(buffer);
    ret = get_cycle_time(0, buffer, &size);
    error = GetLastError();
    if (!ret && !strcmp(winetest_platform, "wine") &&
        (error == ERROR_NOT_SUPPORTED || error == ERROR_CALL_NOT_IMPLEMENTED))
    {
        skip("Host processor cycle accounting is unavailable, error %lu.\n", error);
        return;
    }
    ok(ret, "Query failed, error %lu.\n", error);
    ok(size == required, "Got size %lu, expected %lu.\n", size, required);
    if (!ret) return;
    for (i = 0; i < count; ++i)
    {
        ok(buffer[i] != 0xcccccccccccccccc, "Counter %u was not written.\n", i);
        trace("Processor %u system cycles: %I64u.\n", i, buffer[i]);
        previous[i] = buffer[i];
    }
    for (i = count; i < ARRAY_SIZE(buffer); ++i)
        ok(buffer[i] == 0xcccccccccccccccc, "Buffer overrun at %u.\n", i);

    Sleep(20);
    size = required;
    ret = get_cycle_time(0, buffer, &size);
    ok(ret, "Second query failed, error %lu.\n", GetLastError());
    ok(size == required, "Got size %lu, expected %lu.\n", size, required);
    if (!ret) return;
    for (i = 0; i < count; ++i)
        ok(buffer[i] >= previous[i], "Counter %u decreased from %I64u to %I64u.\n",
           i, previous[i], buffer[i]);

}
