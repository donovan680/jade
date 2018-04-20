/*
 * manager_handler.c
 *
 *  Created on: Apr 20, 2018
 *      Author: pchero
 */

#include <evhtp.h>

#include "slog.h"
#include "common.h"
#include "utils.h"

#include "http_handler.h"
#include "user_handler.h"
#include "pjsip_handler.h"

#include "manager_handler.h"

#define DEF_MANAGER_AUTHTOKEN_TYPE       "manager"

static json_t* get_users_all(void);
static json_t* get_user_info(const char* uuid_user);

static bool create_user_info(const json_t* j_data);
static bool create_user_contact(const char* uuid_user, const char* target);
static bool create_user_permission(const char* uuid_user, const json_t* j_data);
static bool create_user_user(const char* uuid_user, const json_t* j_data);
static bool create_user_target(const char* target);



bool manager_init_handler(void)
{
  return true;
}

bool manager_term_handler(void)
{
  return true;
}

bool manager_reload_handler(void)
{
  int ret;

  ret = manager_term_handler();
  if(ret == false) {
    return false;
  }

  ret = manager_init_handler();
  if(ret == false) {
    return false;
  }

  return true;
}

/**
 * POST ^/manager/login request handler.
 * @param req
 * @param data
 */
void manager_htp_post_manager_login(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_tmp;
  char* username;
  char* password;
  char* authtoken;
  int ret;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_post_manager_login.");

  // get username/pass
  ret = http_get_htp_id_pass(req, &username, &password);
  if(ret == false) {
    sfree(username);
    sfree(password);
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // create authtoken
  authtoken = user_create_authtoken(username, password, DEF_MANAGER_AUTHTOKEN_TYPE);
  sfree(username);
  sfree(password);
  if(authtoken == NULL) {
    http_simple_response_error(req, EVHTP_RES_NOTFOUND, 0, NULL);
    return;
  }

  j_tmp = json_pack("{s:s}",
      "authtoken",  authtoken
      );
  sfree(authtoken);

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);
  json_object_set_new(j_res, "result", j_tmp);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * GET ^/manager/users request handler.
 * @param req
 * @param data
 */
void manager_htp_get_manager_users(evhtp_request_t *req, void *data)
{
  json_t* j_res;
  json_t* j_tmp;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_get_manager_users.");

  // get info
  j_tmp = get_users_all();
  if(j_tmp == NULL) {
    slog(LOG_NOTICE, "Could not get users info.");
    http_simple_response_error(req, EVHTP_RES_NOTFOUND, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);
  json_object_set_new(j_res, "result", json_object());
  json_object_set_new(json_object_get(j_res, "result"), "list", j_tmp);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

/**
 * POST ^/manager/users request handler.
 * @param req
 * @param data
 */
void manager_htp_post_manager_users(evhtp_request_t *req, void *data)
{
  int ret;
  json_t* j_res;
  json_t* j_data;

  if(req == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return;
  }
  slog(LOG_DEBUG, "Fired manager_htp_post_manager_users.");

  // get data
  j_data = http_get_json_from_request_data(req);
  if(j_data == NULL) {
    http_simple_response_error(req, EVHTP_RES_BADREQ, 0, NULL);
    return;
  }

  // create info
  ret = create_user_info(j_data);
  json_decref(j_data);
  if(ret == false) {
    http_simple_response_error(req, EVHTP_RES_SERVERR, 0, NULL);
    return;
  }

  // create result
  j_res = http_create_default_result(EVHTP_RES_OK);

  // response
  http_simple_response_normal(req, j_res);
  json_decref(j_res);

  return;
}

static json_t* get_user_info(const char* uuid_user)
{
  json_t* j_res;
  json_t* j_permissions;

  if(uuid_user == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return NULL;
  }

  // get user info
  j_res = user_get_userinfo_info(uuid_user);
  if(j_res == NULL) {
    slog(LOG_NOTICE, "Could not get user info.");
    return NULL;
  }

  // get permissions info
  j_permissions = user_get_permissions_by_useruuid(uuid_user);
  if(j_permissions != NULL) {
    json_object_set_new(j_res, "permissions", j_permissions);
  }

  return j_res;
}

static json_t* get_users_all(void)
{
  json_t* j_res;
  json_t* j_tmp;
  json_t* j_users;
  json_t* j_user;
  int idx;
  const char* uuid;

  j_users = user_get_userinfos_all();
  if(j_users == NULL) {
    return NULL;
  }

  j_res = json_array();
  json_array_foreach(j_users, idx, j_user) {
    uuid = json_string_value(json_object_get(j_user, "uuid"));

    j_tmp = get_user_info(uuid);
    if(j_tmp == NULL) {
      continue;
    }

    json_array_append_new(j_res, j_tmp);
  }
  json_decref(j_users);

  return j_res;
}

static bool create_user_target(const char* target)
{
  int ret;

  if(target == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  ret = pjsip_create_target_with_default_setting(target);
  if(ret == false) {
    slog(LOG_WARNING, "Could not create pjsip target info.");
    return false;
  }

  return true;
}

static bool create_user_user(const char* uuid_user, const json_t* j_data)
{
  int ret;
  json_t* j_tmp;
  const char* username;
  const char* password;
  const char* name;

  if((uuid_user == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // get mandatory items
  username = json_string_value(json_object_get(j_data, "username"));
  password = json_string_value(json_object_get(j_data, "password"));
  name = json_string_value(json_object_get(j_data, "name"));
  if((username == NULL) || (password == NULL) || (name == NULL)) {
    slog(LOG_NOTICE, "Could not get mandatory items.");
    return false;
  }

  j_tmp = json_pack("{s:s, s:s, s:s}",
      "username",   username,
      "password",   password,
      "name",       name
      );
  ret = user_create_userinfo(uuid_user, j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    slog(LOG_WARNING, "Could not create userinfo.");
    return false;
  }

  return true;
}

static bool create_user_permission(const char* uuid_user, const json_t* j_data)
{
  int ret;
  int idx;
  json_t* j_tmp;
  json_t* j_permissions;
  json_t* j_permission;
  const char* permission;

  if((uuid_user == NULL) || (j_data == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  j_permissions = json_object_get(j_data, "permissions");
  if(j_permissions == NULL) {
    slog(LOG_NOTICE, "Could not get permissions info.");
    return false;
  }

  // create each permissions
  json_array_foreach(j_permissions, idx, j_permission) {
    permission = json_string_value(j_permission);
    if(permission == NULL) {
      continue;
    }

    j_tmp = json_pack("{s:s, s:s}",
        "user_uuid",    uuid_user,
        "permission",   permission
        );
    ret = user_create_permission_info(j_tmp);
    json_decref(j_tmp);
    if(ret == false) {
      slog(LOG_WARNING, "Could not create permission info.");
    }
  }

  return true;
}

static bool create_user_contact(const char* uuid_user, const char* target)
{
  int ret;
  json_t* j_tmp;

  if((uuid_user == NULL) || (target == NULL)) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  j_tmp = json_pack("{s:s, s:s}",
      "user_uuid",    uuid_user,
      "target",       target
      );

  ret = user_create_contact_info(j_tmp);
  json_decref(j_tmp);
  if(ret == false) {
    return false;
  }

  return true;
}

static bool create_user_info(const json_t* j_data)
{
  int ret;
  char* target;
  char* uuid_user;

  if(j_data == NULL) {
    slog(LOG_WARNING, "Wrong input parameter.");
    return false;
  }

  // generate info
  target = utils_gen_uuid();
  uuid_user = utils_gen_uuid();

  // create target
  ret = create_user_target(target);
  if(ret == false) {
    slog(LOG_WARNING, "Could not create user target info.");
    pjsip_delete_target(target);
    sfree(target);
    sfree(uuid_user);
    return false;
  }

  // create user
  ret = create_user_user(uuid_user, j_data);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create user user info.");
    pjsip_delete_target(target);
    sfree(uuid_user);
    sfree(target);
    return false;
  }

  // create permission
  ret = create_user_permission(uuid_user, j_data);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create user permission info.");
    pjsip_delete_target(target);
    user_delete_userinfo_info(uuid_user);
    sfree(uuid_user);
    sfree(target);
    return false;
  }

  // create contact
  ret = create_user_contact(uuid_user, target);
  if(ret == false) {
    slog(LOG_NOTICE, "Could not create user contact info.");
    pjsip_delete_target(target);
    user_delete_userinfo_info(uuid_user);
    sfree(uuid_user);
    sfree(target);
    return false;
  }

  sfree(target);
  sfree(uuid_user);

  // reload pjsip config
  ret = pjsip_reload_config();
  if(ret == false) {
    slog(LOG_WARNING, "Could not reload pjsip_handler.");
    return false;
  }

  return true;
}
