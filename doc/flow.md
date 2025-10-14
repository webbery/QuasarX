# 策略流图
图的json结构形如:
```json
{
  "graph": {
    "id": "graph_001",
    "name": "示例计算图",
    "description": "这是一个包含多种类型节点的计算图示例",
    "nodes": [
      {
        "id": "node_input_1",
        "type": "input",
        "name": "数据输入节点",
        "data_type": "float32",
        "shape": [128, 64],
        "properties": {
          "source": "kafka_topic_1",
          "format": "protobuf"
        }
      },
      {
        "id": "node_feature_1",
        "type": "feature",
        "name": "用户特征提取",
        "feature_type": "embedding",
        "dimension": 256,
        "properties": {
          "lookup_table": "user_embeddings",
          "normalization": "l2"
        }
      },
      {
        "id": "node_operation_1",
        "type": "operation",
        "name": "矩阵乘法",
        "operator": "matmul",
        "inputs": ["node_input_1", "node_feature_1"],
        "properties": {
          "transpose_a": false,
          "transpose_b": true
        }
      },
      {
        "id": "node_strategy_1",
        "type": "strategy",
        "name": "排序策略",
        "strategy_type": "top_k",
        "parameters": {
          "k": 10,
          "metric": "cosine_similarity"
        },
        "inputs": ["node_operation_1"],
        "properties": {
          "cache_enabled": true,
          "timeout_ms": 100
        }
      },
      {
        "id": "node_output_1",
        "type": "output",
        "name": "结果输出",
        "data_type": "string",
        "inputs": ["node_strategy_1"],
        "properties": {
          "sink": "redis_cache",
          "ttl": 3600
        }
      },
      {
        "id": "node_operation_2",
        "type": "operation",
        "name": "激活函数",
        "operator": "relu",
        "inputs": ["node_feature_1"],
        "properties": {
          "alpha": 0.1
        }
      }
    ],
    "edges": [
      {
        "source": "node_input_1",
        "target": "node_operation_1",
        "data_flow": "tensor"
      },
      {
        "source": "node_feature_1",
        "target": "node_operation_1", 
        "data_flow": "embedding"
      },
      {
        "source": "node_operation_1",
        "target": "node_strategy_1",
        "data_flow": "score"
      },
      {
        "source": "node_strategy_1",
        "target": "node_output_1",
        "data_flow": "result"
      },
      {
        "source": "node_feature_1",
        "target": "node_operation_2",
        "data_flow": "feature"
      }
    ],
    "metadata": {
      "version": "1.0",
      "created_time": "2024-01-15T10:30:00Z",
      "framework": "tensorflow"
    }
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
