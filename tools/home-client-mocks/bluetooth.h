/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HOME_BLUETOOTH_MOCK_H
#define HOME_BLUETOOTH_MOCK_H
/* bt_gattc.h uses a quoted include from its own SDK directory. */
#define _BT_BLUETOOTH_H__
#include <stdbool.h>
#include <stdint.h>
#include <uv.h>
#include <bt_addr.h>
#include <bt_status.h>
#include <bt_uuid.h>
typedef struct bt_instance { int live; } bt_instance_t;
typedef enum { BT_LE_ADDR_TYPE_PUBLIC, BT_LE_ADDR_TYPE_RANDOM } ble_addr_type_t;
typedef int ble_phy_type_t;
bt_instance_t *bluetooth_create_instance(void);
bool bluetooth_set_external_uv(bt_instance_t *,uv_loop_t *);
void bluetooth_delete_instance(bt_instance_t *);
#endif
