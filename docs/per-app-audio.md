# Experimental per-app volume

On macOS 14.2 or newer, Volume can lower a supported audio process's level through a temporary audio route. Allow System Audio Recording when asked. Audio is processed in memory and is not saved.

Lowering a slider below 100% starts routing. Returning it to 100% keeps the route active at full volume. Routes can stay active when Volume or the HUD is hidden. Sleep, quit, process loss or relevant device changes stop them.

Support is limited to stereo outputs with matching supported formats: built-in, USB, Bluetooth, HDMI, DisplayPort, Thunderbolt, FireWire and PCI devices. A device that also has a microphone, as most Bluetooth headsets do, is supported: the route keeps the microphone switched off, so it is never opened and a Bluetooth headset keeps its playback quality. AirPlay, aggregate, virtual and multichannel outputs are unavailable. The empty app list says why. Use the ordinary device-volume control when per-app routing is unsupported.
