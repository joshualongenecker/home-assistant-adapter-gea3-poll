"""
Extra PlatformIO build script for the gea2_bridge ESPHome component.

home-assistant-bridge/library.json does not list knolleary/PubSubClient as a
dependency.  PlatformIO's LDF therefore never adds PubSubClient's src/ to the
compiler include path when building that library's own source files
(HomeAssistantBridge.cpp, mqtt_client_adapter.cpp), even with lib_ldf_mode =
deep+.

This pre-build script directly appends PubSubClient's src/ to the SCons
CPPPATH in the main project environment.  Because all library builder
environments are cloned from that main environment, the path becomes visible
to every translation unit — including those inside home-assistant-bridge.
"""

import os

Import("env")  # noqa: F821 – injected by SCons / PlatformIO

libdeps = os.path.join(
    env.subst("$PROJECT_LIBDEPS_DIR"),
    env.subst("$PIOENV"),
)
pubsub_src = os.path.join(libdeps, "PubSubClient", "src")

if os.path.isdir(pubsub_src):
    env.Append(CPPPATH=[pubsub_src])
else:
    print(
        f"[gea2_bridge extra_script] WARNING: PubSubClient src not found at "
        f"{pubsub_src!r}. HomeAssistantBridge.h may fail to compile. "
        "Ensure knolleary/PubSubClient is listed in lib_deps."
    )
