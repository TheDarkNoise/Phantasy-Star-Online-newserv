#include "Player.hh"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <phosg/Filesystem.hh>
#include <phosg/Hash.hh>
#include <stdexcept>

#include "FileContentsCache.hh"
#include "ItemData.hh"
#include "Loggers.hh"
#include "StaticGameData.hh"
#include "Text.hh"
#include "Version.hh"

using namespace std;

// Originally there was going to be a language-based header, but then I decided
// against it. These strings were already in use for that parser, so I didn't
// bother changing them.
static const string PLAYER_FILE_SIGNATURE =
    "newserv player file format; 10 sections present; sequential;";
static const string ACCOUNT_FILE_SIGNATURE =
    "newserv account file format; 7 sections present; sequential;";

static FileContentsCache player_files_cache(300 * 1000 * 1000);

PlayerStats::PlayerStats() noexcept
    : level(0),
      experience(0),
      meseta(0) {}

PlayerVisualConfig::PlayerVisualConfig() noexcept
    : unknown_a2(0),
      name_color(0),
      extra_model(0),
      unknown_a3(0),
      section_id(0),
      char_class(0),
      v2_flags(0),
      version(0),
      v1_flags(0),
      costume(0),
      skin(0),
      face(0),
      head(0),
      hair(0),
      hair_r(0),
      hair_g(0),
      hair_b(0),
      proportion_x(0),
      proportion_y(0) {}

void PlayerDispDataDCPCV3::enforce_v2_limits() {
  // V1/V2 have fewer classes, so we'll substitute some here
  if (this->visual.char_class == 11) {
    this->visual.char_class = 0; // FOmar -> HUmar
  } else if (this->visual.char_class == 10) {
    this->visual.char_class = 1; // RAmarl -> HUnewearl
  } else if (this->visual.char_class == 9) {
    this->visual.char_class = 5; // HUcaseal -> RAcaseal
  }

  // If the player is somehow still not a valid class, make them appear as the
  // "ninja" NPC
  if (this->visual.char_class > 8) {
    this->visual.extra_model = 0;
    this->visual.v2_flags |= 2;
  }
  this->visual.version = 2;
}

PlayerDispDataBB PlayerDispDataDCPCV3::to_bb() const {
  PlayerDispDataBB bb;
  bb.stats = this->stats;
  bb.visual = this->visual;
  bb.visual.name = "         0";
  bb.name = add_language_marker(this->visual.name, 'J');
  bb.config = this->config;
  bb.technique_levels = this->v1_technique_levels;
  return bb;
}

PlayerDispDataBB::PlayerDispDataBB() noexcept
    : play_time(0),
      unknown_a3(0) {}

PlayerDispDataDCPCV3 PlayerDispDataBB::to_dcpcv3() const {
  PlayerDispDataDCPCV3 ret;
  ret.stats = this->stats;
  ret.visual = this->visual;
  ret.visual.name = remove_language_marker(this->name);
  ret.config = this->config;
  ret.v1_technique_levels = this->technique_levels;
  return ret;
}

PlayerDispDataBBPreview PlayerDispDataBB::to_preview() const {
  PlayerDispDataBBPreview pre;
  pre.level = this->stats.level;
  pre.experience = this->stats.experience;
  pre.visual = this->visual;
  pre.name = this->name;
  pre.play_time = this->play_time;
  return pre;
}

void PlayerDispDataBB::apply_preview(const PlayerDispDataBBPreview& pre) {
  this->stats.level = pre.level;
  this->stats.experience = pre.experience;
  this->visual = pre.visual;
  this->name = pre.name;
}

void PlayerDispDataBB::apply_dressing_room(const PlayerDispDataBBPreview& pre) {
  this->visual.name_color = pre.visual.name_color;
  this->visual.extra_model = pre.visual.extra_model;
  this->visual.unknown_a3 = pre.visual.unknown_a3;
  this->visual.section_id = pre.visual.section_id;
  this->visual.char_class = pre.visual.char_class;
  this->visual.v2_flags = pre.visual.v2_flags;
  this->visual.version = pre.visual.version;
  this->visual.v1_flags = pre.visual.v1_flags;
  this->visual.costume = pre.visual.costume;
  this->visual.skin = pre.visual.skin;
  this->visual.face = pre.visual.face;
  this->visual.head = pre.visual.head;
  this->visual.hair = pre.visual.hair;
  this->visual.hair_r = pre.visual.hair_r;
  this->visual.hair_g = pre.visual.hair_g;
  this->visual.hair_b = pre.visual.hair_b;
  this->visual.proportion_x = pre.visual.proportion_x;
  this->visual.proportion_y = pre.visual.proportion_y;
  this->name = pre.name;
}

PlayerDispDataBBPreview::PlayerDispDataBBPreview() noexcept
    : experience(0),
      level(0),
      play_time(0) {}

GuildCardV3::GuildCardV3() noexcept
    : player_tag(0),
      guild_card_number(0),
      present(0),
      language(0),
      section_id(0),
      char_class(0) {}

GuildCardBB::GuildCardBB() noexcept
    : guild_card_number(0),
      present(0),
      language(0),
      section_id(0),
      char_class(0) {}

void GuildCardBB::clear() {
  this->guild_card_number = 0;
  this->name.clear(0);
  this->team_name.clear(0);
  this->description.clear(0);
  this->present = 0;
  this->language = 0;
  this->section_id = 0;
  this->char_class = 0;
}

void GuildCardEntryBB::clear() {
  this->data.clear();
  this->unknown_a1.clear(0);
}

uint32_t GuildCardFileBB::checksum() const {
  return crc32(this, sizeof(*this));
}

void PlayerBank::load(const string& filename) {
  *this = player_files_cache.get_obj_or_load<PlayerBank>(filename).obj;
  for (uint32_t x = 0; x < this->num_items; x++) {
    this->items[x].data.id = 0x0F010000 + x;
  }
}

void PlayerBank::save(const string& filename, bool save_to_filesystem) const {
  player_files_cache.replace(filename, this, sizeof(*this));
  if (save_to_filesystem) {
    save_file(filename, this, sizeof(*this));
  }
}

////////////////////////////////////////////////////////////////////////////////

ClientGameData::ClientGameData()
    : last_play_time_update(0),
      guild_card_number(0),
      should_update_play_time(false),
      bb_player_index(0),
      should_save(true) {}

ClientGameData::~ClientGameData() {
  if (!this->bb_username.empty()) {
    if (this->account_data.get()) {
      this->save_account_data();
    }
    if (this->player_data.get()) {
      this->save_player_data();
    }
  }
}

shared_ptr<SavedAccountDataBB> ClientGameData::account(bool should_load) {
  if (!this->account_data.get() && should_load) {
    if (this->bb_username.empty()) {
      this->account_data.reset(new SavedAccountDataBB());
      this->account_data->signature = ACCOUNT_FILE_SIGNATURE;
    } else {
      this->load_account_data();
    }
  }
  return this->account_data;
}

shared_ptr<SavedPlayerDataBB> ClientGameData::player(bool should_load) {
  if (!this->player_data.get() && should_load) {
    if (this->bb_username.empty()) {
      this->player_data.reset(new SavedPlayerDataBB());
      this->player_data->signature = PLAYER_FILE_SIGNATURE;
    } else {
      this->load_player_data();
    }
  }
  return this->player_data;
}

shared_ptr<const SavedAccountDataBB> ClientGameData::account() const {
  if (!this->account_data.get()) {
    throw runtime_error("account data is not loaded");
  }
  return this->account_data;
}

shared_ptr<const SavedPlayerDataBB> ClientGameData::player() const {
  if (!this->player_data.get()) {
    throw runtime_error("player data is not loaded");
  }
  return this->player_data;
}

string ClientGameData::account_data_filename() const {
  if (this->bb_username.empty()) {
    throw logic_error("non-BB players do not have account data");
  }
  return string_printf("system/players/account_%s.nsa",
      this->bb_username.c_str());
}

string ClientGameData::player_data_filename() const {
  if (this->bb_username.empty()) {
    throw logic_error("non-BB players do not have account data");
  }
  return string_printf("system/players/player_%s_%zu.nsc",
      this->bb_username.c_str(), this->bb_player_index + 1);
}

string ClientGameData::player_template_filename(uint8_t char_class) {
  return string_printf("system/players/default_player_%hhu.nsc", char_class);
}

void ClientGameData::create_player(
    const PlayerDispDataBBPreview& preview,
    shared_ptr<const LevelTable> level_table) {
  shared_ptr<SavedPlayerDataBB> data(new SavedPlayerDataBB(
      load_object_file<SavedPlayerDataBB>(player_template_filename(preview.visual.char_class))));
  if (data->signature != PLAYER_FILE_SIGNATURE) {
    throw runtime_error("player data header is incorrect");
  }

  try {
    data->disp.apply_preview(preview);
    data->disp.stats.char_stats = level_table->base_stats_for_class(data->disp.visual.char_class);
  } catch (const exception& e) {
    throw runtime_error(string_printf("template application failed: %s", e.what()));
  }

  this->player_data = data;

  this->save_player_data();
}

void ClientGameData::load_account_data() {
  string filename = this->account_data_filename();

  shared_ptr<SavedAccountDataBB> data;
  try {
    data.reset(new SavedAccountDataBB(
        player_files_cache.get_obj_or_load<SavedAccountDataBB>(filename).obj));
    if (data->signature != ACCOUNT_FILE_SIGNATURE) {
      throw runtime_error("account data header is incorrect");
    }
    player_data_log.info("Loaded account data file %s", filename.c_str());

  } catch (const exception& e) {
    player_data_log.info("Cannot load account data for %s (%s); using default",
        this->bb_username.c_str(), e.what());
    player_files_cache.delete_key(filename);
    data.reset(new SavedAccountDataBB(
        player_files_cache.get_obj_or_load<SavedAccountDataBB>(
                              "system/players/default.nsa")
            .obj));
    if (data->signature != ACCOUNT_FILE_SIGNATURE) {
      throw runtime_error("default account data header is incorrect");
    }
    player_data_log.info("Loaded default account data file");
  }

  this->account_data = data;
}

void ClientGameData::save_account_data() const {
  if (!this->account_data.get()) {
    throw logic_error("save_account_data called when no account data loaded");
  }
  string filename = this->account_data_filename();
  player_files_cache.replace(filename, this->account_data.get(), sizeof(SavedAccountDataBB));
  if (this->should_save) {
    save_file(filename, this->account_data.get(), sizeof(SavedAccountDataBB));
    player_data_log.info("Saved account data file %s to filesystem", filename.c_str());
  } else {
    player_data_log.info("Saved account data file %s to cache only", filename.c_str());
  }
}

void ClientGameData::load_player_data() {
  this->last_play_time_update = now();
  string filename = this->player_data_filename();
  shared_ptr<SavedPlayerDataBB> data(new SavedPlayerDataBB(
      player_files_cache.get_obj_or_load<SavedPlayerDataBB>(filename).obj));
  if (data->signature != PLAYER_FILE_SIGNATURE) {
    player_files_cache.delete_key(filename);
    throw runtime_error("player data header is incorrect");
  }
  this->player_data = data;
  player_data_log.info("Loaded player data file %s", filename.c_str());
}

void ClientGameData::save_player_data() {
  if (!this->player_data.get()) {
    throw logic_error("save_player_data called when no player data loaded");
  }
  if (this->should_update_play_time) {
    // This is slightly inaccurate, since fractions of a second are truncated
    // off each time we save. I'm lazy, so insert shrug emoji here.
    uint64_t t = now();
    uint64_t seconds = (t - this->last_play_time_update) / 1000000;
    this->player_data->disp.play_time += seconds;
    player_data_log.info("Added %" PRIu64 " seconds to play time", seconds);
    this->last_play_time_update = t;
  }
  string filename = this->player_data_filename();
  player_files_cache.replace(filename, this->player_data.get(), sizeof(SavedPlayerDataBB));
  if (this->should_save) {
    save_file(filename, this->player_data.get(), sizeof(SavedPlayerDataBB));
    player_data_log.info("Saved player data file %s to filesystem", filename.c_str());
  } else {
    player_data_log.info("Saved player data file %s to cache only", filename.c_str());
  }
}

void ClientGameData::import_player(const PSOPlayerDataDCPC& pd) {
  auto player = this->player();
  player->inventory = pd.inventory;
  player->disp = pd.disp.to_bb();
  // TODO: Add these fields to the command structure so we can parse them
  // info_board = pd.info_board;
  // blocked_senders = pd.blocked_senders;
  // auto_reply = pd.auto_reply;
}

void ClientGameData::import_player(const PSOPlayerDataV3& gc, bool is_gc) {
  auto account = this->account();
  auto player = this->player();
  player->inventory = gc.inventory;
  if (is_gc) {
    for (size_t z = 0; z < 30; z++) {
      player->inventory.items[z].data.bswap_data2_if_mag();
    }
  }
  player->disp = gc.disp.to_bb();
  player->info_board = gc.info_board;
  account->blocked_senders = gc.blocked_senders;
  if (gc.auto_reply_enabled) {
    player->auto_reply = gc.auto_reply;
  } else {
    player->auto_reply.clear(0);
  }
}

void ClientGameData::import_player(const PSOPlayerDataBB& bb) {
  auto account = this->account();
  auto player = this->player();
  // Note: we don't copy the inventory and disp here because we already have
  // them (we sent the player data to the client in the first place)
  player->info_board = bb.info_board;
  account->blocked_senders = bb.blocked_senders;
  if (bb.auto_reply_enabled) {
    player->auto_reply = bb.auto_reply;
  } else {
    player->auto_reply.clear(0);
  }
}

PlayerBB ClientGameData::export_player_bb() {
  auto account = this->account();
  auto player = this->player();

  PlayerBB ret;
  ret.inventory = player->inventory;
  ret.disp = player->disp;
  ret.unknown.clear(0);
  ret.option_flags = account->option_flags;
  ret.quest_data1 = player->quest_data1;
  ret.bank = player->bank;
  ret.guild_card_number = this->guild_card_number;
  ret.name = player->disp.name;
  ret.team_name = account->team_name;
  ret.guild_card_description = player->guild_card_description;
  ret.reserved1 = 0;
  ret.reserved2 = 0;
  ret.section_id = player->disp.visual.section_id;
  ret.char_class = player->disp.visual.char_class;
  ret.unknown3 = 0;
  ret.symbol_chats = account->symbol_chats;
  ret.shortcuts = account->shortcuts;
  ret.auto_reply = player->auto_reply;
  ret.info_board = player->info_board;
  ret.unknown5.clear(0);
  ret.challenge_data = player->challenge_data;
  ret.tech_menu_config = player->tech_menu_config;
  ret.unknown6.clear(0);
  ret.quest_data2 = player->quest_data2;
  ret.key_config = account->key_config;
  return ret;
}

void PlayerLobbyDataPC::clear() {
  this->player_tag = 0;
  this->guild_card = 0;
  this->ip_address = 0;
  this->client_id = 0;
  ptext<char16_t, 0x10> name;
}

void PlayerLobbyDataDCGC::clear() {
  this->player_tag = 0;
  this->guild_card = 0;
  this->ip_address = 0;
  this->client_id = 0;
  ptext<char, 0x10> name;
}

void XBNetworkLocation::clear() {
  this->internal_ipv4_address = 0;
  this->external_ipv4_address = 0;
  this->port = 0;
  this->mac_address.clear(0);
  this->unknown_a1.clear(0);
  this->account_id = 0;
  this->unknown_a2.clear(0);
}

void PlayerLobbyDataXB::clear() {
  this->player_tag = 0;
  this->guild_card = 0;
  this->netloc.clear();
  this->client_id = 0;
  this->name.clear(0);
}

void PlayerLobbyDataBB::clear() {
  this->player_tag = 0;
  this->guild_card = 0;
  this->ip_address = 0;
  this->unknown_a1.clear(0);
  this->client_id = 0;
  this->name.clear(0);
  this->unknown_a2 = 0;
}

////////////////////////////////////////////////////////////////////////////////

PlayerInventoryItem::PlayerInventoryItem() {
  this->clear();
}

PlayerInventoryItem::PlayerInventoryItem(const PlayerBankItem& src)
    : present(1),
      extension_data1(0),
      extension_data2(0),
      flags(0),
      data(src.data) {}

void PlayerInventoryItem::clear() {
  this->present = 0x0000;
  this->extension_data1 = 0x00;
  this->extension_data2 = 0x00;
  this->flags = 0x00000000;
  this->data.clear();
}

PlayerBankItem::PlayerBankItem() {
  this->clear();
}

PlayerBankItem::PlayerBankItem(const PlayerInventoryItem& src)
    : data(src.data),
      amount(this->data.stack_size()),
      show_flags(1) {}

void PlayerBankItem::clear() {
  this->data.clear();
  this->amount = 0;
  this->show_flags = 0;
}

PlayerInventory::PlayerInventory()
    : num_items(0),
      hp_materials_used(0),
      tp_materials_used(0),
      language(0) {}

// TODO: Eliminate duplication between this function and the parallel function
// in PlayerBank
void SavedPlayerDataBB::add_item(const PlayerInventoryItem& item) {
  uint32_t pid = item.data.primary_identifier();

  // Annoyingly, meseta is in the disp data, not in the inventory struct. If the
  // item is meseta, we have to modify disp instead.
  if (pid == MESETA_IDENTIFIER) {
    this->disp.stats.meseta += item.data.data2d;
    if (this->disp.stats.meseta > 999999) {
      this->disp.stats.meseta = 999999;
    }
    return;
  }

  // Handle combinable items
  size_t combine_max = item.data.max_stack_size();
  if (combine_max > 1) {
    // Get the item index if there's already a stack of the same item in the
    // player's inventory
    size_t y;
    for (y = 0; y < this->inventory.num_items; y++) {
      if (this->inventory.items[y].data.primary_identifier() == item.data.primary_identifier()) {
        break;
      }
    }

    // If we found an existing stack, add it to the total and return
    if (y < this->inventory.num_items) {
      this->inventory.items[y].data.data1[5] += item.data.data1[5];
      if (this->inventory.items[y].data.data1[5] > combine_max) {
        this->inventory.items[y].data.data1[5] = combine_max;
      }
      return;
    }
  }

  // If we get here, then it's not meseta and not a combine item, so it needs to
  // go into an empty inventory slot
  if (this->inventory.num_items >= 30) {
    throw runtime_error("inventory is full");
  }
  this->inventory.items[this->inventory.num_items] = item;
  this->inventory.num_items++;
}

void PlayerBank::add_item(const PlayerBankItem& item) {
  uint32_t pid = item.data.primary_identifier();

  if (pid == MESETA_IDENTIFIER) {
    this->meseta += item.data.data2d;
    if (this->meseta > 999999) {
      this->meseta = 999999;
    }
    return;
  }

  size_t combine_max = item.data.max_stack_size();
  if (combine_max > 1) {
    size_t y;
    for (y = 0; y < this->num_items; y++) {
      if (this->items[y].data.primary_identifier() == item.data.primary_identifier()) {
        break;
      }
    }

    if (y < this->num_items) {
      this->items[y].data.data1[5] += item.data.data1[5];
      if (this->items[y].data.data1[5] > combine_max) {
        this->items[y].data.data1[5] = combine_max;
      }
      this->items[y].amount = this->items[y].data.data1[5];
      return;
    }
  }

  if (this->num_items >= 200) {
    throw runtime_error("bank is full");
  }
  this->items[this->num_items] = item;
  this->num_items++;
}

// TODO: Eliminate code duplication between this function and the parallel
// function in PlayerBank
PlayerInventoryItem SavedPlayerDataBB::remove_item(
    uint32_t item_id, uint32_t amount, bool allow_meseta_overdraft) {
  PlayerInventoryItem ret;

  // If we're removing meseta (signaled by an invalid item ID), then create a
  // meseta item.
  if (item_id == 0xFFFFFFFF) {
    if (amount <= this->disp.stats.meseta) {
      this->disp.stats.meseta -= amount;
    } else if (allow_meseta_overdraft) {
      this->disp.stats.meseta = 0;
    } else {
      throw out_of_range("player does not have enough meseta");
    }
    ret.data.data1[0] = 0x04;
    ret.data.data2d = amount;
    return ret;
  }

  size_t index = this->inventory.find_item(item_id);
  auto& inventory_item = this->inventory.items[index];

  // If the item is a combine item and are we removing less than we have of it,
  // then create a new item and reduce the amount of the existing stack. Note
  // that passing amount == 0 means to remove the entire stack, so this only
  // applies if amount is nonzero.
  if (amount && (inventory_item.data.stack_size() > 1) &&
      (amount < inventory_item.data.data1[5])) {
    ret = inventory_item;
    ret.data.data1[5] = amount;
    ret.data.id = 0xFFFFFFFF;
    inventory_item.data.data1[5] -= amount;
    return ret;
  }

  // If we get here, then it's not meseta, and either it's not a combine item or
  // we're removing the entire stack. Delete the item from the inventory slot
  // and return the deleted item.
  ret = inventory_item;
  this->inventory.num_items--;
  for (size_t x = index; x < this->inventory.num_items; x++) {
    this->inventory.items[x] = this->inventory.items[x + 1];
  }
  this->inventory.items[this->inventory.num_items] = PlayerInventoryItem();
  return ret;
}

PlayerBankItem PlayerBank::remove_item(uint32_t item_id, uint32_t amount) {
  PlayerBankItem ret;

  if (item_id == 0xFFFFFFFF) {
    if (amount > this->meseta) {
      throw out_of_range("player does not have enough meseta");
    }
    ret.data.data1[0] = 0x04;
    ret.data.data2d = amount;
    this->meseta -= amount;
    return ret;
  }

  size_t index = this->find_item(item_id);
  auto& bank_item = this->items[index];

  if (amount && (bank_item.data.stack_size() > 1) &&
      (amount < bank_item.data.data1[5])) {
    ret = bank_item;
    ret.data.data1[5] = amount;
    ret.amount = amount;
    bank_item.data.data1[5] -= amount;
    bank_item.amount -= amount;
    return ret;
  }

  ret = bank_item;
  this->num_items--;
  for (size_t x = index; x < this->num_items; x++) {
    this->items[x] = this->items[x + 1];
  }
  this->items[this->num_items] = PlayerBankItem();
  return ret;
}

size_t PlayerInventory::find_item(uint32_t item_id) const {
  for (size_t x = 0; x < this->num_items; x++) {
    if (this->items[x].data.id == item_id) {
      return x;
    }
  }
  throw out_of_range("item not present");
}

size_t PlayerInventory::find_equipped_weapon() const {
  ssize_t ret = -1;
  for (size_t y = 0; y < this->num_items; y++) {
    if (!(this->items[y].flags & 0x00000008)) {
      continue;
    }
    if (this->items[y].data.data1[0] != 0) {
      continue;
    }
    if (ret < 0) {
      ret = y;
    } else {
      throw runtime_error("multiple weapons are equipped");
    }
  }
  if (ret < 0) {
    throw out_of_range("no weapon is equipped");
  }
  return ret;
}

size_t PlayerInventory::find_equipped_armor() const {
  ssize_t ret = -1;
  for (size_t y = 0; y < this->num_items; y++) {
    if (!(this->items[y].flags & 0x00000008)) {
      continue;
    }
    if (this->items[y].data.data1[0] != 1 || this->items[y].data.data1[1] != 1) {
      continue;
    }
    if (ret < 0) {
      ret = y;
    } else {
      throw runtime_error("multiple armors are equipped");
    }
  }
  if (ret < 0) {
    throw out_of_range("no armor is equipped");
  }
  return ret;
}

size_t PlayerInventory::find_equipped_mag() const {
  ssize_t ret = -1;
  for (size_t y = 0; y < this->num_items; y++) {
    if (!(this->items[y].flags & 0x00000008)) {
      continue;
    }
    if (this->items[y].data.data1[0] != 2) {
      continue;
    }
    if (ret < 0) {
      ret = y;
    } else {
      throw runtime_error("multiple mags are equipped");
    }
  }
  if (ret < 0) {
    throw out_of_range("no mag is equipped");
  }
  return ret;
}

size_t PlayerBank::find_item(uint32_t item_id) {
  for (size_t x = 0; x < this->num_items; x++) {
    if (this->items[x].data.id == item_id) {
      return x;
    }
  }
  throw out_of_range("item not present");
}

void SavedPlayerDataBB::print_inventory(FILE* stream) const {
  fprintf(stream, "[PlayerInventory] Meseta: %" PRIu32 "\n", this->disp.stats.meseta.load());
  fprintf(stream, "[PlayerInventory] %hhu items\n", this->inventory.num_items);
  for (size_t x = 0; x < this->inventory.num_items; x++) {
    const auto& item = this->inventory.items[x];
    auto name = item.data.name(false);
    auto hex = item.data.hex();
    fprintf(stream, "[PlayerInventory]   %zu: %s (%s)\n", x, hex.c_str(), name.c_str());
  }
}
