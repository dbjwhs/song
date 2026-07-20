# Song — Open-Source Readiness Report

*The closing report of the review engagement. Where `ARCHITECTURE_REVIEW.md` asks
"is the design sound?", this asks "is it ready to put in front of the open-source
community, and how?" It reflects the combined view of the review team (RPC, CI/CD,
and security) plus an open-source maintainer's perspective.*

---

## 1. The one decision that shapes everything: positioning

Song must be launched **as what it is**: a from-scratch, well-engineered *learning and
portfolio project* that demonstrates end-to-end systems work (custom wire protocol,
IDL compiler, process isolation, multi-transport RPC), **not** as a production RPC
framework competing with gRPC or Cap'n Proto.

This matters because the code quality invites the wrong expectation. The testing and
CI discipline are production-grade, so a visitor may assume the *framework* is
production-ready — and then hit the documented trade-offs (trusted-LAN threat model,
native-endian wire format, shared-state multi-client path). Every honest signal below
depends on setting this expectation on the first screen of the README. Get this right
and Song is an *excellent* portfolio piece and teaching resource; get it wrong and the
first serious user files a disappointed issue.

**Action:** add a short "Project status / What this is" callout near the top of the
README — "a learning-first, from-scratch framework; trusted-LAN threat model; not
intended for production adoption" — linking to `SECURITY.md` and
`ARCHITECTURE_REVIEW.md`.

## 2. What is already strong (ready to show)

- **Engineering discipline**: ~1,085 C++ tests + 52 Python tests, zero warnings under
  `-Wall -Wextra -Werror`, a reproducible CI gate, fuzz targets, and MIT headers
  enforced by a pre-commit hook.
- **Honest documentation**: unsupported IDL features are enumerated and rejected in
  the compiler rather than half-working; `SECURITY.md` now documents the threat model
  and the deliberate trade-offs.
- **Clean architecture**: a real transport abstraction, a versioned/capability-
  negotiated protocol, and a self-contained IDL toolchain.
- **Zero mandatory dependencies**: TLS/OpenSSL/Avahi are optional and compile-gated.
- **CI is public-ready**: GitHub Actions across Linux/macOS plus an ASan/UBSan job.

These are exactly the things that make a repo credible at first glance, and they are
already in place.

## 3. What is missing for a public launch

Community hygiene (present / missing):

| Item | Status |
|------|--------|
| `LICENSE` (MIT) | present |
| `SECURITY.md` | present (added this pass) |
| CI workflow + README badge | present |
| `CONTRIBUTING.md` | **missing** |
| `CODE_OF_CONDUCT.md` | **missing** |
| `CHANGELOG.md` | **missing** |
| Issue / PR templates | **missing** |
| Install / packaging (`cmake --install`, `find_package(Song)`) | **missing** |

Technical gaps that affect *reuse* (not correctness):

- **No install/export.** `CMakeLists.txt` builds the library and tools but exposes no
  `install()` rules or package config, so a downstream project cannot `find_package`
  Song or link it without vendoring. For a library this is the single biggest
  "consumability" gap.
- **Build ergonomics.** The README's build steps are clear, but there is no
  one-command bootstrap and no packaged release. A `CMakePresets.json` and a tagged
  release would lower the barrier.
- **Portability boundary.** The native-endian wire format and POSIX fork/exec model
  mean "Linux/macOS only, same architecture on both ends." That is fine, but it should
  be stated as a supported-platforms matrix so nobody expects Windows or cross-arch.

## 4. Maintenance realities to decide before launching

Open-sourcing creates obligations. The team's advice:

- **Set a contribution bar up front.** The pre-commit hooks (license, trailing
  newline) and the `-Werror` gate already encode a high bar; `CONTRIBUTING.md` should
  state it plainly (how to run `tooling/ci.sh`, the commit-message convention, that
  every change needs a test).
- **Decide the support posture.** A portfolio project can legitimately say "issues and
  PRs welcome, but this is maintained on a best-effort basis" — say it, so the
  expectation is set.
- **Security disclosure.** `SECURITY.md` exists; add a real contact channel and a note
  that the trusted-LAN threat model means "network-exposed use is out of scope,"
  which pre-empts a class of low-value reports.

## 5. Pre-launch checklist (concrete, ordered)

1. README "Project status" callout (positioning) — *highest leverage, lowest effort.*
2. `CONTRIBUTING.md` (build, `tooling/ci.sh`, commit convention, test requirement) and
   `CODE_OF_CONDUCT.md` (adopt Contributor Covenant).
3. CMake `install()` + package config export; a `CMakePresets.json`.
4. Supported-platforms matrix + the wire-format/endianness caveat in the README.
5. Issue and PR templates; enable the CI badge's target branch protection.
6. `CHANGELOG.md` and a first tagged release (`v0.1.0`) once the above land.
7. Reconcile the README test count with the current suite (the number drifts every
   time tests are added; consider generating it in CI instead of hard-coding).

## 6. Recommendation

**Launch it — as a portfolio/learning project, with the positioning fixed first.**
The engineering substance is already there and is, frankly, better than most repos
that call themselves frameworks. The gap to a *credible public release* is not code;
it is (a) one paragraph of honest positioning, (b) standard community files, and (c)
install/packaging so the library is actually consumable. Items 1–3 above are a
weekend of work and convert Song from "impressive private repo" into "impressive
public project that says exactly what it is." The deeper architectural investment
(per-connection object isolation, backpressure, cross-arch wire format) is only
warranted if Song is meant to grow past its demonstration purpose — and that is a
choice to make deliberately, not a prerequisite for going public.
