/* SPDX-License-Identifier: Apache-2.0 */
#include <bluetooth.h>
typedef enum { BT_ADAPTER_STATE_OFF=0, BT_ADAPTER_STATE_BLE_ON=2,
               BT_ADAPTER_STATE_ON=4 } bt_adapter_state_t;
bt_adapter_state_t bt_adapter_get_state(bt_instance_t *);
