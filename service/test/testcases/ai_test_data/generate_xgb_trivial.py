#!/usr/bin/env python3
"""
生成 trivial XGBoost 1-树模型 + 配套策略 JSON，用于 test_xgb_business 业务层测试。

XGBoostNode Init 时 features="" 会从上游 out_elements() 自动推断特征，
模型训练时使用的特征名必须与上游 FunctionNode label 对齐。这里训练 1 棵
binary:logistic 树，特征列名 = "MA(5)"，与策略中 MA(5) FunctionNode 输出对齐。

输出 3 个文件到 ai_test_data/（与 xgboost_train_strategy.json 同目录）：
- xgb_trivial_model.json    — XGBoost 模型
- xgb_trivial_meta.json     — meta（features/objective/num_class/params）
- xgb_trivial_strategy.json — 策略 JSON（含 modelFile = production/{name}-{label}.json）

跑一次即可生成持久化产物，测试直接读文件不重训。
"""

import json
from pathlib import Path

import numpy as np
import xgboost as xgb

OUTPUT_DIR = Path(__file__).parent
MODEL_FILE = OUTPUT_DIR / "xgb_trivial_model.json"
META_FILE = OUTPUT_DIR / "xgb_trivial_meta.json"
STRATEGY_FILE = OUTPUT_DIR / "xgb_trivial_strategy.json"

STRATEGY_NAME = "test_xgb_business"
XGB_LABEL = "XGBoost"
SYMBOL = "sz.800001"
MODEL_PATH = f"production/{STRATEGY_NAME}-{XGB_LABEL}.json"
FEATURE_NAME = "MA(5)"


def train_model() -> xgb.Booster:
    """训练 1-棵 binary 分类树，5 个样本硬编码"""
    X = np.array([[1], [2], [3], [4], [5]], dtype=np.float32)
    y = np.array([0, 1, 0, 1, 0], dtype=np.float32)
    dtrain = xgb.DMatrix(X, label=y, feature_names=[FEATURE_NAME])
    params = {
        "objective": "binary:logistic",
        "max_depth": 2,
        "learning_rate": 0.3,
        "verbosity": 0,
    }
    return xgb.train(params, dtrain, num_boost_round=1)


def write_meta(path: Path) -> None:
    """meta 文件：features/objective/num_class/label/params"""
    meta = {
        "strategy_id": STRATEGY_NAME,
        "created_at": "2026-08-18",
        "source": "experiment",
        "label": {"type": "classification", "period": 5, "threshold": 0.0},
        "objective": "binary:logistic",
        "num_class": 2,
        "params": {"learning_rate": 0.3, "max_depth": 2, "n_estimators": 1},
        "features": [FEATURE_NAME],
    }
    path.write_text(json.dumps(meta, indent=2))


def write_strategy(path: Path) -> None:
    """策略 JSON：input → MA(5) → XGBoost → Signal → Portfolio → Execution + DebugNode"""
    strategy = {
        "id": STRATEGY_NAME,
        "name": STRATEGY_NAME,
        "version": 1,
        "description": "XGBoost 业务层测试策略：MA(5) → XGBoost binary → 简单阈值信号 → Portfolio → Execution",
        "source": "A_hfq",
        "nodes": [
            {"id": "1", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "行情数据", "nodeType": "input",
                      "params": {"source": {"value": "股票", "type": "text"},
                                 "code": {"value": [SYMBOL], "type": "text"},
                                 "freq": {"value": "1d", "type": "select"},
                                 "close": {"value": "close", "type": "text"},
                                 "open": {"value": "open", "type": "text"},
                                 "high": {"value": "high", "type": "text"},
                                 "low": {"value": "low", "type": "text"},
                                 "volume": {"value": "volume", "type": "text"}}}},
            {"id": "2", "type": "custom", "position": {"x": 200, "y": 0},
             "data": {"label": "MA(5)", "nodeType": "function",
                      "params": {"method": {"value": "MA", "type": "select"},
                                 "range": {"value": "5d", "type": "text"}}}},
            {"id": "3", "type": "custom", "position": {"x": 400, "y": 0},
             "data": {"label": XGB_LABEL, "nodeType": "xgboost",
                      "params": {"modelFile": {"value": MODEL_PATH, "type": "text"},
                                 "features": {"value": "", "type": "text"},
                                 "objective": {"value": "binary:logistic", "type": "select"},
                                 "num_class": {"value": 2, "type": "number"}}}},
            {"id": "4", "type": "custom", "position": {"x": 600, "y": 0},
             "data": {"label": "信号", "nodeType": "signal",
                      "params": {"code": {"value": [SYMBOL], "type": "text"},
                                 "buy": {"value": "xgb_probs[0] > 0.5", "type": "text"},
                                 "sell": {"value": "xgb_probs[0] < 0.5", "type": "text"}}}},
            {"id": "5", "type": "custom", "position": {"x": 800, "y": 0},
             "data": {"label": "组合", "nodeType": "portfolio",
                      "params": {"target": {"value": 0.5, "type": "number"}}}},
            {"id": "6", "type": "custom", "position": {"x": 1000, "y": 0},
             "data": {"label": "执行", "nodeType": "execution",
                      "params": {}}},
            {"id": "7", "type": "custom", "position": {"x": 800, "y": 200},
             "data": {"label": "xgb_debug", "nodeType": "debug",
                      "params": {"suffix": {"value": "csv", "type": "select"}}}},
        ],
        "edges": [
            {"id": "e1->2", "source": "1", "target": "2",
             "sourceHandle": "1-close", "targetHandle": "2", "type": "default"},
            {"id": "e2->3", "source": "2", "target": "3",
             "sourceHandle": "2", "targetHandle": "3", "type": "default"},
            {"id": "e3->4", "source": "3", "target": "4",
             "sourceHandle": "3", "targetHandle": "4", "type": "default"},
            {"id": "e4->5", "source": "4", "target": "5",
             "sourceHandle": "4", "targetHandle": "5", "type": "default"},
            {"id": "e5->6", "source": "5", "target": "6",
             "sourceHandle": "5", "targetHandle": "6", "type": "default"},
            {"id": "e3->7", "source": "3", "target": "7",
             "sourceHandle": "3", "targetHandle": "7", "type": "default"},
        ],
    }
    path.write_text(json.dumps(strategy, indent=2, ensure_ascii=False))


def main() -> None:
    print(f"Training trivial 1-tree XGBoost model...")
    bst = train_model()
    bst.save_model(str(MODEL_FILE))
    write_meta(META_FILE)
    write_strategy(STRATEGY_FILE)
    print(f"✓ Generated: {MODEL_FILE}")
    print(f"✓ Generated: {META_FILE}")
    print(f"✓ Generated: {STRATEGY_FILE}")


if __name__ == "__main__":
    main()
