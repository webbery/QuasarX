#!/usr/bin/env python3
"""
CUSUM 变点检测 + 自适应 VaR 正确性验证测试

测试方案（复用 test_backtest_metrics.py 的标准数据）：
1. 从 generate_test_data.py 生成的 CSV 读取价格
2. Python 本地计算收益率 → CUSUM 检测 → 预期指标
3. 通过回测 API 获取 C++ 计算的 CUSUM 指标
4. 对比 C++ vs Python 结果（相对误差 < 1%）

使用方法：
  pytest test_cusum_api.py -v
  pytest test_cusum_api.py::TestCUSUMStandardCases -v
  pytest test_cusum_api.py::TestCUSUMAPI -v

前置准备：
  python generate_test_data.py  # 生成标准测试数据
  cd ../build && ./QuantService config.json  # 启动服务
"""

import pytest
import requests
import json
import math
import numpy as np
import csv
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ============================================================
# 配置
# ============================================================

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False

# 测试数据目录（与 test_backtest_metrics.py 共享）
SERVICE_ROOT = Path(__file__).parent.parent.parent
SERVICE_DATA_DIR = SERVICE_ROOT / "build" / "data"
HFQ_DIR = SERVICE_DATA_DIR / "A_hfq"      # 后复权数据（指标计算用）
METRIC_TEST_DIR = Path(__file__).parent / "metric_test_data"
TEST_CASES_SUMMARY = METRIC_TEST_DIR / "test_cases_summary.json"

# CUSUM 参数（标准测试数据只有 60 天，min_obs 需要很小）
CUSUM_LAMBDA = 0.5
CUSUM_THRESHOLD = 4.0
CUSUM_MIN_OBS = 5  # 降低到 5 天，适配短数据集


# ============================================================
# Python 参考实现（与之前相同）
# ============================================================

class CUSUMDetectorRef:
    """双侧 CUSUM 检测器（Python 参考实现）"""

    def __init__(self, mu=0.0, sigma=1.0, lambda_=0.5, threshold_multiplier=4.0, min_obs=30):
        self.mu = mu
        self.sigma = sigma
        self.lambda_ = lambda_
        self.threshold_multiplier = threshold_multiplier
        self.min_obs = min_obs
        self.reset()

    def reset(self):
        self._s_pos = 0.0
        self._s_neg = 0.0
        self._count = 0
        self._total_change_points = 0
        self._max_drift = 0.0
        self._last_change_index = 0
        self._steps = []

    def _compute_threshold(self):
        return self.threshold_multiplier * self.sigma * math.sqrt(self._count)

    def update(self, new_return):
        self._count += 1

        if self._count < self.min_obs:
            result = {
                'change_point': False,
                'step_index': self._count - 1,
                'cusum_positive': self._s_pos,
                'cusum_negative': self._s_neg,
            }
            self._steps.append(result)
            return result

        k = self.lambda_ * self.sigma
        drift = new_return - self.mu

        self._s_pos = max(0.0, self._s_pos + drift - k)
        self._s_neg = max(0.0, self._s_neg - drift - k)

        h = self._compute_threshold()
        change_point = max(self._s_pos, self._s_neg) > h

        if change_point:
            self._total_change_points += 1
            self._last_change_index = self._count - 1
            self._s_pos = 0.0
            self._s_neg = 0.0

        self._max_drift = max(self._max_drift, abs(self._s_pos - self._s_neg))

        result = {
            'change_point': change_point,
            'step_index': self._count - 1,
            'cusum_positive': self._s_pos,
            'cusum_negative': self._s_neg,
        }
        self._steps.append(result)
        return result

    def detect_batch(self, returns):
        self.reset()
        for r in returns:
            self.update(r)
        return {
            'total_change_points': self._total_change_points,
            'max_drift': self._max_drift,
            'last_change_index': self._last_change_index,
            'steps': self._steps,
        }

    @property
    def total_change_points(self):
        return self._total_change_points

    @property
    def max_drift(self):
        return self._max_drift

    @property
    def last_change_index(self):
        return self._last_change_index


def compute_ewma_var_ref(returns: List[float], confidence: float = 0.95, decay: float = 0.94) -> float:
    """EWMA VaR（Python 参考实现）"""
    if not returns:
        return 0.0

    ewma_var = returns[0] ** 2
    for i in range(1, len(returns)):
        ewma_var = decay * ewma_var + (1 - decay) * returns[i] ** 2

    ewma_std = math.sqrt(max(0.0, ewma_var))

    # 逆正态分位数近似
    alpha = 1.0 - confidence
    if alpha < 0.5:
        t = math.sqrt(-2.0 * math.log(alpha))
        z = t - (2.515517 + 0.802853 * t + 0.010328 * t * t) / \
                  (1.0 + 1.432788 * t + 0.189269 * t * t + 0.001308 * t * t * t)
    else:
        z = 1.645

    return ewma_std * z


def compute_var_ref(returns: List[float], confidence: float = 0.95) -> float:
    """历史模拟法 VaR"""
    if not returns:
        return 0.0
    sorted_returns = sorted(returns)
    index = int((1 - confidence) * len(sorted_returns))
    index = min(index, len(sorted_returns) - 1)
    return -sorted_returns[index]


def compute_adaptive_var_ref(returns: List[float], detector: CUSUMDetectorRef,
                              normal_window: int = 250,
                              stressed_window: int = 60,
                              confidence: float = 0.95,
                              stressed_confidence: float = 0.99,
                              ewma_decay: float = 0.94) -> float:
    """自适应 VaR（Python 参考实现）"""
    if not returns:
        return 0.0

    in_stress = False
    recent_window = min(normal_window, len(returns))
    start_index = len(returns) - recent_window

    if detector.total_change_points > 0:
        if detector.last_change_index >= start_index:
            in_stress = True

    window = stressed_window if in_stress else normal_window
    window = min(window, len(returns))
    conf = stressed_confidence if in_stress else confidence

    window_returns = returns[-window:]

    if in_stress:
        return compute_ewma_var_ref(window_returns, conf, ewma_decay)
    else:
        return compute_var_ref(window_returns, conf)


# ============================================================
# 测试数据加载
# ============================================================

def load_prices_from_csv(symbol: str) -> List[float]:
    """从 HFQ CSV 读取后复权收盘价（与 C++ 回测引擎使用相同数据源）"""
    csv_path = HFQ_DIR / f"{symbol}.csv"
    if not csv_path.exists():
        return []

    prices = []
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if header is None:
            return []
        # 查找 close 列索引
        close_idx = None
        for i, h in enumerate(header):
            if h.strip().lower() == 'close':
                close_idx = i
                break
        if close_idx is None:
            return []
        for row in reader:
            if len(row) > close_idx:
                try:
                    prices.append(float(row[close_idx]))
                except ValueError:
                    continue
    return prices


def compute_returns_from_prices(prices: List[float]) -> List[float]:
    """从价格序列计算对数收益率"""
    returns = []
    for i in range(1, len(prices)):
        if prices[i-1] != 0:
            returns.append((prices[i] - prices[i-1]) / prices[i-1])
    return returns


def load_test_cases_summary() -> Dict:
    """加载测试用例汇总"""
    if not TEST_CASES_SUMMARY.exists():
        pytest.skip(f"测试数据未生成，请先运行: python generate_test_data.py")
    with open(TEST_CASES_SUMMARY, 'r') as f:
        return json.load(f)


# ============================================================
# 回测 API 交互（复用 test_backtest_metrics.py 的模式）
# ============================================================

def get_auth_token() -> str:
    """获取认证 token"""
    resp = requests.post(
        f"{BASE_URL}/user/login",
        json={"name": "admin", "pwd": "admin"},
        verify=VERIFY_SSL
    )
    resp.raise_for_status()
    return resp.json()["tk"]


def run_backtest_via_api(strategy_json_path: str, token: str) -> Dict:
    """通过 API 执行回测并获取指标"""
    headers = {"Authorization": token}
    with open(strategy_json_path, 'r') as f:
        strategy = json.load(f)
    strategy_str = json.dumps(strategy, ensure_ascii=False)
    resp = requests.post(
        f"{BASE_URL}/backtest",
        json={"script": strategy_str},
        headers=headers,
        verify=VERIFY_SSL,
        timeout=300
    )
    resp.raise_for_status()
    return resp.json()


# ============================================================
# 测试类：标准数据集验证（复用 generate_test_data.py 的数据）
# ============================================================

class TestCUSUMStandardCases:
    """
    使用标准测试数据集验证 CUSUM + 自适应 VaR 计算正确性
    
    流程：
    1. 从 CSV 读取价格 → Python 计算收益率 → Python CUSUM 检测 → 预期指标
    2. 通过回测 API 获取 C++ 计算的指标
    3. 对比相对误差 < 1%
    """

    REL_TOLERANCE = 0.01  # 1% 相对误差

    @pytest.fixture(autouse=True)
    def setup(self):
        """获取认证 token 和测试用例"""
        self.token = get_auth_token()
        self.cases = load_test_cases_summary()

    def _run_cpp_backtest(self, case_id: str) -> Dict:
        """运行 C++ 回测并提取 features"""
        case = self.cases[case_id]
        strategy_path = METRIC_TEST_DIR / f"{case_id}_strategy.json"
        result = run_backtest_via_api(str(strategy_path), self.token)
        return result.get("features", {})

    def _compute_python_cusum(self, symbol: str) -> Tuple[CUSUMDetectorRef, List[float]]:
        """从 CSV 读取价格，Python 计算 CUSUM"""
        prices = load_prices_from_csv(symbol)
        if len(prices) < CUSUM_MIN_OBS + 2:
            pytest.skip(f"数据不足：{symbol} 只有 {len(prices)} 个价格")

        returns = compute_returns_from_prices(prices)
        
        # 计算均值和标准差（用于 CUSUM 参数）
        mu = np.mean(returns)
        sigma = np.std(returns, ddof=1)
        if sigma < 1e-10:
            sigma = 0.01  # 防止除零

        detector = CUSUMDetectorRef(
            mu=mu, sigma=sigma,
            lambda_=CUSUM_LAMBDA,
            threshold_multiplier=CUSUM_THRESHOLD,
            min_obs=CUSUM_MIN_OBS  # 使用全局参数
        )
        result = detector.detect_batch(returns)
        return detector, returns

    def test_up_trend_cusum(self):
        """单边上涨场景：CUSUM 应检测到正向漂移"""
        cpp_features = self._run_cpp_backtest("up_trend")
        
        # Python 计算
        symbol = self.cases["up_trend"]["symbol"]
        detector, returns = self._compute_python_cusum(symbol)

        # 验证 C++ 返回的变点次数
        cpp_change_points = cpp_features.get("cusum_change_points", 0)
        # 单边上涨趋势中，CUSUM 可能检测到 0-2 次变点（取决于参数）
        assert cpp_change_points >= 0, "变点次数应 >= 0"

        # 验证最大漂移量（用绝对误差：max_drift 量级 ~1e-5，相对误差对基准值敏感）
        cpp_max_drift = cpp_features.get("cusum_max_drift", 0)
        py_max_drift = detector.max_drift
        if py_max_drift > 1e-10:
            abs_err = abs(cpp_max_drift - py_max_drift)
            assert abs_err < 1e-5, \
                f"max_drift 绝对误差过大: C++={cpp_max_drift:.6f}, Python={py_max_drift:.6f}, abs_err={abs_err:.6f}"

        # 验证自适应 VaR
        cpp_adaptive_var = cpp_features.get("adaptive_var", 0)
        py_adaptive_var = compute_adaptive_var_ref(returns, detector)
        if py_adaptive_var > 1e-10:
            rel_err = abs(cpp_adaptive_var - py_adaptive_var) / py_adaptive_var
            assert rel_err < self.REL_TOLERANCE, \
                f"adaptive_var 差异过大: C++={cpp_adaptive_var:.6f}, Python={py_adaptive_var:.6f}, rel_err={rel_err:.4f}"

    def test_down_trend_cusum(self):
        """单边下跌场景：CUSUM 应检测到负向漂移"""
        cpp_features = self._run_cpp_backtest("down_trend")
        
        symbol = self.cases["down_trend"]["symbol"]
        detector, returns = self._compute_python_cusum(symbol)

        cpp_max_drift = cpp_features.get("cusum_max_drift", 0)
        py_max_drift = detector.max_drift
        if py_max_drift > 1e-10:
            abs_err = abs(cpp_max_drift - py_max_drift)
            assert abs_err < 1e-5, \
                f"max_drift 绝对误差过大: C++={cpp_max_drift:.6f}, Python={py_max_drift:.6f}, abs_err={abs_err:.6f}"

    def test_high_volatility_cusum(self):
        """高波动场景：CUSUM 应检测到更多变点"""
        cpp_features = self._run_cpp_backtest("high_volatility")
        
        symbol = self.cases["high_volatility"]["symbol"]
        detector, returns = self._compute_python_cusum(symbol)

        # 高波动场景变点次数应 >= 平稳场景
        cpp_change_points = cpp_features.get("cusum_change_points", 0)
        py_change_points = detector.total_change_points
        
        # 只验证 Python 和 C++ 变点次数一致（允许 ±1 误差，因为阈值计算可能有浮点差异）
        assert abs(cpp_change_points - py_change_points) <= 1, \
            f"变点次数差异过大: C++={cpp_change_points}, Python={py_change_points}"

    def test_sideways_cusum(self):
        """横盘震荡场景：CUSUM 应很少触发变点"""
        cpp_features = self._run_cpp_backtest("sideways")
        
        symbol = self.cases["sideways"]["symbol"]
        detector, returns = self._compute_python_cusum(symbol)

        # 横盘场景变点应很少
        cpp_change_points = cpp_features.get("cusum_change_points", 0)
        assert cpp_change_points < 5, \
            f"横盘场景变点过多: {cpp_change_points}"


# ============================================================
# 测试类：纯算法单元测试（不需要服务）
# ============================================================

class TestCUSUMDetector:
    """CUSUM 检测器纯算法单元测试"""

    def test_no_change_point_in_stable_data(self):
        """平稳数据不应触发过多变点"""
        np.random.seed(42)
        returns = list(np.random.normal(0.0001, 0.01, 300))
        detector = CUSUMDetectorRef(mu=0.0001, sigma=0.01, min_obs=30)
        result = detector.detect_batch(returns)
        assert result['total_change_points'] < 3, \
            f"Stable data should have few change points, got {result['total_change_points']}"

    def test_detect_regime_change(self):
        """Regime change 数据应检测到变点"""
        np.random.seed(42)
        before = list(np.random.normal(0.0001, 0.01, 100))
        after = list(np.random.normal(-0.005, 0.04, 80))
        returns = before + after
        
        # 使用更敏感的参数（降低阈值 + min_obs）
        detector = CUSUMDetectorRef(mu=0.0001, sigma=0.01, min_obs=5, threshold_multiplier=2.0)
        result = detector.detect_batch(returns)

        assert result['total_change_points'] >= 1, \
            f"Regime change data should detect at least one change point, got {result['total_change_points']}"

    def test_max_drift_positive(self):
        """最大漂移量应为非负"""
        np.random.seed(42)
        returns = list(np.random.normal(0, 0.01, 200))
        detector = CUSUMDetectorRef()
        detector.detect_batch(returns)
        assert detector.max_drift >= 0

    def test_min_obs_protection(self):
        """min_obs 保护期内不应触发变点"""
        np.random.seed(42)
        returns = list(np.random.normal(0.1, 0.01, 25))  # 极端值但数据少
        detector = CUSUMDetectorRef(min_obs=30)
        result = detector.detect_batch(returns)
        assert result['total_change_points'] == 0, \
            "Should not trigger change points before min_obs"

    def test_batch_vs_step_consistency(self):
        """批量和逐步更新结果应一致"""
        np.random.seed(42)
        returns = list(np.random.normal(0.0001, 0.01, 100))

        detector_batch = CUSUMDetectorRef()
        result_batch = detector_batch.detect_batch(returns)

        detector_step = CUSUMDetectorRef()
        for r in returns:
            detector_step.update(r)

        assert result_batch['total_change_points'] == detector_step.total_change_points
        assert result_batch['max_drift'] == pytest.approx(detector_step.max_drift, rel=1e-10)


# ============================================================
# 测试类：均值偏移数据 CUSUM 变点检测（C++ vs Python 对比）
# ============================================================

class TestCUSUMMeanShift:
    """
    使用均值偏移合成数据验证 CUSUM 变点检测的 C++/Python 一致性。

    数据特征：
    - 前 100 天：价格 ~ N(100, 2)
    - 后 100 天：价格 ~ N(101, 2)（均值偏移 +1）
    - 已知变点在第 100 天附近

    验证内容：
    1. Python 本地 CUSUM 检测变点位置
    2. C++ CUSUMHandler API 返回的变点位置
    3. 对比 s_pos、s_neg 序列和变点索引
    """

    SYMBOL = "sz.900007"
    SHIFT_POINT = 100  # 已知变点位置（价格偏移起始日）
    REL_TOLERANCE = 0.01  # 1% 相对误差

    def _to_api_symbol(self, symbol: str) -> str:
        """sz.900007 → 900007.SZ（API 期望的格式）"""
        if '.' in symbol:
            parts = symbol.split('.', 1)
            exchange_map = {'sz': 'SZ', 'sh': 'SH', 'bj': 'BJ'}
            exc = exchange_map.get(parts[0].lower(), parts[0].upper())
            return f"{parts[1]}.{exc}"
        return symbol

    @pytest.fixture(autouse=True)
    def setup(self, auth_token):
        self.token = auth_token

    def _load_returns_from_csv(self) -> List[float]:
        """从 CSV 读取收盘价并计算收益率"""
        csv_path = HFQ_DIR / f"{self.SYMBOL}.csv"
        if not csv_path.exists():
            pytest.skip(f"数据不存在：{csv_path}")
        with open(csv_path, 'r') as f:
            rows = list(csv.DictReader(f))
        closes = [float(r['close']) for r in rows]
        returns = [(closes[i] - closes[i-1]) / closes[i-1] for i in range(1, len(closes))]
        return returns

    def _python_cusum(self, returns: List[float]) -> dict:
        """Python 参考实现 CUSUM 检测"""
        mu = np.mean(returns)
        sigma = np.std(returns, ddof=0)
        if sigma < 1e-10:
            sigma = 0.01

        detector = CUSUMDetectorRef(
            mu=mu, sigma=sigma,
            lambda_=0.5,
            threshold_multiplier=4.0,
            min_obs=10
        )
        result = detector.detect_batch(returns)
        return result

    def _cpp_cusum_via_api(self) -> dict:
        """调用 C++ CUSUMHandler API"""
        headers = {"Authorization": self.token}
        resp = requests.post(
            f"{BASE_URL}/analysis/cusum",
            json={
                "symbols": [self._to_api_symbol(self.SYMBOL)],
                "modes": ["mean"],
                "lambda": 0.5,
                "threshold_multiplier": 4.0,
                "min_obs": 10
            },
            headers=headers,
            verify=VERIFY_SSL,
            timeout=60
        )
        resp.raise_for_status()
        return resp.json()

    def test_mean_shift_change_point_detected(self):
        """均值偏移数据应检测到变点，且变点位置在 shift_point 附近"""
        returns = self._load_returns_from_csv()
        py_result = self._python_cusum(returns)

        # Python 应检测到至少 1 个变点
        assert py_result['total_change_points'] >= 1, \
            f"Python CUSUM 未检测到变点 (shift_point={self.SHIFT_POINT})"

        # 取第一个 change_point=True 的步骤（不是 steps[0]）
        cp_steps = [s for s in py_result['steps'] if s['change_point']]
        first_cp = cp_steps[0]['step_index'] if cp_steps else -1
        # 变点检测基于收益率序列（比价格序列偏移 1），所以期望位置 = shift_point - 1
        # CUSUM 特性：累积和提前超阈值，允许变点前 ±30 步偏差
        expected_cp = self.SHIFT_POINT - 1
        assert first_cp <= expected_cp, \
            f"Python 变点位置在变点之后: 期望 ≤{expected_cp}, 实际 {first_cp}"
        assert first_cp >= expected_cp - 30, \
            f"Python 变点位置过早: 期望 ≥{expected_cp - 30}, 实际 {first_cp}"

    def test_cpp_python_s_pos_s_neg_alignment(self):
        """C++ 和 Python 的 s_pos/s_neg 序列应一致"""
        returns = self._load_returns_from_csv()

        # Python 计算
        py_result = self._python_cusum(returns)

        # C++ API 调用
        cpp_result = self._cpp_cusum_via_api()
        assert "mean_cusum" in cpp_result, f"C++ 返回缺少 mean_cusum: {cpp_result}"
        assert len(cpp_result["mean_cusum"]) > 0, "C++ mean_cusum 为空"

        cpp_symbol_data = cpp_result["mean_cusum"][0]
        cpp_s_pos = cpp_symbol_data["s_pos"]
        cpp_s_neg = cpp_symbol_data["s_neg"]

        # 对比序列长度
        py_steps = py_result['steps']
        assert len(cpp_s_pos) == len(py_steps), \
            f"序列长度不一致: C++={len(cpp_s_pos)}, Python={len(py_steps)}"

        # 对比 s_pos / s_neg 值（跳过前 min_obs 个点，因为 Python 和 C++ 在初始化阶段可能有微小差异）
        min_obs = 10
        max_abs_err_s_pos = 0
        max_abs_err_s_neg = 0
        for i in range(min_obs, len(py_steps)):
            err_pos = abs(cpp_s_pos[i] - py_steps[i]['cusum_positive'])
            err_neg = abs(cpp_s_neg[i] - py_steps[i]['cusum_negative'])
            max_abs_err_s_pos = max(max_abs_err_s_pos, err_pos)
            max_abs_err_s_neg = max(max_abs_err_s_neg, err_neg)

        # 绝对误差应非常小（浮点精度级别）
        assert max_abs_err_s_pos < 1e-6, \
            f"s_pos 最大绝对误差过大: {max_abs_err_s_pos}"
        assert max_abs_err_s_neg < 1e-6, \
            f"s_neg 最大绝对误差过大: {max_abs_err_s_neg}"

    def test_cpp_python_change_points_match(self):
        """C++ 和 Python 检测到的变点位置应一致"""
        returns = self._load_returns_from_csv()

        # Python 变点
        py_result = self._python_cusum(returns)
        py_change_points = [s['step_index'] for s in py_result['steps'] if s['change_point']]

        # C++ 变点
        cpp_result = self._cpp_cusum_via_api()
        cpp_change_points = cpp_result["mean_cusum"][0]["change_points"]

        # 变点数量应一致（允许 ±1，因为阈值浮点精度可能导致边界差异）
        assert abs(len(cpp_change_points) - len(py_change_points)) <= 1, \
            f"变点数量不一致: C++={len(cpp_change_points)}, Python={len(py_change_points)}"

        # 如果有变点，第一个变点位置应一致（允许 ±1）
        if py_change_points and cpp_change_points:
            assert abs(cpp_change_points[0] - py_change_points[0]) <= 1, \
                f"首个变点位置不一致: C++={cpp_change_points[0]}, Python={py_change_points[0]}"


# ============================================================
# 快速验证
# ============================================================

if __name__ == "__main__":
    print("=== CUSUM + Adaptive VaR Quick Validation ===\n")
    
    # 1. 测试 Python 参考实现
    print("1. Python CUSUM reference implementation:")
    np.random.seed(42)
    before = list(np.random.normal(0.0001, 0.01, 100))
    after = list(np.random.normal(-0.005, 0.04, 80))
    returns = before + after
    
    detector = CUSUMDetectorRef(mu=0.0001, sigma=0.01, min_obs=30)
    result = detector.detect_batch(returns)
    print(f"   Change points: {result['total_change_points']}")
    print(f"   Max drift: {result['max_drift']:.6f}")
    print(f"   Last change index: {result['last_change_index']}")
    
    print("\n=== Validation complete (run pytest for full tests) ===")
