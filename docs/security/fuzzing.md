# Parser Fuzzing and Sanitizers

## Scope

三个 Clang libFuzzer target 直接调用生产解析入口，不复制 parser：

| Target | 主要输入面 |
|---|---|
| `fuzz_package_verifier` | `.lvapp` header/manifest/table/payload/signature envelope、SHA 与 Ed25519 路径 |
| `fuzz_release_state` | 单槽 release state、checksum/value validation、双槽选择与 split-brain |
| `fuzz_session_protocol` | session control、desktop registry、installer/storage request/response、touch frame、signed session profile、device profile JSON |

`generate_fuzz_corpus` 用生产 encoder 生成有效 state/control/registry/storage/touch/profile seeds；package corpus 使用仓库内 dev/signed test vectors。dictionary 只帮助 libFuzzer 越过 magic/schema 分支，不构成接受规则。

## Local build

只支持带 libFuzzer 的 Clang host build。`LVGL_BUILD_FUZZERS=ON` 会强制打开 ASan + UBSan；MSVC、AArch64 production toolchain 或无 libFuzzer 的编译器会在配置阶段失败，不允许生成没有 sanitizer 的同名 target。

```bash
CC=clang CXX=clang++ cmake -S . -B build/fuzz -G Ninja \
  -DBUILD_TESTING=OFF \
  -DLVGL_BUILD_FUZZERS=ON \
  -DLVGL_BUILD_SDK_TEMPLATE=OFF

cmake --build build/fuzz --target \
  generate_fuzz_corpus \
  fuzz_package_verifier fuzz_release_state fuzz_session_protocol

mkdir -p build/corpus/package build/corpus/state build/corpus/session build/artifacts
cp tests/vectors/lvapp/*.lvapp* build/corpus/package/
cp profiles/y01-4.8.6.json build/corpus/session/device-profile.json
build/fuzz/generate_fuzz_corpus build/corpus

build/fuzz/fuzz_package_verifier build/corpus/package \
  -dict=tests/fuzz/package.dict -max_total_time=30 -max_len=1048576 \
  -rss_limit_mb=2048 -timeout=10 -artifact_prefix=build/artifacts/package-
```

release-state 与 session target 使用各自 `.dict`；CI 的精确参数见 `.github/workflows/security-fuzz.yml`。本地发现的 crash input 必须先原样保存，再用同一 commit/flags 单输入复现；不要把包含私有发布数据的 corpus 提交到公开仓库。

## CI behavior

`security-fuzz.yml` 包含两条独立门禁：

1. contract/model tests 在 Clang ASan + UBSan 下运行；
2. 三个 parser target 各运行 30 秒 bounded smoke，单输入 timeout 10 秒，限制长度/RSS，失败时上传 crash artifact。

GitHub 官方 actions 使用完整 commit SHA，不使用可变 major tag。workflow 仅有 `contents: read` 权限，不接收私钥、生产包或设备凭据。

## Interpretation

一次 30 秒无 crash 只证明该次输入探索未发现问题，不能证明 parser 安全。发布门禁应保存 commit、Clang 版本、corpus digest、运行时长、coverage 和 sanitizer 输出。后续 P0 需要首次远端运行证据、长期 corpus、覆盖率阈值、定期长跑、crash 去重/回归 seed 和修复 SLA；独立安全审计仍不可省略。

## Current verification record

2026-08-24 本地已完成：workflow YAML 解析；四个 harness/generator 用 AArch64 GCC 11.3 在 `-Wall -Wextra -Wpedantic -Werror` 下编译；3 个 native tests 和 51 个 Node tests 通过。本机没有 Clang/libFuzzer，WSL sudo 也不能非交互安装，因此尚未本地执行 sanitizer fuzz；首次 GitHub workflow 运行仍是明确发布阻塞项。
