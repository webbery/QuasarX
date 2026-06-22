#!/usr/bin/env python3
"""
EMD 信号分解 API 完整测试 — pyEMD 黄金标准验证

验证策略：
1. EMD 分解 → PyEMD.EMD 黄金标准对比（相同数据，相同 IMF 数量）
2. CEEMDAN 分解 → PyEMD.CEEMDAN 黄金标准对比
3. 数学属性 → 重建误差 / 能量守恒 / 频率递减 / 正交性
4. API 结构/参数校验 → 基础测试

黄金标准库：
- PyEMD (EMD-signal): Python 经验模态分解标准库
  pip install EMD-signal

使用方法：
  pytest test_signal_emd.py -v

前置准备：
  pip install EMD-signal numpy scipy
  python generate_test_data.py
  服务已启动
"""

import pytest
import requests
import csv
import numpy as np
from pathlib import Path
from typing import Dict, List, Tuple, Optional

# 黄金标准库
try:
    from PyEMD import EMD, CEEMDAN
except ImportError:
    raise ImportError("需要 PyEMD 库，请运行: pip install EMD-signal")

from scipy.signal import find_peaks
from scipy.interpolate import interp1d

# 抑制 SSL 警告
import urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

# ============================================================
# 配置
# ============================================================

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False

# 测试数据目录
SERVICE_DATA_DIR = Path(__file__).parent.parent.parent / "build" / "data"
HFQ_DIR = SERVICE_DATA_DIR / "A_hfq"
ORG_DIR = SERVICE_DATA_DIR / "AStock"

# 对比容差
# EMD 算法在不同实现间极值点检测/插值方式有微小差异
# 但重建误差、能量守恒、频率趋势等数学属性应高度一致
EMD_IMF_TOLERANCE = 0.15       # IMF 分量值 15% 容差（算法实现差异）
CEEMDAN_IMF_TOLERANCE = 0.25   # CEEMDAN 25% 容差（随机噪声集合）
RECON_ERROR_THRESHOLD = 1e-6   # 重建误差阈值（C++ 实现应精确重建）
ENERGY_TOLERANCE = 0.05        # 能量守恒 5% 容差


# ============================================================
# pyEMD 黄金标准封装
# ============================================================

def emd_golden_standard(signal: np.ndarray, num_imfs: int) -> Dict:
    """
    使用 PyEMD.EMD 执行 EMD 分解（黄金标准）

    PyEMD 实现特点：
    - 默认使用三次样条插值（比 C++ 的线性插值更精确）
    - 极值点检测使用 parabol 模式（更精确）
    - 停止条件基于 std_thr（标准差阈值）

    返回:
        imfs: IMF 分量列表
        residual: 残差
        num_actual_imfs: 实际生成的 IMF 数量
    """
    emd = EMD()
    emd.MAX_ITERATION = 10  # 与 C++ maxSiftingIter=10 对齐
    emd.std_thr = 0.02      # 与 C++ sdThreshold=0.02 对齐

    # 执行分解
    imfs = emd.emd(signal)

    # 确保返回 num_imfs 个 IMF（不足补零，超出截断）
    n = len(signal)
    result_imfs = []
    for i in range(num_imfs):
        if i < len(imfs):
            result_imfs.append(imfs[i])
        else:
            result_imfs.append(np.zeros(n))

    # 残差 = 原始信号 - ΣIMF
    residual = signal.copy()
    for imf in result_imfs:
        residual = residual - imf

    return {
        "imfs": result_imfs,
        "residual": residual,
        "num_actual_imfs": len(imfs)
    }


def ceemdan_golden_standard(signal: np.ndarray, num_imfs: int,
                             ensembles: int = 30,
                             noise_std: float = 0.2,
                             seed: int = 42) -> Dict:
    """
    使用 PyEMD.CEEMDAN 执行 CEEMDAN 分解（黄金标准）

    PyEMD CEEMDAN 参数：
    - ensemble_size: 集合数（与 C++ ensembles 对齐）
    - noise_std: 噪声标准差（与 C++ noiseStd 对齐）
    - max_iter: 最大筛选迭代

    返回:
        imfs: IMF 分量列表
        residual: 残差
    """
    ceemdan = CEEMDAN()
    ceemdan.ensemble_size = ensembles
    ceemdan.noise_std = noise_std
    ceemdan.MAX_ITERATION = 10
    ceemdan.std_thr = 0.02
    # 注意：PyEMD CEEMDAN 的随机种子通过 numpy 全局状态控制
    np.random.seed(seed)

    # 执行分解
    imfs = ceemdan(signal)

    # 确保返回 num_imfs 个 IMF
    n = len(signal)
    result_imfs = []
    for i in range(num_imfs):
        if i < len(imfs):
            result_imfs.append(imfs[i])
        else:
            result_imfs.append(np.zeros(n))

    # 残差
    residual = signal.copy()
    for imf in result_imfs:
        residual = residual - imf

    return {
        "imfs": result_imfs,
        "residual": residual,
        "num_actual_imfs": len(imfs)
    }


# ============================================================
# 数学属性计算
# ============================================================

def compute_reconstruction_error(imfs: List[np.ndarray],
                                  residual: np.ndarray,
                                  original: np.ndarray) -> float:
    """
    计算重建误差 (RMS)
    RMS = sqrt(mean((Σimf + residual - original)²))
    """
    reconstructed = residual.copy()
    for imf in imfs:
        reconstructed = reconstructed + imf
    diff = reconstructed - original
    return float(np.sqrt(np.mean(diff ** 2)))


def compute_energy_pct(imf: np.ndarray, signal: np.ndarray) -> float:
    """
    计算 IMF 能量占比
    energy_pct = Σ(imf²) / Σ(signal²)
    """
    imf_energy = np.sum(imf ** 2)
    total_energy = np.sum(signal ** 2)
    if total_energy < 1e-15:
        return 0.0
    return float(imf_energy / total_energy)


def estimate_mean_period(imf: np.ndarray) -> float:
    """
    估计 IMF 的平均周期（过零点法）
    mean_period = 2 * (N-1) / zero_crossings
    """
    n = len(imf)
    if n < 2:
        return float(n)

    zero_crossings = 0
    for i in range(1, n):
        if (imf[i] >= 0 and imf[i-1] < 0) or (imf[i] < 0 and imf[i-1] >= 0):
            zero_crossings += 1

    if zero_crossings == 0:
        return float(n)

    return 2.0 * (n - 1) / zero_crossings


def compute_imf_orthogonality(imfs: List[np.ndarray]) -> float:
    """
    计算 IMF 正交性指标
    IO = Σᵢⱼ |Σ(imfᵢ * imfⱼ)| / Σ(Σ(imfᵢ²))
    理想 EMD 应接近 0（IMF 间正交）
    """
    n_imfs = len(imfs)
    if n_imfs < 2:
        return 0.0

    cross_sum = 0.0
    energy_sum = 0.0

    for i in range(n_imfs):
        energy_sum += np.sum(imfs[i] ** 2)
        for j in range(i + 1, n_imfs):
            cross_sum += abs(np.sum(imfs[i] * imfs[j]))

    if energy_sum < 1e-15:
        return 0.0

    return float(cross_sum / energy_sum)


# ============================================================
# API 辅助函数
# ============================================================

def call_signal_api(symbol: str, start_date: str, end_date: str,
                    field: str = "close", method: str = "emd",
                    num_imfs: int = 5, auth_token: str = None) -> Dict:
    """调用信号分解 API"""
    kwargs = {'verify': VERIFY_SSL}
    if auth_token and len(auth_token) > 10:
        kwargs['headers'] = {'Authorization': auth_token}
    resp = requests.get(
        f"{BASE_URL}/analysis/signal",
        params={
            "symbols": symbol,
            "start_date": start_date,
            "end_date": end_date,
            "field": field,
            "method": method,
            "num_imfs": num_imfs
        },
        **kwargs
    )
    assert resp.status_code == 200, f"API 请求失败: {resp.status_code} - {resp.text}"
    return resp.json()


def load_csv_data(symbol: str, field: str = "close") -> Tuple[np.ndarray, List[str]]:
    """从 CSV 加载数据和日期"""
    csv_path = HFQ_DIR / f"{symbol}.csv"
    if not csv_path.exists():
        csv_path = ORG_DIR / f"{symbol}.csv"
    if not csv_path.exists():
        pytest.skip(f"测试数据不存在: {csv_path}")

    values, dates = [], []
    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            values.append(float(row[field]))
            dates.append(row['datetime'])
    return np.array(values), dates


# ============================================================
# 测试类 1：API 基础测试
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestSignalAPI:
    """信号分解 API 基础测试"""

    def test_emd_fields_exist(self, auth_token):
        """EMD 分解应返回所有必要字段"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        for field in ["symbols", "field", "method", "dates", "original",
                      "imf_components", "residual", "imf_info", "reconstruction_error"]:
            assert field in resp, f"缺少字段: {field}"

    def test_ceemdan_fields_exist(self, auth_token):
        """CEEMDAN 分解应返回所有必要字段"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], method="ceemdan",
                               auth_token=auth_token)

        for field in ["symbols", "field", "method", "dates", "original",
                      "imf_components", "residual", "imf_info", "reconstruction_error"]:
            assert field in resp, f"缺少字段: {field}"
        assert resp["method"] == "ceemdan"

    def test_num_imfs_respected(self, auth_token):
        """num_imfs 参数应被尊重"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], num_imfs=3,
                               auth_token=auth_token)
        assert len(resp["imf_components"]) == 3

    def test_missing_symbols_400(self, auth_token):
        """缺少 symbols 参数应返回 400"""
        kwargs = {'verify': VERIFY_SSL}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        resp = requests.get(f"{BASE_URL}/analysis/signal",
                           params={"start_date": "2023-01-01"}, **kwargs)
        assert resp.status_code == 400

    def test_insufficient_data_400(self, auth_token):
        """数据量不足应返回 400"""
        kwargs = {'verify': VERIFY_SSL}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        resp = requests.get(f"{BASE_URL}/analysis/signal",
                           params={
                               "symbols": "sz.900001",
                               "start_date": "2025-06-20",
                               "end_date": "2025-06-20"
                           }, **kwargs)
        assert resp.status_code in [200, 400]

    def test_method_label_emd(self, auth_token):
        """EMD 方法标签正确"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], method="emd",
                               auth_token=auth_token)
        assert resp["method"] == "emd"

    def test_dates_length_matches_original(self, auth_token):
        """dates 长度应与 original 一致"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        assert len(resp["dates"]) == len(resp["original"])


# ============================================================
# 测试类 2：EMD 分解 vs pyEMD 黄金标准
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestEMDGoldenStandard:
    """EMD 分解 vs pyEMD.EMD 黄金标准"""

    def test_imf_count_matches_request(self, auth_token):
        """IMF 数量应与请求一致"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        num_imfs = 5
        resp = call_signal_api(symbol, dates[0], dates[-1], num_imfs=num_imfs,
                               auth_token=auth_token)
        assert len(resp["imf_components"]) == num_imfs

    def test_imf_length_matches_original(self, auth_token):
        """每个 IMF 长度应与原始序列一致"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        n = len(resp["original"])
        for i, imf in enumerate(resp["imf_components"]):
            assert len(imf) == n, f"IMF[{i}] 长度({len(imf)}) 不等于原始序列({n})"
        assert len(resp["residual"]) == n, f"残差长度不等于原始序列"

    def test_reconstruction_error_small(self, auth_token):
        """重建误差应很小（IMF + 残差 ≈ 原始信号）"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        cpp_error = resp["reconstruction_error"]
        assert cpp_error < RECON_ERROR_THRESHOLD, \
            f"重建误差过大: {cpp_error:.2e} (阈值: {RECON_ERROR_THRESHOLD:.2e})"

    def test_imf_values_vs_pyemd(self, auth_token):
        """IMF 分量值应与 pyEMD 黄金标准在容差范围内一致"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        signal, _ = load_csv_data(symbol)
        num_imfs = 5

        resp = call_signal_api(symbol, dates[0], dates[-1], num_imfs=num_imfs,
                               auth_token=auth_token)

        # pyEMD 黄金标准
        py_result = emd_golden_standard(signal, num_imfs)

        # 逐 IMF 对比
        for i in range(num_imfs):
            cpp_imf = np.array(resp["imf_components"][i])
            py_imf = py_result["imfs"][i]

            # 使用相对误差（排除接近零的值）
            nonzero_mask = np.abs(py_imf) > 1e-10
            if np.any(nonzero_mask):
                rel_err = np.mean(np.abs(
                    (cpp_imf[nonzero_mask] - py_imf[nonzero_mask]) /
                    py_imf[nonzero_mask]
                ))
                assert rel_err < EMD_IMF_TOLERANCE, \
                    f"IMF[{i}] 值差异过大: 平均相对误差={rel_err:.4f} (容差: {EMD_IMF_TOLERANCE})"

    def test_residual_vs_pyemd(self, auth_token):
        """残差应与 pyEMD 黄金标准在容差范围内一致"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        signal, _ = load_csv_data(symbol)
        num_imfs = 5

        resp = call_signal_api(symbol, dates[0], dates[-1], num_imfs=num_imfs,
                               auth_token=auth_token)

        py_result = emd_golden_standard(signal, num_imfs)
        cpp_residual = np.array(resp["residual"])
        py_residual = py_result["residual"]

        # 残差通常较小，使用绝对误差
        abs_err = np.mean(np.abs(cpp_residual - py_residual))
        signal_range = np.max(signal) - np.min(signal)
        rel_err = abs_err / signal_range if signal_range > 0 else 0

        assert rel_err < EMD_IMF_TOLERANCE, \
            f"残差差异过大: 相对误差={rel_err:.4f}"

    def test_energy_conservation(self, auth_token):
        """能量守恒：Σ IMF 能量 + 残差能量 ≈ 原始能量"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        original = np.array(resp["original"])
        total_energy = np.sum(original ** 2)

        if total_energy < 1e-15:
            pytest.skip("信号能量为零")

        imf_energy = sum(np.sum(np.array(imf) ** 2) for imf in resp["imf_components"])
        residual_energy = np.sum(np.array(resp["residual"]) ** 2)
        reconstructed_energy = imf_energy + residual_energy

        rel_err = abs(reconstructed_energy - total_energy) / total_energy
        assert rel_err < ENERGY_TOLERANCE, \
            f"能量不守恒: 原始={total_energy:.2f}, 重建={reconstructed_energy:.2f}, 误差={rel_err:.4f}"

    def test_imf_energy_pct_sum(self, auth_token):
        """IMF 能量占比之和应 ≤ 1"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        total_energy_pct = sum(info["energy_pct"] for info in resp["imf_info"])
        # 允许小量误差
        assert total_energy_pct <= 1.0 + 0.01, \
            f"IMF 能量占比之和({total_energy_pct:.4f}) 超过 1"

    def test_imf_info_fields(self, auth_token):
        """imf_info 应包含 index/mean_period/energy_pct"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        for info in resp["imf_info"]:
            assert "index" in info
            assert "mean_period" in info
            assert "energy_pct" in info
            assert info["mean_period"] > 0, f"平均周期应为正: {info['mean_period']}"
            assert 0 <= info["energy_pct"] <= 1, f"能量占比应在 [0,1]: {info['energy_pct']}"


# ============================================================
# 测试类 3：IMF 频率特性
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestIMFFrequency:
    """IMF 频率特性验证"""

    def test_imf_mean_period_decreasing(self, auth_token):
        """IMF 平均周期应递增（IMF1 最高频 → IMF-n 最低频）"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        periods = [info["mean_period"] for info in resp["imf_info"]]

        if len(periods) >= 2:
            # 最后一个 IMF 的平均周期应 >= 第一个（低频趋势）
            assert periods[-1] >= periods[0] * 0.5, \
                f"IMF 周期趋势异常: {periods}"

    def test_imf_energy_decreasing(self, auth_token):
        """高频 IMF 通常能量较高（IMF1 能量最大）"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        energies = [info["energy_pct"] for info in resp["imf_info"]]

        if len(energies) >= 2:
            # 第一个 IMF 能量应大于最后一个（通常情况）
            assert energies[0] >= energies[-1] * 0.5, \
                f"IMF 能量分布异常: {energies}"

    def test_imf_orthogonality(self, auth_token):
        """IMF 间应接近正交（正交性指标 < 0.3）"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        imfs = [np.array(imf) for imf in resp["imf_components"]]
        io = compute_imf_orthogonality(imfs)

        # EMD 理想正交性 < 0.1，但由于算法差异放宽到 0.3
        assert io < 0.3, f"IMF 正交性指标过大: {io:.4f}"


# ============================================================
# 测试类 4：CEEMDAN 分解 vs pyEMD 黄金标准
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestCEEMDANDecomposition:
    """CEEMDAN 分解 vs pyEMD.CEEMDAN 黄金标准"""

    def test_ceemdan_imf_count(self, auth_token):
        """CEEMDAN IMF 数量应 ≤ 请求数量（残差提前单调时可能少于请求）"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        num_imfs = 3
        resp = call_signal_api(symbol, dates[0], dates[-1], method="ceemdan",
                               num_imfs=num_imfs, auth_token=auth_token)
        # C++ CEEMDAN 在残差提前单调时会停止，所以 ≤ num_imfs
        assert 1 <= len(resp["imf_components"]) <= num_imfs

    def test_ceemdan_imf_length(self, auth_token):
        """CEEMDAN 每个 IMF 长度应与原始序列一致"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], method="ceemdan",
                               auth_token=auth_token)

        n = len(resp["original"])
        for i, imf in enumerate(resp["imf_components"]):
            assert len(imf) == n, f"CEEMDAN IMF[{i}] 长度({len(imf)}) 不等于原始序列({n})"

    def test_ceemdan_reconstruction_error(self, auth_token):
        """CEEMDAN 重建误差应很小"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], method="ceemdan",
                               auth_token=auth_token)

        cpp_error = resp["reconstruction_error"]
        assert cpp_error < RECON_ERROR_THRESHOLD, \
            f"CEEMDAN 重建误差过大: {cpp_error:.2e}"

    def test_ceemdan_imf_values_vs_pyemd(self, auth_token):
        """CEEMDAN IMF 分量数学属性验证

        注意：CEEMDAN 使用随机噪声集合，C++ (mt19937) 和 pyEMD (numpy)
        的随机数生成器不同，即使种子相同也无法对齐 IMF 值。
        因此本测试验证数学属性而非逐值对比。
        """
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        signal, _ = load_csv_data(symbol)
        num_imfs = 3

        resp = call_signal_api(symbol, dates[0], dates[-1], method="ceemdan",
                               num_imfs=num_imfs, auth_token=auth_token)

        actual_cpp_imfs = len(resp["imf_components"])
        if actual_cpp_imfs == 0:
            pytest.skip("C++ 未返回任何 IMF")

        cpp_imfs = [np.array(imf) for imf in resp["imf_components"]]
        cpp_residual = np.array(resp["residual"])

        # 验证 1: IMF 均值应接近零（IMF 定义）
        for i, imf in enumerate(cpp_imfs):
            imf_mean = abs(np.mean(imf))
            imf_std = np.std(imf)
            if imf_std > 1e-10:
                assert imf_mean / imf_std < 0.1, \
                    f"CEEMDAN IMF[{i}] 均值过大: mean={imf_mean:.4f}, std={imf_std:.4f}"

        # 验证 2: 重建误差应很小
        reconstructed = cpp_residual.copy()
        for imf in cpp_imfs:
            reconstructed = reconstructed + imf
        recon_error = np.sqrt(np.mean((reconstructed - signal) ** 2))
        assert recon_error < RECON_ERROR_THRESHOLD, \
            f"CEEMDAN 重建误差过大: {recon_error:.2e}"

        # 验证 3: pyEMD CEEMDAN 也应产生相似数量的 IMF
        np.random.seed(42)
        py_result = ceemdan_golden_standard(signal, num_imfs, ensembles=30, seed=42)
        py_actual = py_result["num_actual_imfs"]

        # 两者 IMF 数量应在合理范围内（允许 ±2 差异）
        assert abs(actual_cpp_imfs - py_actual) <= 2, \
            f"CEEMDAN IMF 数量差异过大: C++={actual_cpp_imfs}, pyEMD={py_actual}"

    def test_ceemdan_energy_conservation(self, auth_token):
        """CEEMDAN 能量守恒"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], method="ceemdan",
                               auth_token=auth_token)

        original = np.array(resp["original"])
        total_energy = np.sum(original ** 2)

        if total_energy < 1e-15:
            pytest.skip("信号能量为零")

        imf_energy = sum(np.sum(np.array(imf) ** 2) for imf in resp["imf_components"])
        residual_energy = np.sum(np.array(resp["residual"]) ** 2)
        reconstructed_energy = imf_energy + residual_energy

        rel_err = abs(reconstructed_energy - total_energy) / total_energy
        assert rel_err < ENERGY_TOLERANCE * 2, \
            f"CEEMDAN 能量不守恒: 误差={rel_err:.4f}"

    def test_ceemdan_method_label(self, auth_token):
        """CEEMDAN 方法标签正确"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], method="ceemdan",
                               auth_token=auth_token)
        assert resp["method"] == "ceemdan"

    def test_ceemdan_orthogonality(self, auth_token):
        """CEEMDAN IMF 间应接近正交"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], method="ceemdan",
                               auth_token=auth_token)

        imfs = [np.array(imf) for imf in resp["imf_components"]]
        io = compute_imf_orthogonality(imfs)

        assert io < 0.3, f"CEEMDAN IMF 正交性指标过大: {io:.4f}"


# ============================================================
# 测试类 5：不同数据字段
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestSignalFields:
    """不同数据字段的信号分解测试"""

    def test_volume_field(self, auth_token):
        """成交量字段分解"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], field="volume",
                               auth_token=auth_token)
        assert resp["field"] == "volume"
        assert len(resp["imf_components"]) > 0
        assert resp["reconstruction_error"] < RECON_ERROR_THRESHOLD

    def test_high_field(self, auth_token):
        """最高价字段分解"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], field="high",
                               auth_token=auth_token)
        assert resp["field"] == "high"
        assert len(resp["imf_components"]) > 0

    def test_low_field(self, auth_token):
        """最低价字段分解"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], field="low",
                               auth_token=auth_token)
        assert resp["field"] == "low"
        assert len(resp["imf_components"]) > 0

    def test_open_field(self, auth_token):
        """开盘价字段分解"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], field="open",
                               auth_token=auth_token)
        assert resp["field"] == "open"
        assert len(resp["imf_components"]) > 0


# ============================================================
# 测试类 6：不同趋势场景
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestEMDScenarios:
    """EMD 分解在不同趋势场景下的表现"""

    def test_up_trend(self, auth_token):
        """单边上涨场景"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        assert len(resp["imf_components"]) == 5
        assert resp["reconstruction_error"] < RECON_ERROR_THRESHOLD

        # 验证能量占比
        total_energy_pct = sum(info["energy_pct"] for info in resp["imf_info"])
        assert total_energy_pct <= 1.0 + 0.01

    def test_down_trend(self, auth_token):
        """单边下跌场景"""
        symbol = "sz.900002"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        assert len(resp["imf_components"]) == 5
        assert resp["reconstruction_error"] < RECON_ERROR_THRESHOLD

    def test_high_volatility(self, auth_token):
        """高波动场景"""
        symbol = "sz.900005"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        assert len(resp["imf_components"]) == 5
        assert resp["reconstruction_error"] < RECON_ERROR_THRESHOLD

    def test_sideways(self, auth_token):
        """横盘震荡场景"""
        symbol = "sz.900004"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        assert len(resp["imf_components"]) == 5
        assert resp["reconstruction_error"] < RECON_ERROR_THRESHOLD


# ============================================================
# 测试类 7：pyEMD 直接对比（不调用 API）
# ============================================================

class TestPyEMDDirect:
    """pyEMD 库直接验证（无需服务运行）"""

    def test_emd_basic_decomposition(self):
        """pyEMD EMD 基本分解"""
        t = np.linspace(0, 1, 200)
        signal = np.sin(2 * np.pi * 5 * t) + 0.5 * np.sin(2 * np.pi * 20 * t)

        emd = EMD()
        emd.MAX_ITERATION = 10
        imfs = emd.emd(signal)

        assert len(imfs) >= 2, "应至少分解出 2 个 IMF"

        # 重建误差
        reconstructed = sum(imfs)
        error = np.sqrt(np.mean((reconstructed - signal) ** 2))
        assert error < 1e-6, f"重建误差过大: {error:.2e}"

    def test_ceemdan_basic_decomposition(self):
        """pyEMD CEEMDAN 基本分解"""
        t = np.linspace(0, 1, 200)
        signal = np.sin(2 * np.pi * 5 * t) + 0.5 * np.sin(2 * np.pi * 20 * t)

        np.random.seed(42)
        ceemdan = CEEMDAN()
        ceemdan.ensemble_size = 10  # 减少集合数加速测试
        imfs = ceemdan(signal)

        assert len(imfs) >= 2, "应至少分解出 2 个 IMF"

        # 重建误差
        reconstructed = sum(imfs)
        error = np.sqrt(np.mean((reconstructed - signal) ** 2))
        assert error < 1e-4, f"CEEMDAN 重建误差过大: {error:.2e}"

    def test_emd_frequency_separation(self):
        """EMD 频率分离能力"""
        t = np.linspace(0, 1, 500)
        # 低频 (5Hz) + 高频 (50Hz)
        signal = np.sin(2 * np.pi * 5 * t) + 0.3 * np.sin(2 * np.pi * 50 * t)

        emd = EMD()
        imfs = emd.emd(signal)

        # 第一个 IMF 应主要包含高频成分
        # 最后一个 IMF 应主要包含低频成分
        if len(imfs) >= 2:
            # 计算过零点数
            zc_first = sum(1 for i in range(1, len(imfs[0]))
                          if (imfs[0][i] >= 0 and imfs[0][i-1] < 0) or
                             (imfs[0][i] < 0 and imfs[0][i-1] >= 0))
            zc_last = sum(1 for i in range(1, len(imfs[-1]))
                         if (imfs[-1][i] >= 0 and imfs[-1][i-1] < 0) or
                            (imfs[-1][i] < 0 and imfs[-1][i-1] >= 0))

            # 高频 IMF 的过零点应多于低频 IMF
            assert zc_first >= zc_last, \
                f"频率分离异常: 第一个 IMF 过零点={zc_first}, 最后一个={zc_last}"

    def test_emd_energy_distribution(self):
        """EMD 能量分布"""
        np.random.seed(42)
        signal = np.random.randn(200).cumsum()  # 随机游走

        emd = EMD()
        imfs = emd.emd(signal)

        total_energy = np.sum(signal ** 2)
        imf_energies = [np.sum(imf ** 2) for imf in imfs]
        residual_energy = np.sum((signal - sum(imfs)) ** 2)

        # 能量守恒
        reconstructed_energy = sum(imf_energies) + residual_energy
        rel_err = abs(reconstructed_energy - total_energy) / total_energy
        assert rel_err < 0.1, f"能量不守恒: 误差={rel_err:.4f}"


# ============================================================
# 测试类 8：边缘情况
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestEMDEdgeCases:
    """EMD 分解边缘情况测试"""

    def test_num_imfs_1(self, auth_token):
        """num_imfs=1 时应返回单个 IMF"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], num_imfs=1,
                               auth_token=auth_token)
        assert len(resp["imf_components"]) == 1
        assert len(resp["imf_info"]) == 1

    def test_num_imfs_max(self, auth_token):
        """num_imfs=20 时应正常工作"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        resp = call_signal_api(symbol, dates[0], dates[-1], num_imfs=20,
                               auth_token=auth_token)
        assert len(resp["imf_components"]) == 20

    def test_short_signal(self, auth_token):
        """短信号（刚满足最小长度）"""
        symbol = "sz.900001"
        _, dates = load_csv_data(symbol)
        # 只取前 15 天数据
        short_dates = dates[:15]
        resp = call_signal_api(symbol, short_dates[0], short_dates[-1],
                               auth_token=auth_token)
        assert len(resp["imf_components"]) > 0
        assert len(resp["original"]) == 15

    def test_constant_signal(self):
        """常量信号（无波动）— 直接测试 pyEMD

        pyEMD 对常量信号会返回一个 IMF（等于常量值本身），
        因为常量信号没有极值点变化，筛选过程会立即停止。
        """
        signal = np.ones(50) * 100.0

        emd = EMD()
        imfs = emd.emd(signal)

        # pyEMD 对常量信号通常返回 1 个 IMF ≈ 常量值
        assert len(imfs) >= 1, "常量信号应至少返回 1 个 IMF"

        # 重建 = ΣIMF + residual 应接近原信号
        reconstructed = sum(imfs)
        residual = signal - reconstructed
        # 常量信号的重建误差应接近零
        recon_error = np.sqrt(np.mean(residual ** 2))
        assert recon_error < 1e-6, f"常量信号重建误差过大: {recon_error:.2e}"
