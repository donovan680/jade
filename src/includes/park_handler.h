/*
 * park_handler.h
 *
 *  Created on: Dec 14, 2017
 *      Author: pchero
 */

#ifndef SRC_PARK_HANDLER_H_
#define SRC_PARK_HANDLER_H_

#include <evhtp.h>

bool park_init_handler(void);
bool park_term_handler(void);
bool park_reload_handler(void);

// callback
bool park_register_callback_db_parkedcall(bool (*func)(enum EN_RESOURCE_UPDATE_TYPES, const json_t*));

// parkinglot
json_t* park_get_parkinglots_all(void);
json_t* park_get_parkinglot_info(const char* name);
bool park_create_parkinglot_info(const json_t* j_tmp);

// parkedcall
json_t* park_get_parkedcalls_all();
json_t* park_get_parkedcall_info(const char* key);
bool park_create_parkedcall_info(const json_t* j_data);
bool park_update_parkedcall_info(const json_t* j_data);
bool park_delete_parkedcall_info(const char* key);

// cfg
json_t* park_cfg_get_parkinglots_all(void);
json_t* park_cfg_get_parkinglot_info(const char* name);
bool park_cfg_create_parkinglot_info(const json_t* j_data);
bool park_cfg_update_parkinglot_info(const json_t* j_data);
bool park_cfg_delete_parkinglot_info(const char* parkinglot);


// configuration
json_t* park_get_configurations_all(void);
json_t* park_get_configuration_info(const char* name);
bool park_update_configuration_info(const json_t* j_data);
bool park_delete_configuration_info(const char* name);


#endif /* SRC_PARK_HANDLER_H_ */
