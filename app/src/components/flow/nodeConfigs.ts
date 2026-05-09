// 节点类型配置定义
export interface ParamConfig {
  value: any
  type: string
  options?: any[]
  min?: number
  max?: number
  step?: number
  unit?: string
  visible?: boolean
  placeholder?: string
  pattern?: string        // 正则表达式，用于验证输入
  errorMsg?: string       // 验证失败时的错误提示
}

export interface NodeTypeConfig {
  label: string
  nodeType: string
  params: Record<string, ParamConfig>
}

export interface KeyMap {
  [key: string]: string
}

// 键名映射（中文 <-> 英文）
export const keyMap: KeyMap = {
  "source": "来源",
  "code": "代码",
  "formula": "公式",
  "method": "方法",
  "smoothTime": "平滑时间",
  "range": "范围",
  "timeRange": "时间范围",
  "indicator": "输出指标",
  "sharp": "夏普比率",
  "maxDrawdown": "最大回撤",
  "totalReturn": "总收益",
  "annualReturn": "年化收益",
  "winRate": "胜率",
  "numTrades": "交易次数",
  "CalmarRatio": "卡玛比率",
  "InformationRatio": "信息比率",
  "freq": "频率",
  "missingHandle": "缺失处理",
  "buy": "买入条件",
  "sell": "卖出条件",
  "1d": "日频",
  "VaR": "VaR",
  "ES": "ES",
  "initialCapital": "初始资金",
  "commission": "佣金费率",
  "stampDuty": "印花税率",
  "minFee": "最低手续费",
  "slippage": "滑点",
  "type": "执行类型",
  "positionRatio": "仓位比例",
  "pool": "交易池",
  "stockA": "标的A",
  "stockB": "标的B",
  "beta": "对冲比例β",
  "window": "窗口大小",
  "allowShort": "允许做空",
  // 风控保护节点参数
  "stop_loss_enabled": "止损开关",
  "stop_loss_percent": "止损比例",
  "take_profit_enabled": "止盈开关",
  "take_profit_percent": "止盈比例",
  "trailing_stop_enabled": "追踪止损开关",
  "trailing_stop_percent": "追踪止损比例",
  "time_stop_enabled": "时间止损开关",
  "max_bars": "最大持仓Bar数",
  // 仓位 sizing 参数
  "sizing_method": "仓位计算方法",
  "max_single_pct": "单标的上限",
  "max_total_pct": "总仓位上限",
  "volatility_target": "波动率目标",
}

// 节点类型配置
export const nodeTypeConfigs: Record<string, NodeTypeConfig> = {
  'data-source': {
    label: "数据输入",
    nodeType: "input",
    params: {
      "来源": {
        value: "股票",
        type: "select",
        options: ["股票", "期货"]
      },
      "代码": {
        value: ["sz.000001"],
        type: "text"
      },
      "close": {
        value: "close",
        type: "text"
      },
      "open": {
        value: "open",
        type: "text"
      },
      "high": {
        value: "high",
        type: "text"
      },
      "low": {
        value: "low",
        type: "text"
      },
      "volume": {
        value: "volume",
        type: "text"
      },
      "频率": {
        value: "1d",
        type: "select",
        options: ["1d", "1m", "5m", "15m", "30m", "1h", "4h", "1w"]
      },
      "缺失处理": {
        value: "skip",
        type: "select",
        options: [
          { label: "跳过", value: "skip" },
          { label: "线性插值", value: "linear" },
          { label: "前向填充", value: "forward" },
          { label: "后向填充", value: "backward" }
        ]
      }
    }
  },
  'signal-generation': {
    label: "交易信号生成",
    nodeType: "signal",
    params: {
      "类型": {
        value: "股票",
        type: "select",
        options: ["股票", "期货", "期权"]
      },
      "代码": {
        value: "",
        type: "text",
        visible: false
      },
      "买入条件": {
        value: "MA_5-MA_15 >= 0",
        type: "text"
      },
      "卖出条件": {
        value: "MA_5-MA_15 < 0",
        type: "text"
      },
      "允许做空": {
        value: false,
        type: "boolean"
      }
    }
  },
  'execution': {
    label: "执行交易",
    nodeType: "execution",
    params: {
      "佣金费率": {
        value: 0.0003,
        type: "number",
        min: 0,
        max: 0.01,
        step: 0.0001,
        unit: "%"
      },
      "印花税率": {
        value: 0.001,
        type: "number",
        min: 0,
        max: 0.01,
        step: 0.0001,
        unit: "%"
      },
      "最低手续费": {
        value: 5,
        type: "number",
        min: 0,
        max: 50,
        step: 1,
        unit: "元"
      },
      "滑点": {
        value: 0.001,
        type: "number",
        min: 0,
        max: 0.01,
        step: 0.0001
      },
      "执行类型": {
        value: 0,
        type: "select",
        options: [
          { label: "立即执行 (市价单)", value: 0 },
          { label: "立即执行 (限价单)", value: 1 }
        ]
      }
    }
  },
  'debug': {
    label: "调试",
    nodeType: "debug",
    params: {
      '下载路径': {
        value: "",
        type: "directory"
      },
      '下载文件': {
        value: "",
        type: "download"
      }
    }
  },
  'test': {
    label: "测试",
    nodeType: "test",
    params: {
      '参数': {
        value: "",
        type: "text"
      }
    }
  },
  'risk': {
    label: '风控保护',
    nodeType: 'protection',
    params: {
      "止损开关": {
        value: false,
        type: "boolean"
      },
      "止损比例": {
        value: 0.05,
        type: "number",
        min: 0.01,
        max: 0.5,
        step: 0.01,
        unit: "%"
      },
      "止盈开关": {
        value: false,
        type: "boolean"
      },
      "止盈比例": {
        value: 0.15,
        type: "number",
        min: 0.01,
        max: 1.0,
        step: 0.01,
        unit: "%"
      },
      "追踪止损开关": {
        value: false,
        type: "boolean"
      },
      "追踪止损比例": {
        value: 0.03,
        type: "number",
        min: 0.01,
        max: 0.3,
        step: 0.01,
        unit: "%"
      },
      "时间止损开关": {
        value: false,
        type: "boolean"
      },
      "最大持仓Bar数": {
        value: 20,
        type: "number",
        min: 1,
        max: 1000,
        step: 1,
        unit: "根"
      }
    }
  },
  'portfolio': {
    label: '投资组合',
    nodeType: 'portfolio',
    params: {
      "配置 ID": {
        value: "",
        type: "config-select",
        options: [],
        placeholder: "选择或创建配置"
      },
      "配置简介": {
        value: "",
        type: "textarea",
        placeholder: "请输入配置简介，描述策略投资目标、风险偏好等"
      },
      "仓位比例": {
        value: 1.0,
        type: "number",
        min: 0,
        max: 1,
        step: 0.1
      }
    }
  },
  'cnn': {
    label: 'CNN 模型',
    nodeType: 'backtest',
    params: {}
  },
  'xgboost': {
    label: 'xgboost',
    nodeType: 'xgboost',
    params: {
      '上传模型': {
        value: "",
        type: "file"
      }
    }
  },
  'basic-index': {
    label: "MA_5",
    nodeType: "function",
    params: {
      "方法": {
        value: "MA",
        type: "select",
        options: [
          { label: "MA (移动平均)", value: "MA" },
          { label: "STD (标准差)", value: "STD" },
          { label: "Return (收益率)", value: "Return" },
          { label: "R2 (拟合优度)", value: "R2" },
          { label: "ZScore (标准化)", value: "ZScore" }
        ]
      },
      "范围": {
        value: "1d",
        type: "select",
        options: [
          { label: "6秒", value: "6s" },
          { label: "30秒", value: "30s" },
          { label: "1分钟", value: "1m" },
          { label: "5分钟", value: "5m" },
          { label: "1小时", value: "1h" },
          { label: "1天", value: "1d" },
          { label: "3天", value: "3d" },
          { label: "5天", value: "5d" },
          { label: "15天", value: "15d" }
        ],
        visible: false  // 仅 STD/Return/R2/ZScore 使用，MA 使用时间范围
      },
      "时间范围": {
        value: "5d",
        type: "text",
        placeholder: "如 5d、30m、1h（正整数 + s/m/h/d 后缀）",
        pattern: "^\\d+[smhd]$",
        errorMsg: "格式错误，必须为正整数 + s/m/h/d 后缀（如 5d、30m、1h）",
        visible: true
      }
    }
  },
  'spread': {
    label: "价差计算",
    nodeType: "spread",
    params: {
      "标的A": {
        value: "sz.600000",
        type: "text",
        placeholder: "输入标的A的代码（如 sz.600000）"
      },
      "标的B": {
        value: "sz.600036",
        type: "text",
        placeholder: "输入标的B的代码（如 sz.600036）"
      },
      "方法": {
        value: "rolling_regression",
        type: "select",
        options: [
          { label: "简单价差 (A - B)", value: "simple_diff" },
          { label: "对数价差 (ln(A) - ln(B))", value: "log_diff" },
          { label: "滚动回归 (A - β×B)", value: "rolling_regression" }
        ]
      },
      "窗口大小": {
        value: 60,
        type: "number",
        min: 10,
        max: 500,
        step: 10,
        unit: "天"
      },
      "对冲比例β": {
        value: 1.0,
        type: "number",
        min: 0.1,
        max: 10.0,
        step: 0.1,
        visible: true
      }
    }
  }
}
