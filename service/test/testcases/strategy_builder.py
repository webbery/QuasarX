#!/usr/bin/env python3
"""
策略图构建器 —— 统一各测试脚本里重复的策略 JSON 构造逻辑。

背景
----
仓库里十几个 Python 脚本各自手写同一套策略图字面量，节点 dict / param dict /
边 dict 只有取值不同。服务端其实只认一种结构（`params.<key>.value`），但没人把
它写下来，于是同一个 input 节点在 14 个文件里被抄了 22 遍，边构造函数
`_make_edges` 在两个生成器里逐字重复。

本模块把那套结构固化成三层：

  1. `P` / `param()`    —— 参数值包装（``{"value": X, "type": T}``）
  2. `*_node()` 工厂     —— 各 nodeType 的节点构造
  3. `StrategyBuilder`  —— 节点/边装配、校验、布局

约定
----
* **只发一种 edge handle 形式**：sourceHandle = ``"<srcId>"`` 或
  ``"<srcId>-<字段>"``，targetHandle = ``"<dstId>"``。服务端把 ``"output"`` /
  ``"input"`` / ``"field-X"`` 归一化到同一语义（`Strategy.cpp:259-267`），
  历史方言没必要继续共存。
* **参数名以服务端实际读取的 key 为准**（见 `KNOWN_PARAMS`）。写错的 key 会被
  服务端静默忽略 —— 例如 execution 节点写 ``"stamp"`` 而不是 ``"stampDuty"``，
  费率会悄悄回落到默认值。`StrategyBuilder.build()` 会对未知 key 告警。
* **protection 节点是例外**：它用嵌套对象（``{"stop_loss": {"enabled": true,
  "percent": 0.05}}``）而不是 ``{"value":..,"type":..}`` 包装，服务端直接读
  ``params["stop_loss"]["enabled"]``（`ProtectionNode.cpp` 嵌套格式分支）。

用法
----
    from strategy_builder import StrategyBuilder, P

    s = (StrategyBuilder("test_ma_cross", "双均线", description="...",
                         backtest=("2024-01-01", "2024-12-31"), source="A_hfq")
         .input("1", symbol="sz.800001")
         .function("2", "MA", 5, label="ma5")
         .signal("3", symbol="sz.800001", buy="ma5[t] > ma5[t-1]")
         .portfolio("4", position_ratio=1.0)
         .execution("5", commission=0.0)
         .connect("1", "2", prop="close")
         .connect("2", "3")
         .connect("3", "4")
         .connect("4", "5")
         .build())
"""

import re
import warnings
from datetime import date, datetime

# ----------------------------------------------------------------------
# 参数值
# ----------------------------------------------------------------------

def param(value, ptype="text"):
    """构造参数字典 ``{"value": value, "type": ptype}``。

    服务端只读 `params.<key>.value`；`type` 是给前端渲染控件用的提示
    （text / number / select / boolean）。
    """
    return {"value": value, "type": ptype}


class P:
    """参数字典的具名构造器。`of()` 按值类型自动推断，省掉手写 type。"""

    @staticmethod
    def text(value):
        return {"value": value, "type": "text"}

    @staticmethod
    def number(value):
        return {"value": value, "type": "number"}

    @staticmethod
    def select(value):
        return {"value": value, "type": "select"}

    @staticmethod
    def boolean(value):
        return {"value": value, "type": "boolean"}

    @staticmethod
    def of(value):
        """按值类型推断：bool→boolean，int/float→number，其余→text。"""
        if isinstance(value, bool):
            return {"value": value, "type": "boolean"}
        if isinstance(value, (int, float)):
            return {"value": value, "type": "number"}
        return {"value": value, "type": "text"}


# ----------------------------------------------------------------------
# 服务端实际读取的参数名
# ----------------------------------------------------------------------

# 服务端会读的 key。写错这里的 key 会被静默忽略 → 参数白名单告警的对象。
KNOWN_PARAMS = {
    "input": {"code", "freq", "source", "missingHandle"},
    "function": {"method", "range"},
    "formula": {"expression"},
    "debug": {"suffix", "mode", "file_path"},
    "signal": {"buy", "sell", "allowShort"},
    "portfolio": {"positionRatio", "allowShort", "max_single_pct", "max_total_pct",
                  "sizing_method", "volatility_target"},
    # 成本四项在 StrategySubSystem.cpp 读取，slippageModel 在 Strategy.cpp 读取
    "execution": {"commission", "stampDuty", "minFee", "slippage", "slippageModel",
                  "type"},
    "cusum": {"mode", "lambda", "threshold_multiplier", "min_obs", "cooldown",
              "calibrate_period", "mu", "sigma", "threshold_cap",
              "consensus_threshold"},
    "emd": {"method", "numIMFs", "tau", "tol", "alpha", "windowSize", "ensembles",
            "noiseStd", "energyVelocityLabel", "volumeRegimeLabel"},
    "xgboost": {"modelFile", "features", "objective", "num_class",
                "fast_mode_symbols"},
    "hmm": {"features", "n_states", "train_window", "warmup_period",
            "retrain_interval", "max_iter", "random_seed", "regularization",
            "state_order", "tol", "skip_while_warming"},
    # protection 用嵌套对象，key 是各个风控器的名字
    "protection": {"stop_loss", "take_profit", "trailing_stop", "time_stop",
                   "formula_stop", "atr_stop_loss", "ma_stop_loss", "r2_stop_loss",
                   "mae_stop_loss"},
}

# 只给前端渲染用、服务端不读的 key。写错无害，不告警。
DECORATIVE_PARAMS = {
    # 输入节点声明要输出哪些行情字段，服务端按 default_props 自己决定输出什么
    "input": {"open", "close", "high", "low", "volume", "turnover"},
    # 合约类型由前端的 source 参数决定，节点上的 contract 服务端不读
    "execution": {"contract"},
    # 信号节点的标的来自上游（SignalNode 从上游节点收集 symbols），节点上的
    # code / type（合约类型）服务端不读
    "signal": {"code", "type"},
}

DEFAULT_POSITION = {"x": 0, "y": 0}

# 输入节点默认输出的行情字段
DEFAULT_QUOTE_FIELDS = ("close", "open", "high", "low", "volume")


# ----------------------------------------------------------------------
# 节点工厂
# ----------------------------------------------------------------------

def _node(nid, node_type, label, params, position=None):
    """所有节点工厂的公共骨架。

    键顺序与前端导出的策略文件保持一致：id / type / position / data，
    data 内为 label / nodeType / params —— 顺序变了虽然 JSON 语义相同，
    但会让策略文件的无意义 diff 变大。
    """
    return {
        "id": nid,
        "type": "custom",
        "position": dict(position) if position else dict(DEFAULT_POSITION),
        "data": {
            "label": label,
            "nodeType": node_type,
            "params": params,
        },
    }


def _codes(symbol):
    """标的可传单个字符串或列表，统一成列表。"""
    return [symbol] if isinstance(symbol, str) else list(symbol)


def _to_bars(value, default=15):
    """窗口值转 bar 数：'15d' / '15' / 15 → 15。

    range 的单位是 bar，不带 s/m/h/d 后缀（迁移见 tools/migrate_range_to_bars.py）。
    """
    text = str(value).strip()
    if not text:
        return default
    return int(text.rstrip("smhdSMHD"))


def input_node(nid, symbol, *, source="股票", freq="1d", label="行情数据",
               fields=DEFAULT_QUOTE_FIELDS, position=None):
    """行情输入节点。

    `fields` 里的字段名（close/open/...）只给前端渲染用，服务端不读它们；
    服务端真正读的是 code / freq / source 三个。`source=None` 表示不写
    source 参数（服务端缺省按股票处理）。
    """
    params = {}
    if source is not None:
        params["source"] = P.text(source)
    params["code"] = P.text(_codes(symbol))
    params["freq"] = P.select(freq)
    for field in fields:
        params[field] = P.text(field)
    return _node(nid, "input", label, params, position)


def function_node(nid, method, range, *, label=None, position=None):
    """指标函数节点（MA / STD / Return / ZScore / R2 / Median / CumMax / VPCorr）。

    `range` 接受 15 / "15" / "15d"，统一序列化成 bar 数的数字。
    """
    return _node(nid, "function", label or method, {
        "method": P.select(method),
        "range": P.number(_to_bars(range)),
    }, position)


def formula_node(nid, expression, *, label="Formula", position=None):
    """公式节点。expression 是公式表达式文本。"""
    return _node(nid, "formula", label, {
        "expression": P.text(expression),
    }, position)


def debug_node(nid, label, *, suffix="csv", position=None):
    """调试输出节点，把上游序列导出成 CSV。"""
    return _node(nid, "debug", label, {
        "suffix": P.select(suffix),
    }, position)


def signal_node(nid, symbol, *, buy="true", sell="false", allow_short=None,
                label="买入信号", position=None):
    """信号节点。buy/sell 是表达式文本（"true" 表示恒真）。

    SignalNode 的标的来自上游节点（它从上游 QuoteInputNode 收集 symbols），
    节点上的 code 服务端不读，所以这里不写。`symbol` 参数保留，一是与其他
    节点工厂签名一致，二是后续若 SignalNode 改回按 code 取标的，放开下面
    注释掉的那行即可。
    """
    params = {
        # "code": P.text(_codes(symbol)),
        "buy": P.text(buy),
        "sell": P.text(sell),
    }
    if allow_short is not None:
        params["allowShort"] = P.boolean(allow_short)
    return _node(nid, "signal", label, params, position)


def portfolio_node(nid, *, position_ratio=1.0, allow_short=None, label="投资组合",
                   position=None):
    """投资组合节点。position_ratio 是仓位比例 (0.0~1.0)。"""
    params = {"positionRatio": P.number(position_ratio)}
    if allow_short is not None:
        params["allowShort"] = P.boolean(allow_short)
    return _node(nid, "portfolio", label, params, position)


def execution_node(nid, *, label="交易执行", commission=0.0, stamp_duty=0.0,
                   min_fee=0, slippage_model=0, slippage=0.0, order_type=1,
                   contract=0, params=None, position=None):
    """交易执行节点。

    成本四项（commission / stampDuty / minFee / slippage）由
    `StrategySubSystem.cpp` 读取；slippageModel 由 `Strategy.cpp` 读取；
    `contract` 服务端不读，只给前端用。

    `params` 可以整体替换默认参数字典 —— 用于产出精简版执行节点
    （只声明自己关心的参数，其余走服务端默认值）。
    """
    if params is None:
        params = {
            "commission": P.number(commission),
            "stampDuty": P.number(stamp_duty),
            "minFee": P.number(min_fee),
            "slippageModel": P.number(slippage_model),
            "slippage": P.number(slippage),
            "type": P.select(order_type),
            "contract": P.select(contract),
        }
    return _node(nid, "execution", label, params, position)


def cusum_node(nid, *, mode="changepoint", lambda_=0.5, threshold_multiplier=4.0,
               min_obs=10, cooldown=0, calibrate_period=0, mu=0.0, sigma=1.0,
               threshold_cap=0.0, label="cusum_signal", position=None):
    """CUSUM 节点。

    这里显式写出 calibrate_period / mu / sigma / threshold_cap，不依赖
    C++ CUSUMConfig 的结构体默认值——否则 C++ 默认值一改，测试语义会静默漂移
    （历史事故：1544005 把 _calibratePeriod 由 0 改为 1，使本用例从"未校准"
    变成"已校准"，输出由全零变成真实累积值，而测试本身毫无提示）。
    """
    return _node(nid, "cusum", label, {
        "mode": P.select(mode),
        "lambda": P.number(lambda_),
        "threshold_multiplier": P.number(threshold_multiplier),
        "min_obs": P.number(min_obs),
        "cooldown": P.number(cooldown),
        "calibrate_period": P.number(calibrate_period),
        "mu": P.number(mu),
        "sigma": P.number(sigma),
        "threshold_cap": P.number(threshold_cap),
    }, position)


def emd_node(nid, *, method="emd", num_imfs=5, label="EMD", position=None):
    """EMD 节点。"""
    return _node(nid, "emd", label, {
        "method": P.select(method),
        "numIMFs": P.number(num_imfs),
    }, position)


def xgboost_node(nid, model_file, *, features="", objective="binary:logistic",
                 num_class=2, label="XGBoost", position=None):
    """XGBoost 节点。"""
    return _node(nid, "xgboost", label, {
        "modelFile": P.text(model_file),
        "features": P.text(features),
        "objective": P.select(objective),
        "num_class": P.number(num_class),
    }, position)


def hmm_node(nid, *, n_states=3, train_window=252, warmup_period=60,
             retrain_interval=60, max_iter=100, random_seed=42,
             model_file=None, label="HMM", position=None):
    """HMM 节点。features 留空时由 HMMNode 从上游 out_elements() 自动推导。

    注意 modelFile 的位置：HMMNode 读的是 ``config["modelFile"]``，而 config
    就是 ``node["data"]``（Strategy.cpp:216），所以它是 data 的兄弟字段，
    不在 params 里。写在 params 下不会被读取（HMMNode.cpp:93）。
    """
    node = _node(nid, "hmm", label, {
        "n_states": P.number(n_states),
        "train_window": P.number(train_window),
        "warmup_period": P.number(warmup_period),
        "retrain_interval": P.number(retrain_interval),
        "max_iter": P.number(max_iter),
        "random_seed": P.number(random_seed),
    }, position)
    if model_file:
        node["data"]["modelFile"] = model_file
    return node


def protection_node(nid, *, label="风控保护", stop_loss=None, take_profit=None,
                    trailing_stop=None, time_stop=None, formula_stop=None,
                    atr_stop_loss=None, ma_stop_loss=None, r2_stop_loss=None,
                    mae_stop_loss=None, position=None):
    """风控保护节点。

    与其他节点不同，protection 用嵌套对象而不是 {"value":..,"type":..} 包装：
    服务端直接读 ``params["stop_loss"]["enabled"]``（ProtectionNode.cpp 嵌套
    格式分支），不经过 ["value"] 中转。

    每个风控器传一个 dict，例如::

        stop_loss={"enabled": True, "percent": 0.05}
        atr_stop_loss={"enabled": True, "period": 20, "multiplier": 2.0}

    服务端还支持中文扁平格式（"止损开关"）和英文扁平格式
    （"stop_loss_enabled"），这里统一产出嵌套格式。
    """
    params = {}
    for key, spec in (("stop_loss", stop_loss),
                      ("take_profit", take_profit),
                      ("trailing_stop", trailing_stop),
                      ("time_stop", time_stop),
                      ("formula_stop", formula_stop),
                      ("atr_stop_loss", atr_stop_loss),
                      ("ma_stop_loss", ma_stop_loss),
                      ("r2_stop_loss", r2_stop_loss),
                      ("mae_stop_loss", mae_stop_loss)):
        if spec is not None:
            params[key] = dict(spec)
    return _node(nid, "protection", label, params, position)


# ----------------------------------------------------------------------
# 边
# ----------------------------------------------------------------------

def make_edges(edge_list):
    """由元组列表生成边。

    每个元素是 ``(source, target)`` 或 ``(source, target, 源字段)``；带字段时
    sourceHandle 为 ``"<srcId>-<字段>"``（例如 ``"1-close"``），否则为
    ``"<srcId>"``。targetHandle 恒为 ``"<targetId>"``。
    """
    edges = []
    for spec in edge_list:
        if len(spec) == 3:
            src, dst, prop = spec
            handle = f"{src}-{prop}"
        else:
            src, dst = spec
            handle = src
        edges.append({
            "id": f"{handle}->{dst}",
            "source": src,
            "target": dst,
            "sourceHandle": handle,
            "targetHandle": dst,
            "type": "default",
        })
    return edges


# ----------------------------------------------------------------------
# 布局
# ----------------------------------------------------------------------

def auto_layout(nodes, edges, x_step=200, y_step=100):
    """按拓扑深度重排节点位置：横向按层、纵向按同层序号。

    返回新的节点列表（不修改入参）。
    """
    incoming = {node["id"]: [] for node in nodes}
    for edge in edges:
        incoming.setdefault(edge["target"], []).append(edge["source"])

    depth = {}

    def compute(nid, seen=()):
        if nid in depth:
            return depth[nid]
        if nid in seen:          # 环：就地截断，深度不再推进
            return 0
        value = 0
        for src in incoming.get(nid, []):
            value = max(value, compute(src, seen + (nid,)) + 1)
        depth[nid] = value
        return value

    for node in nodes:
        compute(node["id"])

    layers = {}
    for node in nodes:
        layers.setdefault(depth[node["id"]], []).append(node)

    laid_out = []
    for level, group in sorted(layers.items()):
        for index, node in enumerate(group):
            moved = dict(node)
            moved["position"] = {"x": level * x_step, "y": index * y_step}
            laid_out.append(moved)
    return laid_out


# ----------------------------------------------------------------------
# 构建器
# ----------------------------------------------------------------------

def _format_date(value):
    if isinstance(value, str):
        return value
    if isinstance(value, (datetime, date)):
        return value.strftime("%Y-%m-%d")
    return str(value)


class StrategyBuilder:
    """策略图装配器。

    节点与边先收集，最后 `build()` 一次性产出完整策略 dict：

        s = (StrategyBuilder("test_x", "名称", description="...",
                             backtest=("2024-01-01", "2024-12-31"),
                             source="A_hfq")
             .input("1", symbol="sz.800001")
             .function("2", "MA", 5, label="ma5")
             .connect("1", "2", prop="close")
             .build())

    `build()` 会校验边的两端节点都存在（写错节点 id 会静默产出断链的图），
    并对服务端不认识的参数名告警。
    """

    def __init__(self, strategy_id, name=None, *, version=1, description=None,
                 capital=None, backtest=None, source=None):
        self._id = strategy_id
        self._name = name if name is not None else strategy_id
        self._version = version
        self._description = description
        self._capital = capital
        self._backtest = backtest
        self._source = source
        self._nodes = []
        self._edge_specs = []

    # -- 节点 ----------------------------------------------------------

    def add(self, node):
        """直接塞一个已经构造好的节点 dict。"""
        self._nodes.append(node)
        return self

    def add_nodes(self, *nodes):
        for node in nodes:
            self.add(node)
        return self

    def input(self, nid, **kwargs):
        return self.add(input_node(nid, **kwargs))

    def function(self, nid, method, range, **kwargs):
        return self.add(function_node(nid, method, range, **kwargs))

    def formula(self, nid, expression, **kwargs):
        return self.add(formula_node(nid, expression, **kwargs))

    def debug(self, nid, label, **kwargs):
        return self.add(debug_node(nid, label, **kwargs))

    def signal(self, nid, symbol, **kwargs):
        return self.add(signal_node(nid, symbol, **kwargs))

    def portfolio(self, nid, **kwargs):
        return self.add(portfolio_node(nid, **kwargs))

    def execution(self, nid, **kwargs):
        return self.add(execution_node(nid, **kwargs))

    def cusum(self, nid, **kwargs):
        return self.add(cusum_node(nid, **kwargs))

    def emd(self, nid, **kwargs):
        return self.add(emd_node(nid, **kwargs))

    def xgboost(self, nid, model_file, **kwargs):
        return self.add(xgboost_node(nid, model_file, **kwargs))

    def hmm(self, nid, **kwargs):
        return self.add(hmm_node(nid, **kwargs))

    def protection(self, nid, **kwargs):
        return self.add(protection_node(nid, **kwargs))

    # -- 边 ------------------------------------------------------------

    def connect(self, source, target, prop=None):
        """连一条边。prop 是源节点要传出的字段名（"close" 等）。"""
        if prop is None:
            self._edge_specs.append((source, target))
        else:
            self._edge_specs.append((source, target, prop))
        return self

    def connect_all(self, specs):
        """批量连边，specs 是 (source, target) 或 (source, target, prop) 元组列表。"""
        for spec in specs:
            self.connect(*spec)
        return self

    # -- 输出 ----------------------------------------------------------

    def build(self, *, layout="keep", validate=True):
        """产出策略 dict。

        layout: "keep" 保留节点自带的位置；"auto" 按拓扑深度重排。
        validate: True 时校验边端点，并对未知参数名告警。
        """
        edges = make_edges(self._edge_specs)
        if validate:
            self._validate(edges)

        nodes = self._nodes
        if layout == "auto":
            nodes = auto_layout(nodes, edges)
        elif layout != "keep":
            raise ValueError(f"未知 layout: {layout!r}（可选 'keep' / 'auto'）")

        envelope = {
            "id": self._id,
            "name": self._name,
            "version": self._version,
        }
        if self._description is not None:
            envelope["description"] = self._description
        if self._capital is not None:
            envelope["capital"] = self._capital
        if self._backtest is not None:
            envelope["backtest"] = self._backtest
        if self._source is not None:
            envelope["source"] = self._source
        envelope["nodes"] = nodes
        envelope["edges"] = edges
        return envelope

    def dump(self, path, **kwargs):
        """build() 并写文件。kwargs 透传给 json.dump。"""
        import json
        kwargs.setdefault("indent", 2)
        kwargs.setdefault("ensure_ascii", False)
        payload = self.build()
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, **kwargs)
        return payload

    # -- 内部 ----------------------------------------------------------

    def _validate(self, edges):
        known = {node["id"] for node in self._nodes}
        for edge in edges:
            if edge["source"] not in known:
                raise ValueError(
                    f"边的 source 节点不存在: {edge['source']!r} → "
                    f"{edge['target']!r}（已知节点: {sorted(known)}）")
            if edge["target"] not in known:
                raise ValueError(
                    f"边的 target 节点不存在: {edge['source']!r} → "
                    f"{edge['target']!r}（已知节点: {sorted(known)}）")

        for node in self._nodes:
            data = node.get("data", {})
            node_type = data.get("nodeType")
            params = data.get("params") or {}
            allowed = KNOWN_PARAMS.get(node_type)
            if allowed is None:
                continue
            decorative = DECORATIVE_PARAMS.get(node_type, set())
            for key in params:
                if key in allowed or key in decorative:
                    continue
                warnings.warn(
                    f"节点 {node['id']!r}({node_type}) 的参数 {key!r} 不在服务端"
                    f"已知参数表里，会被静默忽略。已知参数: {sorted(allowed)}",
                    stacklevel=3)


def make_strategy(strategy_id, nodes, edges, *, name=None, version=1,
                  description=None, capital=None, backtest=None, source=None):
    """一次性产出策略 dict（不经过构建器）。

    保留给只需要"拼一份 dict"的场景；需要校验/布局时用 `StrategyBuilder`。
    """
    builder = StrategyBuilder(strategy_id, name, version=version,
                              description=description, capital=capital,
                              backtest=backtest, source=source)
    return builder.add_nodes(*nodes).connect_all(edges).build()


__all__ = [
    "P", "param",
    "input_node", "function_node", "formula_node", "debug_node", "signal_node",
    "portfolio_node", "execution_node", "cusum_node", "emd_node",
    "xgboost_node", "hmm_node", "protection_node",
    "make_edges", "auto_layout", "make_strategy", "StrategyBuilder",
    "KNOWN_PARAMS", "DECORATIVE_PARAMS",
]
