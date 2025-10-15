# 策略流图
图的json结构形如:
```json
 {
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
            "来源": {
              "value": "CSV文件",
              "type": "select",
              "options": ["CSV文件", "数据库", "API接口", "实时数据流"]
            },
            "路径": {
              "value": "/data/input.csv",
              "type": "text"
            },
            "编码": {
              "value": "UTF-8",
              "type": "select",
              "options": ["UTF-8", "GBK", "ASCII"]
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
        "type": "default",
        "markerEnd": {
          "type": MarkerType.ArrowClosed,
          "color": 'var(--primary)',
        },
        "style": {
          "stroke": 'var(--primary)',
          "strokeWidth": 2,
        },
      },
      {
        "id": "e2->3",
        "source": "2",
        "target": "3",
        "sourceHandle": "output",
        "targetHandle": "input",
        "type": "default",
        "markerEnd": {
          "type": MarkerType.ArrowClosed,
          "color": 'var(--primary)',
        },
        "style": {
          "stroke": 'var(--primary)',
          "strokeWidth": 2,
        },
      },
      {
        "id": "e3->4",
        "source": "3",
        "target": "4",
        "sourceHandle": "output",
        "targetHandle": "input",
        "type": "default",
        "markerEnd": {
          "type": MarkerType.ArrowClosed,
          "color": 'var(--primary)',
        },
        "style": {
          "stroke": 'var(--primary)',
          "strokeWidth": 2,
        },
      },
      {
        "id": "e3->6",
        "source": "3",
        "target": "6",
        "sourceHandle": "output",
        "targetHandle": "input",
        "type": "default",
        "markerEnd": {
          "type": MarkerType.ArrowClosed,
          "color": 'var(--primary)',
        },
        "style": {
          "stroke": 'var(--primary)',
          "strokeWidth": 2,
        },
      },
      {
        "id": "e4->5",
        "source": "4",
        "target": "5",
        "sourceHandle": "output",
        "targetHandle": "input",
        "type": "default",
        "markerEnd": {
          "type": MarkerType.ArrowClosed,
          "color": 'var(--primary)',
        },
        "style": {
          "stroke": 'var(--primary)',
          "strokeWidth": 2,
        },
      }
    ]
  }
}
```
## 节点类型
### 输入节点
### 输出节点
### 策略节点
#### 神经网络模型节点
#### 普通机器学习节点
#### 组合特征节点
### 特征节点
### 运算节点
#### 合并节点
将多个节点的输出作为输入，以某种(平均/求和等)运算合并成一个结果并输出
#### 归一化节点
