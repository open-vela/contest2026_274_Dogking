# openvela, NuttX, and project boundaries

## Classify every change

- **NuttX/kernel:** architecture startup, scheduler/SMP, MMU, interrupts, core drivers, VFS, networking internals.
- **openvela platform/framework:** official services, AI/audio/media/graphics frameworks, lifecycle, adapters, and configuration conventions.
- **Vendor/board:** SoC clocks/resets/pinmux, board initialization, firmware loading, board drivers, and defconfig.
- **Product/application:** commands, policies, credentials, UI, model routing, and workflows.

An NSH shell on NuttX is not automatically an openvela integration. Demonstrate openvela framework APIs/services where available and document why private code is necessary where absent.

## Keep upstream clean

Store durable code, patches, and overlays in the project repository. A build wrapper should verify upstream revisions, record originals, apply project changes, build, copy artifacts/logs out, restore in a trap/finally path, and verify the official tree is clean.

Never hide permanent source only in generated output. Never commit API keys, passwords, private certificates, generated host keys, or proprietary model binaries without explicit rights.

## Prefer official components without forcing them

Use official openvela network, AI agent, audio/media, graphics/LVGL, configuration, IPC, or security components when their contracts fit. Put a narrow board adapter beneath the official interface. If replacement is necessary, preserve a future-compatible boundary.
