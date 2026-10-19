// D3D12 fence wait whose answer is the fence, not the event.
//
// SetEventOnCompletion cannot be cancelled. A wait that timed out leaves its
// registration live, and when the GPU later reaches that value the auto-reset
// event is signalled with nobody waiting. A wait that trusts the event then
// returns at once for a value the GPU has not reached. Here every wake is only a
// prompt to read the fence again, and one deadline set on entry bounds the
// whole call: a stale wake can neither shorten the wait to zero nor restart the
// budget the caller gave.
//
// Requires Log() to be declared before inclusion.
#pragma once

static bool FenceDeadlineWait(ID3D12Fence *fence, HANDLE ev, UINT64 value, DWORD wait_ms,
                              const char *who)
{
    const ULONGLONG begin = GetTickCount64();
    const ULONGLONG deadline = begin + wait_ms;
    const UINT64 before = fence->GetCompletedValue();
    const auto fail = [&](const char *stage, HRESULT hr, DWORD wr, UINT64 after) {
        const DWORD saved_error = GetLastError();
        Log("[wait-cause] %s stage=%s tid=%lu target=%llu before=%llu after=%llu budget_ms=%lu "
            "elapsed_ms=%llu hr=0x%08lX wait=0x%08lX last_error=%lu.",
            who, stage, GetCurrentThreadId(), value, before, after, wait_ms,
            GetTickCount64() - begin, static_cast<unsigned long>(hr), wr, saved_error);
        SetLastError(saved_error);
        return false;
    };
    if (before == UINT64_MAX) return fail("removed-before", S_OK, WAIT_FAILED, before);
    if (before >= value) return true;
    const HRESULT event_hr = fence->SetEventOnCompletion(value, ev);
    if (FAILED(event_hr)) return fail("set-event", event_hr, WAIT_FAILED, before);
    unsigned stale_wakes = 0;
    DWORD wr = WAIT_OBJECT_0;
    for (;;) {
        const UINT64 after = fence->GetCompletedValue();
        if (after == UINT64_MAX) return fail("removed-after", S_OK, wr, after);
        if (after >= value) {
            if (stale_wakes != 0) {
                const DWORD saved_error = GetLastError();
                Log("[wait-cause] %s recovered tid=%lu target=%llu after=%llu stale_wakes=%u "
                    "budget_ms=%lu elapsed_ms=%llu.", who, GetCurrentThreadId(), value, after,
                    stale_wakes, wait_ms, GetTickCount64() - begin);
                SetLastError(saved_error);
            }
            return true;
        }
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) return fail("deadline", S_OK, WAIT_TIMEOUT, after);
        wr = WaitForSingleObject(ev, static_cast<DWORD>(deadline - now));
        if (wr != WAIT_OBJECT_0 && wr != WAIT_TIMEOUT) return fail("wait-result", S_OK, wr, after);
        if (wr == WAIT_OBJECT_0) {
            const UINT64 awake = fence->GetCompletedValue();
            if (awake != UINT64_MAX && awake < value) ++stale_wakes;
        }
    }
}
