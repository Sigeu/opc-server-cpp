#include <open62541/config.h>
#ifdef UA_LOGLEVEL
#undef UA_LOGLEVEL
#endif
#define UA_LOGLEVEL 200
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "include/httplib.h"
#include <windows.h>
#include <variant>
#include <nlohmann/json.hpp>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/log_stdout.h>

using namespace std;
using namespace httplib;
using namespace nlohmann;

// 声明并初始化服务器配置
UA_ServerConfig *serverCfg = nullptr;

// 声明并初始化服务器对象
UA_Server *opcServer = nullptr;

// 声明传感器空间索引
UA_UInt16 sensorNsIndex;

// 更新变量值
void updateVariable(int sensorId, UA_StatusCode status, UA_Variant value)
{
  // 获取变量节点ID
  UA_NodeId nodeId = UA_NODEID_NUMERIC(sensorNsIndex, sensorId);

  // 写入变量的数值
  auto rv = UA_Server_writeValue(opcServer, nodeId, value);
  const char *mv = rv == UA_STATUSCODE_GOOD ? "写入数值成功" : "写入数值失败";
  UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, mv);

  if (status != UA_STATUSCODE_GOOD)
  {
    // 声明并初始化写入值
    UA_WriteValue wv;
    UA_WriteValue_init(&wv);

    // 设定写入值的节点ID和属性类型
    wv.nodeId = nodeId;
    wv.attributeId = UA_ATTRIBUTEID_VALUE;

    // 写入变量的状态
    wv.value.hasStatus = true;
    wv.value.status = status;
    auto rs = UA_Server_write(opcServer, &wv);
    const char *ms = rs == UA_STATUSCODE_GOOD ? "写入状态成功" : "写入状态失败";
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, ms);
  }
}

// 声明设备空间索引
UA_UInt16 deviceNsIndex;

// 创建OPC传感器变量
UA_StatusCode createSensorVariable(int id, int pid, const char *name, const char *desc, UA_Variant value)
{
  UA_VariableAttributes vAttr = UA_VariableAttributes_default;
  vAttr.description = UA_LOCALIZEDTEXT_ALLOC("zh-CN", desc);
  vAttr.displayName = UA_LOCALIZEDTEXT_ALLOC("zh-CN", name);
  vAttr.accessLevel = UA_ACCESSLEVELMASK_READ;
  vAttr.valueRank = UA_VALUERANK_SCALAR;
  vAttr.dataType = value.type->typeId;
  vAttr.value = value;

  UA_StatusCode retval = UA_Server_addVariableNode(
      opcServer,                                           // server
      UA_NODEID_NUMERIC(sensorNsIndex, id),                // requestedNewNodeId
      UA_NODEID_NUMERIC(deviceNsIndex, pid),               // parentNodeId
      UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY),          // referenceTypeId
      UA_QUALIFIEDNAME_ALLOC(sensorNsIndex, name),         // browseName
      UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), // typeDefinition
      vAttr,                                               // objectAttributes
      NULL, NULL);

  string status(UA_StatusCode_name(retval));
  string msg = status + "|创建OPC传感器变量" + "[" + string(name) + "]";
  UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, msg.c_str());

  return retval;
}

// Sensor结构体
struct Sensor
{
  int sensorId;
  string sensorName;
  string updateDate = "0000-00-00 00:00:00";
  UA_StatusCode status = UA_STATUSCODE_GOOD;
};

// Device结构体
struct Device
{
  int deviceId;
  string deviceNo;
  string deviceName;
  map<int, Sensor *> sensorList;
};

// 更新传感器数据
void updateSensorData(Device *device, json sensorData)
{
  // 检查传感器参数id
  if (sensorData["id"] == nullptr)
  {
    UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到传感器参数id");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(sensorData).c_str());
    return;
  }
  // 检查传感器参数sensorName
  if (sensorData["sensorName"] == nullptr)
  {
    UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到传感器参数sensorName");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(sensorData).c_str());
    return;
  }
  // 检查传感器参数isLine
  if (sensorData["isLine"] == nullptr)
  {
    UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到传感器参数isLine");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(sensorData).c_str());
    return;
  }
  // 检查传感器参数updateDate
  if (sensorData["updateDate"] == nullptr)
  {
    UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到传感器参数updateDate");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(sensorData).c_str());
    return;
  }
  // 检查传感器参数sensorTypeId
  if (sensorData["sensorTypeId"] == nullptr)
  {
    UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到传感器参数sensorTypeId");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(sensorData).c_str());
    return;
  }

  // 声明并初始化传感器数值变量
  UA_Variant value;
  UA_Variant_init(&value);

  // 获取传感器类型ID
  int typeId = sensorData["sensorTypeId"];
  if (typeId == 1 || typeId == 4 || typeId == 6 || typeId == 8)
  {
    // 检查传感器参数value
    if (sensorData["value"] == nullptr)
    {
      UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到传感器参数value");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(sensorData).c_str());
      return;
    }
    string valStr = sensorData["value"];
    if (typeId == 1)
    {
      // 检查传感器参数decimalPlacse
      if (sensorData["decimalPlacse"] == nullptr)
      {
        UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到传感器参数decimalPlacse");
        UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(sensorData).c_str());
        return;
      }
      // 获取小数位长度值字符串并转化为数值
      string lenStr = sensorData["decimalPlacse"];
      int len = stoi(lenStr);
      // 长度大于0是浮点数，否则就是整数
      if (len > 0)
      {
        // 浮点数
        UA_Float val = stof(valStr);
        UA_Variant_setScalar(&value, &val, &UA_TYPES[UA_TYPES_FLOAT]);
      }
      else
      {
        // 整数
        UA_IntegerId val = stoi(valStr);
        UA_Variant_setScalar(&value, &val, &UA_TYPES[UA_TYPES_INTEGERID]);
      }
    }
    else
    {
      // 字符串
      UA_String val = UA_String_fromChars(valStr.c_str());
      UA_Variant_setScalar(&value, &val, &UA_TYPES[UA_TYPES_STRING]);
    }
  }
  else if (typeId == 2 || typeId == 5)
  {
    // 检查传感器参数switcher
    if (sensorData["switcher"] == nullptr)
    {
      UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到传感器参数switcher");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(sensorData).c_str());
      return;
    }
    // 将开关转换为布尔值
    int switcher = sensorData["switcher"];
    UA_Boolean val = switcher > 0;
    UA_Variant_setScalar(&value, &val, &UA_TYPES[UA_TYPES_BOOLEAN]);
  }
  else
  {
    string msg = "不支持的传感器类型ID: " + to_string(typeId);
    UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, msg.c_str());
    return;
  }

  // 声明并初始化传感器对象
  Sensor *sensor = nullptr;
  // 通过传感器参数id查找传感器
  auto sensorIter = device->sensorList.find(sensorData["id"]);
  if (sensorIter == device->sensorList.end())
  {
    // 新建传感器
    sensor = new Sensor;
    sensor->sensorId = sensorData["id"];
    sensor->sensorName = sensorData["sensorName"];

    // 加入传感器列表
    device->sensorList[sensorData["id"]] = sensor;

    // 创建OPC传感器变量
    UA_StatusCode retval = createSensorVariable(
        sensor->sensorId, device->deviceId,
        sensor->sensorName.c_str(), sensor->sensorName.c_str(), value);
    // 如果创建OPC传感器变量失败，则返回
    if (retval != UA_STATUSCODE_GOOD)
    {
      return;
    }
  }
  else
  {
    // 赋值传感器
    sensor = sensorIter->second;
  }

  // 获取是否在线
  int isLineValue = sensorData["isLine"];
  // 转换为传感器状态
  sensor->status = isLineValue > 0 ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BAD;

  // 获取更新时间字符串
  string updateDate = sensorData["updateDate"];
  // 时间有变化则更新
  if (sensor->updateDate != updateDate)
  {
    sensor->updateDate = updateDate;
    // 更新变量
    updateVariable(sensor->sensorId, sensor->status, value);
  }
}

// 声明文件夹空间索引
UA_UInt16 folderNsIndex;

// 创建OPC设备对象
UA_StatusCode createDeviceObject(int id, int pid, const char *name, const char *desc)
{
  UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
  oAttr.description = UA_LOCALIZEDTEXT_ALLOC("zh-CN", desc);
  oAttr.displayName = UA_LOCALIZEDTEXT_ALLOC("zh-CN", name);

  UA_StatusCode retval = UA_Server_addObjectNode(
      opcServer,                                     // server
      UA_NODEID_NUMERIC(deviceNsIndex, id),          // requestedNewNodeId
      UA_NODEID_NUMERIC(folderNsIndex, pid),         // parentNodeId
      UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),   // referenceTypeId
      UA_QUALIFIEDNAME_ALLOC(deviceNsIndex, name),   // browseName
      UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), // typeDefinition
      oAttr,                                         // objectAttributes
      NULL, NULL);

  string status(UA_StatusCode_name(retval));
  string msg = status + "|创建OPC设备对象" + "[" + string(name) + "]";
  UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, msg.c_str());

  return retval;
}

// 声明并初始化文件夹ID
int folderId = 1;

// 声明设备列表
map<int, Device *> deviceList;

// 更新设备数据
void updateDeviceData(json deviceData)
{
  // 检查设备参数id
  if (deviceData["id"] == nullptr)
  {
    UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到设备参数id");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(deviceData).c_str());
    return;
  }
  // 检查设备参数deviceName
  if (deviceData["deviceName"] == nullptr)
  {
    UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到设备参数deviceName");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(deviceData).c_str());
    return;
  }
  // 检查设备参数deviceNo
  if (deviceData["deviceNo"] == nullptr)
  {
    UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到设备参数deviceNo");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(deviceData).c_str());
    return;
  }

  // 声明并初始化设备对象
  Device *device = nullptr;
  // 通过设备参数id查找设备
  auto deviceIter = deviceList.find(deviceData["id"]);
  if (deviceIter == deviceList.end())
  {
    // 新建设备
    device = new Device;
    device->deviceId = deviceData["id"];
    device->deviceNo = deviceData["deviceNo"];
    device->deviceName = deviceData["deviceName"];
    // 如果是默认设备名称就加上设备ID
    if (device->deviceName == "4G压力表")
    {
      device->deviceName += "(" + to_string(device->deviceId) + ")";
    }

    // 加入设备列表
    deviceList[deviceData["id"]] = device;

    // 创建OPC设备对象
    UA_StatusCode retval = createDeviceObject(device->deviceId, folderId, device->deviceName.c_str(), device->deviceNo.c_str());
    // 如果创建OPC设备对象失败，则返回
    if (retval != UA_STATUSCODE_GOOD)
    {
      return;
    }
  }
  else
  {
    // 赋值设备
    device = deviceIter->second;
  }

  // 检查设备参数sensorsList
  if (deviceData["sensorsList"] == nullptr)
  {
    UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未找到设备参数sensorsList");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(deviceData).c_str());
    return;
  }
  // 检查设备参数sensorsList是否为数组
  if (!deviceData["sensorsList"].is_array())
  {
    UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "sensorsList不是有效的数组类型");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(deviceData["sensorsList"]).c_str());
    return;
  }
  // 遍历sensorsList数组
  for (int j = 0; j < deviceData["sensorsList"].size(); j++)
  {
    // 声明并赋值传感器JSON数据
    json sensorData = deviceData["sensorsList"][j];
    // 更新传感器数据
    updateSensorData(device, sensorData);
  }
}

// Config结构体
struct Config
{
  string username;
  string password;
  string clientId;
  string secret;
  int userId;
};

// 声明配置变量
Config cfg;

// 声明并初始化请求域名
string url = "https://app.dtuip.com";

// 声明并初始化token
string token = "";

// 声明并初始化失效时间戳
time_t expireTs = time(nullptr);

// 获取token
bool get_token()
{
  // 创建HTTP客户端
  Client cli(url);

  // 配置basic auth
  cli.set_basic_auth(cfg.clientId, cfg.secret);

  // 配置请求参数
  Params params{
      {"grant_type", "password"},
      {"username", cfg.username},
      {"password", cfg.password},
  };

  // 发送HTTP请求
  Result res = cli.Post("/oauth/token", params);

  // 检查请求结果大小，为空则输出错误
  if (res->body.size())
  {
    // 解析请求结果
    json data = json::parse(res->body);
    if (data == nullptr)
    {
      UA_LOG_ERROR(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "解析json数据失败");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, res->body.c_str());
      return false;
    }
    // 检查返回参数userId
    if (data["userId"] == nullptr)
    {
      UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未获取到userId参数");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, res->body.c_str());
      return false;
    }
    // 检查返回参数expires_in
    if (data["expires_in"] == nullptr)
    {
      UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未获取到expires_in参数");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, res->body.c_str());
      return false;
    }
    // 检查返回参数access_token
    if (data["access_token"] == nullptr)
    {
      UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未获取到access_token参数");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, res->body.c_str());
      return false;
    }
    // 设置用户ID
    cfg.userId = data["userId"];

    // 设置失效时间戳
    int expireIn = data["expires_in"];
    expireTs = time(nullptr) + expireIn;

    // 设置token
    token = data["access_token"];
  }
  else
  {
    UA_LOG_ERROR(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "获取token失败");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(res.error()).c_str());
    return false;
  }
  return true;
}

// 获取设备列表数据
void get_device_datas(int page, int size)
{
  // 获取当前时间戳
  time_t currentTs = time(nullptr);
  // 如果token为空，或当前时间戳大于或等于失效时间戳，则重新获取token
  if (token == "" || currentTs >= expireTs)
  {
    // 获取token
    bool status = get_token();
    // 如果获取token失败，则返回
    if (!status)
    {
      return;
    }
  }

  // 创建HTTP客户端
  Client cli(url);

  // 配置bearer auth
  cli.set_bearer_token_auth(token);

  // 创建header数据
  Headers header = {
      {"tlinkAppId", cfg.clientId},
  };

  // 创建POST数据
  json jsonData = {
      {"userId", cfg.userId},
      {"currPage", page},
      {"pageSize", size},
  };

  // 生成POST的body数据
  string body = jsonData.dump();

  // 设定内容类型
  string contentType = "application/json";

  // 发送HTTP请求
  Result res = cli.Post("/api/device/getDeviceSensorDatas", header, body, contentType);

  // 检查请求结果大小，为空则输出错误
  if (res->body.size())
  {
    // 解析请求结果
    json data = json::parse(res->body);
    if (data == nullptr)
    {
      UA_LOG_ERROR(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "解析json数据失败");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, res->body.c_str());
      return;
    }
    // 检查返回参数flag
    if (data["flag"] == nullptr)
    {
      UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未获取到flag参数");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, res->body.c_str());
      return;
    }
    // 赋值并检查返回标示
    string flag = data["flag"];
    if (flag != "00")
    {
      UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "获取设备列表数据失败");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(data["msg"]).c_str());
      return;
    }
    // 检查返回参数rowCount
    if (data["rowCount"] == nullptr)
    {
      UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未获取到rowCount参数");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, res->body.c_str());
      return;
    }
    // 检查返回参数dataList
    if (data["dataList"] == nullptr)
    {
      UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "未获取到dataList参数");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, res->body.c_str());
      return;
    }
    // 检查返回参数dataList是否为数组
    if (!data["dataList"].is_array())
    {
      UA_LOG_WARNING(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "dataList不是有效的数组类型");
      UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(data["dataList"]).c_str());
      return;
    }
    int total = data["rowCount"];
    // 遍历dataList数组
    for (int i = 0; i < data["dataList"].size(); i++)
    {
      // 声明并赋值设备JSON数据
      json deviceData = data["dataList"][i];
      // 更新设备数据
      updateDeviceData(deviceData);
    }

    // 判断当前页数据是否已经达到指定大小，并且总数据量大于当前页数
    // 如果满足条件，则说明还需要获取下一页数据
    if (data["dataList"].size() == size && page * size < total)
    {
      get_device_datas(page + 1, size);
    }
  }
  else
  {
    UA_LOG_ERROR(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "获取设备列表数据失败");
    UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, to_string(res.error()).c_str());
    return;
  }
}

// API请求回调函数
void httpCallback(UA_Server *server, void *data)
{
  // 获取设备列表数据
  get_device_datas(1, 100);
}

// 时间转换函数
UA_DateTime convertToDateTime(UA_UInt64 expectedTime)
{
  UA_UInt64 interval = (UA_UInt64)(expectedTime * UA_DATETIME_MSEC);
  UA_DateTime nextTime = UA_DateTime_nowMonotonic() + (UA_DateTime)interval;

  return nextTime;
}

// 创建OPC文件夹对象
UA_StatusCode createFolderObject(int id, int pid, const char *name, const char *desc)
{
  UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
  oAttr.description = UA_LOCALIZEDTEXT_ALLOC("zh-CN", desc);
  oAttr.displayName = UA_LOCALIZEDTEXT_ALLOC("zh-CN", name);

  UA_UInt16 pNsIndex = folderNsIndex++;
  if (pid == UA_NS0ID_OBJECTSFOLDER)
  {
    pNsIndex = 0;
  }

  UA_StatusCode retval = UA_Server_addObjectNode(
      opcServer,                                   // server
      UA_NODEID_NUMERIC(folderNsIndex, id),        // requestedNewNodeId
      UA_NODEID_NUMERIC(pNsIndex, pid),            // parentNodeId
      UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), // referenceTypeId
      UA_QUALIFIEDNAME_ALLOC(folderNsIndex, name), // browseName
      UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),   // typeDefinition
      oAttr,                                       // objectAttributes
      NULL, NULL);
  string status(UA_StatusCode_name(retval));
  string msg = status + "|创建OPC文件夹对象" + "[" + string(name) + "]";
  UA_LOG_DEBUG(&serverCfg->logger, UA_LOGCATEGORY_SERVER, msg.c_str());

  return retval;
}

// 声明并初始化运行状态
UA_Boolean running = true;

// 信号处理函数
void signalHandler(int sig)
{
  // 运行状态为false
  running = false;
}

// 声明并初始化文件夹名称
string folderName = "拓普瑞";

// OPC-UA服务器
int boot_server(UA_LogLevel log_level)
{
  // 注册信号处理函数，用于处理SIGINT和SIGTERM信号
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  // 创建OPC-UA服务器对象
  opcServer = UA_Server_new();

  // 设置OPC-UA服务器配置
  serverCfg = UA_Server_getConfig(opcServer);
  serverCfg->logger = UA_Log_Stdout_withLevel(log_level);
  UA_ServerConfig_setMinimal(serverCfg, 4840, NULL);
  cout << "===服务端口: 4840===" << endl;

  // 注册命名空间索引
  folderNsIndex = UA_Server_addNamespace(opcServer, (const char *)"folder");
  deviceNsIndex = UA_Server_addNamespace(opcServer, (const char *)"device");
  sensorNsIndex = UA_Server_addNamespace(opcServer, (const char *)"sensor");

  // 创建设备厂家文件夹
  UA_StatusCode retval = createFolderObject(folderId, UA_NS0ID_OBJECTSFOLDER, folderName.c_str(), folderName.c_str());
  // 只有创建文件夹成功才注册回调函数
  if (retval == UA_STATUSCODE_GOOD)
  {
    // 声明回调ID
    UA_UInt64 callbackId = 0;
    // 添加周期性回调，每10000毫秒执行一次
    UA_Server_addRepeatedCallback(opcServer, httpCallback, NULL, 10000, &callbackId);

    // 设定下次回调时间
    UA_DateTime nextTime = convertToDateTime(1000);
    // 添加定时回调，1000毫秒后执行
    UA_Server_addTimedCallback(opcServer, httpCallback, NULL, nextTime, NULL);
  }

  // 启动服务器并等待其停止
  retval = UA_Server_run(opcServer, &running);

  // 删除服务器对象
  UA_Server_delete(opcServer);

  // 清除服务器配置
  UA_ServerConfig_clean(serverCfg);

  // 释放设备列表变量
  for (auto &item : deviceList)
  {
    delete item.second;
  }

  // 返回服务器状态码
  return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}

// 初始化配置
bool init_cfg()
{
  // 声明json变量
  json data;

  // 读取配置文件
  ifstream file("config.json");

  // 读取失败则提示并结束
  if (!file.is_open())
  {
    UA_LOG_FATAL(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "配置文件config.json读取失败");
    return false;
  }

  // 赋值给json变量并检查参数
  file >> data;
  if (data["username"] == NULL)
  {
    UA_LOG_FATAL(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "配置缺少username参数");
    return false;
  }
  else
  {
    cfg.username = data["username"];
  }
  if (data["password"] == NULL)
  {
    UA_LOG_FATAL(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "配置缺少password参数");
    return false;
  }
  else
  {
    cfg.password = data["password"];
  }
  if (data["clientId"] == NULL)
  {
    UA_LOG_FATAL(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "配置缺少clientId参数");
    return false;
  }
  else
  {
    cfg.clientId = data["clientId"];
  }
  if (data["secret"] == NULL)
  {
    UA_LOG_FATAL(&serverCfg->logger, UA_LOGCATEGORY_SERVER, "配置缺少secret参数");
    return false;
  }
  else
  {
    cfg.secret = data["secret"];
  }
  return true;
}

// 主程序入口
int main(int argc, char *argv[])
{
#if defined(_WIN16) || defined(_WIN32) || defined(_WIN64)
  // 改变页码，使用UTF-8
  system("chcp 65001");
#endif

  // 声明并初始化日志等级
  UA_LogLevel log_level = UA_LOGLEVEL_ERROR;

  // 如果有命令行参数
  if (argc > 1)
  {
    // 遍历命令行参数
    for (int i = 1; i < argc; i++)
    {
      // 获取参数字符串
      string params = argv[i];
      // debug模式
      if (params == "/d" || params == "/debug")
      {
        cout << "===开启调试模式===" << endl;
        log_level = UA_LOGLEVEL_DEBUG;
      }
      // 帮助输出
      else if (params == "/h" || params == "/help")
      {
        cout << "开启调试模式: /d 或 /debug" << endl;
        exit(EXIT_SUCCESS);
      }
      else
      {
        cout << "无效的命令参数: " << params << endl;
        cout << "请输入参数 /h 或 /help 获取帮助" << endl;
        exit(EXIT_FAILURE);
      }
    }
  }

  // 初始化配置
  cout << "===加载配置参数===" << endl;
  bool status = init_cfg();
  // 配置失败则退出
  if (!status)
  {
    return EXIT_FAILURE;
  }

  // 启动OPC-UA服务器
  cout << "===启动OPC-UA服务===" << endl;
  int ret = boot_server(log_level);
  // 返回服务器状态码
  return ret;
}
