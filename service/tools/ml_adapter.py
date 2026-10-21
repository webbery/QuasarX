"""模型适配器。

CV runner 只依赖 ModelAdapter 接口；新增 CNN/Transformer 时实现同样的
fit/predict/decode/refit/save 方法即可，不需要改折计划、OOF、指标或 API。

约定：
  - fit/predict 的 X 是二维 numpy（tabular）；序列模型可以在 adapter 内部
    把 X 重排成窗口，或未来扩展 prepare(dataset)。
  - decode 返回 (score, pred_idx, proba)：
      score   越大越看多，用于 IC；
      pred_idx 类别索引（回归为 0）；
      proba   1D（binary）或 2D（multi），回归为预测值。
"""

import os
import time
from dataclasses import dataclass
from typing import Any, Dict, List, Protocol, Tuple

import numpy as np
import xgboost as xgb

from ml_metrics import learning_curve


@dataclass
class FitResult:
    model: Any
    best_rounds: int
    history: Dict[str, Any]


class ModelAdapter(Protocol):
    name: str

    def fit(self, X, y, X_val=None, y_val=None, progress_cb=None) -> FitResult:
        ...

    def predict(self, model, X) -> np.ndarray:
        ...

    def decode(self, raw, classes) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
        ...

    def refit(self, X, y, rounds: int) -> Any:
        ...

    def save(self, model, path: str) -> None:
        ...

    def feature_importance(self, model, feature_cols: List[str]) -> List[dict]:
        ...

    def learning_curve(self, history: Dict[str, Any]) -> List[dict]:
        ...

    def final_rounds(self, fold_rounds: List[int]) -> int:
        ...


class _ProgressCallback(xgb.callback.TrainingCallback):
    def __init__(self, progress_cb):
        self.progress_cb = progress_cb
        self.last_emit = 0.0

    def after_iteration(self, model, epoch, evals_log):
        now = time.time()
        if now - self.last_emit < 0.2 and epoch != model.num_boosted_rounds() - 1:
            return False
        self.last_emit = now

        def first(metric_dict):
            if not metric_dict:
                return None
            for vals in metric_dict.values():
                if vals:
                    return float(vals[-1])
            return None

        self.progress_cb({
            "iteration": int(epoch),
            "train_loss": first(evals_log.get("train", {})),
            "eval_loss": first(evals_log.get("eval", {})),
        })
        return False


class XGBoostAdapter:
    """XGBoost 的 model adapter。"""

    name = "xgboost"

    def __init__(self, params: Dict[str, Any], n_estimators: int, esr: int,
                 objective: str, feature_cols: List[str]):
        self.params = dict(params)
        # 这两个参数走 xgb.train 的独立入口，不能留在 params 里
        self.params.pop("n_estimators", None)
        self.params.pop("early_stopping_rounds", None)
        self.params.pop("early_stopping", None)
        self.n_estimators = int(n_estimators)
        self.esr = int(esr)
        self.objective = objective
        self.feature_cols = list(feature_cols)

    def _dmatrix(self, X, y=None):
        return xgb.DMatrix(X, label=y, feature_names=self.feature_cols)

    def fit(self, X, y, X_val=None, y_val=None, progress_cb=None) -> FitResult:
        dtrain = self._dmatrix(X, y)
        history = {}
        best_iter = None

        if X_val is not None and len(X_val) > 0:
            dval = self._dmatrix(X_val, y_val)
            callbacks = [_ProgressCallback(progress_cb)] if progress_cb else None
            booster = xgb.train(
                self.params, dtrain,
                num_boost_round=self.n_estimators,
                evals=[(dtrain, "train"), (dval, "eval")],
                early_stopping_rounds=self.esr if self.esr > 0 else None,
                verbose_eval=False,
                callbacks=callbacks,
            )
            best_iter = getattr(booster, "best_iteration", None) if self.esr > 0 else None
            history = booster.evals_result() if hasattr(booster, "evals_result") else {}
        else:
            booster = xgb.train(
                self.params, dtrain,
                num_boost_round=self.n_estimators,
                verbose_eval=False,
            )

        if best_iter is None:
            best_iter = self.n_estimators - 1
        return FitResult(model=booster, best_rounds=int(best_iter) + 1,
                         history=history)

    def refit(self, X, y, rounds: int):
        dtrain = self._dmatrix(X, y)
        return xgb.train(self.params, dtrain, num_boost_round=int(rounds),
                         verbose_eval=False)

    def predict(self, model, X) -> np.ndarray:
        dmat = self._dmatrix(X)
        return model.predict(dmat)

    def decode(self, raw, classes) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
        preds = np.asarray(raw)
        if self.objective.startswith("multi:"):
            proba = preds if preds.ndim == 2 else preds.reshape(-1, 1)
            k = proba.shape[1]
            up_idx = classes.index(0) if classes and 0 in classes else 0
            down_idx = classes.index(2) if classes and 2 in classes else k - 1
            score = proba[:, up_idx] - proba[:, down_idx]
            pred_idx = np.argmax(proba, axis=1)
            return score, pred_idx, proba

        if self.objective == "binary:logistic":
            proba = preds.reshape(-1)
            pred_idx = (proba > 0.5).astype(int)
            pos_label = classes[1] if classes and len(classes) > 1 else 1
            score = -proba if pos_label == 2 else proba
            return score, pred_idx, proba

        preds = preds.reshape(-1)
        return preds, np.zeros(len(preds), dtype=int), preds

    def save(self, model, path: str) -> None:
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        model.save_model(path)

    def feature_importance(self, model, feature_cols: List[str]) -> List[dict]:
        gain = model.get_score(importance_type="gain")
        weight = model.get_score(importance_type="weight")
        cover = model.get_score(importance_type="cover")
        out = []
        for feat in feature_cols:
            out.append({
                "feature": feat,
                "gain": float(gain.get(feat, 0.0)),
                "weight": float(weight.get(feat, 0.0)),
                "cover": float(cover.get(feat, 0.0)),
            })
        return out

    def learning_curve(self, history: Dict[str, Any]) -> List[dict]:
        return learning_curve(history)

    def final_rounds(self, fold_rounds: List[int]) -> int:
        rounds = int(np.median(fold_rounds)) if fold_rounds else self.n_estimators
        return max(1, min(rounds, self.n_estimators))
