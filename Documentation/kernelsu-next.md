# KernelSU-Next support

This branch vendors KernelSU-Next `v3.2.0-legacy` at commit
`9b08e88862000d5c50fb2e43a5b75123cf472e54`.

The exported KernelSU version code is fixed to `33129`, matching the
KernelSU-Next legacy version formula for this tag.

## Recommended manager

Use KernelSU-Next Manager from the `v3.2.0` / `v3.2.0-legacy` release family.
For this kernel, use the `33129` manager APKs from that release generation.
Newer managers may work, but the safest match is the same generation as the
kernel driver.

## Compatible manager forks

KernelSU-Next authenticates manager APKs by their signing certificate. This
tree supports a comma- or semicolon-separated list of trusted manager
certificates through `KSU_NEXT_MANAGER_HASHES`.

Built-in trusted manager certificates:

- KernelSU-Next Manager `v3.2.0` / `33129`, including the spoofed build:
  `0x3e6:79e590113c4c4c0c222978e413a5faa801666957b1212a328e46c00c69821bf7`
- WildKSU Manager `v3.1.2` / `33208`, including the spoofed build:
  `0x381:52d52d8c8bfbe53dc2b6ff1c613184e2c03013e090fe8905d8e3d5dc2658c2e4`

Additional compatible forks, such as KowSU builds, can be enabled by appending
their APK signing certificate `size:sha256` pairs:

```sh
KSU_NEXT_MANAGER_HASHES='0x3e6:79e590113c4c4c0c222978e413a5faa801666957b1212a328e46c00c69821bf7,0x381:52d52d8c8bfbe53dc2b6ff1c613184e2c03013e090fe8905d8e3d5dc2658c2e4,...' ./build.sh fire --azip
```

KowSU repositories checked for this branch did not publish a release manager
APK or signing certificate, so no unverified KowSU certificate is trusted by
default. Do not disable manager signature checks or trust unknown certificates;
that would allow an arbitrary APK to control root.
