#include "ShadeModel.h"
#include "ModelEditor.h"

void SomfyGroup::clear() {
  this->setGroupId(255);
  this->setRemoteAddress(0);
  this->repeats = 0;
  this->roomId = 0;
  this->name[0] = 0x00;
  memset(&this->linkedShades, 0x00, sizeof(this->linkedShades));
}
// False when the group is full.  The caller saves the change.
bool SomfyGroup::linkShade(uint8_t shadeId) { return addToList(this->linkedShades, SOMFY_MAX_GROUPED_SHADES, shadeId); }
// False when the shade wasn't in the group.  The caller saves the change.
bool SomfyGroup::unlinkShade(uint8_t shadeId) { return removeFromList(this->linkedShades, SOMFY_MAX_GROUPED_SHADES, shadeId); }
// Linked shade ids are kept packed so a search can stop at the first 0.
void SomfyGroup::compressLinkedShadeIds() { packList(this->linkedShades, SOMFY_MAX_GROUPED_SHADES); }
bool SomfyGroup::hasShadeId(uint8_t shadeId) const {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == 0) break;
    if(this->linkedShades[i] == shadeId) return true;
  }
  return false;
}
int8_t SomfyGroup::p_direction(int8_t dir) {
  int8_t old = this->direction;
  if(old != dir) {
    this->direction = dir;
    this->changed |= GROUP_CHANGED_DIRECTION;
  }
  return old;
}
