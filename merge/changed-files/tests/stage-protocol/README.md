# Stage protocol v1, bridge side

```
tests\stage-protocol\run.cmd <new-output-directory>
```

Needs MSVC (`tests/vulkan-fg-input/vcvars.cmd` finds it) and PowerShell. No game,
GPU or ReShade is used. `run.cmd` copies the production carrier scope and
configuration out of `src/present-adapter.inc` into the output directory and
compiles them with `src/present-adapter-config.h` into `check.exe`.
`consumer.cpp` stands in for Generic: a DLL exporting
`DLSS5StageProtocolVersion` and `DLSS5GetStagePlan`, with a version and plan the
test changes at run time.

For Pipeline 2 and 3, each with Follow 0 and 1, the check covers: a Generic
loaded after the bridge is found; a version other than 1 is ignored; all 125
legal plans (Render/Upscaled/Present 0-4 each) are accepted, the FG input is
active exactly when the Present count is non-zero, and the configuration is
accepted at every legacy hook point; 391 invalid values (counts above 4,
reserved bits) pause the carrier and leave the FG input inactive; the carrier's
plan snapshot is per thread, restored after nested scopes and exceptions; the
relay settings follow Pipeline/Import alone; EnableHooks and the source
overrides are still checked.

Pass: four lines starting with `PASS: P2 Follow=0`, `P2 Follow=1`, `P3 Follow=0`,
`P3 Follow=1`, exit code 0.
