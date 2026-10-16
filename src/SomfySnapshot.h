#ifndef SOMFY_SNAPSHOT_H
#define SOMFY_SNAPSHOT_H
#include "ModelSnapshot.h"
#include "Somfy.h"

// Copies the shade model into the ModelSnapshot types.

void snapshotRoom(SomfyRoom &room, room_snapshot_t &out);
void snapshotShade(SomfyShade &shade, shade_snapshot_t &out);
// Works the group's flags out from its shades without changing the group.
void snapshotGroup(SomfyGroup &group, group_snapshot_t &out);
// The whole shade model.  loop() only.
void fillSnapshot(model_snapshot_t &snap);
// A shade_lookup_t over the live shade model for writeGroup.  ctx is a
// shade_snapshot_t to copy into.
const shade_snapshot_t *liveShade(uint8_t shadeId, void *ctx);
#endif
