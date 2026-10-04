#pragma once

// Installing the private SR carrier hook can fail for reasons that clear on
// their own: NGX briefly maps the game's nvngx_dlss.dll next to the driver's
// DLSS model while a feature is created, so two SR modules are visible for a
// moment, or the SR module has not been loaded yet. Those failures are retried
// on later frames with the private session kept; anything else still refuses.
enum PresentCarrierResult : unsigned {
    PCR_READY = 0,
    PCR_TRANSIENT = 1,
    PCR_FATAL = 2
};

static const unsigned kPresentCarrierRetryLimit = 30;
static const unsigned long long kPresentCarrierRetryMs = 1000;

struct PresentCarrierRetry {
    unsigned attempts;
    unsigned long long next_at;
};

static bool PresentCarrierRetryDue(const PresentCarrierRetry &retry, unsigned long long now)
{
    return now >= retry.next_at;
}

// Records a failed attempt. Returns true when another attempt is allowed.
static bool PresentCarrierRetryAfterFailure(PresentCarrierRetry &retry, PresentCarrierResult result,
                                            unsigned long long now)
{
    ++retry.attempts;
    if (result != PCR_TRANSIENT || retry.attempts >= kPresentCarrierRetryLimit) return false;
    retry.next_at = now + kPresentCarrierRetryMs;
    return true;
}

// Allocation can recover when reachable address space becomes available.
static PresentCarrierResult PresentCarrierHookStatusResult(int minhook_status)
{
    return minhook_status == 9 ? PCR_TRANSIENT : PCR_FATAL;
}
