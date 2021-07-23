// Copyright (c) 2021 KNpTrue and homekit-bridge contributors
//
// Licensed under the MIT License.
// You may not use this file except in compliance with the License.
// See [CONTRIBUTORS.md] for the list of homekit-bridge project authors.

#include <lualib.h>
#include <lauxlib.h>
#include <HAPCharacteristic.h>

#include "app_int.h"
#include "lc.h"
#include "lhaplib.h"
#include "db.h"

#define LHAP_BRIDGED_ACCESSORY_IID_DFT 2

#define LHAP_CASE_CHAR_FORMAT_CODE(format, ptr, code) \
    case kHAPCharacteristicFormat_ ## format: \
        { HAP ## format ## Characteristic *p = ptr; code; } break;

#define LHAP_LOG_TYPE_ERROR(L, name, excepted, got) \
    do { \
        HAPLogError(&lhap_log, "%s: Invalid type: %s", __func__, name); \
        HAPLogError(&lhap_log, "%s: %s excepted, got %s", __func__, \
            lua_typename(L, excepted), \
            lua_typename(L, got)); \
    } while (0)

static const HAPLogObject lhap_log = {
    .subsystem = APP_BRIDGE_LOG_SUBSYSTEM,
    .category = "lhap",
};

static const char *lhap_transport_type_strs[] = {
    NULL,
    "IP",
    "BLE",
};

static const char *lhap_accessory_category_strs[] = {
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

static const char *lhap_characteristic_format_strs[] = {
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

static const char *lhap_characteristic_units_strs[] = {
    "None",
    "Celsius",
    "ArcDegrees",
    "Percentage",
    "Lux",
    "Seconds",
};

static const char *lhap_server_state_strs[] = {
    "Idle",
    "Running",
    "Stopping",
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

#define LHAP_SERVICE_TYPE_FORMAT(type) \
    { #type, &kHAPServiceType_##type, kHAPServiceDebugDescription_##type }

static const lhap_service_type lhap_service_type_tab[] = {
    LHAP_SERVICE_TYPE_FORMAT(AccessoryInformation),
    LHAP_SERVICE_TYPE_FORMAT(GarageDoorOpener),
    LHAP_SERVICE_TYPE_FORMAT(LightBulb),
    LHAP_SERVICE_TYPE_FORMAT(LockManagement),
    LHAP_SERVICE_TYPE_FORMAT(LockMechanism),
    LHAP_SERVICE_TYPE_FORMAT(Outlet),
    LHAP_SERVICE_TYPE_FORMAT(Switch),
    LHAP_SERVICE_TYPE_FORMAT(Thermostat),
    LHAP_SERVICE_TYPE_FORMAT(Pairing),
    LHAP_SERVICE_TYPE_FORMAT(SecuritySystem),
    LHAP_SERVICE_TYPE_FORMAT(CarbonMonoxideSensor),
    LHAP_SERVICE_TYPE_FORMAT(ContactSensor),
    LHAP_SERVICE_TYPE_FORMAT(Door),
    LHAP_SERVICE_TYPE_FORMAT(HumiditySensor),
    LHAP_SERVICE_TYPE_FORMAT(LeakSensor),
    LHAP_SERVICE_TYPE_FORMAT(LightSensor),
    LHAP_SERVICE_TYPE_FORMAT(MotionSensor),
    LHAP_SERVICE_TYPE_FORMAT(OccupancySensor),
    LHAP_SERVICE_TYPE_FORMAT(SmokeSensor),
    LHAP_SERVICE_TYPE_FORMAT(StatelessProgrammableSwitch),
    LHAP_SERVICE_TYPE_FORMAT(TemperatureSensor),
    LHAP_SERVICE_TYPE_FORMAT(Window),
    LHAP_SERVICE_TYPE_FORMAT(WindowCovering),
    LHAP_SERVICE_TYPE_FORMAT(AirQualitySensor),
    LHAP_SERVICE_TYPE_FORMAT(BatteryService),
    LHAP_SERVICE_TYPE_FORMAT(CarbonDioxideSensor),
    LHAP_SERVICE_TYPE_FORMAT(HAPProtocolInformation),
    LHAP_SERVICE_TYPE_FORMAT(Fan),
    LHAP_SERVICE_TYPE_FORMAT(Slat),
    LHAP_SERVICE_TYPE_FORMAT(FilterMaintenance),
    LHAP_SERVICE_TYPE_FORMAT(AirPurifier),
    LHAP_SERVICE_TYPE_FORMAT(HeaterCooler),
    LHAP_SERVICE_TYPE_FORMAT(HumidifierDehumidifier),
    LHAP_SERVICE_TYPE_FORMAT(ServiceLabel),
    LHAP_SERVICE_TYPE_FORMAT(IrrigationSystem),
    LHAP_SERVICE_TYPE_FORMAT(Valve),
    LHAP_SERVICE_TYPE_FORMAT(Faucet),
    LHAP_SERVICE_TYPE_FORMAT(CameraRTPStreamManagement),
    LHAP_SERVICE_TYPE_FORMAT(Microphone),
    LHAP_SERVICE_TYPE_FORMAT(Speaker),
};

typedef struct {
    const char *name;
    const HAPUUID *type;
    const char *debugDescription;
} lhap_characteristic_type;

#define LHAP_CHARACTERISTIC_TYPE_FORMAT(type) \
{ \
    #type, \
    &kHAPCharacteristicType_##type, \
    kHAPCharacteristicDebugDescription_##type, \
}

static const lhap_characteristic_type lhap_characteristic_type_tab[] = {
    LHAP_CHARACTERISTIC_TYPE_FORMAT(AdministratorOnlyAccess),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(AudioFeedback),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(Brightness),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CoolingThresholdTemperature),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentDoorState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentHeatingCoolingState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentRelativeHumidity),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentTemperature),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(HeatingThresholdTemperature),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(Hue),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(Identify),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(LockControlPoint),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(LockManagementAutoSecurityTimeout),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(LockLastKnownAction),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(LockCurrentState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(LockTargetState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(Logs),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(Manufacturer),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(Model),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(MotionDetected),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(Name),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ObstructionDetected),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(On),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(OutletInUse),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(RotationDirection),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(RotationSpeed),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(Saturation),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(SerialNumber),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetDoorState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetHeatingCoolingState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetRelativeHumidity),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetTemperature),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TemperatureDisplayUnits),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(Version),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(PairSetup),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(PairVerify),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(PairingFeatures),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(PairingPairings),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(FirmwareRevision),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(HardwareRevision),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(AirParticulateDensity),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(AirParticulateSize),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(SecuritySystemCurrentState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(SecuritySystemTargetState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(BatteryLevel),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CarbonMonoxideDetected),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ContactSensorState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentAmbientLightLevel),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentHorizontalTiltAngle),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentPosition),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentVerticalTiltAngle),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(HoldPosition),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(LeakDetected),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(OccupancyDetected),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(PositionState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ProgrammableSwitchEvent),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(StatusActive),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(SmokeDetected),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(StatusFault),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(StatusJammed),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(StatusLowBattery),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(StatusTampered),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetHorizontalTiltAngle),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetPosition),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetVerticalTiltAngle),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(SecuritySystemAlarmType),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ChargingState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CarbonMonoxideLevel),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CarbonMonoxidePeakLevel),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CarbonDioxideDetected),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CarbonDioxideLevel),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CarbonDioxidePeakLevel),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(AirQuality),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ServiceSignature),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(AccessoryFlags),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(LockPhysicalControls),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetAirPurifierState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentAirPurifierState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentSlatState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(FilterLifeLevel),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(FilterChangeIndication),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ResetFilterIndication),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentFanState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(Active),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentHeaterCoolerState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetHeaterCoolerState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentHumidifierDehumidifierState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetHumidifierDehumidifierState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(WaterLevel),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(SwingMode),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetFanState),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(SlatType),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(CurrentTiltAngle),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(TargetTiltAngle),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(OzoneDensity),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(NitrogenDioxideDensity),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(SulphurDioxideDensity),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(PM2_5Density),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(PM10Density),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(VOCDensity),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(RelativeHumidityDehumidifierThreshold),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(RelativeHumidityHumidifierThreshold),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ServiceLabelIndex),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ServiceLabelNamespace),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ColorTemperature),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ProgramMode),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(InUse),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(SetDuration),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(RemainingDuration),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ValveType),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(IsConfigured),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ActiveIdentifier),
    LHAP_CHARACTERISTIC_TYPE_FORMAT(ADKVersion),
};

#if LUA_MAXINTEGER < UINT32_MAX
#define LHAP_UINT32_MAX LUA_MAXINTEGER
#else
#define LHAP_UINT32_MAX UINT32_MAX
#endif

static const struct {
    lua_Integer min;
    lua_Integer max;
} lhap_char_integer_value_range_tab[] = {
    [kHAPCharacteristicFormat_UInt8] = {0, UINT8_MAX},
    [kHAPCharacteristicFormat_UInt16] = {0, UINT16_MAX},
    [kHAPCharacteristicFormat_UInt32] = {0, LHAP_UINT32_MAX},
    [kHAPCharacteristicFormat_UInt64] = {0, LUA_MAXINTEGER},
    [kHAPCharacteristicFormat_Int] = {INT32_MIN, INT32_MAX},
};

/**
 * Store the ref id of the var that needs to be saved in the accessory. 
 */
typedef struct {
    int context;
    int identify;
} accessory_ref;

/**
 * Store the ref id of the var that needs to be saved in the characterisitc. 
 */
typedef struct {
    int handleRead;
    int handleWrite;
    int handleSubscribe;
    int handleUnsubscribe;
} char_ref;

// Return the pointer to the structure accessory_ref.
#define LHAP_ACCESSORY_REF(accessory) \
    ((accessory_ref *)((char *)accessory + sizeof(HAPAccessory)))

// Initialize the structure accessory_ref.
#define LHAP_ACCESSORY_REF_INIT(accessory) \
do { \
    LHAP_ACCESSORY_REF(accessory)->context = LUA_REFNIL; \
    LHAP_ACCESSORY_REF(accessory)->identify = LUA_REFNIL; \
} while (0)

// Return the pointer to the structure char_ref.
#define LHAP_CHAR_REF(c, format) \
    ((char_ref *)((char *)c + (size_t)lhap_characteristic_struct_size[format]))

// Initialize the structure char_ref.
#define LHAP_CHAR_REF_INIT(c, format) \
do { \
    LHAP_CHAR_REF(c, format)->handleRead = LUA_REFNIL; \
    LHAP_CHAR_REF(c, format)->handleWrite = LUA_REFNIL; \
    LHAP_CHAR_REF(c, format)->handleSubscribe = LUA_REFNIL; \
    LHAP_CHAR_REF(c, format)->handleUnsubscribe = LUA_REFNIL; \
} while (0)

enum lhap_server_cb_idx {
    LHAP_SERVER_CB_UPDATE_STATE,
    LHAP_SERVER_CB_SESSION_ACCEPT,
    LHAP_SERVER_CB_SESSION_INVALIDATE,
    LHAP_SERVER_CB_MAX,
};

static struct lhap_desc {
    bool is_configure:1;
    bool conf_changed:1;
    size_t attribute_cnt;
    size_t bridged_aid;
    size_t iid;
    int server_cb_ref_ids[LHAP_SERVER_CB_MAX];
    HAPAccessory *primary_acc;
    HAPAccessory **bridged_accs;
    HAPAccessoryServerRef *server;
} gv_lhap_desc = {
    .attribute_cnt = kAttributeCount,
    .bridged_aid = LHAP_BRIDGED_ACCESSORY_IID_DFT,
    .iid = kAttributeCount + 1
};

static bool lhap_ref_var(lua_State *L, int idx, int *ref_id) {
    if (*ref_id != LUA_REFNIL) {
        return false;
    }
    *ref_id = lc_ref(L, idx);
    if (*ref_id == LUA_REFNIL) {
        return false;
    }
    return true;
}

// Find the string and return the string index.
static int lhap_lookup_by_name(const char *name, const char *strs[], int len) {
    for (int i = 0; i < len; i++) {
        if (strs[i] && HAPStringAreEqual(name, strs[i])) {
            return i;
        }
    }
    return -1;
}

// Check if the service is one of "lhap_accessory_services_userdatas"
static bool lhap_service_is_light_userdata(HAPService *service) {
    for (const lhap_lightuserdata *ud = lhap_accessory_services_userdatas;
        ud->ptr; ud++) {
        if (service == ud->ptr) {
            return true;
        }
    }
    return false;
}

static lua_Integer
lhap_tointegerx(lua_State *L, int idx, int *isnum, HAPCharacteristicFormat format, bool is_unsigned) {
    lua_Integer num;
    num = lua_tointegerx(L, -1, isnum);
    if (!(*isnum)) {
        return num;
    }
    lua_Integer min = is_unsigned ? 0 : lhap_char_integer_value_range_tab[format].min;
    if (num < min || num > lhap_char_integer_value_range_tab[format].max) {
        HAPLogError(&lhap_log, "%s: Integer is out of range("
            LUA_INTEGER_FMT ", " LUA_INTEGER_FMT ").",
            __func__, lhap_char_integer_value_range_tab[format].min,
            lhap_char_integer_value_range_tab[format].max);
        *isnum = false;
        return 0;
    }
    return num;
}

static bool
lhap_accessory_aid_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    int isnum;
    lua_Integer aid = lua_tointegerx(L, -1, &isnum);
    if (!isnum || aid <= 0) {
        return false;
    }
    ((HAPAccessory *)arg)->aid = aid;
    return true;
}

static bool
lhap_accessory_category_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    int idx = lhap_lookup_by_name(lua_tostring(L, -1),
        lhap_accessory_category_strs,
        HAPArrayCount(lhap_accessory_category_strs));
    if (idx == -1) {
        return false;
    }
    ((HAPAccessory *)arg)->category = idx;
    return true;
}

static bool
lhap_accessory_name_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return (*((char **)&((HAPAccessory *)arg)->name) =
        lc_new_str(L, -1)) ? true : false;
}

static bool
lhap_accessory_mfg_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return (*((char **)&((HAPAccessory *)arg)->manufacturer) =
        lc_new_str(L, -1)) ? true : false;
}

static bool
lhap_accessory_model_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return (*((char **)&((HAPAccessory *)arg)->model) =
        lc_new_str(L, -1)) ? true : false;
}

static bool
lhap_accessory_sn_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return (*((char **)&((HAPAccessory *)arg)->serialNumber) =
        lc_new_str(L, -1)) ? true : false;
}

static bool
lhap_accessory_fw_ver_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return (*((char **)&((HAPAccessory *)arg)->firmwareVersion) =
        lc_new_str(L, -1)) ? true : false;
}

static bool
lhap_accessory_hw_ver_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return (*((char **)&((HAPAccessory *)arg)->hardwareVersion) =
        lc_new_str(L, -1)) ? true : false;
}

static bool
lhap_service_iid_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    int isnum;
    lua_Integer iid = lua_tointegerx(L, -1, &isnum);
    if (!isnum || iid <= (lua_Integer)kAttributeCount) {
        HAPLogError(&lhap_log, "%s: Invalid IID.", __func__);
        return false;
    }
    ((HAPService *)arg)->iid = iid;
    gv_lhap_desc.attribute_cnt++;
    return true;
}

static bool
lhap_service_type_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    HAPService *service = arg;
    const char *str = lua_tostring(L, -1);
    for (int i = 0; i < HAPArrayCount(lhap_service_type_tab);
        i++) {
        if (HAPStringAreEqual(str, lhap_service_type_tab[i].name)) {
            service->serviceType = lhap_service_type_tab[i].type;
            service->debugDescription = lhap_service_type_tab[i].debugDescription;
            return true;
        }
    }
    return false;
}

static bool
lhap_service_name_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return (*((char **)&((HAPService *)arg)->name) =
        lc_new_str(L, -1)) ? true : false;
}

static bool
lhap_service_props_primary_service_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPServiceProperties *)arg)->primaryService = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_service_props_hidden_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPServiceProperties *)arg)->hidden = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_service_props_supports_conf_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPServiceProperties *)arg)->ble.supportsConfiguration = lua_toboolean(L, -1);
    return true;
}

static const lc_table_kv lhap_service_props_ble_kvs[] = {
    {"supportsConfiguration", LC_TBOOLEAN, lhap_service_props_supports_conf_cb},
    {NULL, LUA_TNONE, NULL},
};

static bool
lhap_service_props_ble_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lc_traverse_table(L, -1, lhap_service_props_ble_kvs, arg);
}

static const lc_table_kv lhap_service_props_kvs[] = {
    {"primaryService", LC_TBOOLEAN, lhap_service_props_primary_service_cb},
    {"hidden", LC_TBOOLEAN, lhap_service_props_hidden_cb},
    {"ble", LC_TTABLE, lhap_service_props_ble_cb},
    {NULL, LC_TNONE, NULL},
};

static bool
lhap_service_props_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lc_traverse_table(L, -1, lhap_service_props_kvs,
        &((HAPService *)arg)->properties);
}

static bool
lhap_characteristic_iid_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    int isnum;
    lua_Integer iid = lua_tointegerx(L, -1, &isnum);
    if (!isnum || iid <= (lua_Integer)kAttributeCount) {
        HAPLogError(&lhap_log, "%s: Invalid IID.", __func__);
        return false;
    }
    ((HAPBaseCharacteristic *)arg)->iid = iid;
    gv_lhap_desc.attribute_cnt++;
    return true;
}

static bool
lhap_characteristic_type_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    HAPBaseCharacteristic *c = arg;
    const char *str = lua_tostring(L, -1);
    for (int i = 0; i < HAPArrayCount(lhap_characteristic_type_tab);
        i++) {
        if (HAPStringAreEqual(str, lhap_characteristic_type_tab[i].name)) {
            c->characteristicType = lhap_characteristic_type_tab[i].type;
            c->debugDescription = lhap_characteristic_type_tab[i].debugDescription;
            return true;
        }
    }
    HAPLogError(&lhap_log, "%s: error type.", __func__);
    return false;
}

static bool
lhap_characteristic_mfg_desc_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return (*((char **)&((HAPBaseCharacteristic *)arg)->manufacturerDescription) =
        lc_new_str(L, -1)) ? true : false;
}

static bool
lhap_char_props_readable_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->readable = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_writable_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->writable = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_support_evt_notify_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->supportsEventNotification = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_hidden_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->hidden = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_read_req_admin_pms_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->readRequiresAdminPermissions = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_write_req_admin_pms_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->writeRequiresAdminPermissions = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_req_timed_write_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->requiresTimedWrite = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_support_auth_data_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->supportsAuthorizationData = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_ip_control_point_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->ip.controlPoint = lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_ip_support_write_resp_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->ip.supportsWriteResponse = lua_toboolean(L, -1);
    return true;
}

static const lc_table_kv lhap_char_props_ip_kvs[] = {
    {"controlPoint", LC_TBOOLEAN, lhap_char_props_ip_control_point_cb},
    {"supportsWriteResponse", LC_TBOOLEAN, lhap_char_props_ip_support_write_resp_cb},
    {NULL, LC_TNONE, NULL},
};

static bool
lhap_char_props_ip_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lc_traverse_table(L, -1, lhap_char_props_ip_kvs, arg);
}

static bool
lhap_char_props_ble_support_bc_notify_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->ble.supportsBroadcastNotification =
        lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_ble_support_disconn_notify_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->ble.supportsDisconnectedNotification =
        lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_ble_read_without_sec_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->ble.readableWithoutSecurity =
        lua_toboolean(L, -1);
    return true;
}

static bool
lhap_char_props_ble_write_without_sec_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    ((HAPCharacteristicProperties *)arg)->ble.writableWithoutSecurity =
        lua_toboolean(L, -1);
    return true;
}

static const lc_table_kv lhap_char_props_ble_kvs[] = {
    {
        "supportsBroadcastNotification",
        LC_TBOOLEAN,
        lhap_char_props_ble_support_bc_notify_cb
    },
    {
        "supportsDisconnectedNotification",
        LC_TBOOLEAN,
        lhap_char_props_ble_support_disconn_notify_cb
    },
    {
        "readableWithoutSecurity",
        LC_TBOOLEAN,
        lhap_char_props_ble_read_without_sec_cb
    },
    {
        "writableWithoutSecurity",
        LC_TBOOLEAN,
        lhap_char_props_ble_write_without_sec_cb
    },
    {NULL, LC_TNONE, NULL},
};

static bool
lhap_char_props_ble_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lc_traverse_table(L, -1, lhap_char_props_ble_kvs, arg);
}

static const lc_table_kv lhap_char_props_kvs[] = {
    {"readable", LC_TBOOLEAN, lhap_char_props_readable_cb},
    {"writable", LC_TBOOLEAN, lhap_char_props_writable_cb},
    {"supportsEventNotification", LC_TBOOLEAN, lhap_char_props_support_evt_notify_cb},
    {"hidden", LC_TBOOLEAN, lhap_char_props_hidden_cb},
    {"readRequiresAdminPermissions", LC_TBOOLEAN, lhap_char_props_read_req_admin_pms_cb},
    {"writeRequiresAdminPermissions", LC_TBOOLEAN, lhap_char_props_write_req_admin_pms_cb},
    {"requiresTimedWrite", LC_TBOOLEAN, lhap_char_props_req_timed_write_cb},
    {"supportsAuthorizationData", LC_TBOOLEAN, lhap_char_props_support_auth_data_cb},
    {"ip", LC_TTABLE, lhap_char_props_ip_cb},
    {"ble", LC_TTABLE, lhap_char_props_ble_cb},
    {NULL, LC_TNONE, NULL},
};

static bool
lhap_characteristic_props_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lc_traverse_table(L, -1, lhap_char_props_kvs,
        &((HAPBaseCharacteristic *)arg)->properties);
}

static bool
lhap_characteristic_units_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    HAPCharacteristicFormat format = ((HAPBaseCharacteristic *)arg)->format;
    if (format < kHAPCharacteristicFormat_UInt8 ||
        format > kHAPCharacteristicFormat_Float) {
        HAPLogError(&lhap_log, "%s: %s characteristic has no unit.",
            __func__, lhap_characteristic_format_strs[format]);
        return false;
    }
    const char *str = lua_tostring(L, -1);
    int idx = lhap_lookup_by_name(str, lhap_characteristic_units_strs,
        HAPArrayCount(lhap_characteristic_units_strs));
    if (idx == -1) {
        HAPLogError(&lhap_log, "%s: Failed to find the unit \"%s\""
            " in lhap_characteristic_units_strs", __func__, str);
        return false;
    }

    switch (format) {
    LHAP_CASE_CHAR_FORMAT_CODE(UInt8, arg, p->units = idx)
    LHAP_CASE_CHAR_FORMAT_CODE(UInt16, arg, p->units = idx)
    LHAP_CASE_CHAR_FORMAT_CODE(UInt32, arg, p->units = idx)
    LHAP_CASE_CHAR_FORMAT_CODE(UInt64, arg, p->units = idx)
    LHAP_CASE_CHAR_FORMAT_CODE(Int, arg, p->units = idx)
    LHAP_CASE_CHAR_FORMAT_CODE(Float, arg, p->units = idx)
    default:
        HAPAssertionFailure();
        break;
    }
    return true;
}

static bool
lhap_char_constraints_max_len_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    int isnum;
    lua_Integer num;
    HAPCharacteristicFormat format = ((HAPBaseCharacteristic *)arg)->format;
    switch (format) {
    LHAP_CASE_CHAR_FORMAT_CODE(String, arg,
        num = lua_tointegerx(L, -1, &isnum);
        p->constraints.maxLength = num;
    )
    LHAP_CASE_CHAR_FORMAT_CODE(Data, arg,
        num = lua_tointegerx(L, -1, &isnum);
        p->constraints.maxLength = num;
    )
    default:
        HAPLogError(&lhap_log, "%s: The constraints of the %s "
            "characteristic has no maxLength.",
            __func__, lhap_characteristic_format_strs[format]);
        return false;
    }
    if (!isnum) {
        HAPLogError(&lhap_log, "%s: Invalid maxLength", __func__);
        return false;
    }
    if (num < 0 || num > LHAP_UINT32_MAX) {
        HAPLogError(&lhap_log, "%s: maxLength is out of range(0, %u).",
            __func__, LHAP_UINT32_MAX);
        return false;
    }
    return true;
}

#define LHAP_CHAR_CONSTRAINTS_VAL_CB_CODE(member, is_unsigned) \
    int isnum; \
    lua_Integer num = 0; \
    HAPCharacteristicFormat format = ((HAPBaseCharacteristic *)arg)->format; \
    if (format >= kHAPCharacteristicFormat_UInt8 && \
        format <= kHAPCharacteristicFormat_Int) { \
        num = lhap_tointegerx(L, -1, &isnum, format, is_unsigned); \
    } \
    switch (format) { \
    LHAP_CASE_CHAR_FORMAT_CODE(UInt8, arg, p->constraints.member = num) \
    LHAP_CASE_CHAR_FORMAT_CODE(UInt16, arg, p->constraints.member = num) \
    LHAP_CASE_CHAR_FORMAT_CODE(UInt32, arg, p->constraints.member = num) \
    LHAP_CASE_CHAR_FORMAT_CODE(UInt64, arg, p->constraints.member = num) \
    LHAP_CASE_CHAR_FORMAT_CODE(Int, arg, p->constraints.member = num) \
    LHAP_CASE_CHAR_FORMAT_CODE(Float, arg, \
        p->constraints.member = lua_tonumberx(L, -1, &isnum)) \
    default: \
        HAPLogError(&lhap_log, "%s: The constraints of the %s " \
            "characteristic has no %s.", __func__, \
            lhap_characteristic_format_strs[format], #member); \
        return false; \
    } \
    if (!isnum) { \
        HAPLogError(&lhap_log, "%s: Invalid %s", __func__, #member); \
        return false; \
    } \
    return true;

static bool
lhap_char_constraints_min_val_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    LHAP_CHAR_CONSTRAINTS_VAL_CB_CODE(minimumValue, false)
}

static bool
lhap_char_constraints_max_val_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    LHAP_CHAR_CONSTRAINTS_VAL_CB_CODE(maximumValue, false)
}

static bool
lhap_char_constraints_step_val_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    LHAP_CHAR_CONSTRAINTS_VAL_CB_CODE(stepValue, true)
}

#undef LHAP_CHAR_CONSTRAINTS_VAL_CB_CODE

static bool
lhap_char_constraints_valid_vals_arr_cb(lua_State *L, size_t i, void *arg) {
    uint8_t **vals = arg;

    if (!lua_isnumber(L, -1)) {
        LHAP_LOG_TYPE_ERROR(L, "element of validValues",
            LUA_TNUMBER, lua_type(L, -1));
        return false;
    }

    int isnum;
    lua_Integer num = lhap_tointegerx(L, -1, &isnum, kHAPCharacteristicFormat_UInt8, true);
    if (!isnum) {
        return false;
    }

    uint8_t *pval = lc_malloc(sizeof(uint8_t));
    if (!pval) {
        HAPLogError(&lhap_log, "%s: Failed to alloc.", __func__);
        return false;
    }
    *pval = num;
    vals[i] = pval;
    return true;
}

static bool
lhap_char_constraints_valid_vals_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    HAPCharacteristicFormat format = ((HAPBaseCharacteristic *)arg)->format;
    switch (format) {
    LHAP_CASE_CHAR_FORMAT_CODE(UInt8, arg,
        uint8_t ***pValidValues = (uint8_t ***)&(p->constraints.validValues);
        lua_Unsigned len = lua_rawlen(L, -1);
        if (!len) {
            HAPLogError(&lhap_log, "%s: Invalid array.", __func__);
            return false;
        }
        uint8_t **vals = lc_calloc((len + 1) * sizeof(uint8_t *));
        if (!vals) {
            HAPLogError(&lhap_log, "%s: Failed to alloc.", __func__);
            return false;
        }
        if (!lc_traverse_array(L, -1, lhap_char_constraints_valid_vals_arr_cb,
            vals)) {
            HAPLogError(&lhap_log, "%s: Failed to parse validValues.", __func__);
            return false;
        }
        *pValidValues = vals;
    )
    default:
        HAPLogError(&lhap_log, "%s: The constraints of the %s "
            "characteristic has no validValues.",
            __func__, lhap_characteristic_format_strs[format]);
        return false;
    }
    return true;
}

static bool
lhap_char_constraints_valid_val_range_start_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    int isnum;
    ((HAPUInt8CharacteristicValidValuesRange *)arg)->start = lhap_tointegerx(L, -1,
        &isnum, kHAPCharacteristicFormat_UInt8, true);
    return isnum;
}

static bool
lhap_char_constraints_valid_val_range_end_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    int isnum;
    ((HAPUInt8CharacteristicValidValuesRange *)arg)->end = lhap_tointegerx(L, -1,
        &isnum, kHAPCharacteristicFormat_UInt8, true);
    return isnum;
}

static const lc_table_kv lhap_char_constraints_valid_val_range_kvs[] = {
    {"start", LC_TNUMBER, lhap_char_constraints_valid_val_range_start_cb},
    {"stop", LC_TNUMBER, lhap_char_constraints_valid_val_range_end_cb},
    {NULL, LC_TNONE, NULL},
};

static bool
lhap_char_constraints_valid_vals_ranges_arr_cb(lua_State *L, size_t i, void *arg) {
    HAPUInt8CharacteristicValidValuesRange **ranges = arg;

    if (!lua_istable(L, -1)) {
        LHAP_LOG_TYPE_ERROR(L, "element of validValuesRanges",
            LUA_TTABLE, lua_type(L, -1));
        return false;
    }
    HAPUInt8CharacteristicValidValuesRange *range =
        lc_calloc(sizeof(HAPUInt8CharacteristicValidValuesRange));
    if (!range) {
        HAPLogError(&lhap_log, "%s: Failed to alloc.", __func__);
        return false;
    }
    if (!lc_traverse_table(L, -1, lhap_char_constraints_valid_val_range_kvs, range)) {
        HAPLogError(&lhap_log, "%s: Failed to parse valid value range.", __func__);
        lc_free(range);
        return false;
    }
    ranges[i] = range;
    return true;
}

static bool
lhap_char_constraints_valid_vals_ranges_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    HAPCharacteristicFormat format = ((HAPBaseCharacteristic *)arg)->format;
    switch (format) {
    LHAP_CASE_CHAR_FORMAT_CODE(UInt8, arg,
        HAPUInt8CharacteristicValidValuesRange ***pValidValuesRanges =
            (HAPUInt8CharacteristicValidValuesRange ***)
            &(p->constraints.validValuesRanges);
        lua_Unsigned len = lua_rawlen(L, -1);
        if (!len) {
            HAPLogError(&lhap_log, "%s: Invalid array.", __func__);
            return false;
        }
        HAPUInt8CharacteristicValidValuesRange **ranges =
            lc_calloc((len + 1) * sizeof(HAPUInt8CharacteristicValidValuesRange *));
        if (!ranges) {
            HAPLogError(&lhap_log, "%s: Failed to alloc ranges.", __func__);
            return false;
        }
        if (!lc_traverse_array(L, -1, lhap_char_constraints_valid_vals_ranges_arr_cb,
            ranges)) {
            HAPLogError(&lhap_log, "%s: Failed to parse validValues.", __func__);
            return false;
        }
        *pValidValuesRanges = ranges;
    )
    default:
        HAPLogError(&lhap_log, "%s: The constraints of the %s "
            "characteristic has no validValues.",
            __func__, lhap_characteristic_format_strs[format]);
        return false;
    }
    return true;
}

static const lc_table_kv lhap_characteristic_constraints_kvs[] = {
    {"maxLen", LC_TNUMBER, lhap_char_constraints_max_len_cb},
    {"minVal", LC_TNUMBER, lhap_char_constraints_min_val_cb},
    {"maxVal", LC_TNUMBER, lhap_char_constraints_max_val_cb},
    {"stepVal", LC_TNUMBER, lhap_char_constraints_step_val_cb},
    {"validVals", LC_TTABLE, lhap_char_constraints_valid_vals_cb},
    {"validValsRanges", LC_TTABLE, lhap_char_constraints_valid_vals_ranges_cb},
    {NULL, LC_TNONE, NULL},
};

static bool
lhap_characteristic_constraints_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lc_traverse_table(L, -1, lhap_characteristic_constraints_kvs, arg);
}

static void
lhap_create_accessory_info_table(lua_State *L, int idx, const HAPAccessory *accessory) {
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, accessory->aid);
    lua_setfield(L, idx, "aid");
    lua_pushstring(L, lhap_accessory_category_strs[accessory->category]);
    lua_setfield(L, idx, "category");
    lua_pushstring(L, accessory->name);
    lua_setfield(L, idx, "name");
    lua_setfield(L, idx, "accessory");
}

static const char *lhap_get_service_type_str(const HAPUUID *type) {
    for (int i = 0; i < HAPArrayCount(lhap_service_type_tab); i++) {
        if (lhap_service_type_tab[i].type == type) {
            return lhap_service_type_tab[i].name;
        }
    }
    return NULL;
}

static void
lhap_create_service_info_table(lua_State *L, int idx, const HAPService *service) {
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, service->iid);
    lua_setfield(L, idx, "iid");
    lua_pushstring(L, lhap_get_service_type_str(service->serviceType));
    lua_setfield(L, idx, "type");
    lua_pushstring(L, service->name);
    lua_setfield(L, idx, "name");
    lua_setfield(L, idx, "service");
}

static const char *lhap_get_char_type_str(const HAPUUID *type) {
    for (int i = 0; i < HAPArrayCount(lhap_characteristic_type_tab); i++) {
        if (lhap_characteristic_type_tab[i].type == type) {
            return lhap_characteristic_type_tab[i].name;
        }
    }
    return NULL;
}

static void
lhap_create_char_info_table(lua_State *L, int idx, const HAPBaseCharacteristic *c) {
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, c->iid);
    lua_setfield(L, idx, "iid");
    lua_pushstring(L, lhap_characteristic_format_strs[c->format]);
    lua_setfield(L, idx, "format");
    lua_pushstring(L, lhap_get_char_type_str(c->characteristicType));
    lua_setfield(L, idx, "type");
    lua_setfield(L, idx, "characteristic");
}

static void
lhap_create_request_table(
        lua_State *L,
        HAPTransportType transportType,
        HAPSessionRef *session,
        const bool *const remote,
        const HAPAccessory *accessory,
        const HAPService *service,
        const HAPBaseCharacteristic *characteristic) {
    lua_createtable(L, 0, remote ? 5 : 4);
    lua_pushstring(L, lhap_transport_type_strs[transportType]);
    lua_setfield(L, -2, "transportType");
    if (remote) {
        lua_pushboolean(L, *remote);
        lua_setfield(L, -2, "remote");
    }
    lua_pushlightuserdata(L, session);
    lua_setfield(L, -2, "session");
    lhap_create_accessory_info_table(L, -2, accessory);
    lhap_create_service_info_table(L, -2, service);
    lhap_create_char_info_table(L, -2, characteristic);
}

static HAP_RESULT_USE_CHECK
HAPError lhap_char_base_handleRead(
        lua_State *L,
        HAPTransportType transportType,
        HAPSessionRef *session,
        const HAPAccessory *accessory,
        const HAPService *service,
        const HAPBaseCharacteristic *characteristic) {
    if (!lc_push_ref(L, LHAP_CHAR_REF(characteristic, characteristic->format)->handleRead)) {
        HAPLogError(&lhap_log, "%s: Failed to push callback.", __func__);
        return kHAPError_Unknown;
    }
    // set the table request
    lhap_create_request_table(L, transportType, session, NULL,
        accessory, service, characteristic);

    // set the context
    bool hasContext = lc_push_ref(L,
        LHAP_ACCESSORY_REF(accessory)->context);

    if (lua_pcall(L, hasContext ? 2 : 1, 2, 0)) {
        HAPLogError(&lhap_log, "%s: %s", __func__, lua_tostring(L, -1));
        return kHAPError_Unknown;
    }
    if (!lua_isnumber(L, -1)) {
        LHAP_LOG_TYPE_ERROR(L, "error code", LUA_TNUMBER, lua_type(L, -1));
        return kHAPError_Unknown;
    }
    return lua_tointeger(L, -1);
}

static HAP_RESULT_USE_CHECK
HAPError lhap_char_Data_handleRead(
        HAPAccessoryServerRef* server,
        const HAPDataCharacteristicReadRequest* request,
        void* valueBytes,
        size_t maxValueBytes,
        size_t* numValueBytes,
        void* _Nullable context) {
    lua_State *L = ((app_context *)context)->L;
    HAPError err = lhap_char_base_handleRead(L, request->transportType,
        request->session, request->accessory, request->service,
        (const HAPBaseCharacteristic *)request->characteristic);

    if (err != kHAPError_None) {
        goto end;
    }

    if (!lua_isstring(L, -2)) {
        LHAP_LOG_TYPE_ERROR(L, "value", LUA_TSTRING, lua_type(L, -2));
        goto end;
    }
    size_t len;
    const char *data = luaL_tolstring(L, -2, &len);
    if (len >= maxValueBytes) {
        HAPLogError(&lhap_log, "%s: value too long", __func__);
        goto end;
    }
    *numValueBytes = len;
    if (len) {
        HAPRawBufferCopyBytes(valueBytes, data, len);
    }

end:
    lua_settop(L, 0);
    lc_collectgarbage(L);
    return err;
}

static HAP_RESULT_USE_CHECK
HAPError lhap_char_Bool_handleRead(
        HAPAccessoryServerRef* server,
        const HAPBoolCharacteristicReadRequest* request,
        bool* value,
        void* _Nullable context) {
    lua_State *L = ((app_context *)context)->L;
    HAPError err = lhap_char_base_handleRead(L, request->transportType,
        request->session, request->accessory, request->service,
        (const HAPBaseCharacteristic *)request->characteristic);

    if (err != kHAPError_None) {
        goto end;
    }

    if (!lua_isboolean(L, -2)) {
        LHAP_LOG_TYPE_ERROR(L, "value", LUA_TBOOLEAN, lua_type(L, -2));
        goto end;
    }

    *value = lua_toboolean(L, -2);
end:
    lua_settop(L, 0);
    lc_collectgarbage(L);
    return err;
}

static HAP_RESULT_USE_CHECK
HAPError lhap_char_number_handleRead(
        lua_State *L,
        HAPTransportType transportType,
        HAPSessionRef *session,
        const HAPAccessory *accessory,
        const HAPService *service,
        const HAPBaseCharacteristic *characteristic,
        void *value) {
    HAPError err = lhap_char_base_handleRead(L, transportType, session,
        accessory, service, characteristic);

    if (err != kHAPError_None) {
        goto end;
    }

    if (!lua_isnumber(L, -2)) {
        LHAP_LOG_TYPE_ERROR(L, "value", LUA_TNUMBER, lua_type(L, -2));
        goto end;
    }

    switch (characteristic->format) {
    case kHAPCharacteristicFormat_UInt8:
        *((uint8_t *)value) = lua_tointeger(L, 1);
        break;
    case kHAPCharacteristicFormat_UInt16:
        *((uint16_t *)value) = lua_tointeger(L, 1);
        break;
    case kHAPCharacteristicFormat_UInt32:
        *((uint32_t *)value) = lua_tointeger(L, 1);
        break;
    case kHAPCharacteristicFormat_UInt64:
        *((uint64_t *)value) = lua_tointeger(L, 1);
        break;
    case kHAPCharacteristicFormat_Int:
        *((int32_t *)value) = lua_tointeger(L, 1);
        break;
    case kHAPCharacteristicFormat_Float:
        *((float *)value) = lua_tonumber(L, 1);
        break;
    default:
        HAPAssertionFailure();
        break;
    }

end:
    lua_settop(L, 0);
    lc_collectgarbage(L);
    return err;
}

#define LHAP_CHAR_NUMBER_HANDLE_READ(format, vtype) \
static HAP_RESULT_USE_CHECK \
HAPError lhap_char_ ## format ## _handleRead( \
        HAPAccessoryServerRef* server, \
        const HAP ## format ##CharacteristicReadRequest* request, \
        vtype* value, \
        void* _Nullable context) { \
    return lhap_char_number_handleRead(((app_context *)context)->L, \
        request->transportType, request->session, request->accessory, request->service, \
        (const HAPBaseCharacteristic *)request->characteristic, value); \
}

LHAP_CHAR_NUMBER_HANDLE_READ(UInt8, uint8_t)
LHAP_CHAR_NUMBER_HANDLE_READ(UInt16, uint16_t)
LHAP_CHAR_NUMBER_HANDLE_READ(UInt32, uint32_t)
LHAP_CHAR_NUMBER_HANDLE_READ(UInt64, uint64_t)
LHAP_CHAR_NUMBER_HANDLE_READ(Int, int32_t)
LHAP_CHAR_NUMBER_HANDLE_READ(Float, float)

#undef LHAP_CHAR_NUMBER_HANDLE_READ

static HAP_RESULT_USE_CHECK
HAPError lhap_char_String_handleRead(
        HAPAccessoryServerRef* server,
        const HAPStringCharacteristicReadRequest* request,
        char* value,
        size_t maxValueBytes,
        void* _Nullable context) {
    lua_State *L = ((app_context *)context)->L;
    HAPError err = lhap_char_base_handleRead(L, request->transportType,
        request->session, request->accessory, request->service,
        (const HAPBaseCharacteristic *)request->characteristic);

    if (err != kHAPError_None) {
        goto end;
    }

    if (!lua_isstring(L, -2)) {
        LHAP_LOG_TYPE_ERROR(L, "value", LUA_TSTRING, lua_type(L, -2));
        goto end;
    }

    size_t len;
    const char *str = lua_tolstring(L, -2, &len);
    if (len >= maxValueBytes) {
        HAPLogError(&lhap_log, "%s: value too long", __func__);
        goto end;
    }
    HAPRawBufferCopyBytes(value, str, len + 1);

end:
    lua_settop(L, 0);
    lc_collectgarbage(L);
    return err;
}

static HAP_RESULT_USE_CHECK
HAPError lhap_char_TLV8_handleRead(
        HAPAccessoryServerRef* server,
        const HAPTLV8CharacteristicReadRequest* request,
        HAPTLVWriterRef* responseWriter,
        void* _Nullable context) {
    lua_State *L = ((app_context *)context)->L;
    HAPError err = lhap_char_base_handleRead(L, request->transportType,
        request->session, request->accessory, request->service,
        (const HAPBaseCharacteristic *)request->characteristic);

    if (err != kHAPError_None) {
        goto end;
    }

    // TODO(KNpTrue): Implement TLV8 in lua.
    HAPAssertionFailure();

end:
    lua_settop(L, 0);
    lc_collectgarbage(L);
    return err;
}

static HAP_RESULT_USE_CHECK
HAPError lhap_char_base_handleWrite(
        lua_State *L,
        HAPTransportType transportType,
        HAPSessionRef *session,
        bool remote,
        const HAPAccessory *accessory,
        const HAPService *service,
        const HAPBaseCharacteristic *characteristic) {
    if (!lc_push_ref(L, LHAP_CHAR_REF(characteristic, characteristic->format)->handleWrite)) {
        HAPLogError(&lhap_log, "%s: Failed to push callback.", __func__);
        return kHAPError_Unknown;
    }
    lhap_create_request_table(L, transportType, session, &remote,
        accessory, service, characteristic);
    return kHAPError_None;
}

static HAP_RESULT_USE_CHECK
HAPError lhap_char_last_handleWrite(lua_State *L,
        HAPAccessoryServerRef* server,
        const HAPAccessory *accessory,
        const HAPService *service,
        const HAPBaseCharacteristic *characteristic) {
    HAPError err = kHAPError_Unknown;

    bool hasContext = lc_push_ref(L,
        LHAP_ACCESSORY_REF(accessory)->context);

    if (lua_pcall(L, hasContext ? 3 : 2, 2, 0)) {
        HAPLogError(&lhap_log, "%s: %s", __func__, lua_tostring(L, -1));
        goto end;
    }
    if (!lua_isnumber(L, -1)) {
        LHAP_LOG_TYPE_ERROR(L, "error code", LUA_TNUMBER, lua_type(L, -1));
        goto end;
    }
    err = lua_tointeger(L, -1);
    if (err != kHAPError_None) {
        goto end;
    }

    if (!lua_isboolean(L, -2)) {
        LHAP_LOG_TYPE_ERROR(L, "changed flag", LUA_TBOOLEAN, lua_type(L, -1));
        goto end;
    }

    if (lua_toboolean(L, -2)) {
        HAPAccessoryServerRaiseEvent(server, characteristic, service, accessory);
    }

end:
    return err;
}

static HAP_RESULT_USE_CHECK
HAPError lhap_char_Data_handleWrite(
        HAPAccessoryServerRef* server,
        const HAPDataCharacteristicWriteRequest* request,
        const void* valueBytes,
        size_t numValueBytes,
        void* _Nullable context) {
    lua_State *L = ((app_context *)context)->L;
    HAPError err = lhap_char_base_handleWrite(L, request->transportType,
        request->session, request->remote, request->accessory,
        request->service, (const HAPBaseCharacteristic *)request->characteristic);

    if (err != kHAPError_None) {
        goto end;
    }

    lua_pushlstring(L, valueBytes, numValueBytes);

    err = lhap_char_last_handleWrite(L, server, request->accessory,
        request->service, (const HAPBaseCharacteristic *)request->characteristic);

end:
    lua_settop(L, 0);
    lc_collectgarbage(L);
    return err;
}

static HAP_RESULT_USE_CHECK
HAPError lhap_char_Bool_handleWrite(
        HAPAccessoryServerRef* server,
        const HAPBoolCharacteristicWriteRequest* request,
        bool value,
        void* _Nullable context) {
    lua_State *L = ((app_context *)context)->L;
    HAPError err = lhap_char_base_handleWrite(L, request->transportType,
        request->session, request->remote, request->accessory,
        request->service, (const HAPBaseCharacteristic *)request->characteristic);

    if (err != kHAPError_None) {
        goto end;
    }

    lua_pushboolean(L, value);

    err = lhap_char_last_handleWrite(L, server, request->accessory,
        request->service, (const HAPBaseCharacteristic *)request->characteristic);

end:
    lua_settop(L, 0);
    lc_collectgarbage(L);
    return err;
}

HAP_RESULT_USE_CHECK
HAPError lhap_char_number_handleWrite(lua_State *L,
        HAPAccessoryServerRef* server,
        HAPTransportType transportType,
        HAPSessionRef *session,
        bool remote,
        const HAPAccessory *accessory,
        const HAPService *service,
        const HAPBaseCharacteristic *characteristic,
        void *value) {
    HAPError err = lhap_char_base_handleWrite(L, transportType, session,
        remote, accessory, service, characteristic);

    if (err != kHAPError_None) {
        goto end;
    }

    lua_Number num;
    switch (characteristic->format) {
    case kHAPCharacteristicFormat_UInt8:
        num = *((uint8_t *)value);
        break;
    case kHAPCharacteristicFormat_UInt16:
        num = *((uint16_t *)value);
        break;
    case kHAPCharacteristicFormat_UInt32:
        num = *((uint32_t *)value);
        break;
    case kHAPCharacteristicFormat_UInt64:
        num = *((uint64_t *)value);
        break;
    case kHAPCharacteristicFormat_Int:
        num = *((int32_t *)value);
        break;
    case kHAPCharacteristicFormat_Float:
        num = *((float *)value);
        break;
    default:
        HAPAssertionFailure();
        break;
    }
    lua_pushnumber(L, num);

    err = lhap_char_last_handleWrite(L, server, accessory,
        service, characteristic);

end:
    lua_settop(L, 0);
    lc_collectgarbage(L);
    return err;
}

#define LHAP_CHAR_NUMBER_HANDLE_WRITE(format, vtype) \
HAP_RESULT_USE_CHECK \
HAPError lhap_char_ ## format ## _handleWrite( \
        HAPAccessoryServerRef* server, \
        const HAP ## format ## CharacteristicWriteRequest* request, \
        vtype value, \
        void* _Nullable context) { \
    return lhap_char_number_handleWrite(((app_context *)context)->L, \
        server, request->transportType, request->session, request->remote, \
        request->accessory, request->service, \
        (const HAPBaseCharacteristic *)request->characteristic, &value); \
}

LHAP_CHAR_NUMBER_HANDLE_WRITE(UInt8, uint8_t)
LHAP_CHAR_NUMBER_HANDLE_WRITE(UInt16, uint16_t)
LHAP_CHAR_NUMBER_HANDLE_WRITE(UInt32, uint32_t)
LHAP_CHAR_NUMBER_HANDLE_WRITE(UInt64, uint64_t)
LHAP_CHAR_NUMBER_HANDLE_WRITE(Int, int32_t)
LHAP_CHAR_NUMBER_HANDLE_WRITE(Float, float)

#undef LHAP_CHAR_NUMBER_HANDLE_WRITE

static HAP_RESULT_USE_CHECK
HAPError lhap_char_String_handleWrite(
        HAPAccessoryServerRef* server,
        const HAPStringCharacteristicWriteRequest* request,
        const char* value,
        void* _Nullable context) {
    lua_State *L = ((app_context *)context)->L;
    HAPError err = lhap_char_base_handleWrite(L, request->transportType,
        request->session, request->remote, request->accessory,
        request->service, (const HAPBaseCharacteristic *)request->characteristic);

    if (err != kHAPError_None) {
        goto end;
    }

    lua_pushstring(L, value);

    err = lhap_char_last_handleWrite(L, server, request->accessory,
        request->service, (const HAPBaseCharacteristic *)request->characteristic);

end:
    lua_settop(L, 0);
    lc_collectgarbage(L);
    return err;
}

static HAP_RESULT_USE_CHECK
HAPError lhap_char_TLV8_handleWrite(
        HAPAccessoryServerRef* server,
        const HAPTLV8CharacteristicWriteRequest* request,
        HAPTLVReaderRef* requestReader,
        void* _Nullable context) {
    lua_State *L = ((app_context *)context)->L;
    HAPError err = lhap_char_base_handleWrite(L, request->transportType,
        request->session, request->remote, request->accessory,
        request->service, (const HAPBaseCharacteristic *)request->characteristic);

    if (err != kHAPError_None) {
        goto end;
    }

    // TODO(KNpTrue): Implement TLV8 in lua.
    HAPAssertionFailure();

    err = lhap_char_last_handleWrite(L, server, request->accessory,
        request->service, (const HAPBaseCharacteristic *)request->characteristic);

end:
    lua_settop(L, 0);
    lc_collectgarbage(L);
    return err;
}

// This definitation is only used to register characteristic callbacks.
#define LHAP_CASE_CHAR_REGISTER_CB(format, cb) \
LHAP_CASE_CHAR_FORMAT_CODE(format, arg, \
    if (!lhap_ref_var(L, -1, &(LHAP_CHAR_REF(p, \
        kHAPCharacteristicFormat_ ## format)->cb))) { \
        HAPLogError(&lhap_log, "%s: Failed to ref cb", __func__); \
        return false; \
    } \
    p->callbacks.cb = lhap_char_ ## format ## _ ## cb)

static bool
lhap_char_callbacks_handle_read_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
#define LHAP_CASE_CHAR_REGISTER_READ_CB(format) \
    LHAP_CASE_CHAR_REGISTER_CB(format, handleRead)

    switch (((HAPBaseCharacteristic *)arg)->format) {
        LHAP_CASE_CHAR_REGISTER_READ_CB(Data)
        LHAP_CASE_CHAR_REGISTER_READ_CB(Bool)
        LHAP_CASE_CHAR_REGISTER_READ_CB(UInt8)
        LHAP_CASE_CHAR_REGISTER_READ_CB(UInt16)
        LHAP_CASE_CHAR_REGISTER_READ_CB(UInt32)
        LHAP_CASE_CHAR_REGISTER_READ_CB(UInt64)
        LHAP_CASE_CHAR_REGISTER_READ_CB(Int)
        LHAP_CASE_CHAR_REGISTER_READ_CB(Float)
        LHAP_CASE_CHAR_REGISTER_READ_CB(String)
        LHAP_CASE_CHAR_REGISTER_READ_CB(TLV8)
    }

#undef LHAP_CASE_CHAR_REGISTER_READ_CB

    return true;
}

static bool
lhap_char_callbacks_handle_write_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
#define LHAP_CASE_CHAR_REGISTER_WRITE_CB(format) \
    LHAP_CASE_CHAR_REGISTER_CB(format, handleWrite)

    switch (((HAPBaseCharacteristic *)arg)->format) {
        LHAP_CASE_CHAR_REGISTER_WRITE_CB(Data)
        LHAP_CASE_CHAR_REGISTER_WRITE_CB(Bool)
        LHAP_CASE_CHAR_REGISTER_WRITE_CB(UInt8)
        LHAP_CASE_CHAR_REGISTER_WRITE_CB(UInt16)
        LHAP_CASE_CHAR_REGISTER_WRITE_CB(UInt32)
        LHAP_CASE_CHAR_REGISTER_WRITE_CB(UInt64)
        LHAP_CASE_CHAR_REGISTER_WRITE_CB(Int)
        LHAP_CASE_CHAR_REGISTER_WRITE_CB(Float)
        LHAP_CASE_CHAR_REGISTER_WRITE_CB(String)
        LHAP_CASE_CHAR_REGISTER_WRITE_CB(TLV8)
    }

#undef LHAP_CASE_CHAR_REGISTER_WRITE_CB

    return true;
}

static bool
lhap_char_callbacks_handle_sub_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return false;
}

static bool
lhap_char_callbacks_handle_unsub_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return false;
}

#undef LHAP_CASE_CHAR_REGISTER_CB

static const lc_table_kv lhap_characteristic_cbs_kvs[] = {
    {"read", LC_TFUNCTION, lhap_char_callbacks_handle_read_cb},
    {"write", LC_TFUNCTION, lhap_char_callbacks_handle_write_cb},
    {"sub", LC_TFUNCTION, lhap_char_callbacks_handle_sub_cb},
    {"unsub", LC_TFUNCTION, lhap_char_callbacks_handle_unsub_cb},
    {NULL, LC_TNONE, NULL},
};

static bool
lhap_characteristic_cbs_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lc_traverse_table(L, -1, lhap_characteristic_cbs_kvs, arg);
}

static const lc_table_kv lhap_characteristic_kvs[] = {
    {"format", LC_TSTRING, NULL},
    {"iid", LC_TNUMBER, lhap_characteristic_iid_cb},
    {"type", LC_TSTRING, lhap_characteristic_type_cb},
    {"mfgDesc", LC_TSTRING, lhap_characteristic_mfg_desc_cb},
    {"props", LC_TTABLE, lhap_characteristic_props_cb},
    {"units", LC_TSTRING, lhap_characteristic_units_cb},
    {"constraints", LC_TTABLE, lhap_characteristic_constraints_cb},
    {"cbs", LC_TTABLE, lhap_characteristic_cbs_cb},
    {NULL, LC_TNONE, NULL},
};

static bool lhap_service_characteristics_arr_cb(lua_State *L, size_t i, void *arg) {
    HAPCharacteristic **characteristics = arg;

    if (!lua_istable(L, -1)) {
        return false;
    }

    lua_pushstring(L, "format");
    lua_gettable(L, -2);
    if (!lua_isstring(L, -1)) {
        LHAP_LOG_TYPE_ERROR(L, "format", LUA_TSTRING, lua_type(L, -1));
        return false;
    }
    int format = lhap_lookup_by_name(lua_tostring(L, -1),
        lhap_characteristic_format_strs,
        HAPArrayCount(lhap_characteristic_format_strs));
    lua_pop(L, 1);
    if (format == -1) {
        HAPLogError(&lhap_log, "%s: Invalid format.", __func__);
        return false;
    }

    HAPCharacteristic *c = lc_calloc(lhap_characteristic_struct_size[format] + sizeof(char_ref));
    if (!c) {
        HAPLogError(&lhap_log, "%s: Failed to alloc memory.", __func__);
        return false;
    }
    LHAP_CHAR_REF_INIT(c, format);
    ((HAPBaseCharacteristic *)c)->format = format;
    characteristics[i] = c;
    if (!lc_traverse_table(L, -1, lhap_characteristic_kvs, c)) {
        HAPLogError(&lhap_log, "%s: Failed to parse characteristic.", __func__);
        return false;
    }
    return true;
}

static void lhap_reset_characteristic(lua_State *L, HAPCharacteristic *characteristic) {
    HAPCharacteristicFormat format =
        ((HAPBaseCharacteristic *)characteristic)->format;
    lc_safe_free(((HAPBaseCharacteristic *)characteristic)->manufacturerDescription);
    switch (format) {
    LHAP_CASE_CHAR_FORMAT_CODE(UInt8, characteristic,
        if (p->constraints.validValues) {
            for (uint8_t **pval = (uint8_t **)p->constraints.validValues;
                *pval; pval++) {
                lc_free(*pval);
            }
            lc_safe_free(p->constraints.validValues);
        }
        if (p->constraints.validValuesRanges) {
            for (HAPUInt8CharacteristicValidValuesRange **prange =
                (HAPUInt8CharacteristicValidValuesRange **)
                p->constraints.validValuesRanges;
                *prange; prange++) {
                lc_free(*prange);
            }
            lc_safe_free(p->constraints.validValuesRanges);
        }
    )
    default:
        break;
    }
    lc_unref(L, LHAP_CHAR_REF(characteristic, format)->handleRead);
    lc_unref(L, LHAP_CHAR_REF(characteristic, format)->handleWrite);
    lc_unref(L, LHAP_CHAR_REF(characteristic, format)->handleSubscribe);
    lc_unref(L, LHAP_CHAR_REF(characteristic, format)->handleUnsubscribe);
}

static bool
lhap_service_chars_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    HAPService *service = arg;
    HAPCharacteristic ***pcharacteristic = (HAPCharacteristic ***)&(service->characteristics);
    // Get the array length.
    lua_Unsigned len = lua_rawlen(L, -1);
    if (len == 0) {
        *pcharacteristic = NULL;
        return true;
    }

    HAPCharacteristic **characteristics = lc_calloc((len + 1) * sizeof(HAPCharacteristic *));
    if (!characteristics) {
        HAPLogError(&lhap_log, "%s: Failed to alloc memory.", __func__);
        return false;
    }
    if (!lc_traverse_array(L, -1, lhap_service_characteristics_arr_cb, characteristics)) {
        HAPLogError(&lhap_log, "%s: Failed to parse characteristics.", __func__);
        for (HAPCharacteristic **c = characteristics; *c; c++) {
            lhap_reset_characteristic(L, *c);
            lc_free(*c);
        }
        lc_safe_free(characteristics);
        return false;
    }
    *pcharacteristic = characteristics;
    return true;
}

static const lc_table_kv lhap_service_kvs[] = {
    {"iid", LC_TNUMBER, lhap_service_iid_cb},
    {"type", LC_TSTRING, lhap_service_type_cb},
    {"name", LC_TSTRING, lhap_service_name_cb},
    {"props", LC_TTABLE, lhap_service_props_cb},
    {"chars", LC_TTABLE, lhap_service_chars_cb},
    {NULL, LC_TNONE, NULL},
};

static void lhap_reset_service(lua_State *L, HAPService *service) {
    lc_safe_free(service->name);
    if (service->characteristics) {
        for (HAPCharacteristic **c = (HAPCharacteristic **)service->characteristics; *c; c++) {
            lhap_reset_characteristic(L, *c);
            lc_free(*c);
        }
        lc_safe_free(service->characteristics);
    }
}

static bool
lhap_accessory_services_arr_cb(lua_State *L, size_t i, void *arg) {
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
    HAPService *s = lc_calloc(sizeof(HAPService));
    if (!s) {
        HAPLogError(&lhap_log, "%s: Failed to alloc memory.", __func__);
        return false;
    }
    services[i] = s;
    if (!lc_traverse_table(L, -1, lhap_service_kvs, s)) {
        HAPLogError(&lhap_log, "%s: Failed to parse service.", __func__);
        return false;
    }
    return true;
}

static bool
lhap_accessory_services_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    HAPAccessory *accessory = arg;
    HAPService ***pservices = (HAPService ***)&(accessory->services);

    // Get the array length.
    lua_Unsigned len = lua_rawlen(L, -1);
    if (len == 0) {
        *pservices = NULL;
        return true;
    }

    HAPService **services = lc_calloc((len + 1) * sizeof(HAPService *));
    if (!services) {
        HAPLogError(&lhap_log, "%s: Failed to alloc memory.", __func__);
        return false;
    }

    if (!lc_traverse_array(L, -1, lhap_accessory_services_arr_cb, services)) {
        HAPLogError(&lhap_log, "%s: Failed to parse services.", __func__);
        for (HAPService **s = services; *s; s++) {
            if (!lhap_service_is_light_userdata(*s)) {
                lhap_reset_service(L, *s);
                lc_free(*s);
            }
        }
        lc_safe_free(services);
        return false;
    }

    *pservices = services;
    return true;
}

static HAP_RESULT_USE_CHECK
HAPError lhap_accessory_identify_cb(
        HAPAccessoryServerRef* server HAP_UNUSED,
        const HAPAccessoryIdentifyRequest* request,
        void* _Nullable context) {
    HAPPrecondition(server);
    HAPPrecondition(request);
    HAPPrecondition(context);

    HAPError err = kHAPError_Unknown;
    lua_State *L = ((app_context *)context)->L;
    const HAPAccessory *accessory = request->accessory;

    // push the callback
    if (!lc_push_ref(L, LHAP_ACCESSORY_REF(accessory)->identify)) {
        HAPLogError(&lhap_log, "%s: Can't get lua function.", __func__);
        return err;
    }

    // set the table request
    lua_createtable(L, 0, 2);
    lua_pushstring(L, lhap_transport_type_strs[request->transportType]);
    lua_setfield(L, -3, "transportType");
    lua_pushboolean(L, request->remote);
    lua_setfield(L, -3, "remote");
    lua_pushlightuserdata(L, request->session);
    lua_setfield(L, -3, "session");
    lhap_create_accessory_info_table(L, -3, request->accessory);

    // push the context
    bool hasContext = lc_push_ref(L,
        LHAP_ACCESSORY_REF(accessory)->context);

    if (lua_pcall(L, hasContext ? 2 : 1, 1, 0)) {
        HAPLogError(&lhap_log, "%s: %s", __func__, lua_tostring(L, -1));
        goto end;
    }

    if (!lua_isnumber(L, -1)) {
        LHAP_LOG_TYPE_ERROR(L, "error code", LUA_TNUMBER, lua_type(L, -1));
        goto end;
    }

    err = lua_tointeger(L, -1);
end:
    lua_settop(L, 0);
    lc_collectgarbage(L);
    return err;
}

static bool
lhap_accessory_cbs_identify_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    if (!lhap_ref_var(L, -1, &(LHAP_ACCESSORY_REF(arg)->identify))) {
        HAPLogError(&lhap_log, "%s: Failed to ref identify cb.", __func__);
        return false;
    }
    ((HAPAccessory *)arg)->callbacks.identify = lhap_accessory_identify_cb;
    return true;
}

static lc_table_kv lhap_accessory_cbs_kvs[] = {
    {"identify", LC_TFUNCTION, lhap_accessory_cbs_identify_cb},
    {NULL, LC_TNONE, NULL},
};

static bool
lhap_accessory_cbs_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lc_traverse_table(L, -1, lhap_accessory_cbs_kvs, arg);
}

static bool
lhap_accesory_context_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lhap_ref_var(L, -1, &(LHAP_ACCESSORY_REF(arg)->context));
}

static const lc_table_kv lhap_accessory_kvs[] = {
    {"aid", LC_TNUMBER, lhap_accessory_aid_cb},
    {"category", LC_TSTRING, lhap_accessory_category_cb},
    {"name", LC_TSTRING, lhap_accessory_name_cb},
    {"mfg", LC_TSTRING, lhap_accessory_mfg_cb},
    {"model", LC_TSTRING, lhap_accessory_model_cb},
    {"sn", LC_TSTRING, lhap_accessory_sn_cb},
    {"fwVer", LC_TSTRING, lhap_accessory_fw_ver_cb},
    {"hwVer", LC_TSTRING, lhap_accessory_hw_ver_cb},
    {"services", LC_TTABLE, lhap_accessory_services_cb},
    {"cbs", LC_TTABLE, lhap_accessory_cbs_cb},
    {"context", LC_TTABLE, lhap_accesory_context_cb},
    {NULL, LC_TNONE, NULL},
};

static void lhap_reset_accessory(lua_State *L, HAPAccessory *accessory) {
    lc_safe_free(accessory->name);
    lc_safe_free(accessory->manufacturer);
    lc_safe_free(accessory->model);
    lc_safe_free(accessory->serialNumber);
    lc_safe_free(accessory->firmwareVersion);
    lc_safe_free(accessory->hardwareVersion);
    if (accessory->services) {
        for (HAPService **s = (HAPService **)accessory->services; *s; s++) {
            if (!lhap_service_is_light_userdata(*s)) {
                lhap_reset_service(L, *s);
                lc_free(*s);
            }
        }
        lc_safe_free(accessory->services);
    }
    lc_unref(L, LHAP_ACCESSORY_REF(accessory)->context);
    lc_unref(L, LHAP_ACCESSORY_REF(accessory)->identify);
}

static bool
lhap_accessories_arr_cb(lua_State *L, size_t i, void *arg) {
    HAPAccessory **accessories = arg;
    if (!lua_istable(L, -1)) {
        HAPLogError(&lhap_log,
            "%s: The type of the element is not table.", __func__);
        return false;
    }
    HAPAccessory *a = lc_calloc(sizeof(HAPAccessory) + sizeof(accessory_ref));
    if (!a) {
        HAPLogError(&lhap_log,
            "%s: Failed to alloc memory.", __func__);
        return false;
    }
    LHAP_ACCESSORY_REF_INIT(a);
    accessories[i] = a;
    if (!lc_traverse_table(L, -1, lhap_accessory_kvs, a)) {
        HAPLogError(&lhap_log,
            "%s: Failed to generate accessory structure from table accessory.",
            __func__);
        return false;
    }
    return true;
}

static bool
lhap_server_cb_update_state_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lhap_ref_var(L, -1, gv_lhap_desc.server_cb_ref_ids +
        LHAP_SERVER_CB_UPDATE_STATE);
}

static bool
lhap_server_cb_session_accept_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lhap_ref_var(L, -1, gv_lhap_desc.server_cb_ref_ids +
        LHAP_SERVER_CB_SESSION_ACCEPT);
}

static bool
lhap_server_cb_session_invalid_cb(lua_State *L, const lc_table_kv *kv, void *arg) {
    return lhap_ref_var(L, -1, gv_lhap_desc.server_cb_ref_ids +
        LHAP_SERVER_CB_SESSION_INVALIDATE);
}

static const lc_table_kv lhap_server_callbacks_kvs[] = {
    {"updatedState", LC_TFUNCTION, lhap_server_cb_update_state_cb},
    {"sessionAccept", LC_TFUNCTION, lhap_server_cb_session_accept_cb},
    {"sessionInvalidate", LC_TFUNCTION, lhap_server_cb_session_invalid_cb},
    {NULL, LC_TNONE, NULL},
};

static void lhap_unref_server_cb(lua_State *L) {
    for (int i = 0; i < HAPArrayCount(gv_lhap_desc.server_cb_ref_ids); i++) {
        lc_unref(L, gv_lhap_desc.server_cb_ref_ids[i]);
        gv_lhap_desc.server_cb_ref_ids[i] = LUA_REFNIL;
    }
}

/**
 * configure(primaryAccessory: table, bridgedAccessories?: table,
 * serverCallbacks: table, confChanged: boolean) -> boolean
 *
 * If the category of the accessory is bridge, the parameters
 * bridgedAccessories is valid.
 */
static int lhap_configure(lua_State *L) {
    lua_Unsigned len = 0;
    struct lhap_desc *desc = &gv_lhap_desc;

    if (desc->is_configure) {
        HAPLogError(&lhap_log,
            "%s: HAP is already configured.", __func__);
        goto err;
    }

    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TTABLE);
    luaL_checktype(L, 4, LUA_TBOOLEAN);

    HAPAccessory *accessory = lc_calloc(sizeof(HAPAccessory) + sizeof(accessory_ref));
    if (!accessory) {
        HAPLogError(&lhap_log, "%s: Failed to alloc memory.", __func__);
        goto err;
    }
    LHAP_ACCESSORY_REF_INIT(accessory);
    desc->primary_acc = accessory;
    if (!lc_traverse_table(L, 1, lhap_accessory_kvs, accessory)) {
        HAPLogError(&lhap_log,
            "%s: Failed to generate accessory structure from table accessory.",
            __func__);
        goto err1;
    }

    if (accessory->aid != 1) {
        HAPLogError(&lhap_log, "Primary accessory must have aid 1.");
        goto err1;
    }

    if (accessory->category != kHAPAccessoryCategory_Bridges) {
        goto parse_cbs;
    }

    len = lua_rawlen(L, 2);
    if (len == 0) {
        goto parse_cbs;
    }

    desc->bridged_accs =
        lc_calloc((len + 1) * sizeof(HAPAccessory *));
    if (!desc->bridged_accs) {
        HAPLogError(&lhap_log, "%s: Failed to alloc memory.", __func__);
        goto err1;
    }

    if (!lc_traverse_array(L, 2, lhap_accessories_arr_cb,
        desc->bridged_accs)) {
        HAPLogError(&lhap_log,
            "%s: Failed to generate bridged accessories structureies"
            " from table bridgedAccessories.", __func__);
        goto err2;
    }
    for (HAPAccessory **pa = desc->bridged_accs; *pa != NULL; pa++) {
        if (!HAPBridgedAccessoryIsValid(*pa)) {
            goto err2;
        }
    }
    desc->conf_changed = lua_toboolean(L, 4);

parse_cbs:
    if (!lc_traverse_table(L, 3, lhap_server_callbacks_kvs, NULL)) {
        HAPLogError(&lhap_log,
            "%s: Failed to parse the server callbacks from table serverCallbacks.",
            __func__);
        goto err3;
    }

    HAPLogInfo(&lhap_log,
        "Accessory \"%s\": %s has been configured.", accessory->name,
        lhap_accessory_category_strs[accessory->category]);
    if (len) {
        HAPLogInfo(&lhap_log,
            "%" LUA_INTEGER_FRMLEN "u"
            " bridged accessories have been configured.", len);
    }
    desc->is_configure = true;
    lua_pushboolean(L, true);
    return 1;

err3:
    lhap_unref_server_cb(L);
err2:
    if (desc->bridged_accs) {
        for (HAPAccessory **pa = desc->bridged_accs; *pa != NULL; pa++) {
            lhap_reset_accessory(L, *pa);
            lc_free(*pa);
        }
        lc_safe_free(desc->bridged_accs);
    }
err1:
    lhap_reset_accessory(L, accessory);
    lc_safe_free(desc->primary_acc);
err:
    lua_pushboolean(L, false);
    return 1;
}

int lhap_unconfigure(lua_State *L) {
    struct lhap_desc *desc = &gv_lhap_desc;

    if (desc->bridged_accs) {
        for (HAPAccessory **pa = desc->bridged_accs; *pa != NULL; pa++) {
            lhap_reset_accessory(L, *pa);
            lc_free(*pa);
        }
        lc_safe_free(desc->bridged_accs);
    }
    if (desc->primary_acc) {
        lhap_reset_accessory(L, desc->primary_acc);
        lc_safe_free(desc->primary_acc);
    }
    lhap_unref_server_cb(L);
    desc->attribute_cnt = kAttributeCount;
    desc->bridged_aid = 1;
    desc->iid = kAttributeCount + 1;
    desc->conf_changed = false;
    desc->is_configure = false;
    desc->server = NULL;
    return 0;
}

/**
 * raiseEvent(accessoryIID:integer, serviceIID:integer, characteristicIID:integer) -> boolean
 */
static int lhap_raise_event(lua_State *L) {
    lua_Integer iid;
    HAPSessionRef *session = NULL;
    struct lhap_desc *desc = &gv_lhap_desc;

    if (!desc->is_configure) {
        HAPLogError(&lhap_log,
            "%s: Please configure first.", __func__);
        goto err;
    }
    if (!desc->server) {
        HAPLogError(&lhap_log,
            "%s: Please set server first.", __func__);
        goto err;
    }

    luaL_checktype(L, 1, LUA_TNUMBER);
    luaL_checktype(L, 2, LUA_TNUMBER);
    luaL_checktype(L, 3, LUA_TNUMBER);
    if (!lua_isnone(L, 4)) {
        luaL_checktype(L, 4, LUA_TLIGHTUSERDATA);
        session = lua_touserdata(L, 4);
    }

    HAPAccessory *a = NULL;
    iid = lua_tointeger(L, 1);
    if (desc->primary_acc->aid == iid) {
        a = desc->primary_acc;
        goto service;
    }
    if (!desc->bridged_accs) {
        goto err;
    }
    for (HAPAccessory **pa = desc->bridged_accs; *pa != NULL; pa++) {
        if ((*pa)->aid == iid) {
            a = *pa;
            goto service;
        }
    }
    goto err;

service:
    if (!a->services) {
        goto err;
    }
    HAPService *s = NULL;
    iid = lua_tointeger(L, 2);
    for (HAPService **ps = (HAPService **)a->services; *ps; ps++) {
        if ((*ps)->iid == iid) {
            s = *ps;
            goto characteristic;
        }
    }
    goto err;

characteristic:
    if (!s->characteristics) {
        goto err;
    }
    HAPCharacteristic *c = NULL;
    iid = lua_tointeger(L, 3);
    for (HAPCharacteristic **pc = (HAPCharacteristic **)s->characteristics; *pc; pc++) {
        if ((*(HAPBaseCharacteristic **)pc)->iid == iid) {
            c = *pc;
            break;
        }
    }
    if (!c) {
        goto err;
    }

    if (session) {
        HAPAccessoryServerRaiseEventOnSession(desc->server, c, s, a, session);
    } else {
        HAPAccessoryServerRaiseEvent(desc->server, c, s, a);
    }

    lua_pushboolean(L, true);
    return 1;

err:
    lua_pushboolean(L, false);
    return 1;
}

static int lhap_get_new_bridged_aid(lua_State *L) {
    lua_pushinteger(L, gv_lhap_desc.bridged_aid++);
    return 1;
}

static int lhap_get_new_iid(lua_State *L) {
    lua_pushinteger(L, gv_lhap_desc.iid++);
    return 1;
}

static const luaL_Reg haplib[] = {
    {"configure", lhap_configure},
    {"unconfigure", lhap_unconfigure},
    {"raiseEvent", lhap_raise_event},
    {"getNewBridgedAccessoryID", lhap_get_new_bridged_aid},
    {"getNewInstanceID", lhap_get_new_iid},
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
    lc_create_enum_table(L, lhap_error_strs,
        HAPArrayCount(lhap_error_strs));
    lua_setfield(L, -2, "Error");

    /* set services */
    for (const lhap_lightuserdata *ud = lhap_accessory_services_userdatas;
        ud->ptr; ud++) {
        lua_pushlightuserdata(L, ud->ptr);
        lua_setfield(L, -2, ud->name);
    }

    for (size_t i = 0; i < HAPArrayCount(gv_lhap_desc.server_cb_ref_ids); i++) {
        gv_lhap_desc.server_cb_ref_ids[i] = LUA_REFNIL;
    }
    return 1;
}

lhap_conf lhap_get_conf(void) {
    HAPAssert(gv_lhap_desc.is_configure);
    lhap_conf conf = {
        .primary_acc = gv_lhap_desc.primary_acc,
        .bridge_accs = (const HAPAccessory *const *)gv_lhap_desc.bridged_accs,
        .conf_changed = gv_lhap_desc.conf_changed,
    };
    return conf;
}

size_t lhap_get_attribute_count(void) {
    if (gv_lhap_desc.is_configure) {
        return gv_lhap_desc.attribute_cnt;
    }
    return 0;
}

void lhap_set_server(HAPAccessoryServerRef *server) {
    gv_lhap_desc.server = server;
}

static inline bool lhap_push_server_cb(lua_State *L, enum lhap_server_cb_idx idx) {
    return lc_push_ref(L, gv_lhap_desc.server_cb_ref_ids[idx]);
}

void lhap_server_handle_update_state(lua_State *L, HAPAccessoryServerState state) {
    HAPPrecondition(L);
    if (!lhap_push_server_cb(L, LHAP_SERVER_CB_UPDATE_STATE)) {
        return;
    }

    lua_pushstring(L, lhap_server_state_strs[state]);
    if (lua_pcall(L, 1, 0, 0)) {
        HAPLogError(&lhap_log, "%s: %s", __func__, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void lhap_server_handle_session_accept(lua_State *L) {
    HAPPrecondition(L);
    if (!lhap_push_server_cb(L, LHAP_SERVER_CB_SESSION_ACCEPT)) {
        return;
    }

    if (lua_pcall(L, 0, 0, 0)) {
        HAPLogError(&lhap_log, "%s: %s", __func__, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void lhap_server_handle_session_invalidate(lua_State *L) {
    HAPPrecondition(L);
    if (!lhap_push_server_cb(L, LHAP_SERVER_CB_SESSION_INVALIDATE)) {
        return;
    }

    if (lua_pcall(L, 0, 0, 0)) {
        HAPLogError(&lhap_log, "%s: %s", __func__, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}
