"""Vulkan stage CPU tests for Generic 8.5.0-rc10-stages1 (vulkan_path.hpp).

usage: python run-cpu-tests.py --source <src/addons/dlss5> --out <new-directory>
                               [--negative-control <directory>]

Production functions and statement ranges are copied out of --source and
compiled with MSVC into a CPU-only test against fake SDK, device and resource
types. No add-on build, GPU, game or external package is used. The manifest
records the source line range and SHA-256 of every compiled fragment.

--negative-control names a directory with vulkan_path.hpp and dlssnr.hpp of an
earlier draft of the stage code (negative-control/ next to this file). It lacks
three fixes: Render keeps Detail stability Auto on, a failed layer is not
retired on its own, and the game's Reset is read only on frames that reach the
model. Each of the six named tests must fail against it, which shows the tests
detect those defects.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import traceback

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

HERE = Path(__file__).resolve().parent
CURRENT = Path()
OUT = Path()
ORIGINAL: Path | None = None
EXPECTED_ORIGINAL = "fad97095ffc0882d3f67fb8f17ad01cbdce06b645b0bfb2be1161ce7ee5a64c5"
MANIFEST: list[dict] = []
STATIC: list[str] = []


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class Source:
    def __init__(self, path: Path):
        self.path = path.resolve()
        self.bytes = path.read_bytes()
        self.text = self.bytes.decode("utf-8-sig").replace("\r\n", "\n")
        self.mask = re.sub(
            r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|(?<![0-9])\'(?:\\.|[^\'\\])*\'',
            lambda m: "".join("\n" if c == "\n" else " " for c in m[0]),
            self.text, flags=re.S,
        )

    def bounds(self, anchor: str, semicolon: bool = False) -> tuple[int, int]:
        hits = [m.start() for m in re.finditer(re.escape(anchor), self.text)]
        if len(hits) != 1:
            raise AssertionError(f"{self.path}: expected unique anchor {anchor!r}, got {len(hits)}")
        start = hits[0]
        opening = self.mask.index("{", start)
        depth = 1
        end = opening + 1
        while depth:
            char = self.mask[end]
            depth += (char == "{") - (char == "}")
            end += 1
        if semicolon:
            if self.text[end] != ";":
                raise AssertionError(f"missing declaration semicolon: {anchor}")
            end += 1
        return start, end

    def fragment(self, start: int, end: int, label: str) -> str:
        text = self.text[start:end]
        line = self.text.count("\n", 0, start) + 1
        MANIFEST.append({
            "label": label, "source": self.path.as_posix(), "start_line": line,
            "end_line": self.text.count("\n", 0, end) + 1,
            "fragment_sha256": sha(text.encode()), "source_sha256": sha(self.bytes),
        })
        return f'\n#line {line} "{self.path.as_posix()}"\n{text}\n'

    def definition(self, anchor: str, semicolon: bool = False) -> str:
        return self.fragment(*self.bounds(anchor, semicolon), anchor)

    def line(self, pattern: str) -> str:
        matches = list(re.finditer(pattern, self.text, re.M))
        if len(matches) != 1:
            raise AssertionError(f"{self.path}: expected one line {pattern!r}, got {len(matches)}")
        match = matches[0]
        return self.fragment(match.start(), match.end(), pattern)

    def between(self, start: str, end: str, label: str) -> str:
        begin = self.text.index(start)
        finish = self.text.index(end, begin)
        return self.fragment(begin, finish, label)


def checked(condition: bool, name: str) -> None:
    if not condition:
        raise AssertionError("STATIC FAIL: " + name)
    STATIC.append(name)
    print("STATIC PASS " + name, flush=True)


def source_text(source: Source, anchor: str) -> str:
    begin, end = source.bounds(anchor)
    return source.text[begin:end]


def validate_static(vk: Source, old: "Source | None") -> None:
    hook = source_text(vk, "template <int Slot>\ninline NVSDK_NGX_Result NVSDK_CONV HookedEvaluate(")
    process = source_text(vk, "inline NrDeclineReason ProcessNr(")
    latch = source_text(vk, "inline void LatchFeatureReset(")
    fail = source_text(vk, "inline void FailStagePass(")
    retire = source_text(vk, "inline void RetireStagePass(")
    suffix = source_text(vk, "inline void RetireStagePasses(")
    drain = source_text(vk, "inline void DrainRetiredFeature(")
    checked(hook.index("const ngx_serial::Call ngx_turn;") < hook.index("RuntimeLock lock(runtime_mutex);")
            < hook.index("LatchFeatureReset("), "NGX turn -> runtime_mutex -> latch")
    checked(hook.index("command_buffer == nullptr || handle == nullptr || parameters == nullptr")
            < hook.index("features.find(handle)") < hook.index("LatchFeatureReset("),
            "nonnull arguments and registry lookup precede reset read")
    checked("feature->second.id == kFeatureDlss || feature->second.id == kFeatureDlssd" in hook
            and "if (eligible) LatchFeatureReset(&feature->second, parameters);" in hook,
            "reset latch guarded by registered SR or RR eligibility")
    for marker in ("if (!enabled.load())", "if (NrYieldsToForeign())", "if (bridge_serves)", "if (!eligible)"):
        checked(hook.index("LatchFeatureReset(") < hook.index(marker), "latch before " + marker)
    checked(vk.text.count("GetInt(parameters, NVSDK_NGX_Parameter_Reset, 0)") == 1,
            "exactly one game Reset read in Vulkan production source")
    checked("!= 1) return;" in latch and "for (auto& stage : feature->stages)" in latch
            and "for (auto& slot : stage.slots) slot.pending_reset = true;" in latch,
            "Reset=1 latches all slots of both feature stages")
    checked("history_reset_epoch" not in latch and "RequestHistoryReset" not in latch,
            "game Reset does not advance global epoch")
    checked("LookSettingsSnapshot(!pre_sr)" in process, "Render Auto off and Upscaled Auto on at actual call")
    checked("RetireStagePass(feature, pass);" in suffix, "suffix retirement reuses single-pass primitive")
    checked("{slot.handle, feature->nr_use[pass], slot.parameters}" in retire
            and "feature->look_history_use[pass]" in retire,
            "retirement carries parameters and independent history use ID")
    checked("runtime.release" not in retire and "DestroyLookHistoryImage" not in retire,
            "single-pass retirement does not synchronously release recorded resources")
    checked("RetireStagePass(feature, pass);" in fail and "RetireStagePasses" not in fail
            and "slot.fail_count = fail_count;" in fail and "slot.failed_ns = SteadyNowNs();" in fail,
            "failure retires only failed slot and restores retry state")
    checked("later = pass + 1" in fail and "later < kMaxNrPasses" in fail
            and "feature->slots[later].pending_reset = true;" in fail,
            "failure invalidates only skipped suffix model histories")
    checked("nr_width" not in fail and "ngx.norm" not in fail and "worksets" not in fail,
            "failure preserves stage dimensions governor and worksets")
    call = process.index("result = runtime.evaluate(")
    catch = process.index("catch (...) {", call)
    branch = process.index("if (NVSDK_NGX_FAILED(result))", catch)
    recovery = process.index("FailStagePass(&feature, pass);", branch)
    ret = process.index("return NrDeclineReason::kFailureBackoff;", recovery)
    checked(process.index("TrackRecordedUse(cmd, feature.nr_use[pass]);") < call < catch < branch < recovery < ret,
            "tracked evaluate result and exception converge on failure recovery")
    checked("slot.fail_count = 0;" in process[ret:], "success clears failure retry count")
    checked(drain.index("if (!RecordedUseComplete(it->use_id))", drain.index("feature->retired_nr.begin()"))
            < drain.index("runtime.release(it->handle)") < drain.index("DestroyOwnedParameters(it->parameters)"),
            "drain requires nonreplay proof then releases handle before parameters")
    norm_read = process.index("frame.frame_reset = feature.norm_pending_reset ? 1 : 0;")
    norm_plan = process.index("const FrameScalePlan plan = PlanFrameScale", norm_read)
    norm_clear = process.index("feature.norm_pending_reset = false;", norm_plan)
    checked(process.index("if (depth == nullptr || motion == nullptr)") < norm_read < norm_plan < norm_clear,
            "missing-guide bypass cannot consume normalization latch")
    checked(process.index("dispatch(kExposureScale", norm_plan) < norm_clear
            and process.index("transition(kScale, resource_usage::shader_resource);", norm_plan) < norm_clear,
            "normalization consumes latch only after shader commands and scale transition")
    checked(hook.index("ProcessNr(command_buffer, handle, parameters, stages::Point::Render")
            < hook.index("\n    result = real(command_buffer, handle, parameters, callback);")
            < hook.index("if (NVSDK_NGX_FAILED(result)) {")
            < hook.index("ProcessNr(command_buffer, handle, parameters, stages::Point::Upscaled"),
            "native SR failure skips Upscaled after entry reset latching")
    checked(vk.text.count("ProcessNr(command_buffer, handle, parameters,") == 2,
            "all ProcessNr production callers are inside reset-latching hook")
    checked("norm_pending_reset" not in source_text(vk, "inline void RetireStagePasses("),
            "zero-stage reconciliation cannot erase pending governor reset")
    if old is None:
        return
    old_process = source_text(old, "inline NrDeclineReason ProcessNr(")
    def selection(text: str) -> str:
        return text[text.index("  if (feature.nr_width != width"):text.index("  // The scratch images this frame needs")]
    checked(selection(process) == selection(old_process),
            "contract recreation backoff and frame-ahead maturation code unchanged")
    checked(source_text(vk, "inline void DrainRetiredFeature(") == source_text(old, "inline void DrainRetiredFeature("),
            "existing recorded-use drain policy unchanged")
    checked("LookSettingsSnapshot(true)" in old_process and "FailStagePass(" not in old_process
            and "LatchFeatureReset(" not in old.text, "original source contains all three negative-control defects")


def generate(vk: Source, shared: Source, label: str, old: bool) -> Path:
    look = Source(CURRENT / "look_stage.hpp")
    reset = Source(CURRENT / "reset_epoch.hpp")
    trace = Source(CURRENT / "norm_trace.hpp")
    values: dict[str, str] = {"SERIAL_HEADER": (CURRENT / "ngx_serial.hpp").as_posix()}
    values["CORE_TYPES"] = shared.line(r"^constexpr uint32_t kMaxNrPasses = .*;$")
    for anchor in ("template <typename Buffer>\nstruct BasicLookHistory", "struct NrFeatureSlot", "enum class FeedSource", "struct NormStream"):
        if anchor == "struct NrFeatureSlot":
            values["CORE_TYPES"] += "\nusing LookHistory = BasicLookHistory<ID3D12Resource*>;\n"
        values["CORE_TYPES"] += shared.definition(anchor, True)
    values["LOOK_TYPES"] = look.between("constexpr uint32_t kFlagBands", "// Root parameters.", "look flags")
    values["LOOK_TYPES"] += look.line(r"^constexpr float kHistoryMaxGapSeconds = .*;$")
    for anchor in ("struct Constants", "struct Settings", "struct Frame"):
        values["LOOK_TYPES"] += look.definition(anchor, True)
    values["LOOK_TYPES"] += look.between("constexpr uint32_t kUpsampleAuto", "// Every per-pixel gain", "upsampling constants")
    values["DEFAULT_CONSTANTS"] = shared.line(r"^inline constexpr float kDetailStabilityAlpha = .*;$")
    values["DEFAULT_CONSTANTS"] += shared.line(r"^inline constexpr int64_t kNormResetQuietNs = .*;$")
    values["SNAP_CONSTANTS"] = "".join(trace.line(r"^constexpr uint32_t " + name + r" = .*;$|^constexpr uint32_t " + name + r" = .*;[^\n]*$")
                                           for name in ("kSnapPrime", "kSnapEpoch", "kSnapReset", "kSnapFeed"))
    values["SHARED_FUNCTIONS"] = reset.definition("inline bool StackSlotNeedsReset(")
    for anchor, semi in (("struct SkinSteering", True), ("inline SkinSteering SentSkinSteering(", False),
                         ("inline look::Settings LookSettingsSnapshot(", False)):
        values["SHARED_FUNCTIONS"] += shared.definition(anchor, semi)
    values["SHARED_FUNCTIONS"] += shared.line(r"^constexpr int64_t kFeatureRetryFirstNs = .*;$")
    values["SHARED_FUNCTIONS"] += shared.line(r"^constexpr int64_t kFeatureRetryMaxNs = .*;$")
    values["SHARED_FUNCTIONS"] += shared.definition("inline bool FeatureFailureExpired(")
    values["SHARED_FUNCTIONS"] += shared.definition(
        "inline void SetModelParameters(NrFeatureSlot& slot, uint32_t slot_index,\n"
        "                               uint64_t chain_reset_epoch, uint32_t create_flags,\n"
        "                               int32_t frame_reset, bool frame_ui_correction) {")
    for anchor, semi in (("struct FrameScalePlan", True), ("inline FrameScalePlan PlanFrameScale(", False),
                         ("template <typename History>\ninline void PlanLookHistory(", False),
                         ("template <typename History>\ninline void FinishLookPass(", False)):
        values["SHARED_FUNCTIONS"] += shared.definition(anchor, semi)
    values["VULKAN_TYPES"] = vk.definition("enum Surface : uint32_t", True)
    for anchor in ("struct Workset", "struct LookHistoryImage", "struct RetiredLookHistory", "struct RetiredNrHandle"):
        values["VULKAN_TYPES"] += vk.definition(anchor, True)
    values["VULKAN_TYPES"] += vk.line(r"^using LookHistoryVk = .*;$")
    for anchor in ("struct StageFeature", "struct Feature {"):
        values["VULKAN_TYPES"] += vk.definition(anchor, True)
    values["TRACKING"] = vk.between("inline std::mutex recorded_uses_mutex;", "inline constexpr VkImageLayout kLayoutUndefined", "complete recorded-use bookkeeping")
    values["LIFETIME_FUNCTIONS"] = ""
    for anchor in ("inline void DestroyWorkset(", "inline void DestroyLookHistoryImage(",
                   "inline void DrainRetiredFeature(", "inline void RetireStagePass(",
                   "inline void RetireStagePasses(", "inline void FailStagePass("):
        if anchor in vk.text:
            values["LIFETIME_FUNCTIONS"] += vk.definition(anchor)
    values["SLOT_SELECTION"] = vk.between("  if (feature.nr_width != width", "  // The scratch images this frame needs", "complete slot contract/retry/create/warmup statements")
    values["MODEL_FRAME_RESET"] = vk.line(r"^  const int32_t frame_reset = .*;$")
    begin = vk.text.index("    SetModelParameters(slot, pass, chain_reset_epoch,")
    end = vk.text.index("    slot.fail_count = 0;", begin) + len("    slot.fail_count = 0;")
    values["EVALUATE_BRANCH"] = vk.fragment(begin, end, "complete model/evaluate/exception/failure/success statements")
    values["LOOK_CALL"] = vk.line(r"^  const look::Settings look_settings = LookSettingsSnapshot\(.*\);$")
    values["NORM_FRAME_RESET"] = vk.line(r"^      frame.frame_reset = .*;$")
    values["NORM_PLAN_CALL"] = vk.line(r"^      const FrameScalePlan plan = PlanFrameScale.*;$")
    values["NORM_CONSUMPTION"] = "" if old else vk.line(r"^      feature.norm_pending_reset = false;$")
    values["PROCESS_ENTRY"] = vk.between("  const stages::Scope stage_scope(stage);", "  command_list* cmd = nullptr;", "ProcessNr reconciliation and zero-stage gates")
    values["GUIDE_GATES"] = vk.line(r"^  if \(depth == nullptr \|\| motion == nullptr\).*;$")
    values["GUIDE_GATES"] += vk.line(r"^  if \(output == nullptr \|\| \(!pre_sr && !output->ReadWrite\)\).*;$")
    values["HOOK_FUNCTIONS"] = "" if old else vk.definition("inline void LatchFeatureReset(")
    values["HOOK_FUNCTIONS"] += vk.definition("template <int Slot>\ninline NVSDK_NGX_Result NVSDK_CONV HookedEvaluate(")
    template_path = HERE / "cpu-tests.cpp.in"
    template = template_path.read_text(encoding="utf-8").replace("@SERIAL_HEADER@", values["SERIAL_HEADER"])
    def insert_fragment(match: re.Match) -> str:
        following_line = template.count("\n", 0, match.start()) + 2
        return values[match[1]] + f'\n#line {following_line} "{template_path.as_posix()}"\n'
    text = re.sub(r"^@([A-Z_]+)@\n", insert_fragment, template, flags=re.M)
    unresolved = re.findall(r"@[A-Z_]+@", text)
    if unresolved:
        raise AssertionError(f"unresolved template tokens: {unresolved}")
    path = OUT / (label + ".cpp")
    path.write_text("#define OLD_SOURCE " + str(int(old))
                    + f'\n#line 1 "{template_path.as_posix()}"\n' + text, encoding="utf-8")
    return path


def execute(command: list[str], log: str, expected: int = 0) -> str:
    env = os.environ.copy()
    env["TEMP"] = env["TMP"] = str(OUT)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    result = subprocess.run(command, cwd=OUT, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    (OUT / log).write_bytes(result.stdout)
    output = result.stdout.decode("utf-8", errors="replace")
    print(output, flush=True)
    if result.returncode != expected:
        raise AssertionError(f"{command[0]} returned {result.returncode}; expected {expected}; log: {OUT / log}")
    return output


def compile_target(label: str) -> None:
    execute([os.environ.get("COMSPEC", "C:/Windows/System32/cmd.exe"), "/d", "/c",
             str(HERE / "build-cpu-tests.cmd"), str(OUT / label)], label + "-build.log")


def main() -> None:
    global CURRENT, OUT, ORIGINAL
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, required=True, help="Generic's src/addons/dlss5 directory")
    parser.add_argument("--out", type=Path, required=True, help="a new output directory")
    parser.add_argument("--negative-control", type=Path, help="directory with the earlier draft's two headers")
    args = parser.parse_args()
    CURRENT = args.source.resolve()
    OUT = args.out.resolve()
    ORIGINAL = args.negative_control.resolve() if args.negative_control else None
    OUT.mkdir(parents=True, exist_ok=False)
    vk = Source(CURRENT / "vulkan_path.hpp")
    old = None
    if ORIGINAL is not None:
        old = Source(ORIGINAL / "vulkan_path.hpp")
        checked(sha(old.bytes) == EXPECTED_ORIGINAL, "negative-control source SHA-256 is the recorded draft")
        original_shared = Source(ORIGINAL / "dlssnr.hpp")
    shared = Source(CURRENT / "dlssnr.hpp")
    validate_static(vk, old)
    generate(vk, shared, "current-cpu-tests", False)
    compile_target("current-cpu-tests")
    current_output = execute([str(OUT / "current-cpu-tests.exe")], "current-cpu-tests-run.log")
    negative_names = [
        "look-auto-render-off", "failure-pass-0-result", "failure-pass-2-exception",
        "reset-survives-missing-guides", "reset-survives-native-sr-failure-stage-isolation",
        "reset-before-bypass-0",
    ] if old is not None else []
    if old is not None:
        generate(old, original_shared, "original-cpu-tests", True)
        compile_target("original-cpu-tests")
    for index, name in enumerate(negative_names, 1):
        output = execute([str(OUT / "original-cpu-tests.exe"), name], f"negative-{index}.log", expected=1)
        if f"FAIL {name}:" not in output or "0 passed, 1 failed" not in output:
            raise AssertionError("negative control must fail its behavioral assertion, not crash: " + name)
        print("EXPECTED NEGATIVE FAIL " + name, flush=True)
    checked((CURRENT / "vulkan_path.hpp").read_bytes() == vk.bytes,
            "tested Vulkan source remained unchanged through CPU run")
    summary = {
        "mode": "CPU only: extracted production functions and statement ranges; fake SDK/device/resources",
        "production_source": vk.path.as_posix(), "production_sha256": sha(vk.bytes),
        "shared_source_sha256_at_extraction": sha(shared.bytes),
        "negative_control_sha256": sha(old.bytes) if old is not None else None,
        "cpu_summary": re.search(r"CPU TESTS:.*", current_output)[0],
        "static_assertions_passed": len(STATIC), "static_assertions": STATIC,
        "old_source_expected_failures": negative_names,
        "coverage_boundary": [
            "Full HookedEvaluate compiled; ProcessNr reconciliation/guide gates and evaluate/create/retry branches compiled as extracted fragments.",
            "Actual retirement, recorded-use bookkeeping, reset latch, LookSettingsSnapshot, SetModelParameters, PlanFrameScale and look-history helpers compiled.",
            "GPU work, image content, submission fences, real SDK ABI, full Generic translation unit and concurrency stress not executed.",
            "Command-buffer safety proof is simulated through actual TrackRecordedUse/ForgetRecordedUses, not measured on GPU.",
            "Normalization dispatch ordering checked statically; CPU plan/latch and per-stage model consumption exercised dynamically.",
        ],
        "extractions": MANIFEST,
    }
    (OUT / "cpu-test-manifest.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (OUT / "static-checks.log").write_text("\n".join("PASS " + name for name in STATIC) + "\n", encoding="utf-8")
    print(f"DONE: {summary['cpu_summary']}; static={len(STATIC)}; expected-old-failures={len(negative_names)}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        traceback.print_exc()
        sys.exit(1)
