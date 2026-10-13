# violetBSD

violetBSD is an independent fork of NetBSD 11.0 for the Foenix A2560Me.
Copyright (c) 2026 violetBSD contributors.  All rights reserved. Original copyright notices, license conditions,
and attribution are retained. See [the kernel copyright banner](sys/conf/copyright)
and the individual source files for their notices.

This repository contains the operating-system source. The surrounding project
workspace provides the supported build, packaging, and upload helpers.
From that workspace, build with:

```sh
./build-a2560me.sh GENERIC
```

See [the porting documentation](../porting-docs/README.md) and
[branding notes](../porting-docs/BRANDING.md). This fork's releases and support
are separate from the upstream NetBSD project.

The upstream [BUILDING](BUILDING) guide describes the inherited build system;
its architecture names, compiler targets, ELF identifiers, and ABI constants
remain NetBSD-compatible. Upstream history and third-party component names
are retained as provenance.
