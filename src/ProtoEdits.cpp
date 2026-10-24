#include <stdio.h>
#include "ProtoEdits.h"

struct edit_op_t {
  proto_type_t type;
  model_edit_type_t edit;
};
static const edit_op_t EDIT_OPS[] = {
  {proto_type_t::addRoom, model_edit_type_t::add_room},
  {proto_type_t::addShade, model_edit_type_t::add_shade},
  {proto_type_t::addGroup, model_edit_type_t::add_group},
  {proto_type_t::saveRoom, model_edit_type_t::save_room},
  {proto_type_t::saveShade, model_edit_type_t::save_shade},
  {proto_type_t::saveGroup, model_edit_type_t::save_group},
  {proto_type_t::deleteRoom, model_edit_type_t::delete_room},
  {proto_type_t::deleteShade, model_edit_type_t::delete_shade},
  {proto_type_t::deleteGroup, model_edit_type_t::delete_group},
  {proto_type_t::linkRemote, model_edit_type_t::link_remote},
  {proto_type_t::unlinkRemote, model_edit_type_t::unlink_remote},
  {proto_type_t::linkToGroup, model_edit_type_t::link_to_group},
  {proto_type_t::unlinkFromGroup, model_edit_type_t::unlink_from_group},
  {proto_type_t::linkRepeater, model_edit_type_t::link_repeater},
  {proto_type_t::unlinkRepeater, model_edit_type_t::unlink_repeater},
  {proto_type_t::setPaired, model_edit_type_t::set_paired},
  {proto_type_t::roomSortOrder, model_edit_type_t::room_order},
  {proto_type_t::shadeSortOrder, model_edit_type_t::shade_order},
  {proto_type_t::groupSortOrder, model_edit_type_t::group_order}
};
bool protoEditType(uint8_t type, model_edit_type_t &out) {
  for(const edit_op_t &op : EDIT_OPS) {
    if(static_cast<uint8_t>(op.type) != type) continue;
    out = op.edit;
    return true;
  }
  return false;
}
bool protoIsReadOp(uint8_t type) {
  switch(static_cast<proto_type_t>(type)) {
    case proto_type_t::getNextRoom:
    case proto_type_t::getNextShade:
    case proto_type_t::getNextGroup:
    case proto_type_t::groupOptions:
      return true;
    default:
      return false;
  }
}

// What the reply's desc points at; every parse runs on async_tcp, one at a
// time.
static char why[64];
// One value under its field's kind: false when it isn't one.
static bool expandValue(const proto_field_info_t &f, JsonVariantConst v, JsonVariant out) {
  switch(f.kind) {
    case 'b':
      // Booleans are 1 and 0; the edit routes read true and false.
      if(!v.is<int>() || (v.as<int>() != 0 && v.as<int>() != 1)) return false;
      out.set(v.as<int>() == 1);
      return true;
    case 'i':
      if(v.is<int64_t>() || v.is<uint64_t>()) return out.set(v);
      return false;
    case 'n':
      if(v.is<float>()) return out.set(v);
      return false;
    case 's':
      if(v.is<const char *>()) return out.set(v);
      return false;
    case 'l':
      if(v.is<JsonArrayConst>()) return out.set(v);
      return false;
    default:
      return false;
  }
}
const char *expandEditPayload(model_edit_type_t type, JsonObjectConst d, JsonDocument &doc) {
  doc.clear();
  switch(type) {
    case model_edit_type_t::room_order:
    case model_edit_type_t::shade_order:
    case model_edit_type_t::group_order: {
      // The route takes the ids on their own, in their new order.
      JsonVariantConst ids = d[protoField(proto_field_t::ids).key];
      if(!ids.is<JsonArrayConst>()) return "ids is missing";
      for(JsonVariantConst id : ids.as<JsonArrayConst>())
        if(!id.is<uint8_t>()) return "ids must be a list of ids";
      doc.set(ids);
      return doc.overflowed() ? "Too large" : nullptr;
    }
    default:
      break;
  }
  JsonObject obj = doc.to<JsonObject>();
  for(JsonPairConst kv : d) {
    const proto_field_info_t *f = protoFieldByKey(kv.key().c_str());
    if(!f) {
      snprintf(why, sizeof(why), "Unknown field %s", kv.key().c_str());
      return why;
    }
    if(!expandValue(*f, kv.value(), obj[f->name].to<JsonVariant>())) {
      snprintf(why, sizeof(why), "%s is not a %s", f->name,
        f->kind == 'b' ? "boolean (1 or 0)" : f->kind == 'i' ? "whole number" : f->kind == 'n' ? "number" :
        f->kind == 's' ? "string" : "list");
      return why;
    }
  }
  return doc.overflowed() ? "Too large" : nullptr;
}
