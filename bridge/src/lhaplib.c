/**
 * Copyright (c) 2021 KNpTrue
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

/**
 * accessory: {
 *     aid: number, // Accessory instance ID.
 *     category: string,  // Category information for the accessory.
 *     name: string,  // The display name of the accessory.
 *     manufacturer: string,  // The manufacturer of the accessory.
 *     model: string, // The model name of the accessory.
 *     serialNumber: string, // The serial number of the accessory.
 *     firmwareVersion: string, // The firmware version of the accessory.
 *     hardwareVersion: string, // The hardware version of the accessory.
 *     services: table // Array of provided services.
 *     callbacks: {
 *         // The callback used to invoke the identify routine.
 *         identify: function() -> int,
 *     } // Callbacks.
 * } // HomeKit accessory.
 *
 * service: {
 *     iid: number, // Instance ID.
 *     type: string, // The type of the service.
 *     name: string, // The name of the service.
 *     properties: {
 *         // The service is the primary service on the accessory.
 *         primaryService: boolean,
 *         hidden: boolean, // The service should be hidden from the user.
 *         ble: {
 *             // The service supports configuration.
 *             supportsConfiguration: boolean,
 *         } These properties only affect connections over Bluetooth LE.
 *     } // HAP Service properties.
 *     characteristics: table, // Array of contained characteristics.
 * } // HomeKit service.
 *
 * characteristic: {
 *     format: string, // Format.
 *     iid: number, // Instance ID.
 *     type: string, // The type of the characteristic.
 *     // Description of the characteristic provided
 *     // by the manufacturer of the accessory.
 *     manufacturerDescription: string,
 *     properties: { // Properties that HomeKit characteristics can have.
 *         readable: boolean, // The characteristic is readable.
 *         writable: boolean, // The characteristic is writable.
 *         // The characteristic supports notifications using 
 *         // the event connection established by the controller.
 *         supportsEventNotification: boolean,
 *         hidden: boolean, // The characteristic should be hidden from the user.
 *         // The characteristic will only be accessible for read operations by admin controllers.
 *         readRequiresAdminPermissions: boolean,
 *         // The characteristic will only be accessible for write operations by admin controllers.
 *         writeRequiresAdminPermissions: boolean,
 *         requiresTimedWrite: boolean, // The characteristic requires time sensitive actions.
 *         // The characteristic requires additional authorization data.
 *         supportsAuthorizationData: boolean,
 *         ip: { // These properties only affect connections over IP (Ethernet / Wi-Fi).
 *             // This flag prevents the characteristic from being read during discovery.
 *             controlPoint: boolean,
 *             // Write operations on the characteristic require a read response value.
 *             supportsWriteResponse: boolean,
 *         },
 *         ble: { // These properties only affect connections over Bluetooth LE.
 *             // The characteristic supports broadcast notifications.
 *             supportsBroadcastNotification: boolean,
 *             // The characteristic supports disconnected notifications.
 *             supportsDisconnectedNotification: boolean,
 *             // The characteristic is always readable, even before a secured session is established.
 *             readableWithoutSecurity: boolean,
 *             // The characteristic is always writable, even before a secured session is established.
 *             writableWithoutSecurity: boolean,
 *         }
 *     }
 * }
 */

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include <lualib.h>
#include <lauxlib.h>
#include <HAPCharacteristic.h>
#include "AppInt.h"
#include "lapi.h"
#include "lhaplib.h"
#include "DB.h"

#define LHAP_MALLOC(size)        malloc(size)
#define LHAP_FREE(p)             do { if (p) { free((void *)p); (p) = NULL; } } while (0)

static const HAPLogObject lhap_log = { .subsystem = kHAPApplication_LogSubsystem, .category = "lhap" };

static const char *lhap_lhap_accessory_category_strs[] = {
    "BridgedAccessory",
    "Other",
    "Bridges",
    "Fans",
    "GarageDoorOpeners",
    "Lighting",
    "Locks",
    "Outlets",
    "Switches",
    "Thermostats",
    "Sensors",
    "SecuritySystems",
    "Doors",
    "Windows",
    "WindowCoverings",
    "ProgrammableSwitches",
    "RangeExtenders",
    "IPCameras",
    NULL,
    "AirPurifiers",
    "Heaters",
    "AirConditioners",
    "Humidifiers",
    "Dehumidifiers",
    NULL,
    NULL,
    NULL,
    NULL,
    "Sprinklers",
    "Faucets",
    "ShowerSystems"
};

static const char *lhap_error_strs[] = {
    "None",
    "Unknown",
    "InvalidState",
    "InvalidData",
    "OutOfResources",
    "NotAuthorized",
    "Busy",
};

static const char *lhap_characteristics_format_strs[] = {
    "Data",
    "Bool",
    "UInt8",
    "UInt16",
    "UInt32",
    "UInt64",
    "Int",
    "Float",
    "String",
    "TLV8",
};

static const size_t lhap_characteristic_struct_size[] = {
    sizeof(HAPDataCharacteristic),
    sizeof(HAPBoolCharacteristic),
    sizeof(HAPUInt8Characteristic),
    sizeof(HAPUInt16Characteristic),
    sizeof(HAPUInt32Characteristic),
    sizeof(HAPUInt64Characteristic),
    sizeof(HAPIntCharacteristic),
    sizeof(HAPFloatCharacteristic),
    sizeof(HAPStringCharacteristic),
    sizeof(HAPTLV8Characteristic),
};

// Lua light userdata.
typedef struct {
    const char *name;
    void *ptr;
} lhap_lightuserdata;

static const lhap_lightuserdata lhap_accessory_services_userdatas[] = {
    {"AccessoryInformationService", (void *)&accessoryInformationService},
    {"HapProtocolInformationService", (void *)&hapProtocolInformationService},
    {"PairingService", (void *)&pairingService},
    {NULL, NULL},
};

typedef struct {
    const char *name;
    const HAPUUID *type;
    const char *debugDescription;
} lhap_service_type;

static const lhap_service_type lhap_service_type_tab[] = {
    {
        "AccessoryInformation",
        &kHAPServiceType_AccessoryInformation,
        kHAPServiceDebugDescription_AccessoryInformation
    },
    {
        "GarageDoorOpener",
        &kHAPServiceType_GarageDoorOpener,
        kHAPServiceDebugDescription_GarageDoorOpener
    },
    {
        "LightBulb",
        &kHAPServiceType_LightBulb,
        kHAPServiceDebugDescription_LightBulb
    },
    {
        "LockManagement",
        &kHAPServiceType_LockManagement,
        kHAPServiceDebugDescription_LockManagement
    },
    {
        "Outlet",
        &kHAPServiceType_Outlet,
        kHAPServiceDebugDescription_Outlet
    },
    {
        "Switch",
        &kHAPServiceType_Switch,
        kHAPServiceDebugDescription_Switch
    },
    {
        "Thermostat",
        &kHAPServiceType_Thermostat,
        kHAPServiceDebugDescription_Thermostat
    },
    {
        "Pairing",
        &kHAPServiceType_Pairing,
        kHAPServiceDebugDescription_Pairing
    },
    {
        "SecuritySystem",
        &kHAPServiceType_SecuritySystem,
        kHAPServiceDebugDescription_SecuritySystem
    },
    {
        "CarbonMonoxideSensor",
        &kHAPServiceType_CarbonDioxideSensor,
        kHAPServiceDebugDescription_CarbonDioxideSensor
    },
    {
        "ContactSensor",
        &kHAPServiceType_ContactSensor,
        kHAPServiceDebugDescription_ContactSensor
    },
    {
        "Door",
        &kHAPServiceType_Door,
        kHAPServiceDebugDescription_Door
    },
    {
        "HumiditySensor",
        &kHAPServiceType_HumiditySensor,
        kHAPServiceDebugDescription_HumiditySensor
    },
    {
        "LeakSensor",
        &kHAPServiceType_LeakSensor,
        kHAPServiceDebugDescription_LeakSensor
    },
    {
        "LightSensor",
        &kHAPServiceType_LightSensor,
        kHAPServiceDebugDescription_LightSensor
    },
    {
        "MotionSensor",
        &kHAPServiceType_MotionSensor,
        kHAPServiceDebugDescription_MotionSensor
    },
    {
        "OccupancySensor",
        &kHAPServiceType_OccupancySensor,
        kHAPServiceDebugDescription_OccupancySensor
    },
    {
        "SmokeSensor",
        &kHAPServiceType_SmokeSensor,
        kHAPServiceDebugDescription_SmokeSensor
    },
    {
        "StatelessProgrammableSwitch",
        &kHAPServiceType_StatelessProgrammableSwitch,
        kHAPServiceDebugDescription_StatelessProgrammableSwitch
    },
    {
        "TemperatureSensor",
        &kHAPServiceType_TemperatureSensor,
        kHAPServiceDebugDescription_TemperatureSensor
    },
    {
        "Window",
        &kHAPServiceType_Window,
        kHAPServiceDebugDescription_Window
    },
    {
        "WindowCovering",
        &kHAPServiceType_WindowCovering,
        kHAPServiceDebugDescription_WindowCovering
    },
    {
        "AirQualitySensor",
        &kHAPServiceType_AirQualitySensor,
        kHAPServiceDebugDescription_AirQualitySensor
    },
    {
        "BatteryService",
        &kHAPServiceType_BatteryService,
        kHAPServiceDebugDescription_BatteryService
    },
    {
        "CarbonDioxideSensor",
        &kHAPServiceType_CarbonDioxideSensor,
        kHAPServiceDebugDescription_CarbonDioxideSensor
    },
    {
        "HAPProtocolInformation",
        &kHAPServiceType_HAPProtocolInformation,
        kHAPServiceDebugDescription_HAPProtocolInformation
    },
    {
        "Fan",
        &kHAPServiceType_Fan,
        kHAPServiceDebugDescription_Fan
    },
    {
        "Slat",
        &kHAPServiceType_Slat,
        kHAPServiceDebugDescription_Slat
    },
    {
        "FilterMaintenance",
        &kHAPServiceType_FilterMaintenance,
        kHAPServiceDebugDescription_FilterMaintenance
    },
    {
        "AirPurifier",
        &kHAPServiceType_AirPurifier,
        kHAPServiceDebugDescription_AirPurifier
    },
    {
        "HeaterCooler",
        &kHAPServiceType_HeaterCooler,
        kHAPServiceDebugDescription_HeaterCooler
    },
    {
        "HumidifierDehumidifier",
        &kHAPServiceType_HumidifierDehumidifier,
        kHAPServiceDebugDescription_HumidifierDehumidifier
    },
    {
        "ServiceLabel",
        &kHAPServiceType_ServiceLabel,
        kHAPServiceDebugDescription_ServiceLabel
    },
    {
        "IrrigationSystem",
        &kHAPServiceType_IrrigationSystem,
        kHAPServiceDebugDescription_IrrigationSystem
    },
    {
        "Valve",
        &kHAPServiceType_Valve,
        kHAPServiceDebugDescription_Valve
    },
    {
        "Faucet",
        &kHAPServiceType_Faucet,
        kHAPServiceDebugDescription_Faucet
    },
    {
        "CameraRTPStreamManagement",
        &kHAPServiceType_CameraRTPStreamManagement,
        kHAPServiceDebugDescription_CameraRTPStreamManagement
    },
    {
        "Microphone",
        &kHAPServiceType_Microphone,
        kHAPServiceDebugDescription_Microphone
    },
    {
        "Speaker",
        &kHAPServiceType_Speaker,
        kHAPServiceDebugDescription_Speaker
    },
};

typedef struct {
    const char *name;
    const HAPUUID *type;
    const char *debugDescription;
    HAPCharacteristicFormat format;
} lhap_characteristic_type;

static struct lhap_desc {
    bool isConfigure:1;
    size_t attributeCount;
    HAPAccessory accessory;
    HAPAccessory **bridgedAccessories;
} gv_lhap_desc = {
    .attributeCount = kAttributeCount
};

// Find the string and return the string index.
static int lhap_lookup_by_name(const char *name, const char *strs[], int len)
{
    for (int i = 0; i < len; i++) {
        if (!strcmp(name, strs[i])) {
            return i;
        }
    }
    return -1;
}

// Check if the service is one of "lhap_accessory_services_userdatas"
static bool lhap_service_is_light_userdata(HAPService *service)
{
    for (const lhap_lightuserdata *ud = lhap_accessory_services_userdatas;
        ud->ptr; ud++) {
        if (service == ud->ptr) {
            return true;
        }
    }
    return false;
}

// Return a new string copy from str.
static char *lhap_new_str(const char *str)
{
    if (!str) {
        return NULL;
    }
    char *copy = LHAP_MALLOC(strlen(str) + 1);
    return copy ? strcpy(copy, str) : NULL;
}

static bool
lhap_accessory_aid_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    ((HAPAccessory *)arg)->aid = lua_tonumber(L, -1);
    return true;
}

static bool
lhap_accessory_category_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    int idx = lhap_lookup_by_name(lua_tostring(L, -1),
        lhap_lhap_accessory_category_strs,
        HAPArrayCount(lhap_lhap_accessory_category_strs));
    if (idx == -1) {
        return false;
    }
    ((HAPAccessory *)arg)->category = idx;
    return true;
}

static bool
lhap_accessory_name_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return (*((char **)&((HAPAccessory *)arg)->name) =
        lhap_new_str(lua_tostring(L, -1))) ? true : false;
}

static bool
lhap_accessory_manufacturer_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return (*((char **)&((HAPAccessory *)arg)->manufacturer) =
        lhap_new_str(lua_tostring(L, -1))) ? true : false;
}

static bool
lhap_accessory_model_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return (*((char **)&((HAPAccessory *)arg)->model) =
        lhap_new_str(lua_tostring(L, -1))) ? true : false;
}

static bool
lhap_accessory_serialnumber_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return (*((char **)&((HAPAccessory *)arg)->serialNumber) =
        lhap_new_str(lua_tostring(L, -1))) ? true : false;
}

static bool
lhap_accessory_firmwareversion_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return (*((char **)&((HAPAccessory *)arg)->firmwareVersion) =
        lhap_new_str(lua_tostring(L, -1))) ? true : false;
}

static bool
lhap_accessory_hardwareversion_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return (*((char **)&((HAPAccessory *)arg)->hardwareVersion) =
        lhap_new_str(lua_tostring(L, -1))) ? true : false;
}

static bool
lhap_service_iid_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    ((HAPService *)arg)->iid = lua_tonumber(L, -1);
    gv_lhap_desc.attributeCount++;
    return true;
}

static bool
lhap_service_type_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    HAPService *service = arg;
    const char *str = lua_tostring(L, -1);
    for (int i = 0; i < HAPArrayCount(lhap_service_type_tab);
        i++) {
        if (!strcmp(str, lhap_service_type_tab[i].name)) {
            service->serviceType = lhap_service_type_tab[i].type;
            service->debugDescription = lhap_service_type_tab[i].debugDescription;
            return true;
        }
    }
    return false;
}

static bool
lhap_service_name_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return (*((char **)&((HAPService *)arg)->name) =
        lhap_new_str(lua_tostring(L, -1))) ? true : false;
}

static bool
lhap_properties_primary_service_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    ((HAPServiceProperties *)arg)->primaryService = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_properties_hidden_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    ((HAPServiceProperties *)arg)->hidden = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_properties_supports_conf_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    ((HAPServiceProperties *)arg)->ble.supportsConfiguration = lua_toboolean(L, -1);
    return true;
}

static const lapi_table_kv lhap_properties_ble_kvs[] = {
    {"supportsConfiguration", LUA_TBOOLEAN, lhap_properties_supports_conf_cb},
    {NULL, LUA_TNONE, NULL},
};

static bool
lhap_properties_ble_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return lapi_traverse_table(L, -1, lhap_properties_ble_kvs,
        &((HAPService *)arg)->properties);
}

static const lapi_table_kv lhap_properties_kvs[] = {
    {"primaryService", LUA_TBOOLEAN, lhap_properties_primary_service_cb},
    {"hidden", LUA_TBOOLEAN, lhap_properties_hidden_cb},
    {"ble", LUA_TTABLE, lhap_properties_ble_cb},
    {NULL, LUA_TNONE, NULL},
};

static bool
lhap_service_properties_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return lapi_traverse_table(L, -1, lhap_properties_kvs,
        &((HAPService *)arg)->properties);
}

static bool
lhap_characteristics_iid_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    ((HAPBaseCharacteristic *)arg)->iid = lua_tointeger(L, -1);
    return true;
}

static bool
lhap_characteristics_type_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return true;
}

static bool
lhap_characteristics_mfg_desc_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return (*((char **)&((HAPBaseCharacteristic *)arg)->manufacturerDescription) =
        lhap_new_str(lua_tostring(L, -1))) ? true : false;
}

static bool
lhap_characteristics_properties_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return true;
}

static const lapi_table_kv lhap_characteristics_kvs[] = {
    {"iid", LUA_TNUMBER, lhap_characteristics_iid_cb},
    {"type", LUA_TSTRING, lhap_characteristics_type_cb},
    {"manufacturerDescription", LUA_TSTRING, lhap_characteristics_mfg_desc_cb},
    {"properties", LUA_TTABLE, lhap_characteristics_properties_cb},
    {NULL, LUA_TNONE, NULL},
};

static bool lhap_service_characteristics_arr_cb(lua_State *L, int i, void *arg)
{
    HAPCharacteristic **characteristics = arg;

    if (!lua_istable(L, -1)) {
        return false;
    }

    lua_pushstring(L, "format");
    lua_gettable(L, -2);
    if (!lua_isstring(L, -1)) {
        return false;
    }
    int idx = lhap_lookup_by_name(lua_tostring(L, -1),
        lhap_characteristics_format_strs, 
        HAPArrayCount(lhap_characteristics_format_strs));
    lua_pop(L, 1);
    if (idx == -1) {
        return false;
    }

    HAPCharacteristic *c = LHAP_MALLOC(lhap_characteristic_struct_size[idx]);
    if (!c) {
        return false;
    }
    memset(c, 0, sizeof(HAPCharacteristic));
    ((HAPBaseCharacteristic *)c)->format = idx;
    characteristics[i] = c;
    if (!lapi_traverse_table(L, -1, lhap_characteristics_kvs, c)) {
        return false;
    }
    return true;
}

static void lhap_reset_characteristic(HAPCharacteristic *characteristic)
{
    LHAP_FREE(((HAPBaseCharacteristic *)characteristic)->manufacturerDescription);
}

static bool
lhap_service_characteristics_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    HAPService *service = arg;
    HAPCharacteristic ***pcharacteristic = (HAPCharacteristic ***)&(service->characteristics);
    // Get the array length.
    size_t len = lua_rawlen(L, -1);
    if (len == 0) {
        *pcharacteristic = NULL;
        return true;
    }

    HAPCharacteristic **characteristics = LHAP_MALLOC(sizeof(HAPCharacteristic *) * (len + 1));
    if (!characteristics) {
        return false;
    }
    if (!lapi_traverse_array(L, -1, lhap_service_characteristics_arr_cb, characteristics)) {
        for (HAPCharacteristic **c = characteristics; *c; c++) {
            lhap_reset_characteristic(*c);
            LHAP_FREE(*c);
        }
        LHAP_FREE(characteristics);
        return false;
    }
    *pcharacteristic = characteristics;
    return true;
}

static const lapi_table_kv lhap_service_kvs[] = {
    {"iid", LUA_TNUMBER, lhap_service_iid_cb},
    {"type", LUA_TSTRING, lhap_service_type_cb},
    {"name", LUA_TSTRING, lhap_service_name_cb},
    {"properties", LUA_TTABLE, lhap_service_properties_cb},
    {"characteristics", LUA_TTABLE, lhap_service_characteristics_cb},
    {NULL, LUA_TNONE, NULL},
};

static void lhap_reset_service(HAPService *service)
{
    LHAP_FREE(service->name);
    for (HAPCharacteristic **c = (HAPCharacteristic **)service->characteristics; *c; c++) {
        lhap_reset_characteristic(*c);
        LHAP_FREE(*c);
    }
    LHAP_FREE(service->characteristics);
}

static bool
lhap_accessory_services_arr_cb(lua_State *L, int i, void *arg)
{
    HAPService **services = arg;

    if (lua_islightuserdata(L, -1)) {
        HAPService *s = lua_touserdata(L, -1);
        if (lhap_service_is_light_userdata(s)) {
            services[i] = s;
        } else {
            return false;
        }
        return true;
    }
    if (!lua_istable(L, -1)) {
        return false;
    }
    HAPService *s = LHAP_MALLOC(sizeof(HAPService));
    if (!s) {
        return false;
    }
    memset(s, 0, sizeof(HAPService));
    services[i] = s;
    if (!lapi_traverse_table(L, -1, lhap_service_kvs, s)) {
        return false;
    }
    return true;
}

static bool
lhap_accessory_services_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    HAPAccessory *accessory = arg;
    HAPService ***pservices = (HAPService ***)&(accessory->services);

    // Get the array length.
    size_t len = lua_rawlen(L, -1);
    if (len == 0) {
        *pservices = NULL;
        return true;
    }

    HAPService **services = LHAP_MALLOC(sizeof(HAPService *) * (len + 1));
    if (!services) {
        return false;
    }
    memset(services, 0, sizeof(HAPService *) * (len + 1));

    if (!lapi_traverse_array(L, -1, lhap_accessory_services_arr_cb, services)) {
        for (HAPService **s = services; *s; s++) {
            if (!lhap_service_is_light_userdata(*s)) {
                lhap_reset_service(*s);
                LHAP_FREE(*s);
            }
        }
        LHAP_FREE(services);
        return false;
    }

    *pservices = services;
    return true;
}

HAP_RESULT_USE_CHECK
HAPError lhap_accessory_identify_cb(
        HAPAccessoryServerRef* server HAP_UNUSED,
        const HAPAccessoryIdentifyRequest* request,
        void* _Nullable context) {
    HAPPrecondition(server);
    HAPPrecondition(request);
    HAPPrecondition(context);

    HAPError err = kHAPError_Unknown;
    lua_State *L = ((AccessoryContext *)context)->L;

    if (!lapi_push_callback(L,
        (size_t)&(request->accessory->callbacks.identify))) {
        HAPLogError(&lhap_log,
            "%s: Can't get lua function", __func__);
        return err;
    }

    if (lua_pcall(L, 0, 1, 0)) {
        HAPLogError(&lhap_log,
            "%s: Failed to call lua function", __func__);
        return err;
    }

    if (!lua_isnumber(L, -1)) {
        HAPLogError(&lhap_log,
            "%s: Illegal return value", __func__);
        goto end;
    }

    err = lua_tonumber(L, -1);
end:
    lua_pop(L, 1);
    lapi_collectgarbage(L);
    return err;
}

static bool
lhap_accessory_cbs_identify_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    if (!lapi_register_callback(L, -1,
        (size_t)&(((HAPAccessory *)arg)->callbacks.identify))) {
        return false;
    }
    ((HAPAccessory *)arg)->callbacks.identify = lhap_accessory_identify_cb;
    return true;
}

static lapi_table_kv lhap_accessory_callbacks_kvs[] = {
    {"identify", LUA_TFUNCTION, lhap_accessory_cbs_identify_cb},
    {NULL, -1, NULL},
};

static bool
lhap_accessory_callbacks_cb(lua_State *L, const lapi_table_kv *kv, void *arg)
{
    return lapi_traverse_table(L, -1, lhap_accessory_callbacks_kvs, arg);
}

static const lapi_table_kv lhap_accessory_kvs[] = {
    {"aid", LUA_TNUMBER, lhap_accessory_aid_cb},
    {"category", LUA_TSTRING, lhap_accessory_category_cb},
    {"name", LUA_TSTRING, lhap_accessory_name_cb},
    {"manufacturer", LUA_TSTRING, lhap_accessory_manufacturer_cb},
    {"model", LUA_TSTRING, lhap_accessory_model_cb},
    {"serialNumber", LUA_TSTRING, lhap_accessory_serialnumber_cb},
    {"firmwareVersion", LUA_TSTRING, lhap_accessory_firmwareversion_cb},
    {"hardwareVersion", LUA_TSTRING, lhap_accessory_hardwareversion_cb},
    {"services", LUA_TTABLE, lhap_accessory_services_cb},
    {"callbacks", LUA_TTABLE, lhap_accessory_callbacks_cb},
    {NULL, LUA_TNONE, NULL},
};

static void lhap_reset_accessory(HAPAccessory *accessory)
{
    LHAP_FREE(accessory->name);
    LHAP_FREE(accessory->manufacturer);
    LHAP_FREE(accessory->model);
    LHAP_FREE(accessory->serialNumber);
    LHAP_FREE(accessory->firmwareVersion);
    LHAP_FREE(accessory->hardwareVersion);
    for (HAPService **s = (HAPService **)accessory->services; *s; s++) {
        if (!lhap_service_is_light_userdata(*s)) {
            lhap_reset_service(*s);
            LHAP_FREE(*s);
        }
    }
    LHAP_FREE(accessory->services);
}

bool lhap_accessories_arr_cb(lua_State *L, int i, void *arg)
{
    HAPAccessory **accessories = arg;
    if (!lua_istable(L, -1)) {
        return false;
    }
    HAPAccessory *a = LHAP_MALLOC(sizeof(HAPAccessory));
    if (!a) {
        return false;
    }
    memset(a, 0, sizeof(HAPAccessory));
    accessories[i] = a;
    if (!lapi_traverse_table(L, -1, lhap_accessory_kvs, a)) {
        return false;
    }
    return true;
}

/**
 * configure(accessory: table, bridgedAccessories?: table) -> boolean
 *
 * If the category of the accessory is bridge, the parameters
 * bridgedAccessories is valid.
*/
static int hap_configure(lua_State *L)
{
    size_t len = 0;
    struct lhap_desc *desc = &gv_lhap_desc;
    HAPAccessory *accessory = &desc->accessory;

    if (desc->isConfigure) {
        HAPLogError(&lhap_log,
            "%s: HAP is already configured.", __func__);
        goto err;
    }

    luaL_checktype(L, 1, LUA_TTABLE);
    if (!lua_isnone(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
    }

    if (!lapi_traverse_table(L, 1, lhap_accessory_kvs, accessory)) {
        HAPLogError(&lhap_log,
            "%s: Failed to generate accessory structure from table accessory.",
            __func__);
        goto err1;
    }

    if (accessory->category != kHAPAccessoryCategory_Bridges) {
        goto end;
    }

    len = lua_rawlen(L, 2);
    if (len == 0) {
        goto end;
    }

    desc->bridgedAccessories =
        LHAP_MALLOC(sizeof(HAPAccessory *) * (len + 1));
    if (!desc->bridgedAccessories) {
        HAPLogError(&lhap_log,
            "%s: Error type(bridgedAccessories).", __func__);
        goto err1;
    }
    memset(desc->bridgedAccessories, 0,
        sizeof(HAPAccessory *) * (len + 1));

    if (!lapi_traverse_array(L, 2, lhap_accessories_arr_cb,
        desc->bridgedAccessories)) {
        HAPLogError(&lhap_log,
            "%s: Failed to generate bridged accessories structureies"
            " from table bridgedAccessories.", __func__);
        goto err2;
    }
end:
    HAPLogInfo(&lhap_log,
        "Accessory \"%s\": (%s) has been configured.", accessory->name,
        lhap_lhap_accessory_category_strs[accessory->category]);
    if (len) {
        HAPLogInfo(&lhap_log,
            "%u bridged accessories have been configured.", len);
    }
    desc->isConfigure = true;
    lua_pushboolean(L, true);
    return 1;

err2:
    for (HAPAccessory **pa = desc->bridgedAccessories; *pa != NULL; pa++) {
        lhap_reset_accessory(*pa);
        LHAP_FREE(*pa);
    }
    LHAP_FREE(desc->bridgedAccessories);
err1:
    lhap_reset_accessory(accessory);
err:
    lua_pushboolean(L, false);
    return 1;
}

static const luaL_Reg haplib[] = {
    {"configure", hap_configure},
    /* placeholders */
    {"Error", NULL},
    {"AccessoryInformationService", NULL},
    {"HapProtocolInformationService", NULL},
    {"PairingService", NULL},
    {NULL, NULL},
};

LUAMOD_API int luaopen_hap(lua_State *L) {
    luaL_newlib(L, haplib);

    /* set Error */
    lapi_create_enum_table(L, lhap_error_strs,
        HAPArrayCount(lhap_error_strs));
    lua_setfield(L, -2, "Error");

    /* set services */
    for (const lhap_lightuserdata *ud = lhap_accessory_services_userdatas;
        ud->ptr; ud++) {
        lua_pushlightuserdata(L, ud->ptr);
        lua_setfield(L, -2, ud->name);
    }
    return 1;
}

const HAPAccessory *lhap_get_accessory(void)
{
    if (gv_lhap_desc.isConfigure) {
        return &gv_lhap_desc.accessory;
    }
    return NULL;
}

const HAPAccessory *const *lhap_get_bridged_accessories(void)
{
    if (gv_lhap_desc.isConfigure) {
        return (const HAPAccessory *const *)gv_lhap_desc.bridgedAccessories;
    }
    return NULL;
}

size_t lhap_get_attribute_count(void)
{
    if (gv_lhap_desc.isConfigure) {
        return gv_lhap_desc.attributeCount;
    }
    return 0;
}

void lhap_deinitialize(void)
{
    struct lhap_desc *desc = &gv_lhap_desc;
    for (HAPAccessory **pa = desc->bridgedAccessories; *pa != NULL; pa++) {
        lhap_reset_accessory(*pa);
        LHAP_FREE(*pa);
    }
    LHAP_FREE(desc->bridgedAccessories);
    lhap_reset_accessory(&desc->accessory);
    desc->attributeCount = kAttributeCount;
    desc->isConfigure = false;
}
