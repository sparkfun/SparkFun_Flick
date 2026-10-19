#pragma once

/* Start mDNS with this hostname unless the sketch already started it. */
bool bl_mdns_ensure(const char *hostname);
