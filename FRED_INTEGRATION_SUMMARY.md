# FRED 数据集成总结

## 概述
为 QuasarX 系统添加了 FRED (Federal Reserve Economic Data) 数据源支持，包括美国利率、国债收益率、汇率等宏观经济数据。

## 修改的文件

### 1. 后端 C++ 服务

#### service/include/Handler/MacroHandler.h
- 添加 FRED 指标白名单：`sofr`, `treasury_yield`, `treasury_spread`, `fx_rate`
- 添加 FRED 指标到 series ID 的映射表
- 添加 FRED 指标显示名称映射
- 新增方法：
  - `FetchFromFred()` - 从 FRED API 获取数据
  - `IsFredIndicator()` - 判断是否为 FRED 指标
  - `GetFredSeriesIds()` - 获取指标对应的 series ID 列表
- 添加 FRED 缓存 TTL 常量：`FRED_CACHE_TTL_SECONDS = 24 * 3600` (1天)

#### service/src/Handler/MacroHandler.cpp
- 实现 FRED 指标映射表和显示名称
- 实现 `FetchFromFred()` 方法：
  - 从 config 读取 FRED API Key
  - 调用 `fetch_fred_data.py` 脚本
  - 解析 FRED 输出格式并转换为标准格式
  - 支持多 series 合并（如 treasury_yield 包含 DGS2/DGS5/DGS10/DGS30）
- 修改 `get()` 方法：
  - 检测是否为 FRED 指标
  - 根据指标类型选择不同的数据获取方式（FRED vs akshare）
  - 使用不同的缓存 TTL（FRED 1天，akshare 7天）
- 修改 `IsCacheExpired()` 方法：
  - 添加 `is_fred` 参数
  - 根据数据源类型使用不同的过期时间

#### service/include/config.h
- 添加 `GetFredKey()` 方法声明

#### service/src/config.cpp
- 实现 `GetFredKey()` 方法：从 `server.fred_key` 读取配置
- 在 `Init()` 中添加 `fred_key` 默认值

#### service/config.template.json
- 添加 `fred_key` 配置项（默认 null）

#### service/src/Util/data.cpp
- 添加 `IsFredIndicator()` 辅助函数
- 修改 `FetchMacroFromPython()` 方法：
  - 检测 FRED 指标并路由到 `fetch_fred_data.py`
  - 从环境变量 `FRED_API_KEY` 读取 API Key
  - 解析 FRED 输出格式并转换为标准格式
- 修改 `IsMacroCacheExpired()` 方法：
  - 添加 `is_fred` 参数
  - FRED 数据使用 1 天缓存，其他数据使用 7 天缓存
- 修改 `FetchMacroData()` 方法：
  - 传递 `is_fred` 标志到缓存检查函数

### 2. 前端 Vue 应用

#### app/src/components/shared/composables/useMacroIndicators.ts
- 添加 FRED 指标到 `INDICATOR_META`：
  - `sofr`: SOFR
  - `treasury_yield`: 国债收益率
  - `treasury_spread`: 国债利差
  - `fx_rate`: 汇率
- 添加 `FRED_INDICATORS` 集合
- 添加 `ALL_INDICATORS` 列表（包含所有指标）
- 添加 `COMMON_INDICATORS` 列表（通用指标，所有国家都显示）
- 修改 `macroOptions` 计算属性：
  - 通用指标在所有国家下显示
  - FRED 指标仅在美国下显示
- 导出 `isFredIndicator()` 函数

### 3. Python 数据获取脚本

#### service/tools/fetch_fred_data.py
- 已在前面的任务中创建
- 支持从 FRED API 下载汇率和利率数据
- 支持命令行传入 API Key
- 输出 JSON 格式供 C++ 解析

## 数据源映射

### FRED 指标 → Series ID

| 指标 | Series ID | 说明 |
|------|-----------|------|
| sofr | SOFR | 担保隔夜融资利率 |
| treasury_yield | DGS2, DGS5, DGS10, DGS30 | 美国国债收益率 (2/5/10/30年) |
| treasury_spread | T10Y2Y, T10Y3M | 国债利差 (10年-2年, 10年-3月) |
| fx_rate | DEXCHUS, DEXUSEU, DEXJPUS, DEXHKUS, DEXUSUK, DEXUSAL, DEXUSCA | 主要货币兑美元汇率 |

## API 使用示例

### 后端 API 调用

```bash
# 获取 SOFR 数据
GET /v0/market/macro?country=usa&indicator=sofr

# 获取美国国债收益率（多 series）
GET /v0/market/macro?country=usa&indicator=treasury_yield

# 获取汇率数据
GET /v0/market/macro?country=usa&indicator=fx_rate

# 强制刷新缓存
GET /v0/market/macro?country=usa&indicator=sofr&refresh=true
```

### 响应格式

单 series 指标（如 sofr）：
```json
{
  "status": "success",
  "country": "usa",
  "indicator": "sofr",
  "name": "SOFR 担保隔夜融资利率",
  "count": 252,
  "data": [
    {"date": "2024-01-02", "value": 5.32},
    ...
  ]
}
```

多 series 指标（如 treasury_yield）：
```json
{
  "status": "success",
  "country": "usa",
  "indicator": "treasury_yield",
  "name": "美国国债收益率",
  "count": 1008,
  "data": [
    {"date": "2024-01-02", "series_id": "DGS2", "value": 4.32},
    {"date": "2024-01-02", "series_id": "DGS10", "value": 3.88},
    ...
  ]
}
```

## 配置说明

### 1. 申请 FRED API Key
访问 https://fred.stlouisfed.org/docs/api/api_key.html 免费申请

### 2. 配置 API Key
在 `service/config.json` 中添加：
```json
{
  "server": {
    "fred_key": "your_api_key_here",
    ...
  }
}
```

或者设置环境变量：
```bash
export FRED_API_KEY=your_api_key_here
```

### 3. 前端使用
在波动率分析、信号分析等面板中：
1. 切换到"宏观指标"模式
2. 选择"美国"国家
3. 在指标下拉框中选择 FRED 指标（SOFR、国债收益率、国债利差、汇率）
4. 点击"开始分析"

## 缓存策略

- **FRED 数据**: 1 天缓存 TTL（日频数据，每日更新）
- **akshare 数据**: 7 天缓存 TTL（月频/季频数据）
- 缓存位置: `data/macro/{country}/{indicator}.json`
- 支持 `refresh=true` 参数强制刷新

## 注意事项

1. FRED API Key 是必需的，未配置时 FRED 指标无法获取数据
2. 多 series 指标的 data 数组中每条记录包含 `series_id` 字段
3. 前端仅在"美国"国家下显示 FRED 指标
4. 分析工具（波动率、信号等）会自动处理 FRED 数据格式
