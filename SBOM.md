# Source SBOM

Source repository inventory only; completeness is UNKNOWN. Syft inventories tracked source/package manifests. Gitlinks and declared vendored/model evidence are inspected at immutable pins. CMake declarations are conditional requirements, not resolved installations. This is NOT proof of operating-system packages, static-link closure, build-time resolution, or the contents of release binaries.

Generation: **success**

## Source identity

wave-owl/fldigi-throb-core `50c9441bab7b4af726535c765a3ab4a619a614e1`

- sbom:role: source-root
- sbom:git-commit: 50c9441bab7b4af726535c765a3ab4a619a614e1
- sbom:git-tree: 20a85742d8186169053edfc47c5a69daf0615372
- sbom:source-repository: https://github.com/wave-owl/fldigi-throb-core
- sbom:release-tags: ["v0.1.2"]
- sbom:license-evidence: [{"path": "LICENSE", "sha256": "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"}]
- sbom:license-resolution: unresolved; manual review required

## Generation errors

None.

## Components and unresolved licenses

| Component | Version | Role | License |
| --- | --- | --- | --- |
| wave-owl/fldigi-throb-core | 50c9441bab7b4af726535c765a3ab4a619a614e1 | source-root | unresolved; manual review required |
| wave-owl/fldigi-dsp-core | 88db1665c1731921be76fe9823a0e1dc392b9b81 | gitlink | unresolved; manual review required |
| src/throb_common.c | e7c3ab3709ad62a7d91829364eb48a05fa325c0d | declared-vendored-source | unresolved; manual review required |
| src/throb_rx.c | e7c3ab3709ad62a7d91829364eb48a05fa325c0d | declared-vendored-source | unresolved; manual review required |
| src/throb_tx.c | e7c3ab3709ad62a7d91829364eb48a05fa325c0d | declared-vendored-source | unresolved; manual review required |
| actions/checkout | v4 | Syft source/package discovery | unresolved |
| actions/checkout | v4 | Syft source/package discovery | unresolved |
| /Users/gerolfziegenhain/src/wave-owl/local/sbom-core-preflight-v2/fldigi-throb-core/.source-input/.github/workflows/ci.yml | unresolved | Syft source/package discovery | unresolved |
| /Users/gerolfziegenhain/src/wave-owl/local/sbom-core-preflight-v2/fldigi-throb-core/.source-input/dsp/.github/workflows/ci.yml | unresolved | Syft source/package discovery | unresolved |

License evidence paths/hashes, source manifests, model hashes and conditional
CMake declarations are recorded in SBOM.cdx.json. Declared upstream pins do not
prove vendored equivalence. No dependency installation or build was performed.
