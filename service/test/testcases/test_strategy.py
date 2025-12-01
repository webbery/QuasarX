import requests
import sys
from tool import check_response, BASE_URL
import pytest
import json

@pytest.mark.usefixtures("auth_token")
class TestStrategy:
    @pytest.mark.timeout(5)
    def test_strategy(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        response = requests.get(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(response)
        assert isinstance(data, list)
        assert len(data) > 0

    @pytest.mark.timeout(600)
    def test_run_backtest_ml(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        kwargs['json'] = {
            "script": """{
                "graph": {
                    "id": "graph_001",
                    "name": "机器学习流水线",
                    "description": "包含数据输入、预处理、特征工程、模型训练和结果输出的完整流水线",
                    "nodes": [
                    {
                        "id": "1",
                        "type": "custom",
                        "data": { 
                            "label": "数据输入",
                            "nodeType": "input",
                            "params": {
                                "source": {
                                    "value": "股票",
                                    "type": "select",
                                    "options": ["股票", "期货", "期权"]
                                },
                                "code": {
                                    "value": ["001038"],
                                    "type": "text"
                                },
                                "formula": {
                                    "value": "price=MA(close,5)",
                                    "type": "text"
                                }
                            }
                        },
                        "position": { "x": 50, "y": 100 }
                    },
                    {
                        "id": "2",
                        "type": "custom",
                        "data": { 
                        "label": "数据预处理",
                        "nodeType": "operation",
                        "params": {
                            "方法": {
                            "value": "Z-score",
                            "type": "select",
                            "options": ["Z-score", "Min-Max", "标准化", "归一化"]
                            },
                            "缺失值": {
                            "value": "填充",
                            "type": "select",
                            "options": ["填充", "删除", "插值"]
                            },
                            "填充值": {
                            "value": "0",
                            "type": "text",
                            "visible": false
                            }
                        }
                        },
                        "position": { "x": 300, "y": 100 }
                    },
                    {
                        "id": "3",
                        "type": "custom",
                        "data": { 
                        "label": "特征工程",
                        "nodeType": "feature",
                        "params": {
                            "特征选择": {
                            "value": "是",
                            "type": "select",
                            "options": ["是", "否"]
                            },
                            "降维": {
                            "value": "PCA",
                            "type": "select",
                            "options": ["PCA", "LDA", "t-SNE", "无"]
                            },
                            "特征数量": {
                            "value": "10",
                            "type": "number",
                            "min": 1,
                            "max": 100
                            }
                        }
                        },
                        "position": { "x": 550, "y": 100 }
                    },
                    {
                        "id": "4",
                        "type": "custom",
                        "data": { 
                        "label": "模型训练(XGBoost)",
                        "nodeType": "operation",
                        "params": {
                            "算法": {
                            "value": "XGBoost",
                            "type": "select",
                            "options": ["XGBoost", "Random Forest", "SVM", "神经网络", "逻辑回归"]
                            },
                            "objective": {
                            "value": "binary:logistic",
                            "type": "select",
                            "options": ["binary:logistic", "reg:linear", "multi:softmax"]
                            },
                            "eval_metric": {
                            "value": "logloss",
                            "type": "select",
                            "options": ["logloss", "error", "auc"]
                            },
                            "max_depth": {
                            "value": 3,
                            "type": "number",
                            "min": 1,
                            "max": 10
                            },
                            "eta": {
                            "value": 0.0001,
                            "type": "number",
                            "step": 0.0001,
                            "min": 0,
                            "max": 1
                            },
                            "迭代次数": {
                            "value": 100,
                            "type": "number",
                            "min": 1,
                            "max": 1000
                            }
                        }
                        },
                        "position": { "x": 800, "y": 50 }
                    },
                    {
                        "id": "6",
                        "type": "custom",
                        "data": { 
                        "label": "模型训练(LSTM)",
                        "nodeType": "operation",
                        "params": {
                            "算法": {
                            "value": "LSTM",
                            "type": "select",
                            "options": ["LSTM", "GRU", "RNN", "Transformer"]
                            },
                            "objective": {
                            "value": "binary:logistic",
                            "type": "select",
                            "options": ["binary:logistic", "regression", "classification"]
                            },
                            "input": {
                            "value": 5,
                            "type": "number",
                            "min": 1,
                            "max": 100
                            },
                            "迭代次数": {
                            "value": 100,
                            "type": "number",
                            "min": 1,
                            "max": 1000
                            }
                        }
                        },
                        "position": { "x": 800, "y": 150 }
                    },
                    {
                        "id": "5",
                        "type": "custom",
                        "data": { 
                        "label": "结果输出",
                        "nodeType": "output",
                        "params": {
                            "格式": {
                            "value": "CSV",
                            "type": "select",
                            "options": ["CSV", "JSON", "数据库", "实时推送"]
                            },
                            "路径": {
                            "value": "/data/output.csv",
                            "type": "text"
                            }
                        }
                        },
                        "position": { "x": 1050, "y": 100 }
                    }
                    ],
                    "edges": [
                    {
                        "id": "e1->2",
                        "source": "1",
                        "target": "2",
                        "sourceHandle": "output",
                        "targetHandle": "input",
                        "type": "default"
                    },
                    {
                        "id": "e2->3",
                        "source": "2",
                        "target": "3",
                        "sourceHandle": "output",
                        "targetHandle": "input",
                        "type": "default"
                    },
                    {
                        "id": "e3->4",
                        "source": "3",
                        "target": "4",
                        "sourceHandle": "output",
                        "targetHandle": "input",
                        "type": "default"
                    },
                    {
                        "id": "e3->6",
                        "source": "3",
                        "target": "6",
                        "sourceHandle": "output",
                        "targetHandle": "input",
                        "type": "default"
                    },
                    {
                        "id": "e4->5",
                        "source": "4",
                        "target": "5",
                        "sourceHandle": "output",
                        "targetHandle": "input",
                        "type": "default"
                    }
                    ]
                }
            }""",
            "tick": "1d",
            "static": [
                {"name": "sharp"}
            ]
        }
        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)
        assert isinstance(data, object)
        assert 'buy' in data
        assert len(data['buy']) > 0
        assert 'sell' in data
        assert len(data['sell']) > 0

        assert 'features' in data
        assert len(data['features']) > 0
        features = data['features']
        assert 'sharp' in features
        macd5 = features['MACD']
        assert len(macd5) > 0
        for symbol in macd5:
            assert isinstance(macd5[symbol], list)
            break

    @pytest.mark.timeout(600)
    def test_run_backtest_ma2(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        script = {
                "graph": {
                    "id": "graph_ma2",
                    "name": "双均线动量流水线",
                    "description": "包含数据输入、特征工程、信号输出和结果输出的完整流水线",
                    "nodes": [
                        {
                            "id": "1",
                            "type": "custom",
                            "data": { 
                                "label": "行情数据输入",
                                "nodeType": "input",
                                "params": {
                                    "source": {
                                        "value": "股票",
                                        "type": "select",
                                        "options": ["股票", "期货"]
                                    },
                                    "code": {
                                        "value": ["001318"],
                                        "type": "text"
                                    },
                                    "freq": {
                                        "value": "1d",
                                        "type": "select",
                                        "options": ["1d", "5m"]
                                    },
                                    "close": {
                                        "value": "close",
                                        "type": "text"
                                    },
                                    "open": {
                                        "value": "open",
                                        "type": "text"
                                    },
                                    "high": {
                                        "value": "high",
                                        "type": "text"
                                    },
                                    "low": {
                                        "value": "low",
                                        "type": "text"
                                    },
                                    "volume": {
                                        "value": "volume",
                                        "type": "text"
                                    }
                                }
                            },
                            "position": { "x": 50, "y": 100 }
                        },
                        {
                            "id": "2",
                            "type": "custom",
                            "data": { 
                                "label": "MA_5",
                                "nodeType": "function",
                                "params": {
                                    "method": {
                                        "value": "MA",
                                        "type": "select",
                                        "options": ["MA"]
                                    },
                                    "smoothTime": {
                                        "value": 5,
                                        "type": "text",
                                    }
                                }
                            },
                            "position": { "x": 550, "y": 100 }
                        },
                        {
                            "id": "3",
                            "type": "custom",
                            "data": { 
                                "label": "MA_15",
                                "nodeType": "function",
                                "params": {
                                    "method": {
                                        "value": "MA",
                                        "type": "select",
                                        "options": ["MA"]
                                    },
                                    "smoothTime": {
                                        "value": 15,
                                        "type": "text",
                                    }
                                }
                            },
                            "position": { "x": 800, "y": 50 }
                        },
                        {
                            "id": "6",
                            "type": "custom",
                            "data": { 
                                "label": "MA_diff",
                                "nodeType": "signal",
                                "params": {
                                    "code": {
                                        "value": ["001318"],
                                        "type": "text"
                                    },
                                    "buy": {
                                        "value": "MA_5[t]>MA_15[t] and MA_5[t-1]<MA_15[t-1] ",
                                        "type": "text"
                                    },
                                    "sell": {
                                        "value": "MA_5[t]<MA_15[t] and MA_5[t-1]>MA_15[t-1] ",
                                        "type": "text"
                                    }
                                }
                            },
                            "position": { "x": 800, "y": 150 }
                        }
                    ],
                    "edges": [
                        {
                            "id": "1-close->2",
                            "source": "1",
                            "target": "2",
                            "sourceHandle": "1-close",
                            "targetHandle": "2",
                            "type": "default"
                        },
                        {
                            "id": "1-close->3",
                            "source": "1",
                            "target": "3",
                            "sourceHandle": "1-close",
                            "targetHandle": "3",
                            "type": "default"
                        },
                        {
                            "id": "3->6",
                            "source": "3",
                            "target": "6",
                            "sourceHandle": "3",
                            "targetHandle": "6",
                            "type": "default"
                        },
                        {
                            "id": "2->6",
                            "source": "2",
                            "target": "6",
                            "sourceHandle": "2",
                            "targetHandle": "6",
                            "type": "default"
                        }
                    ]
                }
            }
        
        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)
        assert isinstance(data, object)
        assert 'buy' in data
        assert len(data['buy']) > 0
        assert 'sell' in data
        assert len(data['sell']) > 0

        assert 'features' in data
        assert len(data['features']) > 0
        features = data['features']
        assert 'sharp' in features
