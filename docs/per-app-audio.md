# Experimental per-app volume

On macOS 14.2 or newer, Volume can lower a supported audio process's level through a temporary audio route. Allow System Audio Recording when asked. Audio is processed in memory and is not saved.

Lowering a slider below 100% starts routing. Returning it to 100% keeps the route active at full volume. Routes can stay active when Volume or the HUD is hidden. Sleep, quit, process loss or relevant device changes stop them.

Support is limited to compatible built-in or USB stereo outputs with no input channels and matching supported formats. Bluetooth, multichannel and incompatible routes are unavailable. Use the ordinary device-volume control when per-app routing is unsupported.
