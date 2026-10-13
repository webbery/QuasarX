# 宏观数据输入节点实现文档

## 概述

在策略工厂中添加了宏观数据输入节点（MacroInputNode），允许策略直接使用宏观经济指标（如利率、汇率、CPI 等）作为输入信号。

## 设计决策

### 1. 输出模式：单输出 handle
- 节点输出一个 `field-value` handle，连接到下游节点的 `input` handle
- 多 series 指标（如 treasury_yield）通过 `seriesId` 参数选择具体 series
- 优点：简单，与现有节点连接逻辑兼容

### 2. 时间对齐策略
- **中国数据**：使用 forward fill（前向填充）
  - 原因：国内宏观数据发布及时，当天数据当天可用
- **美国数据**：使用 T-1 滞后（lag_t1）
  - 原因：FRED 数据通常有 1 天发布延迟，当天数据次日才可用
- 用户可通过 `fillMethod` 参数覆盖默认行为

### 3. 数据缓存复用
- 复用数据中心已下载的宏观数据缓存文件
- 缓存路径：`{db_path}/macro/{country}/{indicator}.json`
- 无需重复下载，保证数据一致性

## 实现文件

### 前端（4 个文件）

#### 1. `app/src/lib/nodes/configs/macroInput.ts`（新建）
定义宏观数据输入节点的配置：
- **节点 ID**: `macro-input`
- **节点类型**: `macroInput`
- **类别**: `input`
- **参数**:
  - `country`: 国家选择（china/usa/global）
  - `indicator`: 指标选择（19 个指标）
  - `seriesId`: Series ID（可选，用于多 series 指标）
  - `fillMethod`: 填充方式（forward/linear/none）

#### 2. `app/src/lib/nodes/index.ts`（修改）
导入新节点配置，自动注册到节点系统

#### 3. `app/src/components/flow/StrategyNodeHandles.vue`（修改）
- 添加 `macroInput` 节点的输出 handle 渲染逻辑
- 显示 `field-value` handle 和 "值" 标签
- 添加对应的 CSS 样式

#### 4. `app/src/components/flow/StrategyNode.vue`（无需修改）
- 现有逻辑已支持 `macroInput` 节点类型
- `isInputNode` 计算属性会自动识别 `macroInput` 为输入节点

### 后端（2 个文件）

#### 1. `service/include/Nodes/MacroNode.h`（新建）
定义 `MacroInputNode` 类：
- 继承自 `QNode`
- 主要方法：
  - `Init()`: 初始化节点参数
  - `Prepare()`: 加载宏观数据
  - `Process()`: 每个 epoch 处理数据
  - `out_elements()`: 定义输出元素类型
- 私有方法：
  - `loadMacroData()`: 从缓存文件加载数据
  - `forwardFill()`: 前向填充
  - `lagT1()`: T-1 滞后
  - `linearInterp()`: 线性插值

#### 2. `service/src/Nodes/MacroNode.cpp`（新建）
实现 `MacroInputNode` 类：
- **数据加载**：
  - 从 `{db_path}/macro/{country}/{indicator}.json` 读取
  - 支持 `seriesId` 过滤
  - 按日期排序
- **时间对齐**：
  - `forward`: 找到 targetDate 及之前最近的值
  - `lag_t1`: 找到 targetDate 之前的值（不包括当天）
  - `linear`: 线性插值（前后点加权平均）
- **输出**：
  - 输出 key：`{indicator}` 或 `{indicator}_{seriesId}`
  - 输出类型：`ArgType::Double_TimeSeries`

## 使用示例

### 场景 1：SOFR 利率作为无风险利率
```
[宏观数据: USA/SOFR] --field-value--> [指标计算: 动态阈值]
```
- 美国数据自动使用 T-1 滞后
- 输出 key: `sofr`

### 场景 2：中国 CPI 作为通胀过滤
```
[宏观数据: China/CPI] --field-value--> [公式计算: CPI > 3%?] --signal--> [风控保护]
```
- 中国数据自动使用前向填充
- 输出 key: `cpi`

### 场景 3：美国国债收益率（指定 series）
```
[宏观数据: USA/treasury_yield, seriesId=DGS10] --field-value--> [指标计算: MA]
```
- 通过 `seriesId` 参数选择 10 年期国债收益率
- 输出 key: `treasury_yield_DGS10`

### 场景 4：汇率作为信号
```
[宏观数据: USA/fx_rate, seriesId=DEXCHUS] --field-value--> [指标计算: Return]
```
- 人民币兑美元汇率
- 输出 key: `fx_rate_DEXCHUS`

## 技术细节

### 1. 连接验证
- `macroInput` 节点的输出 handle 格式：`field-value`
- 下游节点可以连接到任何接受 `field-*` 的输入 handle
- 连接验证逻辑已在 `useFlowOperations.js` 中支持

### 2. 数据格式
缓存文件格式：
```json
{
  "data": [
    {"date": "2024-01-02", "value": 5.32},
    {"date": "2024-01-03", "value": 5.33},
    ...
  ]
}
```

多 series 格式：
```json
{
  "data": [
    {"date": "2024-01-02", "series_id": "DGS2", "value": 4.32},
    {"date": "2024-01-02", "series_id": "DGS10", "value": 3.88},
    ...
  ]
}
```

### 3. 时间对齐算法

#### Forward Fill（前向填充）
```cpp
double forwardFill(const String& targetDate) const {
    for (auto it = _macroData.rbegin(); it != _macroData.rend(); ++it) {
        if (it->first <= targetDate) {
            return it->second;
        }
    }
    return NaN;
}
```

#### T-1 Lag（T-1 滞后）
```cpp
double lagT1(const String& targetDate) const {
    for (auto it = _macroData.rbegin(); it != _macroData.rend(); ++it) {
        if (it->first < targetDate) {  // 严格小于
            return it->second;
        }
    }
    return NaN;
}
```

#### Linear Interpolation（线性插值）
```cpp
double linearInterp(const String& targetDate) const {
    // 找到 targetDate 前后的两个点
    // 按天数比例加权平均
    // 如果只有前点，使用前向填充
    // 如果只有后点，使用后向填充
}
```

### 4. 默认填充策略
```cpp
if (_country == "usa" && _fillMethod == "forward") {
    _fillMethod = "lag_t1";  // 美国数据默认 T-1
}
```

## 编译和部署

### 前端
```bash
cd app
npm run build
```

### 后端
```bash
cd service
make
```

## 测试建议

1. **单元测试**：
   - 测试 `forwardFill()`、`lagT1()`、`linearInterp()` 方法
   - 测试数据加载和 seriesId 过滤

2. **集成测试**：
   - 创建包含宏观数据节点的策略
   - 验证输出值是否正确
   - 验证时间对齐是否符合预期

3. **端到端测试**：
   - 在数据中心下载宏观数据
   - 在策略工厂中使用宏观数据节点
   - 运行回测并检查结果

## 未来扩展

1. **多输出 handle**：
   - 为多 series 指标自动生成多个 handle
   - 需要修改 `StrategyNodeHandles.vue` 和连接验证逻辑

2. **实时数据更新**：
   - 在实盘模式下自动下载最新宏观数据
   - 需要添加数据更新调度逻辑

3. **更多填充策略**：
   - 样条插值
   - 季节性调整
   - 移动平均平滑

4. **数据质量检查**：
   - 检测异常值
   - 自动填补缺失值
   - 数据一致性验证

## 相关文件

- `app/src/lib/nodes/configs/macroInput.ts` - 节点配置
- `app/src/lib/nodes/index.ts` - 节点注册
- `app/src/components/flow/StrategyNodeHandles.vue` - Handle 渲染
- `service/include/Nodes/MacroNode.h` - 节点头文件
- `service/src/Nodes/MacroNode.cpp` - 节点实现
- `service/tools/fetch_fred_data.py` - FRED 数据下载脚本
- `service/src/Handler/MacroHandler.cpp` - 宏观数据 API

## 注意事项

1. **数据依赖**：
   - 必须先在数据中心下载宏观数据
   - 缓存文件必须存在且格式正确

2. **时间对齐**：
   - 美国数据默认 T-1 滞后，符合实际发布延迟
   - 中国数据默认前向填充，假设数据当天可用

3. **性能考虑**：
   - 数据在 `Prepare()` 阶段一次性加载
   - `Process()` 阶段只做查找，性能开销小

4. **错误处理**：
   - 数据加载失败时输出警告
   - 找不到合适值时返回 NaN
