# FML script-connection work in progress

The source edition includes an isolated snapshot of FML's general Psych behavior importer in [FMLScriptConnection](FMLScriptConnection/README.md).

This folder contains working source components and a separate build check. It is excluded from the normal Psych Importer build and does not enable general Lua/HScript connections in the released application.

The existing modchart collector remains in the application's `Modchart.hpp` and `LuaLite.hpp`. Its supported export behavior is independent of this development area.
