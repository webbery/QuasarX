# 测试分类指南

Service 支持三种 exchange 配置模式，修改 config.json 后需要重启服务。本目录按模式分类测试项，并提供自动化测试运行器。

## 快速开始

```bash
cd service/test

# 运行所有可用模式（自动启停服务）
python run_tests.py

# 只运行指定模式
python run_tests.py --mode stock_hist_sim

# 指定测试（部分匹配）
python run_tests.py --tests r2strategy shadow

# 列出所有模式
python run_tests.py --list

# 只打印计划不执行
python run_tests.py --dry-run
```

> 💡 **自定义测试项**: 编辑 `test_modes.json` 中对应模式的 `tests` 数组即可增删测试。

## 三种配置模式

| 模式 | config.json 配置 | C++ 类 | 用途 |
|------|------------------|--------|------|
| **stock_hist_sim** | `"api": "stock_hist_sim"` | StockHistorySimulation | 历史回测，使用历史 K 线数据 |
| **tickflow** | `"api": "tickflow"` | TickFlowBridge | 实时行情，通过 TickFlow API 获取实时数据 |
| **HX** | `"api": "hx"` | HXExchange / StockRealSimulation | 华鑫实盘仿真，真实交易接口 |

## 测试分类

### 1. stock_hist_sim（历史回测模式）

| 测试文件 | 测试内容 |
|----------|----------|
| `test_capital_risk.py` | 风控配置 CRUD（总资金止损、日亏损限额、全部平仓） |
| `test_prediction.py` | 预测操作设置（`/predict/operation`） |
| `test_risk.py` | 风险管理（止损/VaR/回撤，大部分已注释） |
| `test_shadow_mode.py` | 影子模式（日志解析、虚拟账户、回测集成） |
| `test_strategy.py` | **回测模式**: 策略列表/部署/批量回测；**非回测模式**: 策略生命周期管理（删除/停止/重复部署） |
| `test_trade_history.py` | 交易历史查询（分页/时间过滤/回测场景） |
| `test_user.py` | 服务状态、指数行情、配置获取/更新、权限测试 |
| `test_mc.py` | 空文件（预留） |

> 💡 **test_strategy.py 按模式拆分**: 通过登录响应中的 `mode` 字段判断运行模式，回测模式仅运行策略列表/部署/批量回测测试，非回测模式运行策略生命周期管理测试。

### 2. tickflow（实时行情模式）

| 测试文件 | 测试内容 |
|----------|----------|
| `test_sectorquote.py` | 行业板块实时行情（`/stocks/sector/quote`） |
| `test_tick_record.py` | Tick 数据存储（`/record` 接口 + CBOR 目录验证） |
| `test_data.py` | SSE 流式数据同步（`/data/sync` ZIP 下载） |
| `test_shibor.py` | SHIBOR 利率数据查询 |

### 3. HX（华鑫实盘仿真模式）

| 测试文件 | 测试内容 |
|----------|----------|
| `test_order.py` | 订单全生命周期（限价/市价下单、撤单、状态轮询） |
| `test_serverevent.py` | SSE 事件流（系统状态、持仓更新、订单更新） |
| `test_stock.py` | `test_stock_position`（真实持仓）、`test_daily_limit`（交易速率限制） |
| `test_user.py` | `test_get_position`（真实持仓）、`test_get_funds`（真实资金）、`test_get_commission`（佣金费率）、`test_add_exchange`/`test_delete_exchange`（交易所配置管理） |
| `test_stock.py` | 股票列表、个股行情、历史K线、科创板权限、板块资金流 |

## 运行测试

### 方式一：自动化运行器（推荐）

```bash
cd service/test

# 运行所有可用模式
python run_tests.py

# 只运行回测模式
python run_tests.py --mode stock_hist_sim

# 运行指定测试
python run_tests.py --tests r2strategy
```

### 方式二：手动启动服务 + pytest

```bash
cd service/build

# 启动服务（使用对应模式的 config.json）
./QuantService ../test/configs/stock_hist_sim.json   # 回测模式
./QuantService ../test/configs/tickflow.json          # 实时行情模式
./QuantService ../test/configs/hx.json                # 华鑫实盘模式

# 另一个终端运行测试
cd ../test
pytest testcases/test_r2strategy.py -v
```

## 配置文件详解

### `configs/stock_hist_sim.json` — 历史回测模式

```json
"exchange": [{
    "api": "stock_hist_sim",
    "name": "stock-sim",
    "quote": "data",    // 历史数据目录
    "type": "stock"
}]
```

**启动方式**: `./QuantService ../test/configs/stock_hist_sim.json`

**运行模式**: `RuningType::Backtest`

**适用测试**:
| 测试文件 | 核心接口 |
|----------|----------|
| `test_capital_risk.py` | `/risk/capital`, `/risk/daily`, `/risk/closeall` |
| `test_prediction.py` | `/predict/operation` |
| `test_r2strategy.py` | `/backtest` |
| `test_risk.py` | `/risk/*` |
| `test_shadow_mode.py` | `/backtest` + 日志解析 |
| `test_shibor.py` | `/market/shibor` |
| `test_strategy.py` | `/backtest`, `/strategy` |
| `test_trade_history.py` | `/trade/history` |
| `test_stock.py` | `/stocks/list`, `/stocks/detail`, `/stocks/history` |
| `test_user.py` | `/server/status`, `/server/index`, `/server/config` |

---

### `configs/tickflow.json` — 实时行情模式

```json
"exchange": [{
    "api": "tickflow",
    "name": "tickflow-quote",
    "username": "tk_",
    "passwd": "your-api-key",
    "pool": ["600000.SH", "000001.SZ"],
    "type": "stock"
}]
```

**启动方式**: `./QuantService ../test/configs/tickflow.json`

**运行模式**: 默认运行（非 Backtest/Simulation）

**适用测试**:
| 测试文件 | 核心接口 |
|----------|----------|
| `test_sectorquote.py` | `/stocks/sector/quote` |
| `test_tick_record.py` | `/record` + daily 目录验证 |
| `test_data.py` | `/data/sync` |

---

### `configs/hx.json` — 华鑫实盘仿真模式

```json
"exchange": [{
    "api": "hx",
    "name": "hx-sim",
    "username": "",     // 需填写华鑫账号
    "passwd": "",       // 需填写华鑫密码
    "type": "stock"
}]
```

**启动方式**: `./QuantService ../test/configs/hx.json`

**运行模式**: `RuningType::Simualtion`

**适用测试**:
| 测试文件 | 核心接口 |
|----------|----------|
| `test_order.py` | `/trade/order` POST/DELETE/GET |
| `test_serverevent.py` | `/server/event`（system_status / update_position / update_order） |
| `test_stock.py` | `test_stock_position`, `test_daily_limit` |
| `test_user.py` | `test_get_position`, `test_get_funds`, `test_get_commission`, `test_add_exchange` |

> **注意**: HX 模式需要真实的华鑫账号密码，测试前请填写 `username` 和 `passwd` 字段。

## 自动化运行器详解

### 参数说明

| 参数 | 说明 | 示例 |
|------|------|------|
| `--mode` | 选择测试模式 | `--mode stock_hist_sim` |
| `--tests` | 指定测试（部分匹配） | `--tests r2strategy shadow` |
| `--list` | 列出所有模式 | `--list` |
| `--dry-run` | 只打印计划 | `--dry-run` |
| `--no-service` | 不自动启停服务 | `--no-service` |
| `--port` | 服务端口 | `--port 19108` |

### 交互配置

当模式的 `requires_input` 字段指定的配置项为空时，运行器会交互式提示输入：

```
[⚠️] 华鑫实盘仿真模式 — 配置不完整
  请输入账号信息（仅本次有效，不保存到文件）:
  用户名: user123
  密码: ******
[✓] 配置已设置，继续测试...
```

**密码/Key 输入时会隐藏回显**，输入的内容仅用于本次测试运行的临时配置文件，测试结束后自动清理。

### 自定义测试项

编辑 `test_modes.json` 文件：

```json
{
  "modes": {
    "stock_hist_sim": {
      "tests": [
        "test_r2strategy",
        "test_strategy"
        // 删除不需要的，添加新的 test_*.py 文件名（不含后缀）
      ]
    }
  }
}
```

修改后立即生效，无需重启。
