# Stage 1B — Build provenance and artifact integrity

Status: implementation committed on `stage1b/build-provenance-20261124`; CI validation pending. `main` is not modified.

## Scope

Stage 1B removes the major moving inputs from the four repository workflows and makes every published build artifact self-describing and checksum-verifiable.

- GitHub runner label: `ubuntu-24.04`.
- devkitPro toolchain image: `devkitpro/devkita64:20260219`, pinned by the multi-platform OCI index digest `sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282`.
- GitHub Actions are pinned to immutable commit SHAs: checkout v4, upload-artifact v4 and softprops/action-gh-release v2.
- Host-side Debian packages are installed from the timestamped snapshot `20261123T180000Z`, not from the moving `deb.debian.org` head.
- Switch packages are no longer silently updated by `dkp-pacman -S` during native builds. Workflows assert the required packages are already present in the digest-pinned image and record their installed versions.
- The Nextendo overlay fetches libtesla at the exact commit `f633c33875dc43ad0091450c2535ac41f796b512`, instead of cloning a moving default branch.
- The experimental HOME workflow records both the requested and resolved libnx and Atmosphère commits after checkout.

## Build manifest

`scripts/write-build-manifest.sh` emits `BUILD-MANIFEST.txt` with:

- source commit/tree, ref, event, workflow run and attempt;
- build target, runner OS/architecture/image version, container image reference and Debian snapshot;
- pinned action references and `SOURCE_DATE_EPOCH`;
- compiler, Make, Python and CMake versions;
- installed devkitPro package versions and host APT package versions;
- requested/resolved libnx, Atmosphère and third-party source references.

The script deliberately records a small allowlist of build inputs. It does not dump the process environment or GitHub secrets.

## Artifact integrity

`scripts/write-package-checksums.sh` creates a sorted `SHA256SUMS` file covering every regular file in the package directory except `SHA256SUMS` itself, and immediately verifies it.

The manifests/checksums are included with:
- standalone NRO artifact;
- standalone SD-card package;
- experimental HOME library applet artifact;
- tagged release assets;
- experimental HOME installer ZIP (with an additional external `.zip.sha256`);
- Nextendo Tesla overlay artifact.

After extracting an artifact, verify its files from the directory containing `SHA256SUMS`:

```sh
sha256sum -c SHA256SUMS
```

For the experimental system bundle, verify the ZIP sidecar first:

```sh
sha256sum -c DeskNX-system-experimental.zip.sha256
```

## Validation

`tests/test-build-provenance.sh` checks stable manifest fields, confirms that secret-like environment variables are not serialized, and exercises package checksum creation and verification. All four workflows run the relevant manifest/script checks and include the generated provenance in their artifacts.

CI for this Stage 1B branch must pass before this stage is considered closed. The green 23 November runs on PR #70 validate that PR's SDL renderer regression change; they are not evidence for this Stage 1B workflow changes.

## Explicit limitations

This establishes pinned build inputs, provenance and artifact integrity; it does **not** yet promise bit-for-bit reproducible binaries.

- The GitHub-hosted `ubuntu-24.04` image still receives managed point updates. Its image identity is recorded, but not pinned to an immutable VM snapshot.
- Debian packages are sourced from an immutable timestamped snapshot, and the devkitPro container is digest-pinned. GitHub itself, network availability and the GitHub-hosted runner service remain external dependencies.
- Build timestamps/run IDs intentionally differ between runs. `SOURCE_DATE_EPOCH` is set from the checked-out commit to help tools produce stable outputs, but all compiler/linker inputs have not yet been audited for deterministic output.
- `SHA256SUMS` detects accidental or post-publication file changes when checked against the accompanying list. The manifest and checksum list are not signed; they do not authenticate the publisher against an attacker able to replace both the package and its checksums.
- GitHub action SHAs are immutable, but their transitive dependencies and the hosted runner service are not vendored into the repository.

A later hardening stage may add signed attestations and compare two clean builds byte-for-byte. Those are deliberately not claimed here.
