#!/usr/bin/env python3
"""
特征缓存工具：保存和加载预计算的特征数据

用于快速回测模式：特征收集只做 1 次，后续回测直接加载缓存
"""

import numpy as np
import json
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional


def save_feature_cache(
    cache_path: str,
    symbols: List[str],
    features: Dict[str, Dict[str, np.ndarray]],
    feature_names: List[str],
    dates: List[str],
    strategy_id: str = "",
    metadata: Optional[Dict] = None
):
    """
    保存特征缓存到 .npy 文件
    
    Args:
        cache_path: 缓存文件路径（.npy）
        symbols: 标的列表 ["sz.000423", "sz.002825", ...]
        features: 特征数据 {symbol: {feature_name: array}}
            例如: {"sz.000423": {"nimf_0": array([...]), "cusum_drift": array([...])}}
        feature_names: 特征名称列表 ["nimf_0", "nimf_1", "cusum_drift", ...]
        dates: 日期列表 ["2020-01-01", "2020-01-02", ...]
        strategy_id: 策略 ID（用于校验）
        metadata: 额外元数据
    """
    cache_data = {
        "symbols": symbols,
        "features": features,
        "feature_names": feature_names,
        "dates": dates,
        "metadata": {
            "strategy_id": strategy_id,
            "collected_at": datetime.now().isoformat(),
            "n_bars": len(dates),
            "n_symbols": len(symbols),
            "n_features": len(feature_names),
            **(metadata or {})
        }
    }
    
    # 保存为 .npy（支持字典结构）
    np.save(cache_path, cache_data, allow_pickle=True)
    print(f"[FeatureCache] Saved to {cache_path}")
    print(f"  Symbols: {len(symbols)}")
    print(f"  Features: {len(feature_names)}")
    print(f"  Bars: {len(dates)}")


def load_feature_cache(cache_path: str) -> Dict:
    """
    加载特征缓存
    
    Returns:
        {
            "symbols": [...],
            "features": {symbol: {feature_name: array}},
            "feature_names": [...],
            "dates": [...],
            "metadata": {...}
        }
    """
    cache_data = np.load(cache_path, allow_pickle=True).item()
    print(f"[FeatureCache] Loaded from {cache_path}")
    print(f"  Symbols: {len(cache_data['symbols'])}")
    print(f"  Features: {len(cache_data['feature_names'])}")
    print(f"  Bars: {len(cache_data['dates'])}")
    return cache_data


def validate_feature_cache(
    cache_path: str,
    expected_symbols: List[str],
    expected_features: List[str]
) -> bool:
    """
    校验特征缓存是否匹配预期
    
    Args:
        cache_path: 缓存文件路径
        expected_symbols: 预期的标的列表
        expected_features: 预期的特征列表
    
    Returns:
        True 如果匹配，False 如果不匹配
    """
    try:
        cache_data = load_feature_cache(cache_path)
        
        # 校验 symbols
        if set(cache_data["symbols"]) != set(expected_symbols):
            print(f"[FeatureCache] Symbol mismatch!")
            print(f"  Expected: {expected_symbols}")
            print(f"  Got: {cache_data['symbols']}")
            return False
        
        # 校验 features
        if set(cache_data["feature_names"]) != set(expected_features):
            print(f"[FeatureCache] Feature mismatch!")
            print(f"  Expected: {expected_features}")
            print(f"  Got: {cache_data['feature_names']}")
            return False
        
        print(f"[FeatureCache] Validation passed")
        return True
        
    except Exception as e:
        print(f"[FeatureCache] Validation failed: {e}")
        return False


if __name__ == "__main__":
    # 测试代码
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: python feature_cache.py <cache_path>")
        sys.exit(1)
    
    cache_path = sys.argv[1]
    
    if Path(cache_path).exists():
        # 加载并显示
        cache_data = load_feature_cache(cache_path)
        print("\nCache content:")
        print(f"  Symbols: {cache_data['symbols']}")
        print(f"  Features: {cache_data['feature_names']}")
        print(f"  Dates: {cache_data['dates'][:5]} ... (total {len(cache_data['dates'])})")
        print(f"  Metadata: {cache_data['metadata']}")
    else:
        print(f"Cache file not found: {cache_path}")
