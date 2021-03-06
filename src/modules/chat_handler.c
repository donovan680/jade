/*
 * chat_handler.c
 *
 *  Created on: Mar 26, 2018
 *      Author: pchero
 */

#define _GNU_SOURCE

#include <string.h>

#include "common.h"
#include "slog.h"
#include "utils.h"
#include "resource_handler.h"
#include "user_handler.h"
#include <publication_handler.h>

#include "chat_handler.h"


#define DEF_DB_TABLE_CHAT_ROOM  "chat_room"
#define DEF_DB_TABLE_CHAT_USERROOM  "chat_userroom"

static struct st_callback* g_callback_room;
static struct st_callback* g_callback_userroom;
static struct st_callback* g_callback_message;

static bool init_chat_databases(void);
static bool init_chat_database_room(void);
static bool init_chat_database_userroom(void);

static bool init_callbacks(void);
static bool term_callbacks(void);

static bool db_create_table_chat_message(const char* table_name);
static bool db_delete_table_chat_message(const char* table_name);

static json_t* db_get_chat_rooms_info_by_useruuid(const char* user_uuid);
static json_t* db_get_chat_room_info(const char* uuid);
static json_t* db_get_chat_room_info_by_type_members(const enum EN_CHAT_ROOM_TYPE type, const json_t* j_members);
static bool db_create_chat_room_info(const json_t* j_data);
static bool db_update_chat_room_info(json_t* j_data);
static bool db_delete_chat_room_info(const char* uuid);
static char* db_create_tablename_chat_message(const char* uuid);

static json_t* db_get_chat_userroom_info(const char* uuid);
static json_t* db_get_chat_userroom_info_by_user_room(const char* uuid_user, const char* uuid_room);
static json_t* db_get_chat_userrooms_info_by_useruuid(const char* user_uuid);
static json_t* db_get_chat_userrooms_info_by_roomuuid(const char* uuid_room);
static bool db_create_chat_userroom_info(const json_t* j_data);
static bool db_update_chat_userroom_info(const json_t* j_data);
static bool db_delete_chat_userroom_info(const char* uuid);

static bool db_create_chat_message_info(const char* table_name, const json_t* j_data);
static json_t* db_get_chat_messages_info_newest(const char* table_name, const char* timestamp, const unsigned int count);

static void execute_callbacks_room(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data);
static void execute_callbacks_userroom(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data);
static void execute_callbacks_message(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data);

static bool create_message(const char* uuid_message, const char* uuid_room, const char* uuid_user, const json_t* j_message);

static bool create_userroom(const json_t* j_data);
static bool update_userroom(const json_t* j_data);
static bool delete_userroom(const char* uuid);

static bool create_room(const char* uuid, const char* uuid_user, enum EN_CHAT_ROOM_TYPE type, const json_t* j_members);
static bool create_userroom_foreach_room_members(const char* uuid_room);
static json_t* get_room_by_type_members(enum EN_CHAT_ROOM_TYPE type, json_t* j_members);
static bool delete_room_member(const char* uuid_room, const char* uuid_user);

static char* create_default_chatroom_name(const json_t* j_data);

static char* get_room_uuid_by_type_members(enum EN_CHAT_ROOM_TYPE type, json_t* j_members);
static char* get_message_table_by_userroom(const char* uuid_userroom);

static bool is_room_exist(const char* uuid);
static bool is_userroom_exist(const char* uuid);
static bool is_userroom_exist_by_user_room(const char* uuid_user, const char* uuid_room);
static bool is_room_exist_by_type_members(enum EN_CHAT_ROOM_TYPE type, const json_t* j_members);

static bool is_user_in_room(const json_t* j_room, const char* uuid_user);

bool chat_init_handler(void)
{
  int ret;

  slog(LOG_DEBUG, "Fired init_chat_handler.");

  // init databases
  ret = init_chat_databases();
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate database.");
    return false;
  }

  // init callbacks
  ret = init_callbacks();
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate callback.");
    return false;
  }

  return true;
}

bool chat_term_handler(void)
{
  int ret;

  ret = term_callbacks();
  if(ret == false) {
    slog(LOG_NOTICE, "Could not terminate callbacks.");
    return false;
  }

  return true;
}

static bool init_callbacks(void)
{
  g_callback_room = utils_create_callback();
  if(g_callback_room == NULL) {
    slog(LOG_ERR, "Could not create room callback.");
    return false;
  }

  g_callback_userroom = utils_create_callback();
  if(g_callback_userroom == NULL) {
    slog(LOG_ERR, "Could not create userroom callback.");
    return false;
  }

  g_callback_message = utils_create_callback();
  if(g_callback_message == NULL) {
    slog(LOG_ERR, "Could not create message callback.");
    return false;
  }

  return true;
}

static bool term_callbacks(void)
{
  utils_terminate_callback(g_callback_userroom);
  utils_terminate_callback(g_callback_message);
  utils_terminate_callback(g_callback_room);

  return true;
}

/**
 * Initiate chat databases
 * @return
 */
static bool init_chat_databases(void)
{
  int ret;

  slog(LOG_DEBUG, "Fired init_chat_databases.");

  // init room
  ret = init_chat_database_room();
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate database room.");
    return false;
  }

  // init userroom
  ret = init_chat_database_userroom();
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate database userroom.");
    return false;
  }

  return true;
}

/**
 * Initiate chat database. room.
 * @return
 */
static bool init_chat_database_room(void)
{
  int ret;
  const char* create_table;

  create_table =
    "create table if not exists " DEF_DB_TABLE_CHAT_ROOM " ("

    // basic info
    "   uuid              varchar(255),"    // uuid(chat_room)
    "   message_table     varchar(255),"    // chat message table name
    "   type              int,"             // chat room type. see EN_CHAT_ROOM_TYPE

    // owner, creator
    "   uuid_creator      varchar(255),"    // creator user uuid
    "   uuid_owner        varchar(255),"    // owner user uuid

    "   members text,"    // json array of user_uuid.

    // timestamp. UTC."
    "   tm_create     datetime(6),"  // create time
    "   tm_update     datetime(6),"  // latest updated time

    "   primary key(uuid)"
    ");";

  // execute
  ret = resource_exec_file_sql(create_table);
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate database. database[%s]", DEF_DB_TABLE_CHAT_ROOM);
    return false;
  }

  return true;
}

/**
 * Initiate chat database. userroom.
 * @return
 */
static bool init_chat_database_userroom(void)
{
  int ret;
  const char* create_table;

  create_table =
    "create table if not exists " DEF_DB_TABLE_CHAT_USERROOM " ("

    // basic info
    "   uuid          varchar(255),"

    "   uuid_user     varchar(255),"    // uuid(user)
    "   uuid_room     varchar(255),"    // uuid(chat_room)

    "   name      varchar(255),"    // room name
    "   detail    varchar(1023),"   // room detail

    // timestamp. UTC."
    "   tm_create     datetime(6),"  // create time
    "   tm_update     datetime(6),"  // latest updated time

    "   primary key(uuid_user, uuid_room)"
    ");";

  // execute
  ret = resource_exec_file_sql(create_table);
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate database. database[%s]", DEF_DB_TABLE_CHAT_USERROOM);
    return false;
  }

  return true;
}

static bool db_create_table_chat_message(const char* table_name)
{
  int ret;
  char* create_table;

  if(table_name == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired create_table_chat_message. table_name[%s]", table_name);

  asprintf(&create_table,
      "create table %s ("

      // basic info
      "   uuid              varchar(255),"    // uuid
      "   uuid_room         varchar(255),"    // uuid of room
      "   uuid_owner        varchar(255),"    // message owner's uuid

      "   username          varchar(255),"
      "   name              varchar(255),"

      "   message           text,"            // message

      // timestamp. UTC."
      "   tm_create     datetime(6),"   // create time

      "   primary key(uuid)"
      ");",
      table_name
      );

  // execute
  ret = resource_exec_file_sql(create_table);
  sfree(create_table);
  if(ret == false) {
    slog(LOG_ERR, "Could not initiate database. database[%s]", table_name);
    return false;
  }

  return true;
}

/**
 * Delete chat message table of given table name.
 * @param table_name
 * @return
 */
static bool db_delete_table_chat_message(const char* table_name)
{
  int ret;
  char* drop_table;

  if(table_name == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired delete_table_chat_message. table_name[%s]", table_name);

  // todo: validate table_name

  asprintf(&drop_table, "drop table %s", table_name);

  // execute
  ret = resource_exec_file_sql(drop_table);
  sfree(drop_table);
  if(ret == false) {
    slog(LOG_ERR, "Could not delete chat_message table. table_name[%s]", table_name);
    return false;
  }

  return true;
}

/**
 * Get chat_userroom info of given uuid.
 * @param uuid
 * @return
 */
static json_t* db_get_chat_userroom_info(const char* uuid)
{
  json_t* j_res;

  j_res = resource_get_file_detail_item_key_string(DEF_DB_TABLE_CHAT_USERROOM, "uuid", uuid);
  if(j_res == NULL) {
    slog(LOG_ERR, "Could not get chat_userroom info. uuid[%s]", uuid);
    return NULL;
  }

  return j_res;
}

/**
 * Get chat_userroom info of given uuid.
 * @param uuid
 * @return
 */
static json_t* db_get_chat_userroom_info_by_user_room(const char* uuid_user, const char* uuid_room)
{
  json_t* j_res;
  json_t* j_data;

  if((uuid_user == NULL) || (uuid_room == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  j_data = json_pack("{"
      "s:s, s:s "
      "}",

      "uuid_user",    uuid_user,
      "uuid_room",    uuid_room
      );

  j_res = resource_get_file_detail_item_by_obj(DEF_DB_TABLE_CHAT_USERROOM, j_data);
  json_decref(j_data);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}


/**
 * Get list of chat_userrooms info which related given user_uuid.
 * @param uuid
 * @return
 */
static json_t* db_get_chat_userrooms_info_by_useruuid(const char* user_uuid)
{
  json_t* j_res;

  if(user_uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  j_res = resource_get_file_detail_items_key_string(DEF_DB_TABLE_CHAT_USERROOM, "uuid_user", user_uuid);
  if(j_res == NULL) {
    slog(LOG_ERR, "Could not get list of chat_userrooms info. user_uuid[%s]", user_uuid);
    return NULL;
  }

  return j_res;
}

/**
 * Get list of chat_userrooms info which related given user_uuid.
 * @param uuid
 * @return
 */
static json_t* db_get_chat_userrooms_info_by_roomuuid(const char* uuid_room)
{
  json_t* j_res;

  if(uuid_room == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  j_res = resource_get_file_detail_items_key_string(DEF_DB_TABLE_CHAT_USERROOM, "uuid_room", uuid_room);
  if(j_res == NULL) {
    slog(LOG_ERR, "Could not get list of chat_userrooms info. uuid_room[%s]", uuid_room);
    return NULL;
  }

  return j_res;
}

static bool db_create_chat_userroom_info(const json_t* j_data)
{
  int ret;
  const char* uuid;
  json_t* j_tmp;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // insert info
  ret = resource_insert_file_item(DEF_DB_TABLE_CHAT_USERROOM, j_data);
  if(ret == false) {
    slog(LOG_ERR, "Could not insert chat_userroom.");
    return false;
  }

  // get uuid
  uuid = json_string_value(json_object_get(j_data, "uuid"));
  if(uuid == NULL) {
    slog(LOG_NOTICE, "Could not get userroom uuid info.");
    return false;
  }

  // get created info
  j_tmp = chat_get_userroom(uuid);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get created userroom info. uuid[%s]", uuid);
    return false;
  }

  // execute callbacks
  execute_callbacks_userroom(EN_RESOURCE_CREATE, j_tmp);
  json_decref(j_tmp);

  return true;
}

static bool db_update_chat_userroom_info(const json_t* j_data)
{
  int ret;
  const char* uuid;
  json_t* j_tmp;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // update
  ret = resource_update_file_item(DEF_DB_TABLE_CHAT_USERROOM, "uuid", j_data);
  if(ret == false) {
    slog(LOG_ERR, "Could not update chat_userroom.");
    return false;
  }

  // get uuid
  uuid = json_string_value(json_object_get(j_data, "uuid"));
  if(uuid == NULL) {
    slog(LOG_NOTICE, "Could not get userroom uuid info.");
    return false;
  }

  // get updated info
  j_tmp = chat_get_userroom(uuid);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get updated userroom info. uuid[%s]", uuid);
    return false;
  }

  // execute callbacks
  execute_callbacks_userroom(EN_RESOURCE_UPDATE, j_tmp);
  json_decref(j_tmp);

  return true;
}

static bool db_delete_chat_userroom_info(const char* uuid)
{
  int ret;
  json_t* j_tmp;

  if(uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // get delete info
  j_tmp = chat_get_userroom(uuid);
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get delete userroom info. uuid[%s]", uuid);
    return false;
  }

  // delete
  ret = resource_delete_file_items_string(DEF_DB_TABLE_CHAT_USERROOM, "uuid", uuid);
  if(ret == false) {
    slog(LOG_ERR, "Could not delete chat userroom info. uuid[%s]", uuid);
    json_decref(j_tmp);
    return false;
  }

  // execute callbacks
  execute_callbacks_userroom(EN_RESOURCE_DELETE, j_tmp);
  json_decref(j_tmp);

  return true;
}

static bool db_create_chat_room_info(const json_t* j_data)
{
  int ret;
  const char* uuid;
  json_t* j_tmp;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // insert info
  ret = resource_insert_file_item(DEF_DB_TABLE_CHAT_ROOM, j_data);
  if(ret == false) {
    slog(LOG_ERR, "Could not insert chat_room.");
    return false;
  }

  // get uuid
  uuid = json_string_value(json_object_get(j_data, "uuid"));
  if(uuid == NULL) {
    slog(LOG_NOTICE, "Could not get room uuid info.");
    return false;
  }

  // get created info
  j_tmp = chat_get_room(uuid);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get created room info. uuid[%s]", uuid);
    return false;
  }

  // execute callbacks
  execute_callbacks_room(EN_RESOURCE_CREATE, j_tmp);
  json_decref(j_tmp);

  return true;
}

/**
 * Get chat_room info of given uuid.
 * @param uuid
 * @return
 */
static json_t* db_get_chat_room_info(const char* uuid)
{
  json_t* j_res;

  j_res = resource_get_file_detail_item_key_string(DEF_DB_TABLE_CHAT_ROOM, "uuid", uuid);
  if(j_res == NULL) {
    slog(LOG_ERR, "Could not get chat_room info. uuid[%s]", uuid);
    return NULL;
  }

  return j_res;
}

static json_t* db_get_chat_room_info_by_type_members(const enum EN_CHAT_ROOM_TYPE type, const json_t* j_members)
{
  json_t* j_res;
  json_t* j_data;
  json_t* j_tmp;

  if(j_members == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // sort member
  j_tmp = resource_sort_json_array_string(j_members, EN_SORT_ASC);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not sort members info.");
    return NULL;
  }

  j_data = json_pack("{"
      "s:i, s:o "
      "}",

      "type",     type,
      "members",  j_tmp
      );

  j_res = resource_get_file_detail_item_by_obj(DEF_DB_TABLE_CHAT_ROOM, j_data);
  json_decref(j_data);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}

/**
 * Get all chat_rooms info which related given user_uuid.
 * @param uuid
 * @return
 */
static json_t* db_get_chat_rooms_info_by_useruuid(const char* user_uuid)
{
  char* condition;
  json_t* j_res;

  if(user_uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }
  slog(LOG_DEBUG, "Fired db_get_chat_rooms_by_useruuid. user_uuid[%s]", user_uuid);

  asprintf(&condition, "where members like '%%%s%%'", user_uuid);

  j_res = resource_get_file_detail_items_by_condtion(DEF_DB_TABLE_CHAT_ROOM, condition);
  sfree(condition);
  if(j_res == NULL) {
    slog(LOG_ERR, "Could not get chat rooms info. user_uuid[%s]", user_uuid);
    return NULL;
  }

  return j_res;
}

static bool db_update_chat_room_info(json_t* j_data)
{
  int ret;
  const char* uuid;
  json_t* j_tmp;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  ret = resource_update_file_item(DEF_DB_TABLE_CHAT_ROOM, "uuid", j_data);
  if(ret == false) {
    return false;
  }

  // get uuid
  uuid = json_string_value(json_object_get(j_data, "uuid"));
  if(uuid == NULL) {
    slog(LOG_NOTICE, "Could not get room uuid info.");
    return false;
  }

  // get updated info
  j_tmp = chat_get_room(uuid);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get updated room info. uuid[%s]", uuid);
    return false;
  }

  // execute callbacks
  execute_callbacks_room(EN_RESOURCE_UPDATE, j_tmp);
  json_decref(j_tmp);


  return true;
}

static bool db_delete_chat_room_info(const char* uuid)
{
  int ret;
  json_t* j_tmp;

  if(uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // get delete info
  j_tmp = chat_get_room(uuid);
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get delete room info. uuid[%s]", uuid);
    return false;
  }

  ret = resource_delete_file_items_string(DEF_DB_TABLE_CHAT_ROOM, "uuid", uuid);
  if(ret == false) {
    slog(LOG_ERR, "Could not delete chat room info. uuid[%s]", uuid);
    json_decref(j_tmp);
    return false;
  }

  // execute callbacks
  execute_callbacks_room(EN_RESOURCE_DELETE, j_tmp);
  json_decref(j_tmp);

  return true;
}

/**
 * Create room and create userroom foreach
 * @param uuid
 * @param uuid_user
 * @param j_data
 * @return
 */
bool chat_create_room_with_foreach_userroom(const char* uuid, const char* uuid_user, const json_t* j_data)
{
  int ret;
  int type;
  json_t* j_members;

  if((uuid == NULL) || (uuid_user == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  type = json_integer_value(json_object_get(j_data, "type"));
  j_members = json_object_get(j_data, "members");

  // create room
  ret = create_room(uuid, uuid_user, type, j_members);
  if(ret == false) {
    slog(LOG_ERR, "Could not create room info.");
    return false;
  }

  // create each userroom
  ret = create_userroom_foreach_room_members(uuid);
  if(ret == false) {
    slog(LOG_ERR, "Could not crete userroom for room members.");
    return false;
  }

  return true;
}

/**
 * Create default chatroom name.
 * @param j_data
 * @return
 */
static char* create_default_chatroom_name(const json_t* j_data)
{
  int idx;
  const char* uuid;
  json_t* j_user;
  json_t* j_members;
  json_t* j_member;
  char* res;
  char* tmp;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  asprintf(&res, "Chatting with");

  // get
  j_members = json_deep_copy(j_data);
  json_array_foreach(j_members, idx, j_member) {
    // get user info
    uuid = json_string_value(j_member);
    if(uuid == NULL) {
      continue;
    }

    j_user = user_get_userinfo_info(uuid);
    if(j_user == NULL) {
      continue;
    }

    asprintf(&tmp, "%s %s,",
        res,
        json_string_value(json_object_get(j_user, "name"))? : ""
        );
    json_decref(j_user);
    sfree(res);
    res = tmp;
  }
  json_decref(j_members);

  // replace the last , to .
  res[strlen(res) - 1] = '.';

  return res;
}

static bool create_userroom_foreach_room_members(const char* uuid_room)
{
  int ret;
  int idx;
  json_t* j_room;
  json_t* j_members;
  json_t* j_member;
  json_t* j_tmp;
  char* uuid;
  const char* uuid_user;
  char* room_name;

  if(uuid_room == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // get room info
  j_room = db_get_chat_room_info(uuid_room);
  if(j_room == NULL) {
    slog(LOG_NOTICE, "Could not get chat room info. uuid[%s]", uuid_room);
    return false;
  }

  // get member
  j_members = json_object_get(j_room, "members");
  if(j_members == NULL) {
    slog(LOG_NOTICE, "Could not get members info.");
    return false;
  }

  // create default room name
  room_name = create_default_chatroom_name(j_members);

  // create
  json_array_foreach(j_members, idx, j_member) {
    uuid_user = json_string_value(j_member);

    uuid = utils_gen_uuid();
    if(uuid == NULL) {
      slog(LOG_ERR, "Could not generate uuid.");
      continue;
    }

    j_tmp = json_pack("{"
        "s:s, s:s, s:s,"
        "s:s, s:s"
        "}",

        "uuid",         uuid,
        "uuid_room",    uuid_room,
        "uuid_user",    uuid_user,

        "name",   room_name,
        "detail", ""
        );
    sfree(uuid);

    ret = create_userroom(j_tmp);
    json_decref(j_tmp);
    if(ret == false) {
      slog(LOG_WARNING, "Could not create chat userroom.");
      continue;
    }
  }
  json_decref(j_room);
  sfree(room_name);

  return true;
}

/**
 * Get chat room info.
 * @param uuid
 * @return
 */
json_t* chat_get_room(const char* uuid)
{
  json_t* j_res;

  if(uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }
  slog(LOG_DEBUG, "Fired get_chat_room. uuid[%s]", uuid);

  j_res = db_get_chat_room_info(uuid);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}

/**
 * Get all chat rooms info of given useruuid.
 * @param user_uuid
 * @return
 */
json_t* chat_get_rooms_by_useruuid(const char* user_uuid)
{
  json_t* j_res;

  if(user_uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }
  slog(LOG_DEBUG, "Fired get_chat_rooms_by_useruuid. user_uuid[%s]", user_uuid);

  j_res = db_get_chat_rooms_info_by_useruuid(user_uuid);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}

json_t* chat_get_userrooms_by_useruuid(const char* user_uuid)
{
  json_t* j_res;

  j_res = db_get_chat_userrooms_info_by_useruuid(user_uuid);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}

/**
 * Interface for get userroom info.
 * @param uuid
 * @return
 */
json_t* chat_get_userroom(const char* uuid)
{
  json_t* j_res;

  if(uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  j_res = db_get_chat_userroom_info(uuid);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}

json_t* chat_get_userrooms_by_roomuuid(const char* uuid)
{
  json_t* j_res;

  if(uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  j_res = db_get_chat_userrooms_info_by_roomuuid(uuid);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}

/**
 * Interface for create new chat_userroom
 * j_data : {"name":"<string>", "detail":"<string>", "type": <integer>, "members": ["<string>",]}
 * @param uuid_user
 * @param j_data
 * @return
 */
bool chat_create_userroom(const char* uuid_user, const char* uuid_userroom, const json_t* j_data)
{
  int ret;
  int ret_room;
  int type;
  json_t* j_members;
  json_t* j_tmp;
  char* uuid_room;
  char* tmp;

  if((uuid_user == NULL) || (uuid_userroom == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // get member
  j_members = json_object_get(j_data, "members");
  if(j_members == NULL) {
    slog(LOG_NOTICE, "Could not get member info.");
    return false;
  }

  // get type
  type = json_integer_value(json_object_get(j_data, "type"));
  if(type == 0) {
    slog(LOG_NOTICE, "Could not get type info.");
    return false;
  }

  // check room existence
  ret = is_room_exist_by_type_members(type, j_members);
  if(ret == false) {
    // if not exist, create new
    tmp = utils_gen_uuid();
    ret_room = create_room(tmp, uuid_user, type, j_members);
    sfree(tmp);
    if(ret_room == false) {
      slog(LOG_ERR, "Could not create room.");
      return false;
    }
  }

  // get room uuid.
  uuid_room = get_room_uuid_by_type_members(type, j_members);
  if(uuid_room == NULL) {
    slog(LOG_ERR, "Could not get room uuid.");
    return false;
  }

  // check existence
  ret = is_userroom_exist_by_user_room(uuid_user, uuid_room);
  if(ret == true) {
    slog(LOG_NOTICE, "The same userroom is already exist.");
    sfree(uuid_room);
    return false;
  }

  // create userroom request
  j_tmp = json_pack("{"
      "s:s, s:s, s:s,"
      "s:s, s:s"
      "}",

      "uuid",         uuid_userroom,
      "uuid_room",    uuid_room,
      "uuid_user",    uuid_user,

      "name",   json_string_value(json_object_get(j_data, "name"))? : "",
      "detail", json_string_value(json_object_get(j_data, "detail"))? : ""
      );
  sfree(uuid_room);

  // create userroom
  ret = create_userroom(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not create userroom info.");
    chat_delete_room(uuid_room);
    return false;
  }

  return true;
}

/**
 * Interface for create new chat_userroom
 * @param uuid_user
 * @param j_data
 * @return
 */
bool chat_update_userroom(const char* uuid_userroom, const json_t* j_data)
{
  int ret;
  json_t* j_tmp;

  if((uuid_userroom == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  j_tmp = db_get_chat_userroom_info(uuid_userroom);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get userroom info. uuid[%s]", uuid_userroom);
    return false;
  }

  json_object_set(j_tmp, "name", json_object_get(j_data, "name"));
  json_object_set(j_tmp, "detail", json_object_get(j_data, "detail"));

  ret = update_userroom(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    return false;
  }

  return true;
}

/**
 * Create userroom info.
 * @return
 */
static bool create_userroom(const json_t* j_data)
{
  int ret;
  json_t* j_tmp;
  char* timestamp;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired create_chat_userroom.");

  j_tmp = json_deep_copy(j_data);

  timestamp = utils_get_utc_timestamp();
  json_object_set_new(j_tmp, "tm_create", json_string(timestamp));
  sfree(timestamp);

  ret = db_create_chat_userroom_info(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not create userroom info.");
    return false;
  }

  return true;
}

static bool update_userroom(const json_t* j_data)
{
  int ret;
  json_t* j_tmp;
  char* timestamp;

  if(j_data == NULL) {
    return false;
  }

  j_tmp = json_deep_copy(j_data);

  timestamp = utils_get_utc_timestamp();
  json_object_set_new(j_tmp, "tm_update", json_string(timestamp));
  sfree(timestamp);

  ret = db_update_chat_userroom_info(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    return false;
  }

  return true;
}

static bool delete_userroom(const char* uuid)
{
  int ret;

  if(uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  ret = db_delete_chat_userroom_info(uuid);
  if(ret == false) {
    return false;
  }

  return true;
}

/**
 * Delete chat userroom.
 * Delete also related info.
 * @param uuid_user
 * @param uuid_userroom
 * @return
 */
bool chat_delete_userroom(const char* uuid_userroom)
{
  int ret;
  const char* uuid_room;
  const char* uuid_user;
  json_t* j_userroom;

  if(uuid_userroom == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired delete_chat_userroom. uuid[%s]", uuid_userroom);

  // get userroom
  j_userroom = db_get_chat_userroom_info(uuid_userroom);
  if(j_userroom == NULL) {
    slog(LOG_ERR, "Could not get userroom info. uuid[%s]", uuid_userroom);
    return false;
  }

  // get uuid_user
  uuid_user = json_string_value(json_object_get(j_userroom, "uuid_user"));
  if(uuid_user == NULL) {
    slog(LOG_ERR, "Could not get user uuid info.");
    json_decref(j_userroom);
    return false;
  }

  // get uuid_room
  uuid_room = json_string_value(json_object_get(j_userroom, "uuid_room"));
  if(uuid_room == NULL) {
    slog(LOG_ERR, "Could not get room uuid info.");
    json_decref(j_userroom);
    return false;
  }

  // delete member from room
  ret = delete_room_member(uuid_room, uuid_user);
  if(ret == false) {
    slog(LOG_ERR, "Could not dlete room member info.");
    json_decref(j_userroom);
    return false;
  }

  // delete userroom
  ret = delete_userroom(uuid_userroom);
  json_decref(j_userroom);
  if(ret == false) {
    slog(LOG_ERR, "Could not delete userroom info.");
    return false;
  }

  return true;
}

/**
 * Delete all chat module info related with given user uuid.
 * @param uuid_user
 * @return
 */
bool chat_delete_info_by_useruuid(const char* uuid_user)
{
  int idx;
  json_t* j_tmps;
  json_t* j_tmp;
  const char* uuid_userroom;

  if(uuid_user == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // get all userroom by given user uuid
  j_tmps = chat_get_userrooms_by_useruuid(uuid_user);

  // delete all given userroom info
  json_array_foreach(j_tmps, idx, j_tmp) {
    uuid_userroom = json_string_value(json_object_get(j_tmp, "uuid"));
    chat_delete_userroom(uuid_userroom);
  }
  json_decref(j_tmps);

  return true;
}


/**
 * Create chat room
 * @param uuid
 * @return
 */
static bool create_room(
    const char* uuid,
    const char* uuid_user,
    enum EN_CHAT_ROOM_TYPE type,
    const json_t* j_members
    )
{
  char* table_name;
  int ret;
  json_t* j_tmp;
  json_t* j_tmp_members;
  char* timestamp;

  if((uuid == NULL) || (uuid_user == NULL) || (j_members == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // check room existence
  ret = is_room_exist(uuid);
  if(ret == true) {
    slog(LOG_NOTICE, "The given chat room is already exsit.");
    return false;
  }

  // sort members
  j_tmp_members = resource_sort_json_array_string(j_members, EN_SORT_ASC);
  if(j_tmp_members == NULL) {
    slog(LOG_ERR, "Could not get sort members info.");
    return false;
  }

  // check room member existenece
  ret = is_room_exist_by_type_members(type, j_tmp_members);
  if(ret == true) {
    // already exist.
    slog(LOG_NOTICE, "The room member is already exist.");
    json_decref(j_tmp_members);
    return false;
  }

  // create table name for chat_message
  table_name = db_create_tablename_chat_message(uuid);
  if(table_name == NULL) {
    slog(LOG_ERR, "Could not create table name.");
    json_decref(j_tmp_members);
    return false;
  }

  // create table for chat_message
  ret = db_create_table_chat_message(table_name);
  if(ret == false) {
    slog(LOG_ERR, "Could not create table for chat message. table_name[%s]", table_name);
    json_decref(j_tmp_members);
    sfree(table_name);
    return false;
  }

  // create request data
  timestamp = utils_get_utc_timestamp();
  j_tmp = json_pack("{"
      "s:s, s:s, s:s, "
      "s:s, s:i, s:o, "

      "s:s "
      "}",

      "uuid",           uuid,
      "uuid_creator",   uuid_user,
      "uuid_owner",     uuid_user,

      "message_table",  table_name,
      "type",           type,
      "members",        j_tmp_members,

      "tm_create",  timestamp
      );
  sfree(timestamp);

  // create chat_room info
  ret = db_create_chat_room_info(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_ERR, "Could not create chat_room.");
    db_delete_table_chat_message(table_name);
    sfree(table_name);
    return false;
  }
  sfree(table_name);

  return true;
}

static json_t* get_room_by_type_members(enum EN_CHAT_ROOM_TYPE type, json_t* j_members)
{
  json_t* j_res;

  if(j_members == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  j_res = db_get_chat_room_info_by_type_members(type, j_members);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}

/**
 * Delete chat room of given uuid.
 * Delete also related chat info.
 * @param uuid
 * @return
 */
bool chat_delete_room(const char* uuid)
{
  int ret;
  json_t* j_tmp;
  const char* message_table;

  if(uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired delete_chat_room. uuid[%s]", uuid);

  j_tmp = db_get_chat_room_info(uuid);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get chat_room info. uuid[%s]", uuid);
    return false;
  }

  // get message table name
  message_table = json_string_value(json_object_get(j_tmp, "message_table"));
  if(message_table != NULL) {
    // delete
    db_delete_table_chat_message(message_table);
  }
  json_decref(j_tmp);

  // delete chat room
  ret = db_delete_chat_room_info(uuid);
  if(ret == false) {
    slog(LOG_ERR, "Could not delete chat room. uuid[%s]", uuid);
    return false;
  }

  return true;
}

/**
 * Create chat message(Add the given message to chat room).
 * @param uuid
 * @param j_data
 * @return
 */
static bool create_message(const char* uuid_message, const char* uuid_room, const char* uuid_user, const json_t* j_message)
{
  int ret;
  char* timestamp;
  const char* username;
  const char* name;
  json_t* j_user;
  json_t* j_room;
  json_t* j_data;
  const char* message_table;

  if((uuid_message == NULL) || (uuid_room == NULL) || (uuid_user == NULL) || (j_message == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired create_message. uuid_message[%s], uuid_room[%s], uuid_user[%s]",
      uuid_message,
      uuid_room,
      uuid_user
      );

  // check chat room existence
  ret = is_room_exist(uuid_room);
  if(ret == false) {
    slog(LOG_ERR, "The given chat room is not exist. uuid_room[%s]", uuid_room);
    return false;
  }

  // check user existence
  ret = user_is_user_exist(uuid_user);
  if(ret == false) {
    slog(LOG_ERR, "The given user user is not exist. user_uuid[%s]", uuid_user);
    return false;
  }

  // get chat room info
  j_room = db_get_chat_room_info(uuid_room);
  if(j_room == NULL) {
    slog(LOG_ERR, "Could not get chat room info. uuid[%s]", uuid_room);
    return false;
  }

  // check user in the room
  ret = is_user_in_room(j_room, uuid_user);
  if(ret == false) {
    json_decref(j_room);
    slog(LOG_WARNING, "The user is not in the room.");
    return false;
  }

  // get message table
  message_table = json_string_value(json_object_get(j_room, "message_table"));
  if(message_table == NULL) {
    slog(LOG_ERR, "Could not get message_table info.");
    json_decref(j_room);
    return false;
  }

  // get user info
  j_user = user_get_userinfo_info(uuid_user);
  if(j_user == NULL) {
    slog(LOG_NOTICE, "Could not get user info.");
    json_decref(j_room);
    return false;
  }
  username = json_string_value(json_object_get(j_user, "username"));
  name = json_string_value(json_object_get(j_user, "name"));

  // create data info
  timestamp = utils_get_utc_timestamp();
  j_data = json_pack("{"
      "s:s, s:s, s:s, s:s, s:s, "
      "s:O, "
      "s:s "
      "}",

      "uuid",         uuid_message,
      "uuid_owner",   uuid_user,
      "uuid_room",    uuid_room,
      "username",     username? : "",
      "name",         name? : "",

      "message",      j_message,

      "tm_create",    timestamp
      );
  sfree(timestamp);

  // create message
  ret = db_create_chat_message_info(message_table, j_data);
  json_decref(j_room);
  json_decref(j_user);
  json_decref(j_data);
  if(ret == false) {
    slog(LOG_ERR, "Could not create message info.");
    return false;
  }

  return true;
}

/**
 * Create(Add) the message to the given userroom.
 * @param uuid_userroom
 * @param uuid_user
 * @param message
 * @return
 */
bool chat_create_message_to_userroom(const char* uuid_message, const char* uuid_userroom, const char* uuid_user, const json_t* j_message)
{
  int ret;
  const char* uuid_room;
  json_t* j_userroom;

  if((uuid_message == NULL) || (uuid_userroom == NULL) || (uuid_user == NULL) || (j_message == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  ret = is_userroom_exist(uuid_userroom);
  if(ret == false) {
    slog(LOG_ERR, "The given userroom info is not exist. uuid[%s]", uuid_userroom);
    return false;
  }

  // get userroom info
  j_userroom = db_get_chat_userroom_info(uuid_userroom);
  if(j_userroom == NULL) {
    slog(LOG_ERR, "Could not get userroom info. uuid[%s]", uuid_userroom);
    return false;
  }

  // get room uuid
  uuid_room = json_string_value(json_object_get(j_userroom, "uuid_room"));
  if(uuid_room == NULL) {
    slog(LOG_ERR, "Could not get chat room uuid.");
    json_decref(j_userroom);
    return false;
  }

  // create message
  ret = create_message(uuid_message, uuid_room, uuid_user, j_message);
  json_decref(j_userroom);
  if(ret == false) {
    slog(LOG_ERR, "Could not create chat message. uuid_room[%s], uuid_user[%s]", uuid_room, uuid_user);
    return false;
  }

  return true;
}

static json_t* get_room_info_by_userroom(const char* uuid_userroom)
{
  json_t* j_res;
  json_t* j_tmp;
  const char* uuid_room;

  if(uuid_userroom == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // get userroom
  j_tmp = chat_get_userroom(uuid_userroom);
  if(j_tmp == NULL) {
    return NULL;
  }

  uuid_room = json_string_value(json_object_get(j_tmp, "uuid_room"));
  if(uuid_room == NULL) {
    json_decref(j_tmp);
    return NULL;
  }

  j_res = chat_get_room(uuid_room);
  json_decref(j_tmp);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}

/**
 * Return array of members of the given userroom.
 * @param uuid_userroom
 * @return
 */
json_t* chat_get_members_by_userroom(const char* uuid_userroom)
{
  json_t* j_members;
  json_t* j_room;
  
  if(uuid_userroom == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // get room info
  j_room = get_room_info_by_userroom(uuid_userroom);
  if(j_room == NULL) {
    slog(LOG_WARNING, "Could not get room info.");
    return NULL;
  }
  
  // get member
  j_members = json_object_get(j_room, "members");
  if(j_members == NULL) {
    slog(LOG_ERR, "Could not get members info.");
    json_decref(j_members);
    return NULL;
  }
  
  // inc/dec reference
  json_incref(j_members);
  json_decref(j_room);
  
  return j_members;
}

/**
 * Return room uuid string of the given userroom
 * @param uuid_userroom
 * @return
 */
char* chat_get_uuidroom_by_uuiduserroom(const char* uuid_userroom)
{
  char* res;
  const char* tmp_const;
  json_t* j_tmp;

  if(uuid_userroom == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  j_tmp = chat_get_userroom(uuid_userroom);
  if(j_tmp == NULL) {
    return NULL;
  }

  tmp_const = json_string_value(json_object_get(j_tmp, "uuid_room"));
  if(tmp_const == NULL) {
    slog(LOG_ERR, "Could not get room uuid info.");
    json_decref(j_tmp);
    return NULL;
  }

  res = strdup(tmp_const);
  json_decref(j_tmp);

  return res;
}

json_t* chat_get_message_info_by_userroom(const char* uuid_message, const char* uuid_userroom)
{
  json_t* j_res;
  char* table_name;

  if((uuid_message == NULL) || (uuid_userroom == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // get table_name
  table_name = get_message_table_by_userroom(uuid_userroom);
  if(table_name == NULL) {
    slog(LOG_WARNING, "Could not get table name.");
    return NULL;
  }

  // get message
  j_res = chat_get_message_info(table_name, uuid_message);
  sfree(table_name);
  if(j_res == NULL) {
    return NULL;
  }

  return j_res;
}

/**
 * Get chat messages of given chat room uuid.
 * Order by newest first of given timestamp.
 * @param uuid
 * @param count
 * @return
 */
json_t* chat_get_messages_newest_of_room(const char* uuid_room, const char* timestamp, const unsigned int count)
{
  int ret;
  json_t* j_room;
  json_t* j_res;
  const char* table_name;

  if((uuid_room == NULL) || (timestamp == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // check existence
  ret = is_room_exist(uuid_room);
  if(ret == false) {
    slog(LOG_ERR, "The given chat room is not exist. uuid[%s]", uuid_room);
    return NULL;
  }

  // get chat_room
  j_room = db_get_chat_room_info(uuid_room);
  if(j_room == NULL) {
    slog(LOG_ERR, "Could not get chat room info.");
    return NULL;
  }

  table_name = json_string_value(json_object_get(j_room, "message_table"));

  // get messages
  j_res = db_get_chat_messages_info_newest(table_name, timestamp, count);
  json_decref(j_room);
  if(j_res == NULL) {
    slog(LOG_ERR, "Could not get chat messages. uuid[%s]", uuid_room);
    return NULL;
  }

  return j_res;
}

/**
 * Get chat messages.
 * Order by newest first of given timestamp.
 * @param uuid
 * @param count
 * @return
 */
json_t* chat_get_message_info(const char* table_name, const char* uuid)
{
  json_t* j_res;

  if((table_name == NULL) || (uuid == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // get info
  j_res = resource_get_file_detail_item_key_string(table_name, "uuid", uuid);
  if(j_res == NULL) {
    slog(LOG_NOTICE, "Could not get message info.");
    return NULL;
  }

  return j_res;
}

/**
 * Get chat messages of given chat userroom uuid.
 * Order by newest first of given timestamp.
 * @param uuid
 * @param count
 * @return
 */
json_t* chat_get_userroom_messages_newest(const char* uuid_userroom, const char* timestamp, const unsigned int count)
{
  json_t* j_res;
  json_t* j_userroom;
  const char* uuid_room;

  if((uuid_userroom == NULL) || (timestamp == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
  }

  // get userroom info
  j_userroom = chat_get_userroom(uuid_userroom);
  if(j_userroom == NULL) {
    slog(LOG_WARNING, "Could not get userroom info. uuid[%s]", uuid_userroom);
    return NULL;
  }

  // get uuid_room
  uuid_room = json_string_value(json_object_get(j_userroom, "uuid_room"));
  if(uuid_room == NULL) {
    slog(LOG_WARNING, "Could not get room uuid info. uuid_userroom");
    json_decref(j_userroom);
    return NULL;
  }

  // get messages from chat_room
  j_res = chat_get_messages_newest_of_room(uuid_room, timestamp, count);
  json_decref(j_userroom);
  if(j_res == NULL) {
    slog(LOG_WARNING, "Could not get chat room messages info.");
    return NULL;
  }

  return j_res;
}

static bool db_create_chat_message_info(const char* table_name, const json_t* j_data)
{
  int ret;
  const char* uuid;
  json_t* j_tmp;

  if((table_name == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // todo: need to validate table_name

  // insert info
  ret = resource_insert_file_item(table_name, j_data);
  if(ret == false) {
    slog(LOG_ERR, "Could not insert chat_room.");
    return false;
  }

  // get uuid
  uuid = json_string_value(json_object_get(j_data, "uuid"));
  if(uuid == NULL) {
    slog(LOG_NOTICE, "Could not get message uuid info.");
    return false;
  }

  // get created info
  j_tmp = chat_get_message_info(table_name, uuid);
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get created message info.");
    return false;
  }

  // execute callbacks
  execute_callbacks_message(EN_RESOURCE_CREATE, j_tmp);
  json_decref(j_tmp);

  return true;
}

/**
 * Return the array of chat messages of given info.
 * @param table_name
 * @param timestamp
 * @param count
 * @return
 */
static json_t* db_get_chat_messages_info_newest(const char* table_name, const char* timestamp, const unsigned int count)
{
  json_t* j_res;
  char* condition;

  if((table_name == NULL) || (timestamp == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // create condition
  asprintf(&condition, "where tm_create < '%s' order by tm_create desc limit %u", timestamp, count);

  j_res = resource_get_file_detail_items_by_condtion(table_name, condition);
  sfree(condition);

  return j_res;
}

/**
 * Create string for chat_message table name.
 * @param uuid
 * @return
 */
static char* db_create_tablename_chat_message(const char* uuid)
{
  char* res;
  char* tmp;

  if(uuid == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  tmp = utils_string_replace_char(uuid, '-', '_');
  if(tmp == NULL) {
    slog(LOG_ERR, "Could not create table name.");
    return NULL;
  }

  asprintf(&res, "chat_%s_message", tmp);
  sfree(tmp);

  return res;
}

static bool is_room_exist(const char* uuid)
{
  json_t* j_tmp;

  j_tmp = db_get_chat_room_info(uuid);
  if(j_tmp == NULL) {
    return false;
  }
  json_decref(j_tmp);

  return true;
}

static bool is_userroom_exist(const char* uuid)
{
  json_t* j_tmp;

  j_tmp = db_get_chat_userroom_info(uuid);
  if(j_tmp == NULL) {
    return false;
  }
  json_decref(j_tmp);

  return true;
}

/**
 * Return true if the userroom info is exist of given user's uuid and room's uuid.
 * @param uuid_user
 * @param uuid_room
 * @return
 */
static bool is_userroom_exist_by_user_room(const char* uuid_user, const char* uuid_room)
{
  json_t* j_tmp;

  if((uuid_user == NULL) || (uuid_room == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  j_tmp = db_get_chat_userroom_info_by_user_room(uuid_user, uuid_room);
  if(j_tmp == NULL) {
    return false;
  }
  json_decref(j_tmp);

  return true;
}

static bool is_user_in_room(const json_t* j_room, const char* uuid_user)
{
  bool exist;
  int idx;
  int ret;
  json_t* j_member;
  json_t* j_members;
  const char* tmp_const;

  if((j_room == NULL) || (uuid_user == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  j_members = json_object_get(j_room, "members");
  if(j_members == NULL) {
    return NULL;
  }

  exist = false;
  json_array_foreach(j_members, idx, j_member) {
    tmp_const = json_string_value(j_member);
    if(tmp_const == NULL) {
      continue;
    }

    ret = strcmp(tmp_const, uuid_user);
    if(ret == 0) {
      exist = true;
      break;
    }
  }

  if(exist == false) {
    return false;
  }

  return true;
}

/**
 * Return true if there is same members of chat room
 * @param uuid_user
 * @param j_member
 * @return
 */
static bool is_room_exist_by_type_members(enum EN_CHAT_ROOM_TYPE type, const json_t* j_members)
{
  json_t* j_tmp;

  if(j_members == NULL) {
    return false;
  }

  j_tmp = db_get_chat_room_info_by_type_members(type, j_members);
  if(j_tmp == NULL) {
    return false;
  }
  json_decref(j_tmp);

  return true;
}

/**
 * Remove member info of given uuid_user from given uuid_room.
 * @param uuid_room
 * @param uuid_user
 * @return
 */
static bool delete_room_member(const char* uuid_room, const char* uuid_user)
{
  int ret;
  json_t* j_room;
  json_t* j_members;
  json_t* j_member;
  const char* tmp_const;
  int idx;

  if((uuid_room == NULL) || (uuid_user == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // get room info
  j_room = chat_get_room(uuid_room);
  if(j_room == NULL) {
    slog(LOG_ERR, "Could not get room info.");
    return false;
  }

  // get members info
  j_members = json_object_get(j_room, "members");
  if(j_members == NULL) {
    slog(LOG_WARNING, "Could not get members info.");
    json_decref(j_room);
    return false;
  }

  // delete member
  json_array_foreach(j_members, idx, j_member) {
    tmp_const = json_string_value(j_member);
    if(tmp_const == NULL) {
      continue;
    }

    ret = strcmp(tmp_const, uuid_user);
    if(ret != 0) {
      continue;
    }

    // delete
    json_array_remove(j_members, idx);
    break;
  }

  // update
  ret = db_update_chat_room_info(j_room);
  json_decref(j_room);
  if(ret == false) {
    return false;
  }

  return true;
}

/**
 * Return the string of room_uuid of gvien type and members
 * @param type
 * @param j_members
 * @return
 */
static char* get_room_uuid_by_type_members(enum EN_CHAT_ROOM_TYPE type, json_t* j_members)
{
  char* res;
  const char* tmp_const;
  json_t* j_tmp;

  if(j_members == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // get room info
  j_tmp = get_room_by_type_members(type, j_members);
  if(j_tmp == NULL) {
    slog(LOG_ERR, "Could not get room info.");
    return NULL;
  }

  // get room_uuid
  tmp_const = json_string_value(json_object_get(j_tmp, "uuid"));
  if(tmp_const == NULL) {
    json_decref(j_tmp);
    slog(LOG_ERR, "Could not get room uuid.");
    return NULL;
  }

  res = strdup(tmp_const);
  json_decref(j_tmp);

  return res;
}

/**
 * Return true if the given user_uuid owned given uuid_userroom.
 * @param uuid_user
 * @param uuid_userroom
 * @return
 */
bool chat_is_user_userroom_owned(const char* uuid_user, const char* uuid_userroom)
{
  int ret;
  json_t* j_tmp;
  const char* tmp_const;

  if((uuid_user == NULL) || (uuid_userroom == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // get userroom
  j_tmp = chat_get_userroom(uuid_userroom);
  if(j_tmp == NULL) {
    slog(LOG_WARNING, "Could not get userroom info. uuid[%s]", uuid_userroom);
    return false;
  }

  tmp_const = json_string_value(json_object_get(j_tmp, "uuid_user"));
  if(tmp_const == NULL) {
    slog(LOG_ERR, "Could not get uuid_user from chat_userroom. uuid[%s]", uuid_userroom);
    json_decref(j_tmp);
    return false;
  }

  ret = strcmp(tmp_const, uuid_user);
  json_decref(j_tmp);
  if(ret != 0) {
    return false;
  }

  return true;
}

/**
 * Return the table name string of given userroom
 * @param uuid_userroom
 * @return
 */
static char* get_message_table_by_userroom(const char* uuid_userroom)
{
  char* res;
  json_t* j_userroom;
  json_t* j_room;
  const char* uuid_room;
  const char* table_name;

  if(uuid_userroom == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // get userroom
  j_userroom = chat_get_userroom(uuid_userroom);
  if(j_userroom == NULL) {
    slog(LOG_NOTICE, "Could not get userroom info.");
    return NULL;
  }

  // get uuid_room
  uuid_room = json_string_value(json_object_get(j_userroom, "uuid_room"));
  if(uuid_room == NULL) {
    slog(LOG_ERR, "Could not get room uuid info.");
    json_decref(j_userroom);
    return NULL;
  }

  // get room
  j_room = chat_get_room(uuid_room);
  json_decref(j_userroom);
  if(j_room == NULL) {
    slog(LOG_ERR, "Could not get room info.");
    return NULL;
  }

  // get table_name
  table_name = json_string_value(json_object_get(j_room, "message_table"));
  if(table_name == NULL) {
    slog(LOG_ERR, "Could not get table name.");
    json_decref(j_room);
    return NULL;
  }

  res = strdup(table_name);
  json_decref(j_room);

  return res;
}

/**
 * Execute the registered callbacks for room
 * @param j_data
 */
static void execute_callbacks_room(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data)
{
  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired execute_callbacks_room.");

  utils_execute_callbacks(g_callback_room, type, j_data);

  return;
}

/**
 * Register the callback for room
 */
bool chat_register_callback_room(bool (*func)(enum EN_RESOURCE_UPDATE_TYPES, const json_t*))
{
  int ret;

  if(func == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired chat_reigster_callback_room.");

  ret = utils_register_callback(g_callback_room, func);
  if(ret == false) {
    slog(LOG_ERR, "Could not register callback for room.");
    return false;
  }

  return true;
}

/**
 * Register the callback for userroom
 */
bool chat_register_callback_userroom(bool (*func)(enum EN_RESOURCE_UPDATE_TYPES, const json_t*))
{
  int ret;

  if(func == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired chat_reigster_callback_userroom.");

  ret = utils_register_callback(g_callback_userroom, func);
  if(ret == false) {
    slog(LOG_ERR, "Could not register callback for userroom.");
    return false;
  }

  return true;
}

/**
 * Register the callback for message
 */
bool chat_register_callback_message(bool (*func)(enum EN_RESOURCE_UPDATE_TYPES, const json_t*))
{
  int ret;

  if(func == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }
  slog(LOG_DEBUG, "Fired chat_reigster_callback_message.");

  ret = utils_register_callback(g_callback_message, func);
  if(ret == false) {
    slog(LOG_ERR, "Could not register callback for message.");
    return false;
  }

  return true;
}

/**
 * Execute the registered callbacks for userroom
 * @param j_data
 */
static void execute_callbacks_userroom(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data)
{
  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired execute_callbacks_userroom.");

  utils_execute_callbacks(g_callback_userroom, type, j_data);

  return;
}

/**
 * Execute the registered callbacks for message
 * @param j_data
 */
static void execute_callbacks_message(enum EN_RESOURCE_UPDATE_TYPES type, const json_t* j_data)
{
  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired execute_callbacks_message.");

  utils_execute_callbacks(g_callback_message, type, j_data);

  return;
}

